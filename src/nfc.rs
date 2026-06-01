use crate::wallet::Wallet;

pub const PIN_NFC_SDA: i32 = 5;
pub const PIN_NFC_SCL: i32 = 6;
pub const PN532_I2C_ADDR: u8 = 0x24;
pub const NFC_TX_BUFFER_LEN: usize = 220;

const ACK_FRAME: [u8; 6] = [0x00, 0x00, 0xff, 0x00, 0xff, 0x00];
const TYPE4_TARGET_TIMEOUT_MS: u16 = 600;
const TYPE4_APDU_LIMIT: usize = 18;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyncEvent {
    Idle,
    PhoneNear,
    WalletShared,
    SignRequest,
    SignResponse,
    Error,
}

#[derive(Debug, Clone)]
pub struct SyncStatus {
    pub event: SyncEvent,
    pub counter: u32,
    pub target_active: bool,
    pub message: String,
}

#[derive(Debug, Clone)]
pub struct NfcTxPayload {
    pub from: String,
    pub to: String,
    pub network: String,
    pub lamports: u64,
    pub fee_lamports: u64,
    pub tx_bytes: [u8; NFC_TX_BUFFER_LEN],
    pub tx_len: usize,
    pub session_id: String,
    pub valid: bool,
}

impl Default for NfcTxPayload {
    fn default() -> Self {
        Self {
            from: "?".into(),
            to: "?".into(),
            network: "devnet".into(),
            lamports: 0,
            fee_lamports: 0,
            tx_bytes: [0; NFC_TX_BUFFER_LEN],
            tx_len: 0,
            session_id: String::new(),
            valid: false,
        }
    }
}

#[derive(Debug, Clone)]
struct TargetFile {
    bytes: [u8; 1024],
    len: usize,
    response_pending: bool,
}

impl TargetFile {
    fn new() -> Self {
        Self {
            bytes: [0; 1024],
            len: 2,
            response_pending: false,
        }
    }

    fn set_wallet(&mut self, pubkey: &[u8; 32]) -> Result<(), &'static str> {
        let ndef = build_wallet_ndef(pubkey)?;
        self.write_ndef_file(&ndef)
    }

    fn set_sign_response(&mut self, sig: &[u8; 64], session_id: &str) -> Result<(), &'static str> {
        let ndef = build_sign_response_ndef(sig, session_id)?;
        self.write_ndef_file(&ndef)?;
        self.response_pending = true;
        Ok(())
    }

    fn write_ndef_file(&mut self, ndef: &[u8]) -> Result<(), &'static str> {
        if ndef.len() + 2 > self.bytes.len() || ndef.len() > u16::MAX as usize {
            return Err("ndef-too-large");
        }
        self.bytes = [0; 1024];
        self.bytes[0] = (ndef.len() >> 8) as u8;
        self.bytes[1] = ndef.len() as u8;
        self.bytes[2..2 + ndef.len()].copy_from_slice(ndef);
        self.len = ndef.len() + 2;
        Ok(())
    }
}

pub struct NfcService {
    enabled: bool,
    ready: bool,
    tx_payload: Option<NfcTxPayload>,
    key_import: Option<[u8; 32]>,
    sync: SyncStatus,
    target: TargetFile,
    selected_file: u16,
    diag_sessions: u32,
    diag_apdus: u32,
    diag_errors: u32,
    #[cfg(feature = "esp-idf")]
    pn532: pn532_hw::Pn532,
}

impl NfcService {
    pub fn new() -> Self {
        Self {
            enabled: true,
            ready: false,
            tx_payload: None,
            key_import: None,
            sync: SyncStatus {
                event: SyncEvent::Idle,
                counter: 0,
                target_active: false,
                message: String::new(),
            },
            target: TargetFile::new(),
            selected_file: 0,
            diag_sessions: 0,
            diag_apdus: 0,
            diag_errors: 0,
            #[cfg(feature = "esp-idf")]
            pn532: pn532_hw::Pn532::new(),
        }
    }

    pub fn init(&mut self) {
        crate::protocol::emit_line(&format!(
            "[HAL] nfc init pn532 i2c_sda={} i2c_scl={} addr=0x{:02x} mode=type4-target",
            PIN_NFC_SDA, PIN_NFC_SCL, PN532_I2C_ADDR
        ));
        self.ensure_init();
    }

