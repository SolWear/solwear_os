use std::io::{self, BufRead};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Command {
    StatusNow,
    Brightness(u8),
    ClockSync(u64),
    App(String),
    NavHome,
    NavBack,
    RebootBootsel,
    Raw(String),
}

pub struct StatusHeartbeat<'a> {
    pub battery_pct: u8,
    pub voltage: f32,
    pub heap_bytes: u32,
    pub steps: u32,
    pub uptime_sec: u64,
    pub charging: bool,
    pub temperature_c: Option<f32>,
    pub proto: &'a str,
    pub mcu: &'a str,
    pub display: &'a str,
    pub caps: &'a str,
}

pub fn parse_command(input: &str) -> Command {
    let trimmed = input.trim();
    let mut parts = trimmed.split_whitespace();
    match (parts.next(), parts.next(), parts.next()) {
        (Some("status"), Some("now"), _) => Command::StatusNow,
        (Some("bri"), Some(value), _) => value
            .parse::<u8>()
            .map(Command::Brightness)
            .unwrap_or_else(|_| Command::Raw(trimmed.into())),
        (Some("brightness"), Some(value), _) => value
            .parse::<u8>()
            .map(Command::Brightness)
            .unwrap_or_else(|_| Command::Raw(trimmed.into())),
        (Some("clock"), Some("sync"), Some(epoch)) => epoch
            .parse::<u64>()
            .map(Command::ClockSync)
            .unwrap_or_else(|_| Command::Raw(trimmed.into())),
        (Some("app"), Some(name), _) => Command::App(name.to_string()),
        (Some("nav"), Some("home"), _) => Command::NavHome,
        (Some("nav"), Some("back"), _) => Command::NavBack,
        (Some("reboot"), Some("bootsel"), _) => Command::RebootBootsel,
        _ => Command::Raw(trimmed.into()),
    }
}

pub fn poll_serial_command() -> Option<Command> {
    #[cfg(feature = "host-sim")]
    {
        return None;
    }

    #[cfg(feature = "esp-idf")]
    {
        None
    }
}

pub fn emit_status(status: &StatusHeartbeat<'_>) {
    let temp = status
        .temperature_c
        .map(|v| format!(" temp={v:.1}"))
        .unwrap_or_default();
    emit_line(&format!(
        "[STATUS] batt={} volt={:.2} heap={} steps={} uptime={} charging={}{} proto={} mcu={} display={} caps={}",
        status.battery_pct,
        status.voltage,
        status.heap_bytes,
        status.steps,
        status.uptime_sec,
        u8::from(status.charging),
        temp,
        status.proto,
        status.mcu,
        status.display,
        status.caps
    ));
}

pub fn emit_result(topic: &str, message: &str) {
    emit_line(&format!(
        "[RESULT] topic={topic} status=ok message=\"{message}\""
    ));
}

pub fn emit_error(code: &str, message: &str) {
    emit_line(&format!("[ERROR] code={code} message=\"{message}\""));
}

pub fn emit_line(line: &str) {
    #[cfg(feature = "esp-idf")]
    log::info!("{line}");

    #[cfg(not(feature = "esp-idf"))]
    println!("{line}");
}

pub fn free_heap_bytes() -> u32 {
    #[cfg(feature = "esp-idf")]
    {
        unsafe { esp_idf_svc::sys::esp_get_free_heap_size() as u32 }
    }

    #[cfg(not(feature = "esp-idf"))]
    {
        192 * 1024
    }
}

pub fn read_host_command_line() -> Option<Command> {
    let stdin = io::stdin();
    let mut line = String::new();
    stdin.lock().read_line(&mut line).ok()?;
    if line.trim().is_empty() {
        None
    } else {
        Some(parse_command(&line))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_status_command() {
        assert_eq!(parse_command("status now"), Command::StatusNow);
    }

    #[test]
    fn parses_brightness_command() {
        assert_eq!(parse_command("bri 42"), Command::Brightness(42));
    }

    #[test]
    fn preserves_unknown_commands() {
        assert_eq!(
            parse_command("weird thing"),
            Command::Raw("weird thing".into())
        );
    }
}
