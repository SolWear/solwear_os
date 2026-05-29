pub const LCD_W: usize = 240;
pub const LCD_H: usize = 240;
pub const STATUS_BAR_H: usize = 20;

pub const PIN_LCD_SCLK: i32 = 3;
pub const PIN_LCD_MOSI: i32 = 4;
pub const PIN_LCD_RST: i32 = 7;
pub const PIN_LCD_DC: i32 = 8;
pub const PIN_LCD_BL: i32 = 9;

const COLOR_BLACK: u16 = 0x0000;
const COLOR_WHITE: u16 = 0xffff;
const COLOR_SOL_GREEN: u16 = 0x178b;
const COLOR_SOL_PURPLE: u16 = 0x9a9f;
const COLOR_DIM: u16 = 0x4208;
const COLOR_WARN: u16 = 0xfd20;

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
        #[cfg(feature = "esp-idf")]
        {
            if let Err(code) = init_lcd_panel() {
                crate::protocol::emit_error(
                    "display-init",
                    &format!("st7789 panel init failed rc={code}"),
                );
            }
        }

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
        #[cfg(feature = "esp-idf")]
        {
            let _ = set_gpio_output(PIN_LCD_BL, u32::from(self.backlight > 0));
        }
    }

    pub fn begin_frame(&mut self) {
        self.frame_count += 1;
        #[cfg(feature = "esp-idf")]
        fill_fb(COLOR_BLACK);
    }

    pub fn draw_status_bar(&mut self, battery_pct: u8, charging: bool) {
        #[cfg(feature = "esp-idf")]
        {
            fill_rect(0, 0, LCD_W as i32, STATUS_BAR_H as i32, COLOR_DIM);
            let bar_w = ((battery_pct.min(100) as usize * 54) / 100) as i32;
            let color = if charging {
                COLOR_SOL_GREEN
            } else {
                COLOR_WHITE
            };
            fill_rect(180, 6, 56, 8, COLOR_BLACK);
            fill_rect(181, 7, bar_w, 6, color);
        }

        #[cfg(not(feature = "esp-idf"))]
        let _ = (battery_pct, charging, STATUS_BAR_H);
    }

    pub fn draw_screen(&mut self, name: &str) {
        #[cfg(feature = "esp-idf")]
        {
            draw_boot_pattern(self.frame_count, name);
        }

        #[cfg(not(feature = "esp-idf"))]
        let _ = (name, self.backlight);
    }

    pub fn flush(&mut self) {
        #[cfg(feature = "esp-idf")]
        flush_fb();
    }
}

#[cfg(feature = "esp-idf")]
static mut LCD_PANEL: esp_idf_hal::sys::esp_lcd_panel_handle_t = core::ptr::null_mut();

#[cfg(feature = "esp-idf")]
static mut LCD_FB: *mut u16 = core::ptr::null_mut();

