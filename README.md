# SolWearOS

> A from-scratch smartwatch operating system for the **Waveshare RP2040-Touch-LCD-1.69**, with a built-in **Solana** crypto wallet, NFC contract testing, and a square-icon app launcher.

[![Platform](https://img.shields.io/badge/platform-RP2040-blueviolet)]()
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D)]()
[![Build](https://img.shields.io/badge/build-PlatformIO-orange)]()
[![License](https://img.shields.io/badge/license-private-lightgrey)]()

---

## Features

- **Square-icon app launcher** — Settings, Wallet, NFC, Health, Game, Alarm, Stats, Charging
- **Solana wallet** — address storage, NFC tag write/read for `solana:` URIs, contract test stub
- **NFC (PN532)** — *off by default*, only powered on when the wallet/NFC app needs it
- **Charging animation** — auto-pushed when USB is detected, auto-dismissed when removed
- **Live stats** — battery %, voltage, free heap, uptime, steps, FW version
- **Step counter** — QMI8658 IMU with software peak detection
- **Three watch faces** — digital, analog, minimal — with procedural wallpapers
- **Persistent storage** — settings, wallet, weekly step history (LittleFS, 2 MB partition)
- **Sound effects** — passive buzzer with click/beep/melody helpers
- **Service-tool friendly** — emits `[STATUS]` heartbeat over USB CDC every 5 s

## Hardware

| Component | Part | Bus / Pin |
|---|---|---|
| MCU | RP2040 (Waveshare board, 16 MB flash) | — |
| Display | ST7789V2 240×280 1.69" | SPI1 (MOSI=11, SCLK=10, CS=9, DC=8, RST=13, BL=25) |
| Touch | CST816S capacitive | I2C1 (SDA=6, SCL=7) |
| IMU | QMI8658 6-axis | I2C1 (shared with touch) |
| NFC | PN532 v3 | I2C0 (SDA=16, SCL=17) — external |
| Battery | 1200 mAh LiPo | ADC GP29 |
| Buzzer | Passive piezo | PWM GP20 |

## Architecture

```
                       ┌──────────────────┐
                       │      apps/       │  WatchFace · Home · Wallet
                       │                  │  NFC · Health · Game · Alarm
                       │                  │  Charging · Stats · Settings
                       └────────┬─────────┘
                                │
                       ┌────────▼─────────┐
                       │       ui/        │  StatusBar · Theme · Draw helpers
                       └────────┬─────────┘
                                │
                       ┌────────▼─────────┐
                       │      core/       │  ScreenManager · EventSystem
                       │                  │  Storage · SystemClock · Timers
                       └────────┬─────────┘
                                │
                       ┌────────▼─────────┐
                       │      hal/        │  Display · Touch · IMU · NFC
                       │                  │  Battery · Buzzer · Power
                       └──────────────────┘
```

- Single-core (core 0) at 30 fps. FreeRTOS dual-core was disabled because of a stack-region overlap with the heap in earlephilhower v5.x.
- **Lock-free ring buffer** for events (32 entries).
- **Procedural icons & wallpapers** — drawn at runtime to keep flash usage tiny.
- **Strip-mode display fallback** — if the 131 KB full-frame sprite can't be allocated, the HAL falls back to a 240×40 strip so the device still boots.

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- Python 3.9+ (PlatformIO bootstraps its own toolchain on first run)

### Build

```bash
pio run
```

### Prototype build targets

- `env:solwearos` — current RP2040 + LCD production path
- `env:prototype_d_esp32_eink154` — Prototype D scaffold (ESP32 v2 + Waveshare e-ink 1.54 pin profile)

### Flash via UF2 (recommended)

1. Hold the **BOOTSEL** button on the watch and plug it into USB.
2. The board mounts as `RPI-RP2`.
3. Drag `.pio/build/solwearos/firmware.uf2` onto it.
4. The device reboots automatically.

### Flash via picotool (advanced)

```bash
pio run -t upload
```

Requires the `picotool` USB driver. On Windows you may need to use [Zadig](https://zadig.akeo.ie/) to bind WinUSB to the BOOTSEL device. The UF2 method above is much simpler.

### Companion debugger

The [solwear_service_tool](https://github.com/SolWear/solwear_service_tool) desktop app pairs with this firmware over USB serial — live logs, battery dashboard, settings editor, and one-click UF2 flashing.

## Project Layout

```
SolWearOS/
├── include/
│   └── config.h                # Pins, layout constants, app IDs, thresholds
├── src/
│   ├── main.cpp                # Boot + 30 fps event/render loop
│   ├── hal/                    # Hardware abstraction
│   │   ├── hal_display.{h,cpp}
│   │   ├── hal_touch.{h,cpp}
│   │   ├── hal_imu.{h,cpp}
│   │   ├── hal_nfc.{h,cpp}     # Lazy init — off until app opens it
│   │   ├── hal_battery.{h,cpp}
│   │   ├── hal_buzzer.{h,cpp}
│   │   └── hal_power.{h,cpp}
│   ├── core/                   # OS plumbing
│   │   ├── screen_manager.{h,cpp}
│   │   ├── event_system.{h,cpp}
│   │   ├── app_framework.{h,cpp}
│   │   ├── storage.{h,cpp}
│   │   ├── system_clock.{h,cpp}
│   │   └── timer_manager.{h,cpp}
│   ├── ui/
│   │   ├── ui_common.h         # Theme, drawing helpers
│   │   └── status_bar.{h,cpp}
│   ├── apps/                   # All user-facing apps
│   │   ├── app_watchface.{h,cpp}
│   │   ├── app_home.{h,cpp}
│   │   ├── app_settings.{h,cpp}
│   │   ├── app_wallet.{h,cpp}
│   │   ├── app_nfc.{h,cpp}
│   │   ├── app_health.{h,cpp}
│   │   ├── app_game.{h,cpp}
│   │   ├── app_alarm.{h,cpp}
│   │   ├── app_charging.{h,cpp}
│   │   └── app_stats.{h,cpp}
│   └── assets/
│       ├── icons.h             # Procedural 48×48 icons
│       ├── wallpapers.h        # Procedural backgrounds
│       └── sounds.h            # Buzzer melodies
└── platformio.ini              # Build config
```

## Memory Usage

| Resource | Used | Total | % |
|---|---:|---:|---:|
| RAM   | 12 KB  | 256 KB | 4.7% |
| Flash | 196 KB | 14 MB  | 1.4% |

Plenty of headroom for crypto libs, more apps, and BLE companion sync.

## Status Heartbeat

Every 5 seconds the firmware emits a single line over USB CDC for the service tool to parse:

```
[STATUS] batt=87 volt=4.05 heap=180432 steps=123 uptime=456 charging=0 temp=38.5 proto=prototype-a-rp2040-lcd169 mcu=rp2040 display=st7789-240x280 caps=status,watch-control,apps,diagnostics,uf2
```

## Service-Tool Commands

The firmware accepts control commands over USB serial (one command per line):

- `status now`
- `bri <0-100>`
- `buzz test|buzz alarm|buzz on|buzz off`
- `diag on|diag off`
- `app <watchface|home|settings|wallet|nfc|health|game|alarm|stats|charging>`
- `nav home|back`
- `set watchface <0-2>`
- `set wallpaper <n>`
- `set stepgoal <1000-50000>`
- `reboot bootsel`
- `power off`
- `version`
- `help`

## Roadmap

- [ ] BLE companion sync (Android/iOS)
- [ ] Real Solana transaction signing (Ed25519)
- [ ] BIP-39 seed phrase setup wizard
- [ ] OTA updates over BLE
- [ ] Heart rate sensor (when hardware ships)
- [ ] More watch faces & wallpapers

## License

Private — © SolWear. All rights reserved.
