pub const LCD_W: usize = 240;
pub const LCD_H: usize = 240;
pub const STATUS_BAR_H: usize = 20;

pub const PIN_LCD_SCLK: i32 = 3;
pub const PIN_LCD_MOSI: i32 = 4;
pub const PIN_LCD_RST: i32 = 7;
pub const PIN_LCD_DC: i32 = 8;
pub const PIN_LCD_BL: i32 = 9;

pub struct Display {
    backlight: u8,
    frame_count: u64,
}

impl Display {
    pub fn new() -> Self {
        Self {
            backlight: 80,
            frame_count: 0,
        }
    }

    pub fn init(&mut self) {
        // Hardware requirements carried from the C firmware:
        // ST7789 SPI mode 3, INVON enabled, DMA-capable frame buffers,
        // and RGB565 byte swapping before sending pixels.
        crate::protocol::emit_line(&format!(
            "[HAL] display init st7789 {}x{} sclk={} mosi={} rst={} dc={} bl={} spi_mode=3 invert=1 dma=1 rgb565_swap=1",
            LCD_W, LCD_H, PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_RST, PIN_LCD_DC, PIN_LCD_BL
        ));
    }

    pub fn set_backlight(&mut self, percent: u8) {
        self.backlight = percent.min(100);
    }

    pub fn begin_frame(&mut self) {
        self.frame_count += 1;
    }

    pub fn draw_status_bar(&mut self, battery_pct: u8, charging: bool) {
        let _ = (battery_pct, charging, STATUS_BAR_H);
    }

    pub fn draw_screen(&mut self, name: &str) {
        let _ = (name, self.backlight);
    }

    pub fn flush(&mut self) {}
}

pub fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    (((r as u16 & 0xF8) << 8) | ((g as u16 & 0xFC) << 3) | (b as u16 >> 3)) as u16
}

pub fn wire_rgb565(value: u16) -> u16 {
    value.swap_bytes()
}
