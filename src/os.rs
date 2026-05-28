use crate::{
    battery::BatteryMonitor,
    buttons::{ButtonEvent, Buttons},
    display::Display,
    nfc::NfcService,
    protocol::{Command, StatusHeartbeat},
    wallet::Wallet,
};

const VERSION: &str = "0.2.0-rust.0";

pub struct SolWearOs {
    display: Display,
    buttons: Buttons,
    battery: BatteryMonitor,
    nfc: NfcService,
    wallet: Wallet,
    uptime_sec: u64,
    screen: Screen,
    brightness: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Screen {
    Onboard,
    Lock,
    Home,
    Wallet,
    Receive,
    Settings,
    Transactions,
    Stats,
    Games,
}

impl SolWearOs {
    pub fn new() -> Self {
        Self {
            display: Display::new(),
            buttons: Buttons::new(),
            battery: BatteryMonitor::new(),
            nfc: NfcService::new(),
            wallet: Wallet::new(),
            uptime_sec: 0,
            screen: Screen::Onboard,
            brightness: 80,
        }
    }

    pub fn boot(&mut self) {
        self.display.init();
        self.buttons.init();
        self.battery.init();
        self.nfc.init();
        self.wallet.load_public();
        self.screen = if self.wallet.is_onboarded() {
            Screen::Lock
        } else {
            Screen::Onboard
        };
        self.emit_boot_banner();
        self.render();
    }

    pub fn run(&mut self) {
        #[cfg(feature = "host-sim")]
        {
            for _ in 0..3 {
                self.tick();
            }
            return;
        }

        #[cfg(feature = "esp-idf")]
        loop {
            self.tick();
            esp_idf_hal::delay::FreeRtos::delay_ms(33);
        }
    }

    fn tick(&mut self) {
        self.uptime_sec += 1;
        self.battery.update();
        if let Some(event) = self.buttons.poll() {
            self.handle_button(event);
        }
        if let Some(command) = crate::protocol::poll_serial_command() {
            self.handle_command(command);
        }
        self.nfc.poll(&mut self.wallet);
        self.render();
        self.emit_status();
    }

    fn handle_button(&mut self, event: ButtonEvent) {
        match event {
            ButtonEvent::Up => self.screen = Screen::Home,
            ButtonEvent::Down => self.screen = Screen::Stats,
            ButtonEvent::Select => {
                self.screen = match self.screen {
                    Screen::Home => Screen::Wallet,
                    Screen::Wallet => Screen::Receive,
                    _ => Screen::Home,
                }
            }
            ButtonEvent::Back => self.screen = Screen::Home,
            ButtonEvent::BackHold => self.wallet.lock(),
            ButtonEvent::UpHold => self.nfc.toggle_enabled(),
        }
    }

    fn handle_command(&mut self, command: Command) {
        match command {
            Command::StatusNow => self.emit_status(),
            Command::Brightness(value) => {
                self.brightness = value.min(100);
                self.display.set_backlight(self.brightness);
                crate::protocol::emit_result("bri", "ok");
            }
            Command::ClockSync(epoch) => {
                crate::protocol::emit_result("clock", &format!("synced epoch={epoch}"));
            }
            Command::App(name) => {
                self.screen = match name.as_str() {
                    "wallet" => Screen::Wallet,
                    "settings" => Screen::Settings,
                    "stats" | "health" => Screen::Stats,
                    "game" | "games" => Screen::Games,
                    "receive" | "nfc" => Screen::Receive,
                    _ => Screen::Home,
                };
                crate::protocol::emit_result("app", "ok");
            }
            Command::NavHome => {
                self.screen = Screen::Home;
                crate::protocol::emit_result("nav", "home");
            }
            Command::NavBack => {
                self.screen = Screen::Home;
                crate::protocol::emit_result("nav", "back");
            }
            Command::RebootBootsel => crate::protocol::emit_result("reboot", "bootsel-requested"),
            Command::Raw(raw) => crate::protocol::emit_error("unknown-command", &raw),
        }
    }

    fn render(&mut self) {
        self.display.begin_frame();
        self.display
            .draw_status_bar(self.battery.percent(), self.battery.charging());
        self.display.draw_screen(match self.screen {
            Screen::Onboard => "Onboard",
            Screen::Lock => "Locked",
            Screen::Home => "Home",
            Screen::Wallet => "Wallet",
            Screen::Receive => "Receive",
            Screen::Settings => "Settings",
            Screen::Transactions => "Transactions",
            Screen::Stats => "Stats",
            Screen::Games => "Games",
        });
        self.display.flush();
    }

    fn emit_boot_banner(&self) {
        crate::protocol::emit_line(&format!(
            "SolWearOS v{VERSION} proto=prototype-2-esp32s3-lcd13 mcu=esp32s3 display=st7789-240x240-color caps=status,watch-control,apps,nfc,battery,charging"
        ));
    }

    fn emit_status(&self) {
        let status = StatusHeartbeat {
            battery_pct: self.battery.percent(),
            voltage: self.battery.millivolts() as f32 / 1000.0,
            heap_bytes: crate::protocol::free_heap_bytes(),
            steps: 0,
            uptime_sec: self.uptime_sec,
            charging: self.battery.charging(),
            temperature_c: None,
            proto: "prototype-2-esp32s3-lcd13",
            mcu: "esp32s3",
            display: "st7789-240x240-color",
            caps: "status,watch-control,apps,nfc,battery,charging",
        };
        crate::protocol::emit_status(&status);
    }
}
