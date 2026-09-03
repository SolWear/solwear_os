//! Integration tests for the daemon: capability enforcement, manifest
//! validation, package signature verification, the mock HAL, and the wallet
//! confirmation flow.

mod common;

use base64::Engine;
use common::*;
use serde_json::{json, Value};
use solweard::error::{CAPABILITY_DENIED, SHELL_UNAVAILABLE, USER_REJECTED};
use solweard::hal::{Hal, MockHal, ScreenShape, KNOWN_SENSORS};
use solweard::manifest::Manifest;
use solweard::package::Package;
use solweard::rpc;
use solweard::state::Caller;
use std::collections::BTreeMap;
use std::process::Command;

// --- mock HAL --------------------------------------------------------------

#[test]
fn mock_hal_is_deterministic_and_scriptable() {
    let hal = MockHal::from_script_str(
        r#"{
          "device": "pi-round-480",
          "screen": { "width": 240, "height": 240, "shape": "round" },
          "power": { "percent": 12, "charging": true, "estimateMinutes": 45 },
          "brightness": 30,
          "epochMs": 1700000000000,
          "tickMs": 1000,
          "sensors": { "heartRate": { "unit": "bpm", "values": [60, 90] } }
        }"#,
    )
    .expect("script parses");

    assert_eq!(hal.device(), "pi-round-480");
    assert_eq!(hal.screen().width, 240);
    assert_eq!(hal.screen().shape, ScreenShape::Round);
    assert_eq!(hal.power().percent, 12);
    assert!(hal.power().charging);
    assert_eq!(hal.brightness(), 30);

    // The scripted clock is fixed and advances by exactly tickMs per read.
    assert_eq!(hal.now_ms(), 1_700_000_000_000);
    assert_eq!(hal.now_ms(), 1_700_000_001_000);

    // Scripted sensor values cycle in order, so reads are reproducible.
    assert_eq!(hal.read_sensor("heartRate").unwrap().value, 60.0);
    assert_eq!(hal.read_sensor("heartRate").unwrap().value, 90.0);
    assert_eq!(hal.read_sensor("heartRate").unwrap().value, 60.0);
    assert_eq!(hal.read_sensor("heartRate").unwrap().unit, "bpm");

    hal.set_brightness(80).unwrap();
    assert_eq!(hal.brightness(), 80);
    hal.set_brightness(255).unwrap();
    assert_eq!(
        hal.brightness(),
        100,
        "brightness is clamped to a percentage"
    );
}

#[test]
fn mock_hal_answers_every_known_sensor() {
    let hal = MockHal::default();
    for sensor in KNOWN_SENSORS {
        let reading = hal
            .read_sensor(sensor)
            .unwrap_or_else(|e| panic!("MockHal must answer `{sensor}`: {e}"));
        assert_eq!(&reading.sensor, sensor);
        assert!(!reading.unit.is_empty());
    }
    assert!(
        hal.read_sensor("altimeter").is_err(),
        "unknown sensors are refused"
    );
}

#[test]
fn mock_hal_exposes_a_controllable_nfc_transport() {
    let hal = MockHal::default();
    let status = hal.nfc_status();
    assert!(status.available && status.ready);
    assert!(!status.enabled);
    hal.set_nfc_enabled(true).unwrap();
    assert!(hal.nfc_status().enabled);
}

// --- manifest validation ---------------------------------------------------

#[test]
fn manifest_accepts_a_well_formed_document() {
    let manifest =
        Manifest::parse(manifest_json("tech.solwear.demo", &["system", "power", "nfc"]).as_bytes())
            .expect("valid manifest");
    assert_eq!(manifest.id, "tech.solwear.demo");
    assert!(manifest.has_capability("power"));
    assert!(!manifest.has_capability("wallet"));
    assert!(manifest.has_capability("nfc"));
}