    pub fn ensure_init(&mut self) -> bool {
        if self.ready {
            return true;
        }

        #[cfg(feature = "esp-idf")]
        {
            match self.pn532.ensure_init() {
                Ok(fw) => {
                    self.ready = true;
                    crate::protocol::emit_line(&format!(
                        "[NFC] pn532 ready ic=0x{:02x} ver={}.{} support=0x{:02x}",
                        fw[0], fw[1], fw[2], fw[3]
                    ));
                    return true;
                }
                Err(err) => {
                    self.diag_errors = self.diag_errors.wrapping_add(1);
                    self.sync_event(SyncEvent::Error, &format!("PN532 init failed: {err}"));
                    return false;
                }
            }
        }

        #[cfg(not(feature = "esp-idf"))]
        {
            self.ready = true;
            crate::protocol::emit_line("[NFC] host protocol backend ready");
            true
        }
    }

    pub fn toggle_enabled(&mut self) {
        self.enabled = !self.enabled;
        crate::protocol::emit_result("nfc", if self.enabled { "enabled" } else { "disabled" });
    }

    pub fn set_power_max(&mut self) {
        #[cfg(feature = "esp-idf")]
        {
            match self.pn532.set_max_rf_power() {
                Ok(()) => crate::protocol::emit_result("nfc", "rf-power=max"),
                Err(err) => crate::protocol::emit_error("nfc-rf-power", &err),
            }
        }

        #[cfg(not(feature = "esp-idf"))]
        crate::protocol::emit_result("nfc", "rf-power=max host");
    }

    pub fn reset(&mut self) {
        self.ready = false;
        self.selected_file = 0;
        self.target = TargetFile::new();
        #[cfg(feature = "esp-idf")]
        self.pn532.shutdown();
        crate::protocol::emit_result("nfc", "reset");
    }

    pub fn emit_status(&self) {
        crate::protocol::emit_line(&format!(
            "[NFC] status enabled={} ready={} event={:?} counter={} target_active={} sessions={} apdus={} errors={} message=\"{}\"",
            u8::from(self.enabled),
            u8::from(self.ready),
            self.sync.event,
            self.sync.counter,
            u8::from(self.sync.target_active),
            self.diag_sessions,
            self.diag_apdus,
            self.diag_errors,
            self.sync.message
        ));
    }

    pub fn emit_diag(&self) {
        crate::protocol::emit_line(&format!(
            "[NFC] diag path=type4-target tx_valid={} key_import={} target_len={} response_pending={} range_goal_cm=3",
            u8::from(self.tx_payload.as_ref().is_some_and(|p| p.valid)),
            u8::from(self.key_import.is_some()),
            self.target.len,
            u8::from(self.target.response_pending)
        ));
    }

    pub fn take_tx_payload(&mut self) -> Option<NfcTxPayload> {
        self.tx_payload.take()
    }

    pub fn take_key_import(&mut self) -> Option<[u8; 32]> {
        self.key_import.take()
    }

    pub fn queue_sign_response(&mut self, sig: &[u8; 64], session_id: &str) -> bool {
        match self.target.set_sign_response(sig, session_id) {
            Ok(()) => {
                self.sync_event(SyncEvent::SignResponse, "Signature ready");
                crate::protocol::emit_line("[NFC] sign_response queued");
                true
            }
            Err(err) => {
                self.sync_event(SyncEvent::Error, "NFC signature build failed");
                crate::protocol::emit_error("nfc-sign-response", err);
                false
            }
        }
    }

    pub fn poll(&mut self, wallet: &mut Wallet) {
        if !self.enabled {
            return;
        }
        if !self.ensure_init() {
            return;
        }

        if !self.target.response_pending {
            if let Err(err) = self.target.set_wallet(wallet.public_key()) {
                self.sync_event(SyncEvent::Error, "NFC wallet build failed");
                crate::protocol::emit_error("nfc-wallet-ndef", err);
                return;
            }
        }

        #[cfg(feature = "esp-idf")]
        self.poll_hardware_target();

        #[cfg(not(feature = "esp-idf"))]
        {
            let _ = wallet;
        }
    }

