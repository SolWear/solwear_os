# SolWearOS

SolWearOS is the embedded firmware for the SolWear transparent Solana
smartwatch prototype. It runs on an ESP32-S3 Mini with a 240 x 240 ST7789
display, PN532 NFC, four hardware buttons, LiPo battery sensing, and local
wallet storage.

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

## Build

Install ESP-IDF, then build from the repository root:

```bash
idf.py set-target esp32s3
idf.py build
```

Flash a connected board:

```bash
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the serial port for your board, for example `COM5`
on Windows or `/dev/ttyACM0` on Linux.

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