#[test]
fn manifest_rejects_malformed_documents() {
    let cases: &[(&str, &str)] = &[
        (
            "single-label id",
            r#"{"id":"demo","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"index.html"}"#,
        ),
        (
            "uppercase id",
            r#"{"id":"Tech.Solwear","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"index.html"}"#,
        ),
        (
            "bad version",
            r#"{"id":"tech.solwear.a","name":"n","version":"one","sdk":"0.1","type":"app","entry":"index.html"}"#,
        ),
        (
            "bad type",
            r#"{"id":"tech.solwear.a","name":"n","version":"1.0.0","sdk":"0.1","type":"daemon","entry":"index.html"}"#,
        ),
        (
            "absolute entry",
            r#"{"id":"tech.solwear.a","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"/etc/passwd"}"#,
        ),
        (
            "traversing entry",
            r#"{"id":"tech.solwear.a","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"../../evil.html"}"#,
        ),
        (
            "unknown capability",
            r#"{"id":"tech.solwear.a","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"index.html","capabilities":["root"]}"#,
        ),
        (
            "reserved shell id",
            r#"{"id":"tech.solwear.shell","name":"n","version":"1.0.0","sdk":"0.1","type":"app","entry":"index.html"}"#,
        ),
        (
            "missing name",
            r#"{"id":"tech.solwear.a","version":"1.0.0","sdk":"0.1","type":"app","entry":"index.html"}"#,
        ),
    ];
    for (label, document) in cases {
        assert!(
            Manifest::parse(document.as_bytes()).is_err(),
            "{label} must be rejected"
        );
    }
}

// --- package signature verification ----------------------------------------

#[test]
fn signed_package_verifies() {
    let mut files = sample_files("tech.solwear.signed", &["system"]);
    let public_key = sign_files(&mut files);
    let package = Package::read_from_bytes(zip_files(&files)).expect("package reads");
    assert_eq!(package.verify_signature().unwrap(), Some(public_key));
}