    #[cfg(feature = "esp-idf")]
    fn poll_hardware_target(&mut self) {
        match self.pn532.tg_init_as_target(TYPE4_TARGET_TIMEOUT_MS) {
            Ok(()) => {
                self.diag_sessions = self.diag_sessions.wrapping_add(1);
                self.sync_event(SyncEvent::PhoneNear, "Phone touched");
            }
            Err(_) => {
                self.sync.target_active = false;
                return;
            }
        }

        let mut request_written = false;
        let mut response_read = false;
        for _ in 0..TYPE4_APDU_LIMIT {
            let mut apdu = [0u8; 260];
            let apdu_len = match self.pn532.tg_get_data(&mut apdu) {
                Ok(len) => len,
                Err(_) => break,
            };
            self.diag_apdus = self.diag_apdus.wrapping_add(1);

            let mut resp = [0u8; 270];
            let resp_len = self.target_apdu_response(
                &apdu[..apdu_len],
                &mut resp,
                &mut request_written,
                &mut response_read,
            );
            if self.pn532.tg_set_data(&resp[..resp_len]).is_err() {
                self.diag_errors = self.diag_errors.wrapping_add(1);
                break;
            }
            if request_written {
                self.sync_event(SyncEvent::SignRequest, "Sign request received");
                break;
            }
        }

        if response_read && self.target.response_pending {
            self.target = TargetFile::new();
            self.sync_event(SyncEvent::SignResponse, "Signature sent");
            crate::protocol::emit_line("[NFC] sign_response read by phone");
        } else if !request_written && !response_read {
            self.sync_event(SyncEvent::WalletShared, "Open phone to share");
        }
        self.sync.target_active = false;
    }

    fn target_apdu_response(
        &mut self,
        apdu: &[u8],
        resp: &mut [u8],
        request_written: &mut bool,
        response_read: &mut bool,
    ) -> usize {
        target_apdu_response(
            apdu,
            resp,
            &mut self.selected_file,
            &mut self.target,
            &mut self.tx_payload,
            &mut self.key_import,
            request_written,
            response_read,
        )
    }

    fn sync_event(&mut self, event: SyncEvent, message: &str) {
        self.sync.event = event;
        self.sync.counter = self.sync.counter.wrapping_add(1);
        self.sync.target_active = event == SyncEvent::PhoneNear;
        self.sync.message.clear();
        self.sync.message.push_str(message);
        crate::protocol::emit_line(&format!("[NFC] event={event:?} message=\"{message}\""));
    }
}

fn target_apdu_response(
    apdu: &[u8],
    resp: &mut [u8],
    selected_file: &mut u16,
    target: &mut TargetFile,
    tx_payload: &mut Option<NfcTxPayload>,
    key_import: &mut Option<[u8; 32]>,
    request_written: &mut bool,
    response_read: &mut bool,
) -> usize {
    const CC_FILE: [u8; 15] = [
        0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34, 0x04, 0x06, 0xe1, 0x04, 0x04, 0x00, 0x00, 0x00,
    ];
    const SW_OK: [u8; 2] = [0x90, 0x00];
    const SW_NOT_FOUND: [u8; 2] = [0x6a, 0x82];
    const SW_WRONG: [u8; 2] = [0x6b, 0x00];
    const SW_UNSUPPORTED: [u8; 2] = [0x6a, 0x81];

    if apdu.len() < 4 || resp.len() < 2 {
        resp[..2].copy_from_slice(&SW_WRONG);
        return 2;
    }

    let ins = apdu[1];
    let p1 = apdu[2];
    let p2 = apdu[3];
    let lc = apdu.get(4).copied().unwrap_or(0) as usize;

    match ins {
        0xa4 => {
            if p1 == 0x04
                && apdu.len() >= 12
                && lc == 0x07
                && apdu[5..12] == [0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01]
            {
                *selected_file = 0;
                resp[..2].copy_from_slice(&SW_OK);
                return 2;
            }
            if p1 == 0x00 && p2 == 0x0c && apdu.len() >= 7 && lc == 0x02 {
                let file_id = u16::from_be_bytes([apdu[5], apdu[6]]);
                if file_id == 0xe103 || file_id == 0xe104 {
                    *selected_file = file_id;
                    resp[..2].copy_from_slice(&SW_OK);
                    return 2;
                }
            }
            resp[..2].copy_from_slice(&SW_NOT_FOUND);
            2
        }
        0xb0 => {
            let off = u16::from_be_bytes([p1, p2]) as usize;
            let (file, file_len): (&[u8], usize) = match *selected_file {
                0xe103 => (&CC_FILE, CC_FILE.len()),
                0xe104 => (&target.bytes, target.len),
                _ => {
                    resp[..2].copy_from_slice(&SW_NOT_FOUND);
                    return 2;
                }
            };
            if off >= file_len {
                resp[..2].copy_from_slice(&SW_WRONG);
                return 2;
            }
            let le = apdu.last().copied().unwrap_or(0);
            let mut n = if le == 0 { 256 } else { le as usize };
            n = n.min(file_len - off).min(resp.len() - 2);
            resp[..n].copy_from_slice(&file[off..off + n]);
            resp[n..n + 2].copy_from_slice(&SW_OK);
            if *selected_file == 0xe104 && target.response_pending && off == 0 {
                *response_read = true;
            }
            n + 2
        }
        0xd6 => {
            if *selected_file != 0xe104 || apdu.len() < 5 {
                resp[..2].copy_from_slice(&SW_NOT_FOUND);
                return 2;
            }
            let off = u16::from_be_bytes([p1, p2]) as usize;
            let data_len = lc;
            if apdu.len() < 5 + data_len || off + data_len > target.bytes.len() {
                resp[..2].copy_from_slice(&SW_WRONG);
                return 2;
            }
            target.bytes[off..off + data_len].copy_from_slice(&apdu[5..5 + data_len]);
            target.len = target.len.max(off + data_len);
            let ndef_len = u16::from_be_bytes([target.bytes[0], target.bytes[1]]) as usize;
            if ndef_len > 0 && 2 + ndef_len <= target.len {
                if parse_ndef(&target.bytes[2..2 + ndef_len], tx_payload, key_import) {
                    *request_written = true;
                }
            }
            resp[..2].copy_from_slice(&SW_OK);
            2
        }
        _ => {
            resp[..2].copy_from_slice(&SW_UNSUPPORTED);
            2
        }
    }
}

