# SolWearOS

SolWearOS is being rewritten in Rust for the SolWear transparent Solana
smartwatch Prototype 2. The Rust branch targets ESP32-S3 through ESP-IDF Rust
bindings while preserving the existing C/C++ firmware hardware contract.

## Current Prototype

- MCU: Lolin ESP32-S3 Mini
- Display: ST7789 240 x 240 IPS over SPI
- NFC: PN532 over I2C
- Power: TP4056 charger with 350 mAh LiPo
- Input: four active-low tactile buttons
- Storage: NVS for wallet material, SPIFFS for receipts and game state

## Features

- Watchface, settings, transaction, stats, and battery status UI
- Local onboarding flow for wallet creation or import
- Passphrase-protected encrypted wallet seed storage
- NFC pairing/signing interaction surface for the SolWear mobile app
- Built-in games and system UI for the hardware demo

## Repository Layout

| Path | Purpose |
| --- | --- |
| `main/` | App entry point, UI flow, games, clock, and event handling |
| `components/st7789/` | Display driver |
| `components/ui_core/` | Drawing and font helpers |
| `components/hal_buttons/` | Button input HAL |
| `components/hal_nfc/` | PN532 NFC HAL |
| `components/hal_battery/` | Battery measurement HAL |
| `components/wallet/` | Wallet and crypto helpers |

## Rust Build

Install Rust, ESP-IDF, and the ESP Rust tooling:

```powershell
rustup default stable
cargo install espup --locked
espup install
cargo install ldproxy espflash
```

Host simulation checks can run without ESP hardware:

```powershell
cargo test
cargo run
```

Firmware build target on Windows:

```powershell
& 'D:\VSBuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
$env:RUSTUP_HOME='D:\rustup'
$env:CARGO_HOME='D:\cargo'
$env:TEMP='D:\tmp'
$env:TMP='D:\tmp'
. 'D:\espup-solwear-export.ps1'
cargo +esp build -Z build-std=std,panic_abort --release --no-default-features --features esp-idf --target xtensa-esp32s3-espidf
espflash flash --monitor target/xtensa-esp32s3-espidf/release/solwear-os
```

On Windows, build from a short path such as `E:\swo` because `esp-idf-sys`
rejects long output paths.

The legacy C/C++ firmware is preserved in the `cxx-stable-YYYYMMDD` release.
The Rust protocol contract lives in `docs/firmware-protocol.md`.

## Hardware Pins

| Function | Pins |
| --- | --- |
| Display SPI | SCLK GPIO3, MOSI GPIO4, RST GPIO7, DC GPIO8, BL GPIO9 |
| PN532 I2C | SDA GPIO5, SCL GPIO6 |
| Buttons | K1 GPIO13, K2 GPIO12, K3 GPIO11, K4 GPIO10 |
| Battery ADC | GPIO2 |

## Notes

Generated ESP-IDF build output is intentionally not committed. Keep local
configuration, serial monitor logs, and generated binaries out of the repo.

## NFC Bring-Up Checklist

- Use Android reader mode as the primary flow: the phone reads/writes and the
  watch emulates an NFC Forum Type 4 tag through PN532 target mode.
- For the current prototype, start each tap around 2-3 cm from the watch NFC
  coil, then hold the phone steady over the strongest antenna spot until the
  app leaves the reading or writing state.
- Signing is a two-tap flow: write request, remove phone, confirm on SolWear,
  then tap again to read the signature response.
- Release acceptance target: at least 18 successful wallet reads out of 20,
  and at least 18 complete sign cycles out of 20, using the documented antenna
  position.
- If tuned firmware still misses that target, treat antenna geometry and
  enclosure placement as the next blocker before adding more protocol fallback.
