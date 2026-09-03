//! Installed application store.
//!
//! Each app lives in its own directory under `<data_dir>/apps/<appId>`,
//! holding the extracted package plus an `install.json` written by the daemon
//! that records how and when it arrived.

use crate::error::{RpcError, PACKAGE_ERROR};
use crate::manifest::{validate_app_id, Manifest};
use crate::package::Package;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

const INSTALL_META: &str = "install.json";
/// Downloads are capped well below the package size limit to keep a hostile
/// registry from filling the device's storage.
const MAX_DOWNLOAD_BYTES: u64 = 64 * 1024 * 1024;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallMeta {
    pub installed_at_ms: u64,
    pub source: String,
    /// Base64 publisher key when the package carried a valid signature.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub publisher_key: Option<String>,
}

/// What `apps.list` returns for a single installed app.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppRecord {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(rename = "type")]
    pub app_type: String,
    pub entry: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
    pub capabilities: Vec<String>,
    pub author: String,
    pub description: String,
    pub installed_at_ms: u64,
    pub signed: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher_key: Option<String>,
    /// Where the shell should point the app's iframe.
    pub url: String,
}

pub struct AppStore {
    apps_dir: PathBuf,
    mutations: Mutex<()>,
    next_staging_id: AtomicU64,
}

impl AppStore {
    pub fn new(apps_dir: PathBuf) -> std::io::Result<AppStore> {
        std::fs::create_dir_all(&apps_dir)?;
        Ok(AppStore {
            apps_dir,
            mutations: Mutex::new(()),
            next_staging_id: AtomicU64::new(1),
        })
    }

    pub fn apps_dir(&self) -> &Path {
        &self.apps_dir
    }

    pub fn app_dir(&self, app_id: &str) -> Result<PathBuf, RpcError> {
        validate_app_id(app_id)
            .map_err(|e| RpcError::new(PACKAGE_ERROR, format!("invalid app id: {e}")))?;
        Ok(self.apps_dir.join(app_id))
    }

    /// Manifest of one installed app, or `None` when it is not installed.
    pub fn manifest(&self, app_id: &str) -> Option<Manifest> {
        let dir = self.app_dir(app_id).ok()?;
        let bytes = std::fs::read(dir.join("manifest.json")).ok()?;
        let manifest = Manifest::parse(&bytes).ok()?;
        if manifest.id == app_id {
            Some(manifest)
        } else {
            None
        }
    }

    pub fn is_installed(&self, app_id: &str) -> bool {
        self.manifest(app_id).is_some()
    }

    pub fn list(&self) -> Vec<AppRecord> {
        let mut records = Vec::new();
        let Ok(entries) = std::fs::read_dir(&self.apps_dir) else {
            return records;
        };
        for entry in entries.flatten() {
            if !entry.path().is_dir() {
                continue;
            }
            let app_id = entry.file_name().to_string_lossy().to_string();
            let Some(manifest) = self.manifest(&app_id) else {
                continue;
            };
            let meta: Option<InstallMeta> = std::fs::read(entry.path().join(INSTALL_META))
                .ok()
                .and_then(|bytes| serde_json::from_slice(&bytes).ok());
            records.push(record_for(&manifest, meta.as_ref()));
        }
        records.sort_by(|a, b| a.id.cmp(&b.id));
        records
    }

    /// Install from a local path or an `http(s)` URL. Returns the installed
    /// record. A package that carries a signature must verify; an unsigned
    /// package is accepted as a sideload.
    pub fn install(&self, source: &str, now_ms: u64) -> Result<AppRecord, RpcError> {
        let bytes = fetch_source(source)?;
        self.install_bytes(source, bytes, now_ms, true, None, None)
    }