#[test]
fn signing_payload_matches_the_sdk_v1_contract() {
    let hashes = BTreeMap::from([
        (
            "index.html".to_string(),
            "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03".to_string(),
        ),
        (
            "manifest.json".to_string(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad".to_string(),
        ),
    ]);
    let expected = concat!(
        "SolWear .swa signature v1\n",
        "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  index.html\n",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  manifest.json\n",
    );
    assert_eq!(
        solweard::package::canonical_listing(&hashes),
        expected.as_bytes()
    );
}

#[test]
fn unsigned_package_is_accepted_as_a_sideload() {
    let files = sample_files("tech.solwear.unsigned", &["system"]);
    let package = Package::read_from_bytes(zip_files(&files)).expect("package reads");
    assert_eq!(package.verify_signature().unwrap(), None);
}

#[test]
fn tampered_archive_is_rejected() {
    // Sign a package, then rewrite one byte of a covered file.
    let mut files = sample_files("tech.solwear.tampered", &["system"]);
    sign_files(&mut files);
    files.insert(
        "index.html".to_string(),
        b"<!doctype html><script>steal()</script>".to_vec(),
    );

    let package = Package::read_from_bytes(zip_files(&files)).expect("package reads");
    let error = package
        .verify_signature()
        .expect_err("tampered package must not verify");
    assert!(
        error.to_string().contains("index.html"),
        "error names the modified file: {error}"
    );
}

#[test]
fn added_file_invalidates_the_signature() {
    let mut files = sample_files("tech.solwear.extra", &["system"]);
    sign_files(&mut files);
    files.insert(
        "assets/payload.js".to_string(),
        b"console.log('smuggled')".to_vec(),
    );

    let package = Package::read_from_bytes(zip_files(&files)).expect("package reads");
    assert!(
        package.verify_signature().is_err(),
        "a file outside the signature is rejected"
    );
}

#[test]
fn forged_signature_is_rejected() {
    let mut files = sample_files("tech.solwear.forged", &["system"]);
    sign_files(&mut files);
    // Keep the hashes honest but swap in a signature from a different key.
    let mut document: Value =
        serde_json::from_slice(files.get("signature.json").unwrap()).expect("signature json");
    let other = ed25519_dalek::SigningKey::from_bytes(&[9u8; 32]);
    let listing_signature = {
        use ed25519_dalek::Signer;
        other.sign(b"a different message")
    };
    document["signature"] =
        json!(base64::engine::general_purpose::STANDARD.encode(listing_signature.to_bytes()));
    files.insert(
        "signature.json".to_string(),
        serde_json::to_vec(&document).unwrap(),
    );

    let package = Package::read_from_bytes(zip_files(&files)).expect("package reads");
    assert!(
        package.verify_signature().is_err(),
        "a signature from another key is rejected"
    );
}

#[test]
fn package_without_manifest_is_rejected() {
    let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    files.insert("index.html".to_string(), b"<h1>no manifest</h1>".to_vec());
    assert!(Package::read_from_bytes(zip_files(&files)).is_err());
}

#[test]
fn package_missing_its_entry_point_is_rejected() {
    let mut files = sample_files("tech.solwear.noentry", &["system"]);
    files.remove("index.html");
    assert!(Package::read_from_bytes(zip_files(&files)).is_err());
}

// --- capability enforcement ------------------------------------------------

async fn call(
    state: &std::sync::Arc<solweard::state::AppState>,
    caller: &Caller,
    method: &str,
    params: Value,
) -> Result<Value, solweard::error::RpcError> {
    rpc::dispatch(state, caller, method, &params).await
}

#[tokio::test]
async fn capabilities_gate_every_namespace() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    // Install an app that may read the system and power namespaces only.
    let mut files = sample_files("tech.solwear.limited", &["system", "power"]);
    sign_files(&mut files);
    let source = write_swa(dir.path(), "limited.swa", &zip_files(&files));
    let installed = call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({ "source": source }),
    )
    .await
    .expect("install succeeds");
    assert_eq!(installed["appId"], "tech.solwear.limited");
    assert_eq!(installed["version"], "1.0.0");

    let app = Caller::App("tech.solwear.limited".to_string());

    // Granted namespaces work.
    let info = call(&state, &app, "system.info", json!({}))
        .await
        .expect("system.info allowed");
    assert_eq!(info["device"], "test-watch");
    assert_eq!(info["screen"]["shape"], "round");
    let stats = call(&state, &app, "system.stats", json!({}))
        .await
        .expect("system.stats allowed");
    assert_eq!(stats["platform"]["arch"], std::env::consts::ARCH);
    assert!(stats["uptimeMs"].is_number());
    let power = call(&state, &app, "power.status", json!({}))
        .await
        .expect("power.status allowed");
    assert_eq!(power["percent"], 42);

    // Ungranted namespaces are refused with the specified error code.
    for method in [
        "wallet.publicKey",
        "sensors.read",
        "display.setBrightness",
        "apps.list",
    ] {
        let error = call(
            &state,
            &app,
            method,
            json!({ "sensor": "heartRate", "percent": 50 }),
        )
        .await
        .expect_err("call outside the granted capabilities must fail");
        assert_eq!(error.code, CAPABILITY_DENIED, "{method} must return -32001");
    }

    // The shell namespace is never reachable from an app, whatever it declares.
    let error = call(
        &state,
        &app,
        "shell.confirmResponse",
        json!({ "requestId": "c1", "approved": true }),
    )
    .await
    .expect_err("apps cannot drive the shell");
    assert_eq!(error.code, CAPABILITY_DENIED);

    // An app that is not installed holds no capabilities at all.
    let stranger = Caller::App("tech.solwear.ghost".to_string());
    let error = call(&state, &stranger, "system.info", json!({}))
        .await
        .expect_err("unknown apps are refused");
    assert_eq!(error.code, CAPABILITY_DENIED);

    // The shell is privileged across the whole surface.
    call(&state, &Caller::Shell, "wallet.publicKey", json!({}))
        .await
        .expect("shell may read the key");
    call(&state, &Caller::Shell, "apps.list", json!({}))
        .await
        .expect("shell may list apps");
}

