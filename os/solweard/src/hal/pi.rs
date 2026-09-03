//! Raspberry Pi HAL.
//!
//! Everything is read from userspace interfaces that exist on a stock
//! Raspberry Pi OS Lite install: `/sys/class/power_supply` for the battery,
//! `/sys/class/backlight` for the panel, the industrial I/O subsystem for
//! I2C attached sensors, `/sys/class/thermal` for die temperature, and
//! `nmcli` for the network. Development boards frequently have none of these,
//! so every reader degrades to a sane default instead of failing the daemon.

use super::{
    sensor_unavailable, system_now_ms, Hal, NetworkStatus, NfcStatus, PowerStatus, Screen,
    ScreenShape, SensorReading,
};
use crate::error::{RpcError, HAL_UNAVAILABLE};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

const POWER_SUPPLY_DIR: &str = "/sys/class/power_supply";
const BACKLIGHT_DIR: &str = "/sys/class/backlight";
const IIO_DIR: &str = "/sys/bus/iio/devices";
const THERMAL_ZONE: &str = "/sys/class/thermal/thermal_zone0/temp";

#[derive(Debug)]
pub struct PiHal {
    screen: Screen,
    nfc_enabled: AtomicBool,
}

impl PiHal {
    pub fn new() -> PiHal {
        PiHal {
            screen: detect_screen(),
            nfc_enabled: AtomicBool::new(false),
        }
    }
}

impl Default for PiHal {
    fn default() -> Self {
        Self::new()
    }
}

fn read_trimmed(path: impl AsRef<Path>) -> Option<String> {
    std::fs::read_to_string(path)
        .ok()
        .map(|s| s.trim().to_string())
}

fn read_number<T: std::str::FromStr>(path: impl AsRef<Path>) -> Option<T> {
    read_trimmed(path)?.parse().ok()
}

/// First entry in a sysfs class directory that contains `probe`.
fn first_entry_with(dir: &str, probe: &str) -> Option<PathBuf> {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .collect();
    entries.sort();
    entries.into_iter().find(|p| p.join(probe).exists())
}

/// The panel size is not discoverable in a useful way from a headless daemon,
/// so it is configured through the environment and written into the image by
/// the build script. `SOLWEAR_SCREEN=480x480:round`.
fn detect_screen() -> Screen {
    let Ok(raw) = std::env::var("SOLWEAR_SCREEN") else {
        return Screen::default();
    };
    let (size, shape) = match raw.split_once(':') {
        Some((size, shape)) => (
            size,
            ScreenShape::parse(shape).unwrap_or(ScreenShape::Round),
        ),
        None => (raw.as_str(), ScreenShape::Round),
    };
    match size.split_once('x') {
        Some((w, h)) => match (w.trim().parse(), h.trim().parse()) {
            (Ok(width), Ok(height)) => Screen {
                width,
                height,
                shape,
            },
            _ => Screen::default(),
        },
        None => Screen::default(),
    }
}

fn backlight_device() -> Option<PathBuf> {
    first_entry_with(BACKLIGHT_DIR, "brightness")
}

impl Hal for PiHal {
    fn device(&self) -> String {
        read_trimmed("/sys/firmware/devicetree/base/model")
            .map(|s| s.trim_end_matches('\0').to_string())
            .or_else(|| read_trimmed("/proc/device-tree/model"))
            .unwrap_or_else(|| "linux-generic".to_string())
    }

    fn screen(&self) -> Screen {
        self.screen
    }

    fn power(&self) -> PowerStatus {
        let Some(supply) = first_entry_with(POWER_SUPPLY_DIR, "capacity") else {
            // No battery: a mains powered development board reports as full and
            // charging, which is what the shell should display.
            return PowerStatus::default();
        };

        let percent: u8 = read_number(supply.join("capacity")).unwrap_or(100);
        let status = read_trimmed(supply.join("status")).unwrap_or_default();
        let charging = matches!(status.as_str(), "Charging" | "Full");

        // Prefer the gauge's own estimate when the driver exposes one,
        // otherwise derive minutes from charge and current.
        let estimate_minutes = read_number::<u64>(supply.join("time_to_empty_now"))
            .map(|seconds| (seconds / 60) as u32)
            .or_else(|| {
                let charge_now: f64 = read_number(supply.join("charge_now"))?;
                let current_now: f64 = read_number(supply.join("current_now"))?;
                if current_now <= 0.0 {
                    return None;
                }
                Some((charge_now / current_now * 60.0) as u32)
            })
            .unwrap_or(0);

        PowerStatus {
            percent: percent.min(100),
            charging,
            estimate_minutes,
        }
    }

    fn brightness(&self) -> u8 {
        let Some(device) = backlight_device() else {
            return 100;
        };
        let current: f64 = read_number(device.join("brightness")).unwrap_or(0.0);
        let max: f64 = read_number(device.join("max_brightness")).unwrap_or(0.0);
        if max <= 0.0 {
            return 100;
        }
        ((current / max) * 100.0).round().clamp(0.0, 100.0) as u8
    }

