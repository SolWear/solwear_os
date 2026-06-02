use crate::{
    battery::BatteryMonitor,
    buttons::{ButtonEvent, Buttons},
    display::{Display, DisplayModel, TxOverlayModel},
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
    uptime_ms: u64,
    screen: Screen,
    brightness: u8,
    home_grid: bool,
    selected_index: usize,
    watchface: u8,
    balance_sol: f32,
    tx_overlay: Option<TxOverlayState>,
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
    PingPong,
    Tetris,
    Tamagotchi,
}

#[derive(Debug, Clone)]
struct TxOverlayState {
    from: String,
    to: String,
    network: String,
    lamports: u64,
    fee_lamports: u64,
}

impl SolWearOs {
    pub fn new() -> Self {
        Self {
            display: Display::new(),
            buttons: Buttons::new(),
            battery: BatteryMonitor::new(),
            nfc: NfcService::new(),
            wallet: Wallet::new(),
            uptime_ms: 0,
            screen: Screen::Onboard,
            brightness: 80,
            home_grid: false,
            selected_index: 0,
            watchface: 0,
            balance_sol: 0.0,
            tx_overlay: None,
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
        self.uptime_ms = self.uptime_ms.saturating_add(33);
        self.battery.update();
        if let Some(event) = self.buttons.poll() {
            self.handle_button(event);
        }
        if let Some(command) = crate::protocol::poll_serial_command() {
            self.handle_command(command);
        }
        self.nfc.poll(&mut self.wallet);
        self.handle_nfc_payloads();
        self.render();
        self.emit_status();
    }

    fn handle_button(&mut self, event: ButtonEvent) {
        match event {
            ButtonEvent::Up => self.screen = Screen::Home,
            ButtonEvent::Down => {
                if self.screen == Screen::Home {
                    self.home_grid = true;
                    self.selected_index = (self.selected_index + 1).min(6);
                } else if self.screen == Screen::Settings || self.screen == Screen::Games {
                    self.selected_index = self.selected_index.saturating_add(1).min(
                        if self.screen == Screen::Settings {
                            8
                        } else {
                            2
                        },
                    );
                } else {
                    self.screen = Screen::Stats;
                }
            }
            ButtonEvent::Select => {
                if self.tx_overlay.is_some() {
                    crate::protocol::emit_result("tx", "review-requested");
                    return;
                }
                match self.screen {
                    Screen::Home if self.home_grid => self.open_selected_app(),
                    Screen::Home => self.watchface = (self.watchface + 1) % 6,
                    Screen::Wallet => self.screen = Screen::Receive,
                    Screen::Settings => {
                        if self.selected_index < 6 {
                            self.watchface = self.selected_index as u8;
                        }
                    }
                    Screen::Games => {
                        self.screen = match self.selected_index {
                            0 => Screen::PingPong,
                            1 => Screen::Tetris,
                            _ => Screen::Tamagotchi,
                        };
                    }
                    Screen::PingPong | Screen::Tetris | Screen::Tamagotchi => {
                        crate::protocol::emit_result("game", "action");
                    }
                    _ => self.screen = Screen::Home,
                }
            }
            ButtonEvent::Back => {
                if self.tx_overlay.take().is_some() {
                    crate::protocol::emit_result("tx", "rejected");
                } else if self.screen == Screen::Home && self.home_grid {
                    self.home_grid = false;
                } else if matches!(
                    self.screen,
                    Screen::PingPong | Screen::Tetris | Screen::Tamagotchi
                ) {
                    self.screen = Screen::Games;
                } else {
                    self.screen = Screen::Home;
                }
            }
            ButtonEvent::BackHold => self.wallet.lock(),
            ButtonEvent::UpHold => {
                self.nfc.toggle_enabled();
            }
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
                self.home_grid = false;
                crate::protocol::emit_result("nav", "home");
            }
            Command::NavBack => {
                self.screen = Screen::Home;
                crate::protocol::emit_result("nav", "back");
            }
            Command::NfcStatus => self.nfc.emit_status(),
            Command::NfcReset => self.nfc.reset(),
            Command::NfcDiag => self.nfc.emit_diag(),
            Command::NfcPowerMax => self.nfc.set_power_max(),
            Command::RebootBootsel => crate::protocol::emit_result("reboot", "bootsel-requested"),
            Command::Raw(raw) => crate::protocol::emit_error("unknown-command", &raw),
        }
    }