    /// Install using the stricter store/device path. Signed packages are the
    /// default; developer sideloading must be opted into explicitly.
    pub fn install_verified(
        &self,
        source: &str,
        now_ms: u64,
        allow_unsigned: bool,
        expected_sha256: Option<&str>,
        expected_publisher: Option<&str>,
    ) -> Result<AppRecord, RpcError> {
        let bytes = fetch_source(source)?;
        self.install_bytes(
            source,
            bytes,
            now_ms,
            allow_unsigned,
            expected_sha256,
            expected_publisher,
        )
    }

    fn install_bytes(
        &self,
        source: &str,
        bytes: Vec<u8>,
        now_ms: u64,
        allow_unsigned: bool,
        expected_sha256: Option<&str>,
        expected_publisher: Option<&str>,
    ) -> Result<AppRecord, RpcError> {
        if let Some(expected) = expected_sha256 {
            if expected.len() != 64 || !expected.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                return Err(RpcError::new(
                    PACKAGE_ERROR,
                    "expected SHA-256 is not 64 hex characters",
                ));
            }
            let actual = crate::package::hex_sha256(&bytes);
            if !expected.eq_ignore_ascii_case(&actual) {
                return Err(RpcError::new(
                    PACKAGE_ERROR,
                    format!("package SHA-256 mismatch: expected {expected}, got {actual}"),
                ));
            }
        }

        let package = Package::read_from_bytes(bytes)
            .map_err(|e| RpcError::new(PACKAGE_ERROR, e.to_string()))?;
        let publisher_key = package
            .verify_signature()
            .map_err(|e| RpcError::new(PACKAGE_ERROR, e.to_string()))?;
        if !allow_unsigned && publisher_key.is_none() {
            return Err(RpcError::new(
                PACKAGE_ERROR,
                "unsigned packages require developer sideload mode",
            ));
        }
        if let Some(expected) = expected_publisher {
            match publisher_key.as_deref() {
                Some(actual) if actual == expected => {}
                Some(_) => {
                    return Err(RpcError::new(
                        PACKAGE_ERROR,
                        "package publisher key does not match the expected publisher",
                    ))
                }
                None => {
                    return Err(RpcError::new(
                        PACKAGE_ERROR,
                        "package has no publisher signature",
                    ))
                }
            }
        }

        let app_id = package.manifest.id.clone();
        let target = self.app_dir(&app_id)?;
        let staging_id = self.next_staging_id.fetch_add(1, Ordering::Relaxed);
        let staging = self
            .apps_dir
            .join(format!(".install-{}-{staging_id}", std::process::id()));
        let backup = self
            .apps_dir
            .join(format!(".backup-{}-{staging_id}", std::process::id()));

        let _guard = self
            .mutations
            .lock()
            .map_err(|_| RpcError::internal("app store lock poisoned"))?;
        package
            .extract_to(&staging)
            .map_err(|e| RpcError::new(PACKAGE_ERROR, e.to_string()))?;

        let meta = InstallMeta {
            installed_at_ms: now_ms,
            source: source.to_string(),
            publisher_key,
        };
        let meta_json =
            serde_json::to_vec_pretty(&meta).map_err(|e| RpcError::internal(e.to_string()))?;
        if let Err(error) = std::fs::write(staging.join(INSTALL_META), meta_json) {
            let _ = std::fs::remove_dir_all(&staging);
            return Err(RpcError::new(
                PACKAGE_ERROR,
                format!("cannot record install metadata: {error}"),
            ));
        }

        let had_previous = target.exists();
        if had_previous {
            std::fs::rename(&target, &backup).map_err(|e| {
                let _ = std::fs::remove_dir_all(&staging);
                RpcError::new(PACKAGE_ERROR, format!("cannot prepare app update: {e}"))
            })?;
        }
        if let Err(error) = std::fs::rename(&staging, &target) {
            if had_previous {
                let _ = std::fs::rename(&backup, &target);
            }
            let _ = std::fs::remove_dir_all(&staging);
            return Err(RpcError::new(
                PACKAGE_ERROR,
                format!("cannot activate installed app: {error}"),
            ));
        }
        if had_previous {
            if let Err(error) = std::fs::remove_dir_all(&backup) {
                tracing::warn!(path = %backup.display(), "could not clean up old app version: {error}");
            }
        }