#[tokio::test]
async fn install_list_launch_and_uninstall_round_trip() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());
    let mut events = state.register_shell();

    let mut files = sample_files("tech.solwear.roundtrip", &["system"]);
    let public_key = sign_files(&mut files);
    let source = write_swa(dir.path(), "roundtrip.swa", &zip_files(&files));

    call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({ "source": source }),
    )
    .await
    .unwrap();

    let listed = call(&state, &Caller::Shell, "apps.list", json!({}))
        .await
        .unwrap();
    let apps = listed["apps"].as_array().expect("apps array");
    assert_eq!(apps.len(), 1);
    assert_eq!(apps[0]["id"], "tech.solwear.roundtrip");
    assert_eq!(apps[0]["signed"], true);
    assert_eq!(apps[0]["publisherKey"], public_key);
    assert_eq!(apps[0]["url"], "/apps/tech.solwear.roundtrip/index.html");

    // Files really landed on disk, including nested assets.
    assert!(dir
        .path()
        .join("apps/tech.solwear.roundtrip/index.html")
        .is_file());
    assert!(dir
        .path()
        .join("apps/tech.solwear.roundtrip/assets/note.txt")
        .is_file());

    call(
        &state,
        &Caller::Shell,
        "apps.launch",
        json!({ "appId": "tech.solwear.roundtrip" }),
    )
    .await
    .unwrap();

    // The shell is told to install and then to launch.
    let mut methods = Vec::new();
    while let Ok(event) = events.try_recv() {
        let parsed: Value = serde_json::from_str(&event).unwrap();
        methods.push(parsed["method"].as_str().unwrap_or_default().to_string());
    }
    assert!(methods.contains(&"apps.changed".to_string()));
    assert!(methods.contains(&"apps.launch".to_string()));

    call(
        &state,
        &Caller::Shell,
        "apps.uninstall",
        json!({ "appId": "tech.solwear.roundtrip" }),
    )
    .await
    .unwrap();
    let listed = call(&state, &Caller::Shell, "apps.list", json!({}))
        .await
        .unwrap();
    assert!(listed["apps"].as_array().unwrap().is_empty());
    assert!(!dir.path().join("apps/tech.solwear.roundtrip").exists());
}

#[tokio::test]
async fn installing_a_tampered_package_fails() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    let mut files = sample_files("tech.solwear.evil", &["system"]);
    sign_files(&mut files);
    files.insert(
        "index.html".to_string(),
        b"<script>evil()</script>".to_vec(),
    );
    let source = write_swa(dir.path(), "evil.swa", &zip_files(&files));

    let error = call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({ "source": source }),
    )
    .await
    .expect_err("a tampered package must not install");
    assert!(error.message.contains("signature"), "{}", error.message);
    assert!(
        !dir.path().join("apps/tech.solwear.evil").exists(),
        "nothing is written on failure"
    );
}

#[tokio::test]
async fn registry_metadata_is_checked_before_install() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    let mut files = sample_files("tech.solwear.pinned", &["system"]);
    let publisher = sign_files(&mut files);
    let archive = zip_files(&files);
    let digest = solweard::package::hex_sha256(&archive);
    let source = write_swa(dir.path(), "pinned.swa", &archive);

    let installed = call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({
            "source": source,
            "expectedSha256": digest,
            "expectedPublisherKey": publisher,
        }),
    )
    .await
    .expect("matching registry metadata installs");
    assert_eq!(installed["appId"], "tech.solwear.pinned");

    call(
        &state,
        &Caller::Shell,
        "apps.uninstall",
        json!({ "appId": "tech.solwear.pinned" }),
    )
    .await
    .unwrap();
    let error = call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({
            "source": source,
            "expectedSha256": "0000000000000000000000000000000000000000000000000000000000000000",
            "expectedPublisherKey": publisher,
        }),
    )
    .await
    .expect_err("a mismatched registry digest must fail");
    assert!(error.message.contains("SHA-256 mismatch"));
    assert!(!dir.path().join("apps/tech.solwear.pinned").exists());
}

#[test]
fn install_subcommand_uses_the_on_device_verifier() {
    let dir = tempfile::tempdir().expect("temp dir");
    let mut files = sample_files("tech.solwear.command", &["system"]);
    sign_files(&mut files);
    let source = write_swa(dir.path(), "command.swa", &zip_files(&files));

    let output = Command::new(env!("CARGO_BIN_EXE_solweard"))
        .args(["install", &source])
        .env("SOLWEAR_DATA_DIR", dir.path())
        .output()
        .expect("run solweard install");
    assert!(
        output.status.success(),
        "install command failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).trim(),
        "tech.solwear.command 1.0.0"
    );
    assert!(dir
        .path()
        .join("apps/tech.solwear.command/index.html")
        .is_file());

    let unsigned = sample_files("tech.solwear.unsigned-command", &["system"]);
    let unsigned_source = write_swa(dir.path(), "unsigned.swa", &zip_files(&unsigned));
    let refused = Command::new(env!("CARGO_BIN_EXE_solweard"))
        .args(["install", &unsigned_source])
        .env("SOLWEAR_DATA_DIR", dir.path())
        .output()
        .expect("run unsigned install");
    assert!(
        !refused.status.success(),
        "unsigned install requires an explicit opt-in"
    );
}