fn build_wallet_ndef(pubkey: &[u8; 32]) -> Result<Vec<u8>, &'static str> {
    let pubkey = base58_encode(pubkey)?;
    let json = format!(r#"{{"version":1,"pubkey":"{pubkey}","network":"devnet"}}"#);
    build_external_ndef("solwear:wallet", json.as_bytes())
}

fn build_sign_response_ndef(sig: &[u8; 64], session_id: &str) -> Result<Vec<u8>, &'static str> {
    let sig_b64 = b64_encode(sig);
    let json = format!(r#"{{"version":1,"signature":"{sig_b64}","session_id":"{session_id}"}}"#);
    build_external_ndef("solwear:sign_response", json.as_bytes())
}

fn build_external_ndef(record_type: &str, payload: &[u8]) -> Result<Vec<u8>, &'static str> {
    let type_bytes = record_type.as_bytes();
    if type_bytes.len() > 255 {
        return Err("type-too-large");
    }

    let short_record = payload.len() <= 255;
    let mut out = Vec::with_capacity(8 + type_bytes.len() + payload.len());
    out.push(if short_record { 0xd4 } else { 0xc4 });
    out.push(type_bytes.len() as u8);
    if short_record {
        out.push(payload.len() as u8);
    } else {
        out.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    }
    out.extend_from_slice(type_bytes);
    out.extend_from_slice(payload);
    Ok(out)
}

fn parse_ndef(
    data: &[u8],
    tx_payload: &mut Option<NfcTxPayload>,
    key_import: &mut Option<[u8; 32]>,
) -> bool {
    let mut found = false;
    let mut i = 0;
    while i < data.len() {
        if data[i] == 0xfe {
            break;
        }
        if data[i] == 0x00 {
            i += 1;
            continue;
        }

        let flags = data[i];
        i += 1;
        if i >= data.len() {
            break;
        }
        let type_len = data[i] as usize;
        i += 1;
        if i >= data.len() {
            break;
        }

        let payload_len = if flags & 0x10 != 0 {
            let n = data[i] as usize;
            i += 1;
            n
        } else {
            if i + 4 > data.len() {
                break;
            }
            let n = u32::from_be_bytes([data[i], data[i + 1], data[i + 2], data[i + 3]]) as usize;
            i += 4;
            n
        };

        if flags & 0x08 != 0 {
            if i >= data.len() {
                break;
            }
            i += data[i] as usize + 1;
        }

        if i + type_len > data.len() {
            break;
        }
        let record_type = core::str::from_utf8(&data[i..i + type_len]).unwrap_or("");
        i += type_len;

        if i + payload_len > data.len() {
            break;
        }
        let payload = &data[i..i + payload_len];
        i += payload_len;

        if record_type.contains("sign_request") {
            let json = core::str::from_utf8(payload).unwrap_or("");
            let mut parsed = NfcTxPayload::default();
            if let Some(value) = json_string_value(json, "tx_bytes") {
                let decoded = b64_decode(&value, &mut parsed.tx_bytes);
                parsed.tx_len = decoded;
            }
            if let Some(value) = json_string_value(json, "from") {
                parsed.from = value;
            }
            if let Some(value) = json_string_value(json, "to") {
                parsed.to = value;
            }
            if let Some(value) = json_string_value(json, "network") {
                parsed.network = value;
            }
            if let Some(value) = json_string_value(json, "session_id") {
                parsed.session_id = value;
            }
            parsed.lamports = json_u64_value(json, "lamports").unwrap_or(0);
            parsed.fee_lamports = json_u64_value(json, "fee_lamports").unwrap_or(0);
            parsed.valid = true;
            crate::protocol::emit_line(&format!(
                "[NFC] sign_request parsed tx_len={} session=\"{}\"",
                parsed.tx_len, parsed.session_id
            ));
            *tx_payload = Some(parsed);
            found = true;
        } else if record_type.contains("key_import") && payload.len() >= 32 {
            let mut seed = [0u8; 32];
            seed.copy_from_slice(&payload[..32]);
            *key_import = Some(seed);
            found = true;
        }

        if flags & 0x40 != 0 {
            break;
        }
    }
    found
}

