# Rust Bring-Up Notes

## Verified

- Rust, MSVC Build Tools, `rustfmt`, `espup`, `ldproxy`, and `espflash` were installed.
- Host simulation compiles with `cargo check`.
- Host tests pass with `cargo test`.
- ESP Rust STD toolchain for `esp32s3` was installed to `D:\rustup` / `D:\cargo` through `espup`.
- ESP-IDF Rust target build succeeds from the short-path workspace `E:\swo`
  with `esp-idf-hal` 0.46 and ESP-IDF `v5.3.3`.

## Current ESP Build Status

The release firmware build now completes:

```powershell
cargo +esp build -Z build-std=std,panic_abort --release --no-default-features --features esp-idf --target xtensa-esp32s3-espidf
```

The last serial monitor check showed the ESP32-S3 in ROM download mode:

```text
boot:0x20 (DOWNLOAD(USB/UART0))
waiting for download
```

The release ELF at `E:\swo\target\xtensa-esp32s3-espidf\release\solwear-os`
was flashed successfully with `espflash 4.4.0`. A short monitor sample after
reset attached successfully but produced no firmware output, so boot/serial
visibility still needs hardware smoke testing.

After a black-screen report, `Display::init()` was updated to pulse LCD reset
GPIO7 and drive backlight GPIO9 high, matching the legacy C firmware's
active-high backlight behavior. The updated release build flashed
successfully; COM8 remained reachable after clearing stale monitor processes.

The display Rust port now follows the stable C ST7789 driver: SPI2, mode 3,
40 MHz pixel clock, DC GPIO8, reset GPIO7, CS tied low, ESP-IDF `esp_lcd`
ST7789 panel driver, invert-on, display-on, a DMA-capable 240x240 RGB565
framebuffer, byte-swapped pixel storage, and 40-row bitmap flushes. The first
frame is a simple color pattern, not the final watch UI.

The stable hardware contracts for display, buttons, battery, and NFC are
captured in `docs/hardware-port-contract.md`.

## Next Bring-Up Step

Confirm whether the display shows the simple color pattern after reset. If it
does, continue porting UI primitives and then buttons. If it stays blank with
backlight on, inspect the ST7789 init return path, SPI mode/pins, and panel
reset timing.

```powershell
robocopy E:\SOLWEAR\_publish_solwear_os E:\swo /E /XD .git target build
cd E:\swo
cargo +esp build -Z build-std=std,panic_abort --release --no-default-features --features esp-idf --target xtensa-esp32s3-espidf
espflash flash --monitor target/xtensa-esp32s3-espidf/release/solwear-os
```
