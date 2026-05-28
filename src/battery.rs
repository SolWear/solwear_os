pub const PIN_BAT_ADC: i32 = 2;
pub const BAT_FULL_MV: u16 = 4200;
pub const BAT_EMPTY_MV: u16 = 3000;
pub const BAT_CAPACITY_MAH: u16 = 350;

pub struct BatteryMonitor {
    millivolts: u16,
    charging: bool,
}

impl BatteryMonitor {
    pub fn new() -> Self {
        Self {
            millivolts: 3800,
            charging: false,
        }
    }

    pub fn init(&mut self) {
        crate::protocol::emit_line(&format!(
            "[BATT] adc init pin={} divider=100k/100k capacity={}mah",
            PIN_BAT_ADC, BAT_CAPACITY_MAH
        ));
    }

    pub fn update(&mut self) {
        #[cfg(feature = "host-sim")]
        {
            self.millivolts = 3810;
        }
    }

    pub fn percent(&self) -> u8 {
        if self.millivolts <= BAT_EMPTY_MV {
            return 0;
        }
        if self.millivolts >= BAT_FULL_MV {
            return 100;
        }
        (((self.millivolts - BAT_EMPTY_MV) as u32 * 100) / (BAT_FULL_MV - BAT_EMPTY_MV) as u32)
            as u8
    }

    pub fn millivolts(&self) -> u16 {
        self.millivolts
    }

    pub fn charging(&self) -> bool {
        self.charging
    }
}
