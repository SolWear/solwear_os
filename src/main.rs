mod battery;
mod buttons;
mod display;
mod nfc;
mod os;
mod protocol;
mod wallet;

use os::SolWearOs;

#[cfg(feature = "esp-idf")]
fn platform_init() {
    esp_idf_hal::sys::link_patches();
    println!("SolWearOS Rust platform init complete");
}

#[cfg(not(feature = "esp-idf"))]
fn platform_init() {
    println!("SolWearOS Rust host simulation init complete");
}

fn main() {
    platform_init();
    let mut os = SolWearOs::new();
    os.boot();
    os.run();
}
