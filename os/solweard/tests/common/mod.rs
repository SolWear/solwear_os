//! Helpers shared by the integration tests: building `.swa` archives in
//! memory, signing them, and standing up a daemon state on a temporary
//! directory so no test needs root or hardware.

#![allow(dead_code)]

use base64::Engine;
use ed25519_dalek::{Signer, SigningKey};
use solweard::config::Config;
use solweard::hal::MockHal;
use solweard::state::AppState;
use std::collections::BTreeMap;
use std::io::Write;
use std::path::Path;
use std::sync::Arc;

pub const TEST_SEED: [u8; 32] = [7u8; 32];

pub fn manifest_json(id: &str, capabilities: &[&str]) -> String {
    let caps: Vec<String> = capabilities.iter().map(|c| format!("\"{c}\"")).collect();
    format!(
        r#"{{
  "id": "{id}",
  "name": "Test App",
  "version": "1.0.0",
  "sdk": "0.1",
  "type": "app",
  "entry": "index.html",
  "capabilities": [{}],
  "author": "SolWear",
  "description": "Fixture app."
}}"#,
        caps.join(", ")
    )
}

/// A minimal package: manifest, entry document, one asset.
pub fn sample_files(id: &str, capabilities: &[&str]) -> BTreeMap<String, Vec<u8>> {
    let mut files = BTreeMap::new();
    files.insert(
        "manifest.json".to_string(),
        manifest_json(id, capabilities).into_bytes(),
    );
    files.insert(
        "index.html".to_string(),
        b"<!doctype html><title>fixture</title><h1>hello</h1>".to_vec(),
    );
    files.insert("assets/note.txt".to_string(), b"fixture asset".to_vec());
    files
}

pub fn zip_files(files: &BTreeMap<String, Vec<u8>>) -> Vec<u8> {
    let mut cursor = std::io::Cursor::new(Vec::new());
    {
        let mut writer = zip::ZipWriter::new(&mut cursor);
        let options = zip::write::SimpleFileOptions::default()
            .compression_method(zip::CompressionMethod::Deflated);
        for (name, bytes) in files {
            writer
                .start_file(name.as_str(), options)
                .expect("start zip entry");
            writer.write_all(bytes).expect("write zip entry");
        }
        writer.finish().expect("finish zip");
    }
    cursor.into_inner()
}

/// Add a valid `signature.json` covering every file already in the map.
pub fn sign_files(files: &mut BTreeMap<String, Vec<u8>>) -> String {
    let key = SigningKey::from_bytes(&TEST_SEED);
    let hashes: BTreeMap<String, String> = files
        .iter()
        .map(|(name, bytes)| (name.clone(), solweard::package::hex_sha256(bytes)))
        .collect();
    let listing = solweard::package::canonical_listing(&hashes);
    let signature = key.sign(&listing);
    let public_key =
        base64::engine::general_purpose::STANDARD.encode(key.verifying_key().to_bytes());

    let document = serde_json::json!({
        "version": 1,
        "algorithm": "ed25519",
        "publicKey": public_key,
        "signature": base64::engine::general_purpose::STANDARD.encode(signature.to_bytes()),
        "files": hashes,
        "signedAt": "2026-01-01T00:00:00.000Z",
    });
    files.insert(
        "signature.json".to_string(),
        serde_json::to_vec_pretty(&document).expect("serialise signature"),
    );
    public_key
}

pub fn write_swa(dir: &Path, name: &str, bytes: &[u8]) -> String {
    let path = dir.join(name);
    std::fs::write(&path, bytes).expect("write .swa");
    path.to_string_lossy().to_string()
}

/// Daemon state rooted in a temporary directory with a scripted mock HAL.
pub fn test_state(data_dir: &Path) -> Arc<AppState> {
    let hal = MockHal::from_script_str(
        r#"{
          "device": "test-watch",
          "screen": { "width": 480, "height": 480, "shape": "round" },
          "power": { "percent": 42, "charging": false, "estimateMinutes": 300 },
          "brightness": 55,
          "epochMs": 1700000000000,
          "tickMs": 0,
          "timezone": "UTC",
          "sensors": { "heartRate": { "unit": "bpm", "values": [60, 61, 62] } }
        }"#,
    )
    .expect("mock hal script");

    let config = Config {
        data_dir: data_dir.to_path_buf(),
        shell_dir: data_dir.join("shell"),
        rpc_addr: "127.0.0.1:0".parse().unwrap(),
        http_addr: "127.0.0.1:0".parse().unwrap(),
    };
    AppState::new(config, Arc::new(hal)).expect("daemon state")
}