fn json_string_value(json: &str, key: &str) -> Option<String> {
    let needle = format!("\"{key}\"");
    let mut p = json.find(&needle)? + needle.len();
    p += json[p..].find(':')? + 1;
    let bytes = json.as_bytes();
    while p < bytes.len() && matches!(bytes[p], b' ' | b'\t' | b'\r' | b'\n') {
        p += 1;
    }
    if bytes.get(p) != Some(&b'"') {
        return None;
    }
    p += 1;
    let start = p;
    while p < bytes.len() && bytes[p] != b'"' {
        p += 1;
    }
    (p > start).then(|| json[start..p].to_string())
}

fn json_u64_value(json: &str, key: &str) -> Option<u64> {
    let needle = format!("\"{key}\"");
    let mut p = json.find(&needle)? + needle.len();
    p += json[p..].find(':')? + 1;
    let bytes = json.as_bytes();
    while p < bytes.len() && matches!(bytes[p], b' ' | b'\t' | b'\r' | b'\n') {
        p += 1;
    }
    let start = p;
    while p < bytes.len() && bytes[p].is_ascii_digit() {
        p += 1;
    }
    (p > start).then(|| json[start..p].parse().ok()).flatten()
}

fn pn532_frame(cmd: &[u8]) -> Result<Vec<u8>, &'static str> {
    if cmd.is_empty() || cmd.len() > 262 {
        return Err("bad-command-len");
    }
    let len = (cmd.len() + 1) as u8;
    let mut sum = 0xd4u8;
    let mut frame = Vec::with_capacity(cmd.len() + 8);
    frame.extend_from_slice(&[0x00, 0x00, 0xff, len, 0u8.wrapping_sub(len), 0xd4]);
    for &b in cmd {
        frame.push(b);
        sum = sum.wrapping_add(b);
    }
    frame.push(0u8.wrapping_sub(sum));
    frame.push(0x00);
    Ok(frame)
}

fn parse_pn532_response(
    expected_code: u8,
    raw: &[u8],
    out: &mut [u8],
) -> Result<usize, &'static str> {
    let mut p = 0;
    if raw.first() == Some(&0x01) {
        p = 1;
    }
    while p < raw.len() && raw[p] == 0 {
        p += 1;
    }
    if p >= raw.len() || raw[p] != 0xff {
        return Err("bad-preamble");
    }
    p += 1;
    if p + 1 >= raw.len() {
        return Err("short-len");
    }
    let len = raw[p] as usize;
    let lcs = raw[p + 1];
    p += 2;
    if (len as u8).wrapping_add(lcs) != 0 || len < 2 {
        return Err("bad-len-checksum");
    }
    if p + len >= raw.len() {
        return Err("short-frame");
    }
    if raw[p] != 0xd5 || raw[p + 1] != expected_code {
        return Err("unexpected-response");
    }
    let mut sum = 0u8;
    for b in &raw[p..p + len] {
        sum = sum.wrapping_add(*b);
    }
    if sum.wrapping_add(raw[p + len]) != 0 {
        return Err("bad-data-checksum");
    }
    let payload_len = len - 2;
    let n = payload_len.min(out.len());
    out[..n].copy_from_slice(&raw[p + 2..p + 2 + n]);
    Ok(n)
}

fn b64_encode(input: &[u8]) -> String {
    const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(input.len().div_ceil(3) * 4);
    let mut i = 0;
    while i < input.len() {
        let b0 = input[i];
        let b1 = input.get(i + 1).copied().unwrap_or(0);
        let b2 = input.get(i + 2).copied().unwrap_or(0);
        out.push(B64[(b0 >> 2) as usize] as char);
        out.push(B64[(((b0 & 0x03) << 4) | (b1 >> 4)) as usize] as char);
        out.push(if i + 1 < input.len() {
            B64[(((b1 & 0x0f) << 2) | (b2 >> 6)) as usize] as char
        } else {
            '='
        });
        out.push(if i + 2 < input.len() {
            B64[(b2 & 0x3f) as usize] as char
        } else {
            '='
        });
        i += 3;
    }
    out
}