// --- notifications ---------------------------------------------------------

#[tokio::test]
async fn notifications_are_attributed_to_the_calling_app() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    let mut files = sample_files("tech.solwear.notifier", &["notifications"]);
    sign_files(&mut files);
    let source = write_swa(dir.path(), "notifier.swa", &zip_files(&files));
    call(
        &state,
        &Caller::Shell,
        "apps.install",
        json!({ "source": source }),
    )
    .await
    .unwrap();

    let app = Caller::App("tech.solwear.notifier".to_string());
    // An app cannot post under someone else's name.
    let posted = call(
        &state,
        &app,
        "notifications.post",
        json!({ "title": "Ping", "body": "body", "appId": "tech.solwear.someoneelse" }),
    )
    .await
    .unwrap();
    assert!(posted["id"].is_string());

    let listed = call(&state, &Caller::Shell, "notifications.list", json!({}))
        .await
        .unwrap();
    let items = listed["items"].as_array().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0]["appId"], "tech.solwear.notifier");
    assert_eq!(items[0]["title"], "Ping");
    assert_eq!(items[0]["timestampMs"], 1_700_000_000_000u64);
}

// --- wallet ----------------------------------------------------------------

#[tokio::test]
async fn wallet_key_is_stable_and_private() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    let key = call(&state, &Caller::Shell, "wallet.publicKey", json!({}))
        .await
        .unwrap();
    let public_key = key["publicKey"].as_str().expect("base58 key").to_string();
    assert!(bs58::decode(&public_key).into_vec().unwrap().len() == 32);
    assert!(
        key.get("privateKey").is_none(),
        "the private key is never returned"
    );

    // Reopening the same data directory yields the same identity.
    drop(state);
    let reopened = test_state(dir.path());
    let again = call(&reopened, &Caller::Shell, "wallet.publicKey", json!({}))
        .await
        .unwrap();
    assert_eq!(again["publicKey"], public_key);

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = std::fs::metadata(dir.path().join("wallet.key"))
            .unwrap()
            .permissions()
            .mode();
        assert_eq!(mode & 0o777, 0o600, "the key file must be owner-only");
    }
}

#[tokio::test]
async fn wallet_passphrase_encrypts_at_rest_and_survives_restart() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());
    let public_key = state.wallet.public_key();

    call(
        &state,
        &Caller::Shell,
        "wallet.setPassphrase",
        json!({
            "passphrase": "correct horse battery staple",
            "name": "Primary"
        }),
    )
    .await
    .expect("protect wallet");
    call(&state, &Caller::Shell, "wallet.lock", json!({}))
        .await
        .expect("lock wallet");
    let encrypted = std::fs::read(dir.path().join("wallet.key")).unwrap();
    assert_ne!(encrypted.len(), 32, "the raw seed was replaced");
    assert!(!String::from_utf8_lossy(&encrypted).contains("correct horse"));

    let wrong = call(
        &state,
        &Caller::Shell,
        "wallet.unlock",
        json!({ "passphrase": "definitely wrong" }),
    )
    .await
    .expect_err("wrong passphrase is refused");
    assert_eq!(wrong.code, USER_REJECTED);
    drop(state);

    let reopened = test_state(dir.path());
    assert!(
        reopened.wallet.is_locked(),
        "an encrypted wallet starts locked"
    );
    assert_eq!(reopened.wallet.public_key(), public_key);
    call(
        &reopened,
        &Caller::Shell,
        "wallet.unlock",
        json!({ "passphrase": "correct horse battery staple" }),
    )
    .await
    .expect("correct passphrase unlocks after restart");
    assert!(!reopened.wallet.is_locked());
    assert_eq!(reopened.wallet.name(), "Primary");
}

#[tokio::test]
async fn signing_without_a_shell_is_refused() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());

    let error = call(
        &state,
        &Caller::Shell,
        "wallet.signTransaction",
        json!({ "appId": "tech.solwear.signer", "message": "AQID", "encoding": "base64" }),
    )
    .await
    .expect_err("no shell means no prompt and no signature");
    assert_eq!(error.code, SHELL_UNAVAILABLE);
}

