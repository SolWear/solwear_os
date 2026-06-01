# SolWear Firmware Protocol

This protocol is the stable contract between SolWearOS Rust firmware and `solwear_st`.

## Boot Banner

```text
SolWearOS v0.2.0-rust.0 proto=prototype-2-esp32s3-lcd13 mcu=esp32s3 display=st7789-240x240-color caps=status,watch-control,apps,nfc,battery,charging
```

## Status Heartbeat

The firmware emits a heartbeat line at least once per second:

```text
[STATUS] batt=<0-100> volt=<volts> heap=<bytes> steps=<count> uptime=<seconds> charging=<0|1> proto=<id> mcu=<id> display=<id> caps=<csv>
```

Optional field:

```text
temp=<celsius>
```

## Commands

Commands are newline-delimited UTF-8 over USB serial at 115200 baud.

```text
status now
bri <0-100>
brightness <0-100>
clock sync <unix_epoch_seconds>
app <home|wallet|settings|stats|health|game|games|receive|nfc>
nav home
nav back
reboot bootsel
nfc status
nfc reset
nfc diag
nfc power max
```

## Structured Results

```text
[RESULT] topic=<topic> status=ok message="<message>"
[ERROR] code=<code> message="<message>"
```

## NFC Type 4 Transaction Flow

Prototype 2 exposes the watch wallet as a PN532-backed NFC Forum Type 4 tag.
The Rust firmware preserves the legacy C APDU behavior:

- SELECT NDEF application `D2 76 00 00 85 01 01`
- SELECT CC file `E103`
- SELECT NDEF file `E104`
- READ BINARY (`0xB0`) and UPDATE BINARY (`0xD6`)
- 1024-byte writable NDEF file capacity

Wallet reads emit an external NDEF record:

```text
type=solwear:wallet
payload={"version":1,"pubkey":"<base58>","network":"devnet"}
```

Sign requests are accepted when the external record type contains
`sign_request`, including `solwear:sign_request`, `solvare:sign_request`, and
bare `sign_request`.

```text
payload={"version":1,"tx_bytes":"<base64>","from":"...","to":"...","network":"devnet","lamports":0,"fee_lamports":0,"session_id":"..."}
```

Sign responses are queued as:

```text
type=solwear:sign_response
payload={"version":1,"signature":"<base64-64-byte-ed25519>","session_id":"..."}
```

NFC diagnostics use structured log lines:

```text
[NFC] status enabled=<0|1> ready=<0|1> event=<event> counter=<n> target_active=<0|1> sessions=<n> apdus=<n> errors=<n> message="<text>"
[NFC] diag path=type4-target tx_valid=<0|1> key_import=<0|1> target_len=<bytes> response_pending=<0|1> range_goal_cm=3
```

The hardware acceptance gate for this phase is reliable Type 4 signing at 3 cm
using firmware-only PN532 timing/RF configuration changes first.

## Prototype 2 Hardware Contract

| Part | Contract |
| --- | --- |
| MCU | Lolin ESP32-S3 Mini |
| Display | ST7789 240x240 IPS over SPI |
| Display pins | SCLK GPIO3, MOSI GPIO4, RST GPIO7, DC GPIO8, BL GPIO9 |
| Display quirks | SPI mode 3, inverted color enabled, DMA-safe framebuffer, RGB565 byte-swap |
| NFC | PN532 over I2C, SDA GPIO5, SCL GPIO6, address `0x24` |
| Buttons | K1 GPIO13, K2 GPIO12, K3 GPIO11, K4 GPIO10, active-low |
| Battery | GPIO2 ADC, 100K/100K divider, 350 mAh LiPo |