    fn open_selected_app(&mut self) {
        self.screen = match self.selected_index {
            0 => Screen::Wallet,
            1 => Screen::Receive,
            2 => Screen::Transactions,
            3 => Screen::Stats,
            4 => {
                self.selected_index = 0;
                Screen::Games
            }
            5 => {
                self.selected_index = self.watchface as usize;
                Screen::Settings
            }
            _ => {
                self.wallet.lock();
                Screen::Lock
            }
        };
    }

    fn handle_nfc_payloads(&mut self) {
        if let Some(seed) = self.nfc.take_key_import() {
            crate::protocol::emit_line(&format!(
                "[NFC] key_import received seed_prefix={:02x}{:02x}{:02x}{:02x}",
                seed[0], seed[1], seed[2], seed[3]
            ));
        }

        if let Some(tx) = self.nfc.take_tx_payload() {
            self.screen = Screen::Transactions;
            self.tx_overlay = Some(TxOverlayState {
                from: tx.from.clone(),
                to: tx.to.clone(),
                network: tx.network.clone(),
                lamports: tx.lamports,
                fee_lamports: tx.fee_lamports,
            });
            crate::protocol::emit_line(&format!(
                "[NFC] tx_request from=\"{}\" to=\"{}\" network=\"{}\" lamports={} fee={} tx_len={} session=\"{}\"",
                tx.from, tx.to, tx.network, tx.lamports, tx.fee_lamports, tx.tx_len, tx.session_id
            ));

            let mut sig = [0u8; 64];
            if self.wallet.sign(&tx.tx_bytes[..tx.tx_len], &mut sig) {
                if !self.nfc.queue_sign_response(&sig, &tx.session_id) {
                    crate::protocol::emit_error("nfc-sign-response", "queue failed");
                }
            }
        }
    }

    fn render(&mut self) {
        self.display.begin_frame();
        let screen_name = match self.screen {
            Screen::Onboard => "Onboard",
            Screen::Lock => "Locked",
            Screen::Home => "Home",
            Screen::Wallet => "Wallet",
            Screen::Receive => "Receive",
            Screen::Settings => "Settings",
            Screen::Transactions => "Transactions",
            Screen::Stats => "Stats",
            Screen::Games => "Games",
            Screen::PingPong => "Ping Pong",
            Screen::Tetris => "Tetris",
            Screen::Tamagotchi => "Tamagotchi",
        };
        let tx_overlay = self.tx_overlay.as_ref().map(|tx| TxOverlayModel {
            from: tx.from.as_str(),
            to: tx.to.as_str(),
            network: tx.network.as_str(),
            lamports: tx.lamports,
            fee_lamports: tx.fee_lamports,
        });
        let pubkey_short = self.wallet.public_key_short();
        let model = DisplayModel {
            screen: screen_name,
            home_slide_grid: self.home_grid,
            selected_index: self.selected_index,
            watchface: self.watchface,
            battery_pct: self.battery.percent(),
            charging: self.battery.charging(),
            nfc_armed: self.nfc.enabled(),
            uptime_sec: self.uptime_ms / 1000,
            wallet_name: self.wallet.name(),
            pubkey_short: &pubkey_short,
            balance_sol: self.balance_sol,
            tx_overlay,
        };
        self.display.draw_legacy(&model);
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
            uptime_sec: self.uptime_ms / 1000,
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