#[tokio::test]
async fn signing_requires_an_affirmative_confirmation() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());
    let mut events = state.register_shell();

    // Answer the prompt with a refusal.
    let refusing = std::sync::Arc::clone(&state);
    let responder = tokio::spawn(async move {
        while let Some(event) = events.recv().await {
            let parsed: Value = serde_json::from_str(&event).unwrap();
            if parsed["method"] == "wallet.confirmRequest" {
                let request_id = parsed["params"]["requestId"].as_str().unwrap().to_string();
                assert_eq!(parsed["params"]["appId"], "tech.solwear.signer");
                refusing.resolve_confirmation(&request_id, false);
                return events;
            }
        }
        events
    });

    let error = call(
        &state,
        &Caller::Shell,
        "wallet.signTransaction",
        json!({ "appId": "tech.solwear.signer", "message": "AQID" }),
    )
    .await
    .expect_err("a refusal must not produce a signature");
    assert_eq!(error.code, USER_REJECTED);

    let mut events = responder.await.unwrap();

    // Now approve, and check the signature verifies against the device key.
    let approving = std::sync::Arc::clone(&state);
    tokio::spawn(async move {
        while let Some(event) = events.recv().await {
            let parsed: Value = serde_json::from_str(&event).unwrap();
            if parsed["method"] == "wallet.confirmRequest" {
                let request_id = parsed["params"]["requestId"].as_str().unwrap().to_string();
                approving.resolve_confirmation(&request_id, true);
                return;
            }
        }
    });

    let signed = call(
        &state,
        &Caller::Shell,
        "wallet.signTransaction",
        json!({ "appId": "tech.solwear.signer", "message": "AQID" }),
    )
    .await
    .expect("an approved request signs");

    let signature_bytes = bs58::decode(signed["signature"].as_str().unwrap())
        .into_vec()
        .unwrap();
    assert_eq!(signature_bytes.len(), 64);

    let key = call(&state, &Caller::Shell, "wallet.publicKey", json!({}))
        .await
        .unwrap();
    let key_bytes: [u8; 32] = bs58::decode(key["publicKey"].as_str().unwrap())
        .into_vec()
        .unwrap()
        .try_into()
        .unwrap();
    let verifying = ed25519_dalek::VerifyingKey::from_bytes(&key_bytes).unwrap();
    let signature = ed25519_dalek::Signature::from_bytes(&signature_bytes.try_into().unwrap());
    use ed25519_dalek::Verifier;
    verifying
        .verify(&[1u8, 2, 3], &signature)
        .expect("signature covers the decoded message");

    let activity = call(&state, &Caller::Shell, "wallet.activity", json!({}))
        .await
        .expect("activity is readable after signing");
    assert_eq!(activity["items"].as_array().unwrap().len(), 1);
    assert_eq!(activity["items"][0]["byteLength"], 3);
    assert_eq!(activity["items"][0]["appId"], "tech.solwear.signer");
}

// --- protocol --------------------------------------------------------------

#[test]
fn requests_must_be_well_formed_json_rpc() {
    assert!(rpc::parse_request("not json").is_err());
    assert!(
        rpc::parse_request(r#"{"method":"system.info","id":1}"#).is_err(),
        "jsonrpc required"
    );
    assert!(
        rpc::parse_request(r#"{"jsonrpc":"2.0","method":"system.info","params":[1,2],"id":1}"#)
            .is_err(),
        "positional parameters are not part of the contract"
    );
    let request =
        rpc::parse_request(r#"{"jsonrpc":"2.0","method":"system.info","id":7,"appId":"a.b"}"#)
            .expect("valid request");
    assert_eq!(request.method, "system.info");
    assert_eq!(request.app_id.as_deref(), Some("a.b"));
    assert!(request.params.is_object());
}

#[tokio::test]
async fn unknown_methods_report_method_not_found() {
    let dir = tempfile::tempdir().expect("temp dir");
    let state = test_state(dir.path());
    let error = call(&state, &Caller::Shell, "system.teleport", json!({}))
        .await
        .expect_err("unknown method");
    assert_eq!(error.code, solweard::error::METHOD_NOT_FOUND);
}
