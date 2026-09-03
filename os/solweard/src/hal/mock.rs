//! Deterministic HAL used by tests and by the desktop emulator.
//!
//! Behaviour is scriptable from a JSON file so a test can pin the battery
//! level, the clock, and every sensor reading. The script format is:
//!
//! ```json
//! {
//!   "device": "solwear-emulator",
//!   "screen": { "width": 480, "height": 480, "shape": "round" },
//!   "power": { "percent": 72, "charging": false, "estimateMinutes": 540 },
//!   "brightness": 60,
//!   "epochMs": 1700000000000,
//!   "tickMs": 1000,
//!   "timezone": "UTC",
//!   "network": { "connected": true, "ssid": "SolWear", "signal": 72 },
//!   "sensors": {
//!     "heartRate": { "unit": "bpm", "values": [62, 64, 66] },
//!     "temperature": { "unit": "C", "value": 31.5 }
//!   }
//! }
//! ```
//!
//! When `epochMs` is present the clock starts there and advances by `tickMs`
//! (default zero, i.e. frozen) on every read. Sensors with a `values` list
//! cycle through it in order, so repeated reads are reproducible.

use super::{
    sensor_unavailable, system_now_ms, Hal, NetworkStatus, PowerStatus, Screen, ScreenShape,
    SensorReading,
};
use crate::error::RpcError;
use serde::Deserialize;
use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::sync::Mutex;

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct ScriptSensor {
    #[serde(default)]
    unit: Option<String>,
    #[serde(default)]
    value: Option<f64>,
    #[serde(default)]
    values: Option<Vec<f64>>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct ScriptPower {
    percent: u8,
    #[serde(default)]
    charging: bool,
    #[serde(default)]
    estimate_minutes: u32,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct ScriptScreen {
    width: u32,
    height: u32,
    #[serde(default = "default_shape")]
    shape: String,
}

fn default_shape() -> String {
    "round".to_string()
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Script {
    #[serde(default)]
    device: Option<String>,
    #[serde(default)]
    screen: Option<ScriptScreen>,
    #[serde(default)]
    power: Option<ScriptPower>,
    #[serde(default)]
    brightness: Option<u8>,
    #[serde(default)]
    epoch_ms: Option<u64>,
    #[serde(default)]
    tick_ms: Option<u64>,
    #[serde(default)]
    timezone: Option<String>,
    #[serde(default)]
    network: Option<NetworkStatus>,
    #[serde(default)]
    sensors: Option<BTreeMap<String, ScriptSensor>>,
}

#[derive(Debug)]
struct SensorTrack {
    unit: String,
    values: Vec<f64>,
    cursor: usize,
}

#[derive(Debug)]
struct MockState {
    brightness: u8,
    clock_ms: Option<u64>,
    sensors: BTreeMap<String, SensorTrack>,
}

#[derive(Debug)]
pub struct MockHal {
    device: String,
    screen: Screen,
    power: PowerStatus,
    network: NetworkStatus,
    timezone: String,
    tick_ms: u64,
    state: Mutex<MockState>,
}

impl Default for MockHal {
    fn default() -> Self {
        MockHal::from_script(Script::default())
    }
}

impl MockHal {
    pub fn from_script_file(path: &OsStr) -> anyhow::Result<MockHal> {
        let text = std::fs::read_to_string(path)?;
        let script: Script = serde_json::from_str(&text)?;
        Ok(MockHal::from_script(script))
    }

    pub fn from_script_str(text: &str) -> anyhow::Result<MockHal> {
        let script: Script = serde_json::from_str(text)?;
        Ok(MockHal::from_script(script))
    }

    fn from_script(script: Script) -> MockHal {
        let mut screen = script
            .screen
            .map(|s| Screen {
                width: s.width.max(1),
                height: s.height.max(1),
                shape: ScreenShape::parse(&s.shape).unwrap_or(ScreenShape::Round),
            })
            .unwrap_or_default();
        if let Some(env_screen) = screen_from_env() {
            screen = env_screen;
        }

        let power = script
            .power
            .map(|p| PowerStatus {
                percent: p.percent.min(100),
                charging: p.charging,
                estimate_minutes: p.estimate_minutes,
            })
            .unwrap_or(PowerStatus {
                percent: 76,
                charging: false,
                estimate_minutes: 512,
            });

        let mut sensors = default_sensor_tracks();
        if let Some(scripted) = script.sensors {
            for (name, spec) in scripted {
                let values = match (spec.values, spec.value) {
                    (Some(list), _) if !list.is_empty() => list,
                    (_, Some(single)) => vec![single],
                    _ => continue,
                };
                let unit = spec
                    .unit
                    .or_else(|| sensors.get(&name).map(|t| t.unit.clone()))
                    .unwrap_or_else(|| "raw".to_string());
                sensors.insert(
                    name,
                    SensorTrack {
                        unit,
                        values,
                        cursor: 0,
                    },
                );
            }
        }

        MockHal {
            device: script.device.unwrap_or_else(|| "solwear-mock".to_string()),
            screen,
            power,
            network: script.network.unwrap_or(NetworkStatus {
                connected: true,
                ssid: Some("SolWear-Dev".to_string()),
                signal: Some(78),
            }),
            timezone: script.timezone.unwrap_or_else(|| "UTC".to_string()),
            tick_ms: script.tick_ms.unwrap_or(0),
            state: Mutex::new(MockState {
                brightness: script.brightness.unwrap_or(70).min(100),
                clock_ms: script.epoch_ms,
                sensors,
            }),
        }
    }
}

fn default_sensor_tracks() -> BTreeMap<String, SensorTrack> {
    let mut map = BTreeMap::new();
    let defaults: &[(&str, &str, &[f64])] = &[
        ("heartRate", "bpm", &[68.0, 71.0, 74.0, 70.0]),
        ("steps", "steps", &[4211.0]),
        ("accelerometer", "g", &[0.98, 1.01, 0.99]),
        ("temperature", "C", &[31.4]),
        ("ambientLight", "lux", &[120.0, 240.0]),
    ];
    for (name, unit, values) in defaults {
        map.insert(
            (*name).to_string(),
            SensorTrack {
                unit: (*unit).to_string(),
                values: values.to_vec(),
                cursor: 0,
            },
        );
    }
    map
}

/// `SOLWEAR_SCREEN=800x480:rect` overrides the scripted panel, which is how the
/// emulator switches device profiles without rewriting the script file.
fn screen_from_env() -> Option<Screen> {
    let raw = std::env::var("SOLWEAR_SCREEN").ok()?;
    let (size, shape) = match raw.split_once(':') {
        Some((size, shape)) => (
            size,
            ScreenShape::parse(shape).unwrap_or(ScreenShape::Round),
        ),
        None => (raw.as_str(), ScreenShape::Round),
    };
    let (w, h) = size.split_once('x')?;
    Some(Screen {
        width: w.trim().parse().ok()?,
        height: h.trim().parse().ok()?,
        shape,
    })
}

impl Hal for MockHal {
    fn device(&self) -> String {
        self.device.clone()
    }

    fn screen(&self) -> Screen {
        self.screen
    }

    fn power(&self) -> PowerStatus {
        self.power
    }

    fn brightness(&self) -> u8 {
        self.state.lock().expect("mock hal state").brightness
    }

    fn set_brightness(&self, percent: u8) -> Result<(), RpcError> {
        self.state.lock().expect("mock hal state").brightness = percent.min(100);
        Ok(())
    }

    fn read_sensor(&self, sensor: &str) -> Result<SensorReading, RpcError> {
        let timestamp_ms = self.now_ms();
        let mut state = self.state.lock().expect("mock hal state");
        let track = state
            .sensors
            .get_mut(sensor)
            .ok_or_else(|| sensor_unavailable(sensor))?;
        let value = track.values[track.cursor % track.values.len()];
        track.cursor = track.cursor.wrapping_add(1);
        Ok(SensorReading {
            sensor: sensor.to_string(),
            value,
            unit: track.unit.clone(),
            timestamp_ms,
        })
    }

    fn network(&self) -> NetworkStatus {
        self.network.clone()
    }

    fn now_ms(&self) -> u64 {
        let mut state = self.state.lock().expect("mock hal state");
        match state.clock_ms {
            Some(current) => {
                state.clock_ms = Some(current + self.tick_ms);
                current
            }
            None => system_now_ms(),
        }
    }

    fn timezone(&self) -> String {
        self.timezone.clone()
    }
}
