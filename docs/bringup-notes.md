# Rust Bring-Up Notes

## Verified

- Rust, MSVC Build Tools, `rustfmt`, `espup`, `ldproxy`, and `espflash` were installed.
- Host simulation compiles with `cargo check`.
- Host tests pass with `cargo test`.
- ESP Rust STD toolchain for `esp32s3` was installed to `D:\rustup` / `D:\cargo` through `espup`.

## Current ESP Build Status

The ESP-IDF target build progresses through Rust `build-std` and dependency compilation, but local tool installation is still blocked by the Windows ESP-IDF tools location:

- Default `C:\Users\dedpu\.espressif` is too full for `esp-clang`.
- Setting `ESP_IDF_TOOLS_INSTALL_DIR=D:\espressif` avoids C: pressure, but `esp-idf-sys` currently reports `Matching variant not found`.
- A local ESP-IDF `tools.json` backup was created at `D:\esp-idf-v6.0.1\esp-idf-v6.0.1\tools\tools.json.solwear-bak`; the active file was sanitized to remove unsupported `win-arm64` entries for the current x64 host.

## Next Bring-Up Step

Finish ESP-IDF Rust tool installation on a drive with enough free space, then build from a short path:

```powershell
robocopy E:\SOLWEAR\_publish_solwear_os E:\swo /E /XD .git target build
cd E:\swo
cargo +esp build -Z build-std=std,panic_abort --release --no-default-features --features esp-idf --target xtensa-esp32s3-espidf
```
