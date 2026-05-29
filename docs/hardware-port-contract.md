# SolWear Hardware Port Contract

This records the stable C firmware behavior that the Rust rewrite must preserve.

## Display: ST7789 240x240

- Driver path: `components/st7789/st7789.c`
- Bus: `SPI2_HOST`
- Pins: SCLK GPIO3, MOSI GPIO4, RST GPIO7, DC GPIO8, BL GPIO9
- CS is tied to ground; configure CS as `-1`
- Backlight is active-high; GPIO9 high means on
- SPI mode must be 3; mode 0 produces bad pixels
- Pixel clock: 40 MHz
- Panel driver: ESP-IDF `esp_lcd_new_panel_st7789`
- Panel quirks: `invert_color(true)` and `disp_on_off(true)`
- Pixel format: RGB565
- Wire format: RGB565 values must be byte-swapped before flush
- Framebuffer: 240 * 240 * 2 bytes allocated from DMA-capable heap
- Flush strategy: send 40-row stripes through `esp_lcd_panel_draw_bitmap`

## Buttons

- Driver path: `components/hal_buttons/hal_buttons.c`
- Pins: K1 GPIO13, K2 GPIO12, K3 GPIO11, K4 GPIO10
- Electrical behavior: active-low inputs with internal pullups enabled
- Poll interval/debounce: 20 ms
- Hold threshold: 5000 ms, only K1 and K4 emit hold events
- K1 double-press window: 350 ms
- K4 triple-press window: 600 ms
- Stable mapping:
  - K1 press / double / hold
  - K2 press
  - K3 press
  - K4 press / double / triple / hold

## Battery

- Driver path: `components/hal_battery/hal_battery.c`
- Pin: GPIO2
- ADC: ADC1 channel 1, one-shot mode
- Attenuation: `ADC_ATTEN_DB_12`
- Width: 12-bit
- Divider: 100K / 100K, so `Vbat = Vadc * 2`
- Voltage clamp: 3000-4200 mV
- Charging heuristic: `mv_bat > 4100`
- Percent uses the firmware lookup table, not a linear estimate:
  4200=100, 4100=90, 4000=80, 3900=70, 3800=60, 3700=50,
  3600=35, 3500=20, 3400=10, 3300=5, 3000=0.

## NFC: PN532

- Driver path: `components/hal_nfc/hal_nfc.c`
- Bus: I2C0
- Pins: SDA GPIO5, SCL GPIO6
- Address: `0x24`
- Speed: 100 kHz
- Internal pullups: enabled
- Initialization is lazy; the stable firmware powers up PN532 on demand
- Startup command flow:
  - `GetFirmwareVersion` command `0x02`, expect response `0x03`
  - `SAMConfiguration` command `{0x14, 0x01, 0x14, 0x01}`, expect `0x15`
  - Set max RF power through PN532 registers `0x6311`, `0x6312`, `0x6309`
  - RF retries through `RFConfiguration` item `0x05`
- Primary phone flow is PN532 target mode as NFC Forum Type 4 tag
- Wallet record external type: `solwear:wallet`
- Sign request type match: contains `sign_request`
- Sign response external type: `solwear:sign_response`
