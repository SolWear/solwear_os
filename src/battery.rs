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
        const TABLE: &[(u16, u8)] = &[
            (4200, 100),
            (4100, 90),
            (4000, 80),
            (3900, 70),
            (3800, 60),
            (3700, 50),
            (3600, 35),
            (3500, 20),
            (3400, 10),
            (3300, 5),
            (3000, 0),
        ];
        let mv = self.millivolts.clamp(BAT_EMPTY_MV, BAT_FULL_MV);
        for pair in TABLE.windows(2) {
            let (hi_mv, hi_pct) = pair[0];
            let (lo_mv, lo_pct) = pair[1];
            if mv >= lo_mv {
                let span_mv = hi_mv - lo_mv;
                let span_pct = hi_pct - lo_pct;
                return lo_pct + (((mv - lo_mv) as u32 * span_pct as u32) / span_mv as u32) as u8;
            }
        }
        0
    }

    pub fn millivolts(&self) -> u16 {
        self.millivolts
    }

    pub fn charging(&self) -> bool {
        self.charging
    }
}
