use crate::wallet::Wallet;

pub const PIN_NFC_SDA: i32 = 5;
pub const PIN_NFC_SCL: i32 = 6;
pub const PN532_I2C_ADDR: u8 = 0x24;

pub struct NfcService {
    enabled: bool,
    counter: u32,
}

impl NfcService {
    pub fn new() -> Self {
        Self {
            enabled: true,
            counter: 0,
        }
    }

    pub fn init(&mut self) {
        crate::protocol::emit_line(&format!(
            "[HAL] nfc init pn532 i2c_sda={} i2c_scl={} addr=0x{:02x}",
            PIN_NFC_SDA, PIN_NFC_SCL, PN532_I2C_ADDR
        ));
    }

    pub fn toggle_enabled(&mut self) {
        self.enabled = !self.enabled;
        crate::protocol::emit_result("nfc", if self.enabled { "enabled" } else { "disabled" });
    }

    pub fn poll(&mut self, wallet: &mut Wallet) {
        if !self.enabled {
            return;
        }
        self.counter = self.counter.wrapping_add(1);
        let _ = wallet.public_key();
    }
}
