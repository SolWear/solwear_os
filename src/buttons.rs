pub const PIN_BTN_K1: i32 = 13;
pub const PIN_BTN_K2: i32 = 12;
pub const PIN_BTN_K3: i32 = 11;
pub const PIN_BTN_K4: i32 = 10;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ButtonEvent {
    Up,
    Down,
    Select,
    Back,
    UpHold,
    BackHold,
}

#[derive(Clone, Copy)]
struct KeyState {
    pin: i32,
    pressed: bool,
    stable: bool,
    last_raw: bool,
    debounce_ms: u16,
    press_ms: u32,
    hold_sent: bool,
}

pub struct Buttons {
    keys: [KeyState; 4],
}

impl Buttons {
    pub fn new() -> Self {
        Self {
            keys: [
                KeyState::new(PIN_BTN_K1),
                KeyState::new(PIN_BTN_K2),
                KeyState::new(PIN_BTN_K3),
                KeyState::new(PIN_BTN_K4),
            ],
        }
    }

    pub fn init(&mut self) {
        #[cfg(feature = "esp-idf")]
        {
            for key in &self.keys {
                let _ = configure_input_pullup(key.pin);
            }
        }

        crate::protocol::emit_line(&format!(
            "[HAL] buttons init active_low k1={} k2={} k3={} k4={}",
            PIN_BTN_K1, PIN_BTN_K2, PIN_BTN_K3, PIN_BTN_K4
        ));
    }

    pub fn poll(&mut self) -> Option<ButtonEvent> {
        #[cfg(not(feature = "esp-idf"))]
        {
            None
        }

        #[cfg(feature = "esp-idf")]
        {
            const TICK_MS: u32 = 33;
            for (idx, key) in self.keys.iter_mut().enumerate() {
                let raw_pressed = read_pressed(key.pin);
                if raw_pressed != key.last_raw {
                    key.last_raw = raw_pressed;
                    key.debounce_ms = 0;
                    continue;
                }
                key.debounce_ms = key.debounce_ms.saturating_add(TICK_MS as u16);
                if key.debounce_ms < 20 {
                    continue;
                }
                if raw_pressed != key.stable {
                    key.stable = raw_pressed;
                    if raw_pressed {
                        key.pressed = true;
                        key.press_ms = 0;
                        key.hold_sent = false;
                    } else if key.pressed {
                        key.pressed = false;
                        if !key.hold_sent {
                            return Some(match idx {
                                0 => ButtonEvent::Up,
                                1 => ButtonEvent::Down,
                                2 => ButtonEvent::Select,
                                _ => ButtonEvent::Back,
                            });
                        }
                    }
                }
                if key.stable && key.pressed {
                    key.press_ms = key.press_ms.saturating_add(TICK_MS);
                    if key.press_ms >= 5_000 && !key.hold_sent {
                        key.hold_sent = true;
                        if idx == 0 {
                            return Some(ButtonEvent::UpHold);
                        }
                        if idx == 3 {
                            return Some(ButtonEvent::BackHold);
                        }
                    }
                }
            }
            None
        }
    }
}

impl KeyState {
    const fn new(pin: i32) -> Self {
        Self {
            pin,
            pressed: false,
            stable: false,
            last_raw: false,
            debounce_ms: 0,
            press_ms: 0,
            hold_sent: false,
        }
    }
}

#[cfg(feature = "esp-idf")]
fn configure_input_pullup(pin: i32) -> Result<(), i32> {
    let gpio = pin as esp_idf_hal::sys::gpio_num_t;
    unsafe {
        check(esp_idf_hal::sys::gpio_reset_pin(gpio))?;
        check(esp_idf_hal::sys::gpio_set_direction(
            gpio,
            esp_idf_hal::sys::gpio_mode_t_GPIO_MODE_INPUT,
        ))?;
        check(esp_idf_hal::sys::gpio_set_pull_mode(
            gpio,
            esp_idf_hal::sys::gpio_pull_mode_t_GPIO_PULLUP_ONLY,
        ))
    }
}

#[cfg(feature = "esp-idf")]
fn read_pressed(pin: i32) -> bool {
    unsafe { esp_idf_hal::sys::gpio_get_level(pin as esp_idf_hal::sys::gpio_num_t) == 0 }
}

#[cfg(feature = "esp-idf")]
fn check(rc: i32) -> Result<(), i32> {
    if rc == esp_idf_hal::sys::ESP_OK {
        Ok(())
    } else {
        Err(rc)
    }
}