#[cfg(feature = "esp-idf")]
fn init_lcd_panel() -> Result<(), i32> {
    set_gpio_output(PIN_LCD_BL, 1)?;

    unsafe {
        if !LCD_PANEL.is_null() && !LCD_FB.is_null() {
            return Ok(());
        }

        init_spi_bus()?;
        let mut io: esp_idf_hal::sys::esp_lcd_panel_io_handle_t = core::ptr::null_mut();
        let io_cfg = esp_idf_hal::sys::esp_lcd_panel_io_spi_config_t {
            cs_gpio_num: -1,
            dc_gpio_num: PIN_LCD_DC,
            spi_mode: 3,
            pclk_hz: 40 * 1000 * 1000,
            trans_queue_depth: 10,
            on_color_trans_done: None,
            user_ctx: core::ptr::null_mut(),
            lcd_cmd_bits: 8,
            lcd_param_bits: 8,
            flags: Default::default(),
        };
        check(esp_idf_hal::sys::esp_lcd_new_panel_io_spi(
            esp_idf_hal::sys::spi_host_device_t_SPI2_HOST
                as esp_idf_hal::sys::esp_lcd_spi_bus_handle_t,
            &io_cfg,
            &mut io,
        ))?;

        let mut panel_cfg = esp_idf_hal::sys::esp_lcd_panel_dev_config_t {
            reset_gpio_num: PIN_LCD_RST,
            __bindgen_anon_1: Default::default(),
            data_endian: esp_idf_hal::sys::lcd_rgb_data_endian_t_LCD_RGB_DATA_ENDIAN_BIG,
            bits_per_pixel: 16,
            flags: Default::default(),
            vendor_config: core::ptr::null_mut(),
        };
        panel_cfg.__bindgen_anon_1.rgb_ele_order =
            esp_idf_hal::sys::lcd_rgb_element_order_t_LCD_RGB_ELEMENT_ORDER_RGB;

        check(esp_idf_hal::sys::esp_lcd_new_panel_st7789(
            io,
            &panel_cfg,
            core::ptr::addr_of_mut!(LCD_PANEL),
        ))?;
        check(esp_idf_hal::sys::esp_lcd_panel_reset(LCD_PANEL))?;
        check(esp_idf_hal::sys::esp_lcd_panel_init(LCD_PANEL))?;
        check(esp_idf_hal::sys::esp_lcd_panel_invert_color(
            LCD_PANEL, true,
        ))?;
        check(esp_idf_hal::sys::esp_lcd_panel_disp_on_off(LCD_PANEL, true))?;

        let bytes = LCD_W * LCD_H * core::mem::size_of::<u16>();
        LCD_FB = esp_idf_hal::sys::heap_caps_malloc(
            bytes,
            esp_idf_hal::sys::MALLOC_CAP_DMA | esp_idf_hal::sys::MALLOC_CAP_8BIT,
        ) as *mut u16;
        if LCD_FB.is_null() {
            return Err(esp_idf_hal::sys::ESP_ERR_NO_MEM);
        }
    }

    fill_fb(COLOR_BLACK);
    draw_boot_pattern(0, "BOOT");
    flush_fb();
    crate::protocol::emit_line("[HAL] display st7789 panel ready mode=3 invert=1 fb=dma");
    Ok(())
}

#[cfg(feature = "esp-idf")]
fn init_spi_bus() -> Result<(), i32> {
    let mut bus = esp_idf_hal::sys::spi_bus_config_t {
        __bindgen_anon_1: Default::default(),
        __bindgen_anon_2: Default::default(),
        sclk_io_num: PIN_LCD_SCLK,
        __bindgen_anon_3: Default::default(),
        __bindgen_anon_4: Default::default(),
        data4_io_num: -1,
        data5_io_num: -1,
        data6_io_num: -1,
        data7_io_num: -1,
        max_transfer_sz: (LCD_W * 40 * core::mem::size_of::<u16>() + 64) as i32,
        flags: 0,
        isr_cpu_id: esp_idf_hal::sys::esp_intr_cpu_affinity_t_ESP_INTR_CPU_AFFINITY_AUTO,
        intr_flags: 0,
    };

    unsafe {
        bus.__bindgen_anon_1.mosi_io_num = PIN_LCD_MOSI;
        bus.__bindgen_anon_2.miso_io_num = -1;
        bus.__bindgen_anon_3.quadwp_io_num = -1;
        bus.__bindgen_anon_4.quadhd_io_num = -1;

        let rc = esp_idf_hal::sys::spi_bus_initialize(
            esp_idf_hal::sys::spi_host_device_t_SPI2_HOST,
            &bus,
            esp_idf_hal::sys::spi_common_dma_t_SPI_DMA_CH_AUTO,
        );
        if rc == esp_idf_hal::sys::ESP_ERR_INVALID_STATE {
            return Ok(());
        }
        check(rc)
    }
}

