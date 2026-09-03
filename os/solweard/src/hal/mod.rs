//! Hardware abstraction layer.
//!
//! Two implementations exist: [`PiHal`] talks to a real Raspberry Pi through
//! sysfs, the industrial I/O subsystem and `nmcli`, while [`MockHal`] returns
//! deterministic values that tests and the emulator can script. Every method
//! must be answerable by both; a call that only works on hardware is a bug.

mod mock;
mod pi;

pub use mock::MockHal;
pub use pi::PiHal;

use crate::error::{RpcError, HAL_UNAVAILABLE};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Physical shape of the panel. The shell uses this to decide whether content
/// needs a safe inset away from clipped corners.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ScreenShape {
    Round,
    Square,
    Rect,
}

impl ScreenShape {
    pub fn parse(value: &str) -> Option<ScreenShape> {
        match value.trim().to_ascii_lowercase().as_str() {
            "round" | "circle" => Some(ScreenShape::Round),
            "square" => Some(ScreenShape::Square),
            "rect" | "rectangle" | "wide" => Some(ScreenShape::Rect),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Screen {
    pub width: u32,
    pub height: u32,
    pub shape: ScreenShape,
}

impl Default for Screen {
    fn default() -> Self {
        Screen {
            width: 480,
            height: 480,
            shape: ScreenShape::Round,
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PowerStatus {
    pub percent: u8,
    pub charging: bool,
    /// Minutes of runtime left, or minutes to full when charging.
    pub estimate_minutes: u32,
}

impl Default for PowerStatus {
    fn default() -> Self {
        PowerStatus {
            percent: 100,
            charging: true,
            estimate_minutes: 0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SensorReading {
    pub sensor: String,
    pub value: f64,
    pub unit: String,
    pub timestamp_ms: u64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NetworkStatus {
    pub connected: bool,
    pub ssid: Option<String>,
    /// Signal strength as a percentage, when the backend reports one.
    pub signal: Option<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NfcStatus {
    pub available: bool,
    pub ready: bool,
    pub enabled: bool,
    pub backend: String,
    pub mode: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

/// Sensors every implementation is expected to name consistently.
pub const KNOWN_SENSORS: &[&str] = &[
    "heartRate",
    "steps",
    "accelerometer",
    "temperature",
    "ambientLight",
];

pub trait Hal: Send + Sync {
    /// Short human readable hardware identifier, e.g. `raspberrypi-4b`.
    fn device(&self) -> String;
    fn screen(&self) -> Screen;
    fn power(&self) -> PowerStatus;
    fn brightness(&self) -> u8;
    fn set_brightness(&self, percent: u8) -> Result<(), RpcError>;
    fn read_sensor(&self, sensor: &str) -> Result<SensorReading, RpcError>;
    fn network(&self) -> NetworkStatus;
    fn nfc_status(&self) -> NfcStatus;
    fn set_nfc_enabled(&self, enabled: bool) -> Result<(), RpcError>;
    /// Milliseconds since the Unix epoch. Mockable so tests get a fixed clock.
    fn now_ms(&self) -> u64;
    fn timezone(&self) -> String;
}

pub fn sensor_unavailable(sensor: &str) -> RpcError {
    RpcError::new(
        HAL_UNAVAILABLE,
        format!("sensor `{sensor}` is not available on this device"),
    )
}

pub fn system_now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Choose an implementation from the environment.
///
/// `SOLWEAR_HAL` accepts `pi` or `mock`. When unset the daemon uses `PiHal` on
/// Linux and `MockHal` everywhere else, so a developer machine never needs
/// hardware. `SOLWEAR_HAL_SCRIPT` points `MockHal` at a JSON script.
pub fn from_env() -> Arc<dyn Hal> {
    let requested = std::env::var("SOLWEAR_HAL").unwrap_or_default();
    let use_pi = match requested.trim().to_ascii_lowercase().as_str() {
        "pi" | "hardware" | "real" => true,
        "mock" | "fake" => false,
        "" => cfg!(target_os = "linux"),
        other => {
            tracing::warn!("unknown SOLWEAR_HAL value `{other}`, falling back to mock");
            false
        }
    };

    if use_pi {
        tracing::info!("HAL: PiHal (sysfs, industrial I/O, nmcli)");
        Arc::new(PiHal::new())
    } else {
        let hal = match std::env::var_os("SOLWEAR_HAL_SCRIPT") {
            Some(path) => match MockHal::from_script_file(&path) {
                Ok(hal) => {
                    tracing::info!("HAL: MockHal scripted from {}", path.to_string_lossy());
                    hal
                }
                Err(err) => {
                    tracing::error!("failed to load SOLWEAR_HAL_SCRIPT: {err}; using defaults");
                    MockHal::default()
                }
            },
            None => {
                tracing::info!("HAL: MockHal (defaults)");
                MockHal::default()
            }
        };
        Arc::new(hal)
    }
}