        Ok(record_for(&package.manifest, Some(&meta)))
    }

    pub fn uninstall(&self, app_id: &str) -> Result<(), RpcError> {
        let _guard = self
            .mutations
            .lock()
            .map_err(|_| RpcError::internal("app store lock poisoned"))?;
        let dir = self.app_dir(app_id)?;
        if !dir.is_dir() {
            return Err(RpcError::new(
                PACKAGE_ERROR,
                format!("app `{app_id}` is not installed"),
            ));
        }
        std::fs::remove_dir_all(&dir)
            .map_err(|e| RpcError::new(PACKAGE_ERROR, format!("cannot remove `{app_id}`: {e}")))
    }
}

fn record_for(manifest: &Manifest, meta: Option<&InstallMeta>) -> AppRecord {
    AppRecord {
        id: manifest.id.clone(),
        name: manifest.name.clone(),
        version: manifest.version.clone(),
        app_type: manifest.app_type.clone(),
        entry: manifest.entry.clone(),
        icon: manifest.icon.clone(),
        capabilities: manifest.capabilities.clone(),
        author: manifest.author.clone(),
        description: manifest.description.clone(),
        installed_at_ms: meta.map(|m| m.installed_at_ms).unwrap_or(0),
        signed: meta.and_then(|m| m.publisher_key.as_ref()).is_some(),
        publisher_key: meta.and_then(|m| m.publisher_key.clone()),
        url: format!("/apps/{}/{}", manifest.id, manifest.entry),
    }
}

/// Read a `.swa` from a filesystem path, a `file://` URL, or over HTTP.
///
/// Remote fetches are delegated to `curl`, which is present on Raspberry Pi OS
/// and on macOS. Keeping a TLS stack out of the daemon keeps the binary small
/// and, more usefully, keeps the build free of C dependencies so it cross
/// compiles for the device from any developer machine.
fn fetch_source(source: &str) -> Result<Vec<u8>, RpcError> {
    if source.starts_with("http://") || source.starts_with("https://") {
        return download(source);
    }

    let path = match source.strip_prefix("file://") {
        Some(rest) => PathBuf::from(rest),
        None => PathBuf::from(source),
    };
    if let Ok(metadata) = std::fs::metadata(&path) {
        if metadata.len() > MAX_DOWNLOAD_BYTES {
            return Err(RpcError::new(
                PACKAGE_ERROR,
                format!(
                    "package `{}` exceeds the 64 MiB package limit",
                    path.display()
                ),
            ));
        }
    }
    std::fs::read(&path).map_err(|e| {
        RpcError::new(
            PACKAGE_ERROR,
            format!("cannot read `{}`: {e}", path.display()),
        )
    })
}

fn download(url: &str) -> Result<Vec<u8>, RpcError> {
    let output = Command::new("curl")
        .arg("--fail")
        .arg("--silent")
        .arg("--show-error")
        .arg("--location")
        .arg("--max-time")
        .arg("120")
        .arg("--max-filesize")
        .arg(MAX_DOWNLOAD_BYTES.to_string())
        .arg("--output")
        .arg("-")
        .arg(url)
        .output()
        .map_err(|e| {
            RpcError::new(
                PACKAGE_ERROR,
                format!("cannot run curl to download `{url}`: {e}"),
            )
        })?;

    if !output.status.success() {
        let detail = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(RpcError::new(
            PACKAGE_ERROR,
            format!(
                "download of `{url}` failed: {}",
                if detail.is_empty() {
                    "curl reported an error".to_string()
                } else {
                    detail
                }
            ),
        ));
    }
    if output.stdout.len() as u64 > MAX_DOWNLOAD_BYTES {
        return Err(RpcError::new(
            PACKAGE_ERROR,
            format!("download of `{url}` exceeded the 64 MiB package limit"),
        ));
    }
    Ok(output.stdout)
}