#[cfg(feature = "esp-idf")]
fn set_gpio_output(pin: i32, level: u32) -> Result<(), i32> {
    let gpio = pin as esp_idf_hal::sys::gpio_num_t;
    unsafe {
        let mut rc = esp_idf_hal::sys::gpio_reset_pin(gpio);
        if rc != esp_idf_hal::sys::ESP_OK {
            return Err(rc);
        }

        rc = esp_idf_hal::sys::gpio_set_direction(
            gpio,
            esp_idf_hal::sys::gpio_mode_t_GPIO_MODE_OUTPUT,
        );
        if rc != esp_idf_hal::sys::ESP_OK {
            return Err(rc);
        }

        rc = esp_idf_hal::sys::gpio_set_level(gpio, level);
        if rc != esp_idf_hal::sys::ESP_OK {
            return Err(rc);
        }
    }

    Ok(())
}

#[cfg(feature = "esp-idf")]
fn fill_fb(color: u16) {
    unsafe {
        if LCD_FB.is_null() {
            return;
        }
        let c = wire_rgb565(color);
        for i in 0..(LCD_W * LCD_H) {
            *LCD_FB.add(i) = c;
        }
    }
}

#[cfg(feature = "esp-idf")]
fn fill_rect(x: i32, y: i32, w: i32, h: i32, color: u16) {
    unsafe {
        if LCD_FB.is_null() || w <= 0 || h <= 0 {
            return;
        }
        let x0 = x.max(0).min(LCD_W as i32);
        let y0 = y.max(0).min(LCD_H as i32);
        let x1 = (x + w).max(0).min(LCD_W as i32);
        let y1 = (y + h).max(0).min(LCD_H as i32);
        if x1 <= x0 || y1 <= y0 {
            return;
        }
        let c = wire_rgb565(color);
        for yy in y0..y1 {
            let row = LCD_FB.add(yy as usize * LCD_W);
            for xx in x0..x1 {
                *row.add(xx as usize) = c;
            }
        }
    }
}

#[cfg(feature = "esp-idf")]
fn draw_boot_pattern(frame: u64, name: &str) {
    let phase = (frame as i32 * 9) % LCD_W as i32;
    fill_rect(0, STATUS_BAR_H as i32, LCD_W as i32, 4, COLOR_SOL_PURPLE);
    fill_rect(0, 220, LCD_W as i32, 6, COLOR_SOL_GREEN);
    fill_rect(18, 42, 204, 112, COLOR_DIM);
    fill_rect(22, 46, 196, 104, COLOR_BLACK);
    fill_rect(34, 62, 52, 52, COLOR_SOL_PURPLE);
    fill_rect(94, 62, 52, 52, COLOR_SOL_GREEN);
    fill_rect(154, 62, 52, 52, COLOR_WHITE);
    fill_rect(24 + phase - LCD_W as i32, 166, 42, 18, COLOR_WARN);
    fill_rect(24 + phase, 166, 42, 18, COLOR_WARN);
    fill_rect(36, 194, (name.len() as i32 * 7).min(168), 8, COLOR_WHITE);
}

#[cfg(feature = "esp-idf")]
fn flush_fb() {
    unsafe {
        if LCD_PANEL.is_null() || LCD_FB.is_null() {
            return;
        }
        const STRIPE: usize = 40;
        let mut y = 0;
        while y < LCD_H {
            let rows = (LCD_H - y).min(STRIPE);
            let ptr = LCD_FB.add(y * LCD_W) as *const core::ffi::c_void;
            let rc = esp_idf_hal::sys::esp_lcd_panel_draw_bitmap(
                LCD_PANEL,
                0,
                y as i32,
                LCD_W as i32,
                (y + rows) as i32,
                ptr,
            );
            if rc != esp_idf_hal::sys::ESP_OK {
                crate::protocol::emit_error("display-flush", &format!("draw rc={rc}"));
                return;
            }
            y += rows;
        }
    }
}

#[cfg(feature = "esp-idf")]
fn check(rc: i32) -> Result<(), i32> {
    if rc == esp_idf_hal::sys::ESP_OK {
        Ok(())
    } else {
        Err(rc)
    }
}

pub fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    (((r as u16 & 0xF8) << 8) | ((g as u16 & 0xFC) << 3) | (b as u16 >> 3)) as u16
}

pub fn wire_rgb565(value: u16) -> u16 {
    value.swap_bytes()
}