    fn set_brightness(&self, percent: u8) -> Result<(), RpcError> {
        let device = backlight_device().ok_or_else(|| {
            RpcError::new(
                HAL_UNAVAILABLE,
                "no backlight device under /sys/class/backlight",
            )
        })?;
        let max: f64 = read_number(device.join("max_brightness")).ok_or_else(|| {
            RpcError::new(
                HAL_UNAVAILABLE,
                "backlight device does not report max_brightness",
            )
        })?;
        let target = ((percent.min(100) as f64 / 100.0) * max).round() as u64;
        std::fs::write(device.join("brightness"), target.to_string()).map_err(|e| {
            RpcError::new(
                HAL_UNAVAILABLE,
                format!("cannot write backlight brightness: {e}"),
            )
        })
    }

    fn read_sensor(&self, sensor: &str) -> Result<SensorReading, RpcError> {
        let timestamp_ms = system_now_ms();
        let (value, unit) = match sensor {
            "temperature" => {
                let millidegrees: f64 =
                    read_number(THERMAL_ZONE).ok_or_else(|| sensor_unavailable(sensor))?;
                (millidegrees / 1000.0, "C")
            }
            "ambientLight" => {
                let raw = read_iio("in_illuminance_raw")
                    .or_else(|| read_iio("in_illuminance_input"))
                    .ok_or_else(|| sensor_unavailable(sensor))?;
                (raw, "lux")
            }
            "accelerometer" => {
                // Magnitude of the three axes, in units of gravity.
                let x = read_iio("in_accel_x_raw").ok_or_else(|| sensor_unavailable(sensor))?;
                let y = read_iio("in_accel_y_raw").unwrap_or(0.0);
                let z = read_iio("in_accel_z_raw").unwrap_or(0.0);
                let scale = read_iio("in_accel_scale").unwrap_or(1.0);
                let magnitude = ((x * x + y * y + z * z).sqrt() * scale) / 9.806_65;
                (magnitude, "g")
            }
            "heartRate" => {
                let raw = read_iio("in_proximity_raw").ok_or_else(|| sensor_unavailable(sensor))?;
                (raw, "bpm")
            }
            "steps" => {
                let raw = read_iio("in_steps_input")
                    .or_else(|| read_iio("in_steps_raw"))
                    .ok_or_else(|| sensor_unavailable(sensor))?;
                (raw, "steps")
            }
            other => return Err(sensor_unavailable(other)),
        };

        Ok(SensorReading {
            sensor: sensor.to_string(),
            value,
            unit: unit.to_string(),
            timestamp_ms,
        })
    }

    fn network(&self) -> NetworkStatus {
        let Ok(output) = Command::new("nmcli")
            .args(["-t", "-f", "ACTIVE,SSID,SIGNAL", "dev", "wifi"])
            .output()
        else {
            // nmcli is absent (or NetworkManager is not running): report
            // disconnected rather than propagating an error to the shell.
            return NetworkStatus::default();
        };
        if !output.status.success() {
            return NetworkStatus::default();
        }
        let text = String::from_utf8_lossy(&output.stdout);
        for line in text.lines() {
            let mut parts = line.split(':');
            let active = parts.next().unwrap_or("no");
            if active != "yes" {
                continue;
            }
            let ssid = parts.next().unwrap_or("").to_string();
            let signal = parts.next().and_then(|s| s.parse::<u8>().ok());
            return NetworkStatus {
                connected: true,
                ssid: if ssid.is_empty() { None } else { Some(ssid) },
                signal,
            };
        }
        NetworkStatus::default()
    }

    fn nfc_status(&self) -> NfcStatus {
        let i2c = Path::new("/dev/i2c-1").exists();
        // PN532 target-mode requires a dedicated userspace worker. Merely
        // finding an I2C controller is not enough to claim that it is ready.
        let worker = Path::new("/usr/lib/solwear/solwear-nfc-pn532").exists();
        NfcStatus {
            available: i2c,
            ready: i2c && worker,
            enabled: self.nfc_enabled.load(Ordering::Relaxed) && i2c && worker,
            backend: "pn532-i2c".to_string(),
            mode: "type4-wallet".to_string(),
            detail: Some(if !i2c {
                "/dev/i2c-1 is unavailable".to_string()
            } else if !worker {
                "PN532 target-mode worker is not installed".to_string()
            } else {
                "PN532 target-mode worker detected".to_string()
            }),
        }
    }

    fn set_nfc_enabled(&self, enabled: bool) -> Result<(), RpcError> {
        let status = self.nfc_status();
        if enabled && !status.ready {
            return Err(RpcError::new(
                HAL_UNAVAILABLE,
                status
                    .detail
                    .unwrap_or_else(|| "NFC is unavailable".to_string()),
            ));
        }
        self.nfc_enabled.store(enabled, Ordering::Relaxed);
        Ok(())
    }

    fn now_ms(&self) -> u64 {
        system_now_ms()
    }

    fn timezone(&self) -> String {
        std::env::var("TZ")
            .ok()
            .filter(|s| !s.is_empty())
            .or_else(|| read_trimmed("/etc/timezone"))
            .or_else(|| {
                std::fs::read_link("/etc/localtime")
                    .ok()
                    .and_then(|target| {
                        let text = target.to_string_lossy().to_string();
                        text.split_once("zoneinfo/").map(|(_, tz)| tz.to_string())
                    })
            })
            .unwrap_or_else(|| "UTC".to_string())
    }
}

/// Read a named attribute from the first industrial I/O device that exposes it.
fn read_iio(attribute: &str) -> Option<f64> {
    let device = first_entry_with(IIO_DIR, attribute)?;
    read_number(device.join(attribute))
}