fn b64_decode(input: &str, out: &mut [u8]) -> usize {
    fn val(c: u8) -> Option<u8> {
        match c {
            b'A'..=b'Z' => Some(c - b'A'),
            b'a'..=b'z' => Some(c - b'a' + 26),
            b'0'..=b'9' => Some(c - b'0' + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }

    let bytes = input.as_bytes();
    let mut written = 0;
    let mut i = 0;
    while i + 3 < bytes.len() && written < out.len() {
        let Some(v0) = val(bytes[i]) else { break };
        let Some(v1) = val(bytes[i + 1]) else { break };
        let v2 = if bytes[i + 2] == b'=' {
            0
        } else {
            val(bytes[i + 2]).unwrap_or(0)
        };
        let v3 = if bytes[i + 3] == b'=' {
            0
        } else {
            val(bytes[i + 3]).unwrap_or(0)
        };
        out[written] = (v0 << 2) | (v1 >> 4);
        written += 1;
        if written < out.len() && bytes[i + 2] != b'=' {
            out[written] = (v1 << 4) | (v2 >> 2);
            written += 1;
        }
        if written < out.len() && bytes[i + 3] != b'=' {
            out[written] = (v2 << 6) | v3;
            written += 1;
        }
        i += 4;
    }
    written
}

fn base58_encode(input: &[u8]) -> Result<String, &'static str> {
    const B58: &[u8; 58] = b"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if input.is_empty() {
        return Ok(String::new());
    }
    let mut buf = input.to_vec();
    let zeros = buf.iter().take_while(|&&b| b == 0).count();
    let mut tmp = Vec::with_capacity(72);
    let mut start = zeros;
    while start < buf.len() {
        let mut rem = 0u32;
        for b in buf.iter_mut().skip(start) {
            let v = (rem << 8) | u32::from(*b);
            *b = (v / 58) as u8;
            rem = v % 58;
        }
        tmp.push(B58[rem as usize]);
        while start < buf.len() && buf[start] == 0 {
            start += 1;
        }
    }
    let mut out = String::with_capacity(zeros + tmp.len());
    for _ in 0..zeros {
        out.push('1');
    }
    for b in tmp.iter().rev() {
        out.push(*b as char);
    }
    Ok(out)
}

#[cfg(feature = "esp-idf")]
mod pn532_hw {
    use super::{
        parse_pn532_response, pn532_frame, ACK_FRAME, PIN_NFC_SCL, PIN_NFC_SDA, PN532_I2C_ADDR,
    };
    use esp_idf_hal::delay::FreeRtos;
    use esp_idf_hal::sys;

    pub struct Pn532 {
        bus: sys::i2c_master_bus_handle_t,
        dev: sys::i2c_master_dev_handle_t,
    }

    impl Pn532 {
        pub fn new() -> Self {
            Self {
                bus: core::ptr::null_mut(),
                dev: core::ptr::null_mut(),
            }
        }

        pub fn ensure_init(&mut self) -> Result<[u8; 4], String> {
            if self.dev.is_null() {
                self.init_i2c()?;
            }
            let mut fw = [0u8; 8];
            let n = self.cmd(&[0x02], 0x03, &mut fw, 120)?;
            if n < 4 {
                return Err("short firmware response".into());
            }
            self.cmd(&[0x14, 0x01, 0x14, 0x01], 0x15, &mut [], 120)?;
            self.set_max_rf_power()?;
            Ok([fw[0], fw[1], fw[2], fw[3]])
        }

        pub fn shutdown(&mut self) {
            unsafe {
                if !self.dev.is_null() {
                    sys::i2c_master_bus_rm_device(self.dev);
                    self.dev = core::ptr::null_mut();
                }
                if !self.bus.is_null() {
                    sys::i2c_del_master_bus(self.bus);
                    self.bus = core::ptr::null_mut();
                }
            }
        }

        pub fn set_max_rf_power(&mut self) -> Result<(), String> {
            self.write_register(0x6311, 0x3f)?;
            self.write_register(0x6312, 0x83)?;
            self.write_register(0x6309, 0x3f)?;
            self.rf_configuration(0x05, &[0xff, 0x01, 0x05])
        }

        pub fn tg_init_as_target(&mut self, timeout_ms: u16) -> Result<(), String> {
            let cmd = [
                0x8c, 0x05, 0x04, 0x00, 0xa5, 0xb6, 0xc7, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            ];
            self.cmd(&cmd, 0x8d, &mut [], timeout_ms).map(|_| ())
        }

