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
const COLOR_LINE: u16 = 0x7bef;
const COLOR_PANEL: u16 = 0x1082;
const COLOR_WARN: u16 = 0xfd20;

pub struct DisplayModel<'a> {
    pub screen: &'a str,
    pub home_slide_grid: bool,
    pub selected_index: usize,
    pub watchface: u8,
    pub battery_pct: u8,
    pub charging: bool,
    pub nfc_armed: bool,
    pub uptime_sec: u64,
    pub wallet_name: &'a str,
    pub pubkey_short: &'a str,
    pub balance_sol: f32,
    pub tx_overlay: Option<TxOverlayModel<'a>>,
}

pub struct TxOverlayModel<'a> {
    pub from: &'a str,
    pub to: &'a str,
    pub network: &'a str,
    pub lamports: u64,
    pub fee_lamports: u64,
}

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

    pub fn draw_legacy(&mut self, model: &DisplayModel<'_>) {
        #[cfg(feature = "esp-idf")]
        draw_legacy_screen(self.frame_count, model);

        #[cfg(not(feature = "esp-idf"))]
        let _ = model;
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
fn draw_legacy_screen(frame: u64, model: &DisplayModel<'_>) {
    fill_fb(COLOR_BLACK);
    draw_status_bar_raw(
        model.uptime_sec,
        model.battery_pct,
        model.charging,
        model.screen,
    );

    match model.screen {
        "Onboard" => render_onboard(frame),
        "Locked" => render_lock(),
        "Home" => {
            if model.home_slide_grid {
                render_home_grid(model.selected_index);
            } else {
                render_watchface(frame, model);
            }
        }
        "Wallet" => render_wallet(model),
        "Receive" => render_receive(frame, model),
        "Settings" => render_settings(model.selected_index, model.watchface),
        "Transactions" => render_transactions(model),
        "Stats" => render_stats(model),
        "Games" => render_games(model.selected_index),
        "Ping Pong" => render_game_placeholder("PING PONG", "K1/K2 MOVE  K3 PAUSE"),
        "Tetris" => render_game_placeholder("TETRIS", "K1/K2 MOVE  K3 ROT"),
        "Tamagotchi" => render_tamagotchi(frame),
        _ => render_watchface(frame, model),
    }

    if let Some(tx) = &model.tx_overlay {
        render_tx_overlay(tx);
    }
}

#[cfg(feature = "esp-idf")]
fn draw_status_bar_raw(uptime_sec: u64, battery_pct: u8, charging: bool, title: &str) {
    fill_rect(0, 0, LCD_W as i32, STATUS_BAR_H as i32, COLOR_BLACK);
    hline(0, STATUS_BAR_H as i32 - 1, LCD_W as i32, COLOR_DIM);

    let minutes = (uptime_sec / 60) % 60;
    let hours = (12 + (uptime_sec / 3600)) % 24;
    text(4, 2, &format!("{hours:02}:{minutes:02}"), COLOR_WHITE, 2);

    if !matches!(title, "Home" | "Onboard") {
        text_center(2, title, COLOR_WHITE, 2);
    }
    battery_icon(210, 4, battery_pct, charging);
}

#[cfg(feature = "esp-idf")]
fn render_onboard(frame: u64) {
    solwear_logo(LCD_W as i32 / 2, 56, 46, COLOR_WHITE);
    text_center(108, "SOLWEAR", COLOR_WHITE, 2);
    text_center(128, "Solana Hardware Wallet", COLOR_DIM, 1);
    hline(40, 148, 160, COLOR_LINE);
    let pulse = if (frame / 15) & 1 == 0 {
        COLOR_WHITE
    } else {
        COLOR_DIM
    };
    text_center(168, "Press K3 to begin", pulse, 1);
}

#[cfg(feature = "esp-idf")]
fn render_lock() {
    draw_app_icon(6, LCD_W as i32 / 2, 45, COLOR_DIM, COLOR_BLACK);
    rounded_rect_outline(28, 76, 184, 58, 6, COLOR_LINE);
    text_center(92, "ENTER PASSWORD", COLOR_WHITE, 1);
    text_center(112, "K1/K2 choose", COLOR_DIM, 1);
    text_center(126, "K3 unlock", COLOR_DIM, 1);
    text_center(198, "HOLD K4 TO LOCK", COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn render_watchface(frame: u64, model: &DisplayModel<'_>) {
    rect(
        0,
        STATUS_BAR_H as i32,
        LCD_W as i32,
        LCD_H as i32 - STATUS_BAR_H as i32,
        COLOR_BLACK,
    );
    let seconds = model.uptime_sec % 60;
    let minutes = (model.uptime_sec / 60) % 60;
    let hours = (12 + (model.uptime_sec / 3600)) % 24;
    let time = format!("{hours:02}:{minutes:02}");
    let sec = format!(":{seconds:02}");
    let face = model.watchface % 6;

    match face {
        0 => {
            sunrise_scene(frame, 120, 104);
            text_center(26, "GOOD MORNING", COLOR_DIM, 1);
            let tw = text_width(&time, 3);
            text((LCD_W as i32 - tw) / 2, 156, &time, COLOR_WHITE, 3);
            text((LCD_W as i32 + tw) / 2 + 2, 166, &sec, COLOR_DIM, 2);
            text_center(
                196,
                if model.nfc_armed {
                    "NFC READY"
                } else {
                    "NFC OFF"
                },
                COLOR_DIM,
                1,
            );
            meter(30, 210, 180, 5, model.battery_pct, COLOR_WHITE);
        }
        1 => {
            night_sky(frame);
            text_center(26, "GOOD NIGHT", COLOR_DIM, 1);
            moon_scene(120, 95);
            let tw = text_width(&time, 3);
            text((LCD_W as i32 - tw) / 2, 152, &time, COLOR_WHITE, 3);
            text((LCD_W as i32 + tw) / 2 + 2, 162, &sec, COLOR_DIM, 2);
            text_center(196, "HOLD K4 TO LOCK", COLOR_DIM, 1);
            meter(54, 212, 132, 5, model.battery_pct, COLOR_WHITE);
        }
        2 => {
            text_center(40, "SOLWEAR", COLOR_DIM, 1);
            let tw = text_width(&time, 4);
            text((LCD_W as i32 - tw) / 2, 78, &time, COLOR_WHITE, 4);
            text_center(122, &sec, COLOR_DIM, 2);
            rect_outline(34, 158, 172, 34, COLOR_LINE);
            text_center(168, fallback(model.wallet_name, "PROTO V2"), COLOR_WHITE, 1);
            text_center(182, model.pubkey_short, COLOR_DIM, 1);
        }
        3 => analog_watch(hours as i32, minutes as i32, seconds as i32, &time),
        4 => {
            text_center(30, "SOL BALANCE", COLOR_DIM, 1);
            let bal = format!("{:.4}", model.balance_sol);
            let bw = text_width(&bal, 3);
            text((LCD_W as i32 - bw) / 2, 44, &bal, COLOR_WHITE, 3);
            hline(20, 82, 200, COLOR_LINE);
            text_center(94, "WALLET", COLOR_DIM, 1);
            text_center(108, model.pubkey_short, COLOR_WHITE, 1);
            hline(20, 122, 200, COLOR_LINE);
            nfc_bars(frame, 142);
            text_center(
                154,
                if model.nfc_armed {
                    "PHONE READS TAG"
                } else {
                    "NFC OFF"
                },
                COLOR_WHITE,
                1,
            );
            text_center(198, "K3 CYCLES FACE", COLOR_DIM, 1);
        }
        _ => {
            solwear_logo(120, 50, 38, COLOR_WHITE);
            text_center(98, "SOLWEAR", COLOR_DIM, 1);
            let tw = text_width(&time, 4);
            text((LCD_W as i32 - tw) / 2, 112, &time, COLOR_WHITE, 4);
            hline(20, 148, 200, COLOR_LINE);
            text_center(160, "TRUSTED SIGNER", COLOR_DIM, 1);
            text_center(
                178,
                if model.nfc_armed {
                    "NFC READY"
                } else {
                    "NFC OFF"
                },
                COLOR_WHITE,
                1,
            );
        }
    }
    nfc_icon(222, STATUS_BAR_H as i32 + 4, model.nfc_armed);
}

#[cfg(feature = "esp-idf")]
fn render_home_grid(selected: usize) {
    let labels = [
        "Wallet", "Receive", "Activity", "Stats", "Games", "Settings", "Lock",
    ];
    let page = selected / 4;
    let start = page * 4;
    let w = 94;
    let h = 78;
    let gx = 14;
    let gy = 10;
    let x0 = (LCD_W as i32 - 2 * w - gx) / 2;
    let y0 = STATUS_BAR_H as i32 + 12;
    for pos in 0..4 {
        let idx = start + pos;
        if idx >= labels.len() {
            break;
        }
        let col = (pos % 2) as i32;
        let row = (pos / 2) as i32;
        draw_app_tile(
            x0 + col * (w + gx),
            y0 + row * (h + gy),
            idx,
            labels[idx],
            selected == idx,
        );
    }
    for i in 0..2 {
        let dx = LCD_W as i32 / 2 - 8 + i * 16;
        if i as usize == page {
            rounded_rect(dx - 8, LCD_H as i32 - 23, 16, 6, 3, COLOR_WHITE);
        } else {
            circle_fill(dx, LCD_H as i32 - 20, 2, COLOR_LINE);
        }
    }
}

#[cfg(feature = "esp-idf")]
fn render_wallet(model: &DisplayModel<'_>) {
    text_center(36, "SOL BALANCE", COLOR_DIM, 1);
    let bal = format!("{:.4}", model.balance_sol);
    let bw = text_width(&bal, 3);
    text((LCD_W as i32 - bw) / 2, 50, &bal, COLOR_WHITE, 3);
    text_center(80, "SOL", COLOR_DIM, 1);
    hline(16, 92, 208, COLOR_LINE);
    text_center(104, "WALLET", COLOR_DIM, 1);
    text_center(118, model.pubkey_short, COLOR_WHITE, 1);
    hline(16, 132, 208, COLOR_LINE);
    if model.nfc_armed {
        nfc_bars(model.uptime_sec, 157);
        text_center(170, "TAP PHONE TO SHARE", COLOR_WHITE, 1);
    } else {
        text_center(154, "NFC OFF", COLOR_DIM, 2);
        text_center(174, "enable in settings", COLOR_DIM, 1);
    }
    text_center(206, "K4 back", COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn render_receive(frame: u64, model: &DisplayModel<'_>) {
    text_center(32, fallback(model.wallet_name, "SolWear"), COLOR_WHITE, 2);
    text_center(54, "WALLET ADDRESS", COLOR_DIM, 1);
    let cx = 120;
    let cy = 116;
    if model.nfc_armed {
        let phase = (frame / 12) % 3;
        for i in 0..3 {
            let c = if i == phase { COLOR_WHITE } else { COLOR_LINE };
            circle(cx, cy, 18 + i as i32 * 17, c);
        }
        circle_fill(cx, cy, 6, COLOR_WHITE);
        text_center(170, "hold phone within 3cm", COLOR_WHITE, 1);
    } else {
        for i in 0..3 {
            circle(cx, cy, 18 + i * 17, COLOR_LINE);
        }
        line(cx - 26, cy - 26, cx + 26, cy + 26, COLOR_DIM);
        text_center(170, "NFC disabled", COLOR_DIM, 1);
    }
    rounded_rect_outline(28, 180, 184, 26, 4, COLOR_LINE);
    text_center(190, model.pubkey_short, COLOR_WHITE, 1);
    text_center(218, "K4 back", COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn render_settings(selected: usize, watchface: u8) {
    let items = [
        "Watchface: GM",
        "Watchface: GN",
        "Watchface: Digital",
        "Watchface: Analog",
        "Watchface: Wallet",
        "Watchface: Minimal",
        "Change Password",
        "About",
        "GM Big Text",
    ];
    for (i, item) in items.iter().enumerate() {
        let y = STATUS_BAR_H as i32 + 4 + i as i32 * 24;
        if y + 21 > LCD_H as i32 - 12 {
            break;
        }
        let sel = selected == i;
        let fg = if sel { COLOR_BLACK } else { COLOR_WHITE };
        if sel {
            rounded_rect(6, y, LCD_W as i32 - 12, 21, 4, COLOR_WHITE);
        } else {
            rounded_rect_outline(6, y, LCD_W as i32 - 12, 21, 4, COLOR_LINE);
        }
        let label = if i < 6 && i == watchface as usize {
            format!("{} *", item)
        } else {
            item.to_string()
        };
        text(14, y + 6, &label, fg, 1);
    }
    text_center(
        LCD_H as i32 - 12,
        "K1/K2 nav  K3 select  K4 back",
        COLOR_DIM,
        1,
    );
}

#[cfg(feature = "esp-idf")]
fn render_transactions(model: &DisplayModel<'_>) {
    let bal = format!("{:.4} SOL", model.balance_sol);
    text_center(30, &bal, COLOR_WHITE, 2);
    hline(16, 46, 208, COLOR_LINE);
    circle(120, 128, 26, COLOR_LINE);
    hline(104, 128, 32, COLOR_LINE);
    text_center(168, "No activity yet", COLOR_DIM, 1);
    text_center(184, "phone sends, watch signs", COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn render_stats(model: &DisplayModel<'_>) {
    text_center(30, "BATTERY", COLOR_DIM, 1);
    let pct = format!("{}%", model.battery_pct);
    let w = text_width(&pct, 3);
    text((LCD_W as i32 - w) / 2, 42, &pct, COLOR_WHITE, 3);
    if model.charging {
        text_center(72, "CHARGING", COLOR_SOL_GREEN, 1);
    }
    meter(20, 78, 200, 8, model.battery_pct, COLOR_WHITE);
    hline(16, 94, 208, COLOR_LINE);
    let rows = [
        format!(
            "NFC       {}",
            if model.nfc_armed { "Armed" } else { "Off" }
        ),
        format!("Wallet    {}", fallback(model.wallet_name, "SolWear")),
        "Cell      350mAh LW303040".to_string(),
        "SolWearOS Rust".to_string(),
    ];
    for (i, row) in rows.iter().enumerate() {
        text(
            14,
            110 + i as i32 * 18,
            row,
            if i == 0 && model.nfc_armed {
                COLOR_WHITE
            } else {
                COLOR_DIM
            },
            1,
        );
    }
}

#[cfg(feature = "esp-idf")]
fn render_games(selected: usize) {
    let names = ["Ping Pong", "Tetris", "Tamagotchi"];
    for (i, name) in names.iter().enumerate() {
        let y = STATUS_BAR_H as i32 + 8 + i as i32 * 62;
        let sel = selected == i;
        let fg = if sel { COLOR_BLACK } else { COLOR_WHITE };
        let ic = if sel { COLOR_BLACK } else { COLOR_DIM };
        if sel {
            rounded_rect(16, y, 208, 56, 6, COLOR_WHITE);
        } else {
            rounded_rect_outline(16, y, 208, 56, 6, COLOR_LINE);
        }
        text(78, y + 20, name, fg, 2);
        let ix = 48;
        let iy = y + 28;
        match i {
            0 => circle_fill(ix, iy, 7, ic),
            1 => {
                rect(ix - 8, iy - 8, 8, 8, ic);
                rect(ix, iy - 8, 8, 8, ic);
                rect(ix - 8, iy, 8, 8, ic);
            }
            _ => {
                circle(ix, iy, 9, ic);
                pixel(ix - 3, iy - 3, ic);
                pixel(ix + 3, iy - 3, ic);
                hline(ix - 4, iy + 3, 8, ic);
            }
        }
    }
    text_center(
        LCD_H as i32 - 12,
        "K1/K2 select  K3 play  K4 back",
        COLOR_DIM,
        1,
    );
}

#[cfg(feature = "esp-idf")]
fn render_game_placeholder(title: &str, hint: &str) {
    text_center(72, title, COLOR_WHITE, 3);
    hline(24, 122, 192, COLOR_LINE);
    text_center(150, hint, COLOR_DIM, 1);
    text_center(178, "K4 back", COLOR_DIM, 2);
}

#[cfg(feature = "esp-idf")]
fn render_tamagotchi(frame: u64) {
    text_center(42, "TAMAGOTCHI", COLOR_WHITE, 2);
    let cy = 112 + if (frame / 12) & 1 == 0 { 0 } else { 2 };
    circle_fill(120, cy, 22, COLOR_WHITE);
    circle_fill(112, cy - 6, 4, COLOR_BLACK);
    circle_fill(128, cy - 6, 4, COLOR_BLACK);
    hline(114, cy + 9, 12, COLOR_BLACK);
    meter(32, 168, 176, 6, 80, COLOR_WHITE);
    meter(32, 184, 176, 6, 70, COLOR_DIM);
    meter(32, 200, 176, 6, 60, COLOR_LINE);
    text_center(224, "K1 Feed K2 Play K3 Nap", COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn render_tx_overlay(tx: &TxOverlayModel<'_>) {
    for y in (18..LCD_H as i32 - 18).step_by(2) {
        for x in (0..LCD_W as i32).step_by(2) {
            pixel(x, y, COLOR_BLACK);
        }
    }
    rect(8, 20, LCD_W as i32 - 16, LCD_H as i32 - 40, COLOR_BLACK);
    rect_outline(8, 20, LCD_W as i32 - 16, LCD_H as i32 - 40, COLOR_WHITE);
    draw_app_icon(6, 120, 34, COLOR_WHITE, COLOR_BLACK);
    text_center(54, "VERIFY REQUEST", COLOR_WHITE, 1);
    hline(16, 64, 208, COLOR_LINE);
    text(16, 74, "From", COLOR_DIM, 1);
    text(68, 74, &short_text(tx.from), COLOR_WHITE, 1);
    text(16, 90, "To", COLOR_DIM, 1);
    text(68, 90, &short_text(tx.to), COLOR_WHITE, 1);
    let sol = tx.lamports as f64 / 1_000_000_000.0;
    let amount = if tx.lamports == 0 {
        "TX REQUEST".to_string()
    } else {
        format!("{sol:.4} SOL")
    };
    text_center(108, &amount, COLOR_WHITE, 2);
    let fee = tx.fee_lamports as f64 / 1_000_000_000.0;
    text_center(
        132,
        &format!("{}  fee {:.6}", fallback(tx.network, "devnet"), fee),
        COLOR_DIM,
        1,
    );
    hline(16, 142, 208, COLOR_LINE);
    rounded_rect(14, 150, 96, 34, 5, COLOR_WHITE);
    text(32, 161, "K3 Review", COLOR_BLACK, 1);
    rounded_rect_outline(118, 150, 96, 34, 5, COLOR_WHITE);
    text(136, 161, "K4 Reject", COLOR_WHITE, 1);
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
fn pixel(x: i32, y: i32, color: u16) {
    unsafe {
        if LCD_FB.is_null() || x < 0 || y < 0 || x >= LCD_W as i32 || y >= LCD_H as i32 {
            return;
        }
        *LCD_FB.add(y as usize * LCD_W + x as usize) = wire_rgb565(color);
    }
}

#[cfg(feature = "esp-idf")]
fn rect(x: i32, y: i32, w: i32, h: i32, color: u16) {
    fill_rect(x, y, w, h, color);
}

#[cfg(feature = "esp-idf")]
fn hline(x: i32, y: i32, w: i32, color: u16) {
    rect(x, y, w, 1, color);
}

#[cfg(feature = "esp-idf")]
fn vline(x: i32, y: i32, h: i32, color: u16) {
    rect(x, y, 1, h, color);
}

#[cfg(feature = "esp-idf")]
fn rect_outline(x: i32, y: i32, w: i32, h: i32, color: u16) {
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

#[cfg(feature = "esp-idf")]
fn rounded_rect(x: i32, y: i32, w: i32, h: i32, r: i32, color: u16) {
    if r < 1 {
        rect(x, y, w, h, color);
        return;
    }
    rect(x + r, y, w - 2 * r, h, color);
    rect(x, y + r, r, h - 2 * r, color);
    rect(x + w - r, y + r, r, h - 2 * r, color);
    for dy in 0..=r {
        let dx = isqrt((r * r - dy * dy) as u32) as i32;
        hline(x + r - dx, y + r - dy, dx + 1, color);
        hline(x + w - r, y + r - dy, dx + 1, color);
        hline(x + r - dx, y + h - r + dy - 1, dx + 1, color);
        hline(x + w - r, y + h - r + dy - 1, dx + 1, color);
    }
}

#[cfg(feature = "esp-idf")]
fn solwear_logo(cx: i32, y: i32, height: i32, color: u16) {
    let w1 = height * 42 / 46;
    let w2 = height * 34 / 46;
    let w3 = height * 22 / 46;
    let gap1 = height * 16 / 46;
    let gap2 = height * 14 / 46;
    let total = w1 + gap1 + w2 + gap2 + w3;
    let x = cx - total / 2;

    rounded_rect(x, y, w1, height, w1 / 2, color);
    rounded_rect(x + w1 + gap1, y, w2, height, w2 / 2, color);
    rounded_rect(x + w1 + gap1 + w2 + gap2, y, w3, height, w3 / 2, color);
}

#[cfg(feature = "esp-idf")]
fn rounded_rect_outline(x: i32, y: i32, w: i32, h: i32, r: i32, color: u16) {
    if r < 1 {
        rect_outline(x, y, w, h, color);
        return;
    }
    hline(x + r, y, w - 2 * r, color);
    hline(x + r, y + h - 1, w - 2 * r, color);
    vline(x, y + r, h - 2 * r, color);
    vline(x + w - 1, y + r, h - 2 * r, color);
    let mut px = 0;
    let mut py = r;
    let mut d = 1 - r;
    while px <= py {
        pixel(x + r - px, y + r - py, color);
        pixel(x + w - r + px - 1, y + r - py, color);
        pixel(x + r - px, y + h - r + py - 1, color);
        pixel(x + w - r + px - 1, y + h - r + py - 1, color);
        pixel(x + r - py, y + r - px, color);
        pixel(x + w - r + py - 1, y + r - px, color);
        pixel(x + r - py, y + h - r + px - 1, color);
        pixel(x + w - r + py - 1, y + h - r + px - 1, color);
        if d < 0 {
            d += 2 * px + 3;
        } else {
            d += 2 * (px - py) + 5;
            py -= 1;
        }
        px += 1;
    }
}

#[cfg(feature = "esp-idf")]
fn circle(cx: i32, cy: i32, r: i32, color: u16) {
    let mut x = 0;
    let mut y = r;
    let mut d = 3 - 2 * r;
    while y >= x {
        for (dx, dy) in [
            (x, y),
            (y, x),
            (-x, y),
            (-y, x),
            (x, -y),
            (y, -x),
            (-x, -y),
            (-y, -x),
        ] {
            pixel(cx + dx, cy + dy, color);
        }
        x += 1;
        if d > 0 {
            y -= 1;
            d += 4 * (x - y) + 10;
        } else {
            d += 4 * x + 6;
        }
    }
}

#[cfg(feature = "esp-idf")]
fn circle_fill(cx: i32, cy: i32, r: i32, color: u16) {
    for dy in -r..=r {
        let dx = isqrt((r * r - dy * dy) as u32) as i32;
        hline(cx - dx, cy + dy, dx * 2 + 1, color);
    }
}

#[cfg(feature = "esp-idf")]
fn line(mut x0: i32, mut y0: i32, x1: i32, y1: i32, color: u16) {
    let dx = (x1 - x0).abs();
    let sx = if x0 < x1 { 1 } else { -1 };
    let dy = -(y1 - y0).abs();
    let sy = if y0 < y1 { 1 } else { -1 };
    let mut err = dx + dy;
    loop {
        pixel(x0, y0, color);
        if x0 == x1 && y0 == y1 {
            break;
        }
        let e2 = 2 * err;
        if e2 >= dy {
            err += dy;
            x0 += sx;
        }
        if e2 <= dx {
            err += dx;
            y0 += sy;
        }
    }
}

#[cfg(feature = "esp-idf")]
fn text_width(s: &str, scale: i32) -> i32 {
    s.chars().count() as i32 * 6 * scale
}

#[cfg(feature = "esp-idf")]
fn text(mut x: i32, y: i32, s: &str, color: u16, scale: i32) {
    for ch in s.chars() {
        x = char_draw(x, y, ch, color, scale);
    }
}

#[cfg(feature = "esp-idf")]
fn text_center(y: i32, s: &str, color: u16, scale: i32) {
    text(
        (LCD_W as i32 - text_width(s, scale)) / 2,
        y,
        s,
        color,
        scale,
    );
}

#[cfg(feature = "esp-idf")]
fn char_draw(x: i32, y: i32, ch: char, color: u16, scale: i32) -> i32 {
    let code = ch as usize;
    let idx = if (32..=126).contains(&code) {
        code - 32
    } else {
        b'?' as usize - 32
    };
    for (col, bits) in FONT6X8[idx].iter().enumerate() {
        for row in 0..8 {
            if bits & (1 << row) != 0 {
                for sy in 0..scale {
                    for sx in 0..scale {
                        pixel(x + col as i32 * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
    x + 6 * scale
}

#[cfg(feature = "esp-idf")]
fn battery_icon(x: i32, y: i32, pct: u8, charging: bool) {
    rect_outline(x, y, 24, 12, COLOR_WHITE);
    rect(x + 24, y + 3, 3, 6, COLOR_WHITE);
    let fill = ((20 * pct.min(100) as i32) / 100).min(20);
    if fill > 0 {
        rect(
            x + 2,
            y + 2,
            fill,
            8,
            if pct > 15 { COLOR_WHITE } else { COLOR_DIM },
        );
    }
    if charging {
        pixel(x + 12, y + 2, COLOR_WHITE);
        pixel(x + 11, y + 5, COLOR_WHITE);
        pixel(x + 12, y + 5, COLOR_WHITE);
        pixel(x + 11, y + 9, COLOR_WHITE);
    }
}

#[cfg(feature = "esp-idf")]
fn nfc_icon(x: i32, y: i32, armed: bool) {
    let c = if armed { COLOR_WHITE } else { COLOR_DIM };
    circle(x + 6, y + 6, 6, c);
    circle(x + 6, y + 6, 3, c);
    pixel(x + 6, y + 6, c);
    if !armed {
        hline(x, y + 6, 14, COLOR_WHITE);
    }
}

#[cfg(feature = "esp-idf")]
fn meter(x: i32, y: i32, w: i32, h: i32, pct: u8, color: u16) {
    rect_outline(x, y, w, h, color);
    let fill = (w - 4) * pct.min(100) as i32 / 100;
    if fill > 0 {
        rect(x + 2, y + 2, fill, h - 4, color);
    }
}

#[cfg(feature = "esp-idf")]
fn sunrise_scene(frame: u64, cx: i32, cy: i32) {
    let rise = ((frame as i32 / 4) % 9) - 4;
    let sy = cy - rise;
    rect(
        0,
        STATUS_BAR_H as i32,
        LCD_W as i32,
        sy - STATUS_BAR_H as i32,
        COLOR_PANEL,
    );
    circle(sx(cx), sy, 44, rgb565(0x20, 0x08, 0x00));
    circle(sx(cx), sy, 36, rgb565(0x60, 0x24, 0x04));
    circle_fill(cx, sy, 22, COLOR_WHITE);
    rect(0, sy + 1, LCD_W as i32, 48, COLOR_BLACK);
    hline(cx - 110, sy, 220, COLOR_WHITE);
    hline(cx - 88, sy - 1, 176, rgb565(0xB0, 0x50, 0x10));
    for i in 0..9 {
        let x2 = cx - 44 + i * 11;
        line(
            cx,
            sy - 26,
            x2,
            sy - 48 + (i % 2) * 8,
            if i == (frame as i32 / 8) % 9 {
                COLOR_WHITE
            } else {
                COLOR_DIM
            },
        );
    }
}

#[cfg(feature = "esp-idf")]
fn sx(x: i32) -> i32 {
    x
}

#[cfg(feature = "esp-idf")]
fn night_sky(frame: u64) {
    const STARS: [(i32, i32); 32] = [
        (12, 42),
        (220, 60),
        (45, 35),
        (178, 85),
        (93, 110),
        (231, 75),
        (67, 130),
        (154, 38),
        (23, 95),
        (198, 55),
        (118, 148),
        (34, 50),
        (205, 100),
        (88, 65),
        (147, 140),
        (72, 62),
        (189, 120),
        (11, 80),
        (233, 155),
        (56, 32),
        (167, 90),
        (103, 115),
        (38, 68),
        (215, 105),
        (79, 45),
        (142, 160),
        (26, 58),
        (197, 88),
        (62, 132),
        (183, 72),
        (108, 48),
        (44, 125),
    ];
    for (i, (x, y)) in STARS.iter().enumerate() {
        let drift = (frame as i32 / (2 + (i % 7) as i32)) % LCD_W as i32;
        let xx = (x - drift + LCD_W as i32) % LCD_W as i32;
        let c = if (frame as usize / 6 + i) % 11 == 0 {
            COLOR_WHITE
        } else {
            COLOR_DIM
        };
        pixel(xx, *y, c);
        if i % 7 == 0 {
            pixel(xx - 1, *y, c);
            pixel(xx + 1, *y, c);
            pixel(xx, *y - 1, c);
            pixel(xx, *y + 1, c);
        }
    }
}

#[cfg(feature = "esp-idf")]
fn moon_scene(cx: i32, cy: i32) {
    circle_fill(cx, cy, 21, COLOR_WHITE);
    circle_fill(cx + 8, cy - 5, 21, COLOR_BLACK);
    circle(cx, cy, 21, COLOR_WHITE);
}

#[cfg(feature = "esp-idf")]
fn analog_watch(hours: i32, minutes: i32, seconds: i32, time: &str) {
    let cx = 120;
    let cy = STATUS_BAR_H as i32 + 90;
    let r = 70;
    circle(cx, cy, r, COLOR_WHITE);
    circle(cx, cy, r - 9, COLOR_LINE);
    for i in 0..12 {
        let (sx, sy) = clock_point(cx, cy, r - 13, i * 5);
        let (ex, ey) = clock_point(cx, cy, r - 4, i * 5);
        line(
            sx,
            sy,
            ex,
            ey,
            if i % 3 == 0 { COLOR_WHITE } else { COLOR_DIM },
        );
    }
    let (hx, hy) = clock_point(cx, cy, 38, ((hours % 12) * 5 + minutes / 12) as i32);
    let (mx, my) = clock_point(cx, cy, 58, minutes);
    let (sx2, sy2) = clock_point(cx, cy, 62, seconds);
    line(cx, cy, hx, hy, COLOR_WHITE);
    line(cx, cy, mx, my, COLOR_WHITE);
    line(cx, cy, sx2, sy2, COLOR_DIM);
    circle_fill(cx, cy, 4, COLOR_WHITE);
    text_center(184, time, COLOR_DIM, 1);
}

#[cfg(feature = "esp-idf")]
fn clock_point(cx: i32, cy: i32, r: i32, minute_mark: i32) -> (i32, i32) {
    const SIN60: [i32; 60] = [
        0, 105, 208, 309, 407, 500, 588, 669, 743, 809, 866, 914, 951, 978, 995, 1000, 995, 978,
        951, 914, 866, 809, 743, 669, 588, 500, 407, 309, 208, 105, 0, -105, -208, -309, -407,
        -500, -588, -669, -743, -809, -866, -914, -951, -978, -995, -1000, -995, -978, -951, -914,
        -866, -809, -743, -669, -588, -500, -407, -309, -208, -105,
    ];
    let idx = minute_mark.rem_euclid(60) as usize;
    let sin = SIN60[idx];
    let cos = SIN60[(idx + 15) % 60];
    (cx + r * sin / 1000, cy - r * cos / 1000)
}

#[cfg(feature = "esp-idf")]
fn nfc_bars(frame: u64, y: i32) {
    let phase = (frame / 8) % 3;
    for i in 0..3 {
        let bh = 6 + i as i32 * 5;
        let c = if i == phase as usize {
            COLOR_WHITE
        } else {
            COLOR_LINE
        };
        rect(104 + i as i32 * 14, y - bh, 10, bh, c);
    }
}

#[cfg(feature = "esp-idf")]
fn draw_app_tile(x: i32, y: i32, icon: usize, label: &str, selected: bool) {
    let bg = if selected { COLOR_WHITE } else { COLOR_BLACK };
    let fg = if selected { COLOR_BLACK } else { COLOR_WHITE };
    rect(x, y, 94, 78, bg);
    rect_outline(
        x,
        y,
        94,
        78,
        if selected { COLOR_WHITE } else { COLOR_LINE },
    );
    text(x + (94 - text_width(label, 1)) / 2, y + 64, label, fg, 1);
    draw_app_icon(icon, x + 47, y + 31, fg, bg);
}

#[cfg(feature = "esp-idf")]
fn draw_app_icon(icon: usize, cx: i32, cy: i32, c: u16, bg: u16) {
    match icon {
        0 => {
            rect_outline(cx - 18, cy - 10, 36, 24, c);
            hline(cx - 16, cy - 3, 32, c);
            circle_fill(cx + 10, cy + 2, 2, c);
        }
        1 => {
            line(cx, cy - 18, cx, cy + 4, c);
            line(cx - 9, cy - 5, cx, cy + 4, c);
            line(cx + 9, cy - 5, cx, cy + 4, c);
            rect_outline(cx - 18, cy + 9, 36, 10, c);
        }
        2 => {
            for i in 0..3 {
                circle_fill(cx - 15, cy - 12 + i * 12, 2, c);
                hline(cx - 8, cy - 12 + i * 12, 28, c);
            }
        }
        3 => {
            rect(cx - 17, cy + 4, 6, 14, c);
            rect(cx - 7, cy - 4, 6, 22, c);
            rect(cx + 3, cy - 14, 6, 32, c);
            rect(cx + 13, cy - 8, 6, 26, c);
        }
        4 => {
            rect_outline(cx - 20, cy - 10, 22, 20, c);
            hline(cx - 16, cy, 14, c);
            vline(cx - 9, cy - 7, 14, c);
            circle_fill(cx + 13, cy - 4, 3, c);
            circle_fill(cx + 22, cy + 5, 3, c);
        }
        5 => {
            circle(cx, cy, 13, c);
            circle(cx, cy, 5, c);
            hline(cx - 20, cy, 7, c);
            hline(cx + 14, cy, 7, c);
            vline(cx, cy - 20, 7, c);
            vline(cx, cy + 14, 7, c);
        }
        _ => {
            rect_outline(cx - 15, cy - 1, 30, 20, c);
            circle(cx, cy - 4, 12, c);
            rect(cx - 12, cy - 4, 24, 8, bg);
            vline(cx, cy + 6, 8, c);
        }
    }
}

#[cfg(feature = "esp-idf")]
fn short_text(src: &str) -> String {
    if src.is_empty() || src == "?" {
        return "unknown".into();
    }
    if src.len() <= 12 {
        return src.into();
    }
    format!("{}...{}", &src[..4], &src[src.len() - 4..])
}

#[cfg(feature = "esp-idf")]
fn fallback<'a>(value: &'a str, default: &'a str) -> &'a str {
    if value.is_empty() {
        default
    } else {
        value
    }
}

#[cfg(feature = "esp-idf")]
fn isqrt(n: u32) -> u32 {
    let mut x = n;
    let mut y = x.div_ceil(2);
    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }
    x
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

#[cfg(feature = "esp-idf")]
const FONT6X8: [[u8; 6]; 95] = [
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    [0x00, 0x00, 0x5F, 0x00, 0x00, 0x00],
    [0x00, 0x07, 0x00, 0x07, 0x00, 0x00],
    [0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00],
    [0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00],
    [0x23, 0x13, 0x08, 0x64, 0x62, 0x00],
    [0x36, 0x49, 0x55, 0x22, 0x50, 0x00],
    [0x00, 0x05, 0x03, 0x00, 0x00, 0x00],
    [0x00, 0x1C, 0x22, 0x41, 0x00, 0x00],
    [0x00, 0x41, 0x22, 0x1C, 0x00, 0x00],
    [0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00],
    [0x08, 0x08, 0x3E, 0x08, 0x08, 0x00],
    [0x00, 0x50, 0x30, 0x00, 0x00, 0x00],
    [0x08, 0x08, 0x08, 0x08, 0x08, 0x00],
    [0x00, 0x60, 0x60, 0x00, 0x00, 0x00],
    [0x20, 0x10, 0x08, 0x04, 0x02, 0x00],
    [0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00],
    [0x00, 0x42, 0x7F, 0x40, 0x00, 0x00],
    [0x42, 0x61, 0x51, 0x49, 0x46, 0x00],
    [0x21, 0x41, 0x45, 0x4B, 0x31, 0x00],
    [0x18, 0x14, 0x12, 0x7F, 0x10, 0x00],
    [0x27, 0x45, 0x45, 0x45, 0x39, 0x00],
    [0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00],
    [0x01, 0x71, 0x09, 0x05, 0x03, 0x00],
    [0x36, 0x49, 0x49, 0x49, 0x36, 0x00],
    [0x06, 0x49, 0x49, 0x29, 0x1E, 0x00],
    [0x00, 0x36, 0x36, 0x00, 0x00, 0x00],
    [0x00, 0x56, 0x36, 0x00, 0x00, 0x00],
    [0x08, 0x14, 0x22, 0x41, 0x00, 0x00],
    [0x14, 0x14, 0x14, 0x14, 0x14, 0x00],
    [0x00, 0x41, 0x22, 0x14, 0x08, 0x00],
    [0x02, 0x01, 0x51, 0x09, 0x06, 0x00],
    [0x32, 0x49, 0x79, 0x41, 0x3E, 0x00],
    [0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00],
    [0x7F, 0x49, 0x49, 0x49, 0x36, 0x00],
    [0x3E, 0x41, 0x41, 0x41, 0x22, 0x00],
    [0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00],
    [0x7F, 0x49, 0x49, 0x49, 0x41, 0x00],
    [0x7F, 0x09, 0x09, 0x09, 0x01, 0x00],
    [0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00],
    [0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00],
    [0x00, 0x41, 0x7F, 0x41, 0x00, 0x00],
    [0x20, 0x40, 0x41, 0x3F, 0x01, 0x00],
    [0x7F, 0x08, 0x14, 0x22, 0x41, 0x00],
    [0x7F, 0x40, 0x40, 0x40, 0x40, 0x00],
    [0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00],
    [0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00],
    [0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00],
    [0x7F, 0x09, 0x09, 0x09, 0x06, 0x00],
    [0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00],
    [0x7F, 0x09, 0x19, 0x29, 0x46, 0x00],
    [0x46, 0x49, 0x49, 0x49, 0x31, 0x00],
    [0x01, 0x01, 0x7F, 0x01, 0x01, 0x00],
    [0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00],
    [0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00],
    [0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00],
    [0x63, 0x14, 0x08, 0x14, 0x63, 0x00],
    [0x07, 0x08, 0x70, 0x08, 0x07, 0x00],
    [0x61, 0x51, 0x49, 0x45, 0x43, 0x00],
    [0x00, 0x7F, 0x41, 0x41, 0x00, 0x00],
    [0x02, 0x04, 0x08, 0x10, 0x20, 0x00],
    [0x00, 0x41, 0x41, 0x7F, 0x00, 0x00],
    [0x04, 0x02, 0x01, 0x02, 0x04, 0x00],
    [0x40, 0x40, 0x40, 0x40, 0x40, 0x00],
    [0x00, 0x01, 0x02, 0x04, 0x00, 0x00],
    [0x20, 0x54, 0x54, 0x54, 0x78, 0x00],
    [0x7F, 0x48, 0x44, 0x44, 0x38, 0x00],
    [0x38, 0x44, 0x44, 0x44, 0x20, 0x00],
    [0x38, 0x44, 0x44, 0x48, 0x7F, 0x00],
    [0x38, 0x54, 0x54, 0x54, 0x18, 0x00],
    [0x08, 0x7E, 0x09, 0x01, 0x02, 0x00],
    [0x0C, 0x52, 0x52, 0x52, 0x3E, 0x00],
    [0x7F, 0x08, 0x04, 0x04, 0x78, 0x00],
    [0x00, 0x44, 0x7D, 0x40, 0x00, 0x00],
    [0x20, 0x40, 0x44, 0x3D, 0x00, 0x00],
    [0x7F, 0x10, 0x28, 0x44, 0x00, 0x00],
    [0x00, 0x41, 0x7F, 0x40, 0x00, 0x00],
    [0x7C, 0x04, 0x18, 0x04, 0x78, 0x00],
    [0x7C, 0x08, 0x04, 0x04, 0x78, 0x00],
    [0x38, 0x44, 0x44, 0x44, 0x38, 0x00],
    [0x7C, 0x14, 0x14, 0x14, 0x08, 0x00],
    [0x08, 0x14, 0x14, 0x18, 0x7C, 0x00],
    [0x7C, 0x08, 0x04, 0x04, 0x08, 0x00],
    [0x48, 0x54, 0x54, 0x54, 0x20, 0x00],
    [0x04, 0x3F, 0x44, 0x40, 0x20, 0x00],
    [0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00],
    [0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00],
    [0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00],
    [0x44, 0x28, 0x10, 0x28, 0x44, 0x00],
    [0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00],
    [0x44, 0x64, 0x54, 0x4C, 0x44, 0x00],
    [0x00, 0x08, 0x36, 0x41, 0x00, 0x00],
    [0x00, 0x00, 0x7F, 0x00, 0x00, 0x00],
    [0x00, 0x41, 0x36, 0x08, 0x00, 0x00],
    [0x0C, 0x02, 0x0C, 0x00, 0x00, 0x00],
];
