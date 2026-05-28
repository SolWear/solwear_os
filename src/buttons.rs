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

pub struct Buttons;

impl Buttons {
    pub fn new() -> Self {
        Self
    }

    pub fn init(&mut self) {
        crate::protocol::emit_line(&format!(
            "[HAL] buttons init active_low k1={} k2={} k3={} k4={}",
            PIN_BTN_K1, PIN_BTN_K2, PIN_BTN_K3, PIN_BTN_K4
        ));
    }

    pub fn poll(&mut self) -> Option<ButtonEvent> {
        None
    }
}