        pub fn tg_get_data(&mut self, data: &mut [u8]) -> Result<usize, String> {
            let mut out = [0u8; 270];
            let n = self.cmd(&[0x86], 0x87, &mut out, 220)?;
            if n < 1 || out[0] != 0x00 {
                return Err("tg-get-data-status".into());
            }
            let copy = (n - 1).min(data.len());
            data[..copy].copy_from_slice(&out[1..1 + copy]);
            Ok(copy)
        }

        pub fn tg_set_data(&mut self, data: &[u8]) -> Result<(), String> {
            if data.len() + 1 > 263 {
                return Err("tg-set-data-too-large".into());
            }
            let mut cmd = [0u8; 264];
            cmd[0] = 0x8e;
            cmd[1..1 + data.len()].copy_from_slice(data);
            let mut out = [0u8; 8];
            let n = self.cmd(&cmd[..1 + data.len()], 0x8f, &mut out, 220)?;
            if n >= 1 && out[0] == 0x00 {
                Ok(())
            } else {
                Err("tg-set-data-status".into())
            }
        }

        fn init_i2c(&mut self) -> Result<(), String> {
            unsafe {
                let mut bus_cfg: sys::i2c_master_bus_config_t = core::mem::zeroed();
                bus_cfg.i2c_port = sys::i2c_port_t_I2C_NUM_0 as i32;
                bus_cfg.sda_io_num = PIN_NFC_SDA;
                bus_cfg.scl_io_num = PIN_NFC_SCL;
                bus_cfg.__bindgen_anon_1.clk_source =
                    sys::soc_periph_i2c_clk_src_t_I2C_CLK_SRC_DEFAULT;
                bus_cfg.glitch_ignore_cnt = 7;
                bus_cfg.flags.set_enable_internal_pullup(1);
                check(sys::i2c_new_master_bus(&bus_cfg, &mut self.bus), "i2c bus")?;

                let mut dev_cfg: sys::i2c_device_config_t = core::mem::zeroed();
                dev_cfg.dev_addr_length = sys::i2c_addr_bit_len_t_I2C_ADDR_BIT_LEN_7;
                dev_cfg.device_address = PN532_I2C_ADDR as u16;
                dev_cfg.scl_speed_hz = 100_000;
                check(
                    sys::i2c_master_bus_add_device(self.bus, &dev_cfg, &mut self.dev),
                    "i2c dev",
                )?;
            }
            Ok(())
        }

        fn write_register(&mut self, addr: u16, value: u8) -> Result<(), String> {
            let cmd = [0x08, (addr >> 8) as u8, addr as u8, value];
            let mut out = [0u8; 4];
            let n = self.cmd(&cmd, 0x09, &mut out, 300)?;
            if n >= 1 && out[0] != 0x00 {
                Err(format!(
                    "write-register 0x{addr:04x} status=0x{:02x}",
                    out[0]
                ))
            } else {
                Ok(())
            }
        }

        fn rf_configuration(&mut self, item: u8, data: &[u8]) -> Result<(), String> {
            let mut cmd = [0u8; 17];
            cmd[0] = 0x32;
            cmd[1] = item;
            let n = data.len().min(15);
            cmd[2..2 + n].copy_from_slice(&data[..n]);
            self.cmd(&cmd[..2 + n], 0x33, &mut [], 300).map(|_| ())
        }

        fn cmd(
            &mut self,
            cmd: &[u8],
            expected_code: u8,
            out: &mut [u8],
            timeout_ms: u16,
        ) -> Result<usize, String> {
            let frame = pn532_frame(cmd).map_err(str::to_string)?;
            self.write(&frame, 160)?;
            self.read_ack(220)?;
            self.read_response(expected_code, out, timeout_ms)
        }

        fn write(&mut self, data: &[u8], timeout_ms: u32) -> Result<(), String> {
            unsafe {
                check(
                    sys::i2c_master_transmit(
                        self.dev,
                        data.as_ptr(),
                        data.len(),
                        timeout_ms as i32,
                    ),
                    "pn532 write",
                )
            }
        }

        fn read(&mut self, data: &mut [u8], timeout_ms: u32) -> Result<(), String> {
            unsafe {
                check(
                    sys::i2c_master_receive(
                        self.dev,
                        data.as_mut_ptr(),
                        data.len(),
                        timeout_ms as i32,
                    ),
                    "pn532 read",
                )
            }
        }

        fn wait_ready(&mut self, timeout_ms: u16) -> bool {
            let start = unsafe { sys::esp_timer_get_time() };
            while ((unsafe { sys::esp_timer_get_time() } - start) / 1000) < i64::from(timeout_ms) {
                let mut status = [0u8; 1];
                if self.read(&mut status, 60).is_ok() && (status[0] & 0x01) != 0 {
                    return true;
                }
                FreeRtos::delay_ms(10);
            }
            false
        }

