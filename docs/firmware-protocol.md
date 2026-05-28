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
```

## Structured Results

```text
[RESULT] topic=<topic> status=ok message="<message>"
[ERROR] code=<code> message="<message>"
```

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