        fn read_ack(&mut self, timeout_ms: u16) -> Result<(), String> {
            if !self.wait_ready(timeout_ms) {
                return Err("ack-timeout".into());
            }
            let mut buf = [0u8; 7];
            self.read(&mut buf, 120)?;
            if buf[1..] == ACK_FRAME {
                Ok(())
            } else {
                Err(format!("bad-ack {:02x?}", &buf[1..]))
            }
        }

        fn read_response(
            &mut self,
            expected_code: u8,
            out: &mut [u8],
            timeout_ms: u16,
        ) -> Result<usize, String> {
            if !self.wait_ready(timeout_ms) {
                return Err("response-timeout".into());
            }
            let mut raw = [0u8; 300];
            self.read(&mut raw, 180)?;
            parse_pn532_response(expected_code, &raw, out).map_err(str::to_string)
        }
    }

    fn check(rc: i32, what: &str) -> Result<(), String> {
        if rc == sys::ESP_OK {
            Ok(())
        } else {
            Err(format!("{what} rc={rc}"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_legacy_pn532_frame() {
        let frame = pn532_frame(&[0x02]).unwrap();
        assert_eq!(
            frame,
            vec![0x00, 0x00, 0xff, 0x02, 0xfe, 0xd4, 0x02, 0x2a, 0x00]
        );
    }

    #[test]
    fn parses_pn532_response_payload() {
        let raw = [
            0x01, 0x00, 0x00, 0xff, 0x06, 0xfa, 0xd5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xe8, 0x00,
        ];
        let mut out = [0u8; 8];
        let n = parse_pn532_response(0x03, &raw, &mut out).unwrap();
        assert_eq!(&out[..n], &[0x32, 0x01, 0x06, 0x07]);
    }

    #[test]
    fn builds_wallet_ndef_as_external_type() {
        let pubkey = [0u8; 32];
        let ndef = build_wallet_ndef(&pubkey).unwrap();
        assert_eq!(ndef[0], 0xd4);
        assert!(ndef
            .windows("solwear:wallet".len())
            .any(|chunk| chunk == b"solwear:wallet"));
    }

    #[test]
    fn parses_solwear_and_solvare_sign_requests() {
        for record_type in [
            "solwear:sign_request",
            "solvare:sign_request",
            "sign_request",
        ] {
            let json = br#"{"version":1,"tx_bytes":"AQIDBA==","from":"a","to":"b","network":"devnet","lamports":7,"fee_lamports":2,"session_id":"s"}"#;
            let ndef = build_external_ndef(record_type, json).unwrap();
            let mut tx = None;
            let mut key = None;
            assert!(parse_ndef(&ndef, &mut tx, &mut key));
            let tx = tx.unwrap();
            assert_eq!(tx.tx_len, 4);
            assert_eq!(&tx.tx_bytes[..4], &[1, 2, 3, 4]);
            assert_eq!(tx.from, "a");
            assert_eq!(tx.to, "b");
            assert_eq!(tx.lamports, 7);
            assert_eq!(tx.fee_lamports, 2);
            assert_eq!(tx.session_id, "s");
        }
    }

    #[test]
    fn responds_to_type4_select_and_read() {
        let mut target = TargetFile::new();
        target.set_wallet(&[0u8; 32]).unwrap();
        let mut selected = 0;
        let mut tx = None;
        let mut key = None;
        let mut request_written = false;
        let mut response_read = false;
        let mut resp = [0u8; 270];
        let select_app = [
            0x00, 0xa4, 0x04, 0x00, 0x07, 0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01,
        ];
        let n = target_apdu_response(
            &select_app,
            &mut resp,
            &mut selected,
            &mut target,
            &mut tx,
            &mut key,
            &mut request_written,
            &mut response_read,
        );
        assert_eq!(&resp[..n], &[0x90, 0x00]);
        let select_ndef = [0x00, 0xa4, 0x00, 0x0c, 0x02, 0xe1, 0x04];
        target_apdu_response(
            &select_ndef,
            &mut resp,
            &mut selected,
            &mut target,
            &mut tx,
            &mut key,
            &mut request_written,
            &mut response_read,
        );
        let read_len = [0x00, 0xb0, 0x00, 0x00, 0x02];
        let n = target_apdu_response(
            &read_len,
            &mut resp,
            &mut selected,
            &mut target,
            &mut tx,
            &mut key,
            &mut request_written,
            &mut response_read,
        );
        assert_eq!(n, 4);
        assert_eq!(&resp[n - 2..n], &[0x90, 0x00]);
    }
}
