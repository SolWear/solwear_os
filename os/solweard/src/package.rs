//! `.swa` package reading, signature verification, and extraction.
//!
//! A `.swa` file is a ZIP archive holding `manifest.json`, an entry point,
//! optional assets, and an optional `signature.json`. The signature covers the
//! SHA-256 of every other file in the archive, so changing any byte of any file
//! — or adding or removing a file — invalidates it.

use crate::manifest::{validate_relative_path, Manifest};
use base64::Engine;
use ed25519_dalek::{Signature, Verifier, VerifyingKey, SIGNATURE_LENGTH};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::io::Read;
use std::path::Path;

pub const SIGNATURE_FILE: &str = "signature.json";
pub const MANIFEST_FILE: &str = "manifest.json";
pub const SIGNATURE_VERSION: u32 = 1;
pub const SIGNING_PREAMBLE: &str = "SolWear .swa signature v1\n";

/// Refuse absurdly large packages rather than exhausting memory on a Pi.
const MAX_TOTAL_UNCOMPRESSED: u64 = 64 * 1024 * 1024;
const MAX_ENTRIES: usize = 4096;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct SignatureFile {
    pub version: u32,
    pub algorithm: String,
    /// Base64 encoded raw Ed25519 public key of the publisher.
    pub public_key: String,
    /// Base64 encoded raw Ed25519 signature over the canonical file listing.
    pub signature: String,
    /// Hex encoded SHA-256 of every file in the archive except this one.
    pub files: BTreeMap<String, String>,
    /// Informational ISO-8601 timestamp written by the CLI. It is not signed.
    #[serde(default)]
    pub signed_at: String,
}

#[derive(Debug, thiserror::Error)]
pub enum PackageError {
    #[error("cannot read package: {0}")]
    Io(#[from] std::io::Error),
    #[error("not a valid .swa archive: {0}")]
    Zip(#[from] zip::result::ZipError),
    #[error("{0}")]
    Manifest(String),
    #[error("package is missing required file `{0}`")]
    MissingFile(String),
    #[error("package rejected: {0}")]
    Invalid(String),
    #[error("signature verification failed: {0}")]
    Signature(String),
}

#[derive(Debug, Clone)]
pub struct Package {
    pub manifest: Manifest,
    /// Every regular file in the archive, keyed by its normalised path.
    pub files: BTreeMap<String, Vec<u8>>,
    pub signature: Option<SignatureFile>,
}

impl Package {
    pub fn read_from_path(path: &Path) -> Result<Package, PackageError> {
        let file = std::fs::File::open(path)?;
        Package::read(std::io::BufReader::new(file))
    }

    pub fn read_from_bytes(bytes: Vec<u8>) -> Result<Package, PackageError> {
        Package::read(std::io::Cursor::new(bytes))
    }

    pub fn read<R: Read + std::io::Seek>(reader: R) -> Result<Package, PackageError> {
        let mut archive = zip::ZipArchive::new(reader)?;
        if archive.len() > MAX_ENTRIES {
            return Err(PackageError::Invalid(format!(
                "archive has {} entries, the limit is {MAX_ENTRIES}",
                archive.len()
            )));
        }

        let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
        let mut total: u64 = 0;

        for index in 0..archive.len() {
            let mut entry = archive.by_index(index)?;
            if entry.is_dir() {
                continue;
            }
            let name = entry
                .enclosed_name()
                .ok_or_else(|| {
                    PackageError::Invalid(format!("unsafe path in archive: `{}`", entry.name()))
                })?
                .to_string_lossy()
                .replace('\\', "/");
            validate_relative_path(&name, "archive entry")
                .map_err(|e| PackageError::Invalid(e.to_string()))?;

            total = total.saturating_add(entry.size());
            if total > MAX_TOTAL_UNCOMPRESSED {
                return Err(PackageError::Invalid(
                    "archive expands beyond the 64 MiB package limit".to_string(),
                ));
            }

            let mut bytes = Vec::with_capacity(entry.size().min(1 << 20) as usize);
            entry.read_to_end(&mut bytes)?;
            if files.insert(name.clone(), bytes).is_some() {
                return Err(PackageError::Invalid(format!(
                    "duplicate archive entry `{name}`"
                )));
            }
        }

        let manifest_bytes = files
            .get(MANIFEST_FILE)
            .ok_or_else(|| PackageError::MissingFile(MANIFEST_FILE.to_string()))?;
        let manifest =
            Manifest::parse(manifest_bytes).map_err(|e| PackageError::Manifest(e.to_string()))?;

        if !files.contains_key(&manifest.entry) {
            return Err(PackageError::MissingFile(manifest.entry.clone()));
        }

        let signature = match files.get(SIGNATURE_FILE) {
            Some(bytes) => Some(serde_json::from_slice::<SignatureFile>(bytes).map_err(|e| {
                PackageError::Signature(format!("signature.json is malformed: {e}"))
            })?),
            None => None,
        };

        Ok(Package {
            manifest,
            files,
            signature,
        })
    }

    /// Hash of every file except `signature.json`, hex encoded.
    pub fn file_hashes(&self) -> BTreeMap<String, String> {
        self.files
            .iter()
            .filter(|(name, _)| name.as_str() != SIGNATURE_FILE)
            .map(|(name, bytes)| (name.clone(), hex_sha256(bytes)))
            .collect()
    }

    /// Verify the embedded signature, if there is one.
    ///
    /// Returns the signing public key when the package was signed, and `None`
    /// for an unsigned sideload. Any signature that is present must be valid:
    /// a package with a broken signature is always rejected.
    pub fn verify_signature(&self) -> Result<Option<String>, PackageError> {
        let Some(signature) = &self.signature else {
            return Ok(None);
        };
        if signature.version != SIGNATURE_VERSION {
            return Err(PackageError::Signature(format!(
                "unsupported signature format version `{}`",
                signature.version
            )));
        }
        if !signature.algorithm.eq_ignore_ascii_case("ed25519") {
            return Err(PackageError::Signature(format!(
                "unsupported signature algorithm `{}`",
                signature.algorithm
            )));
        }

        let actual = self.file_hashes();
        if actual.len() != signature.files.len() {
            return Err(PackageError::Signature(format!(
                "signature covers {} files but the archive contains {}",
                signature.files.len(),
                actual.len()
            )));
        }
        for (name, hash) in &actual {
            match signature.files.get(name) {
                Some(expected) if expected.eq_ignore_ascii_case(hash) => {}
                Some(_) => {
                    return Err(PackageError::Signature(format!(
                        "file `{name}` has been modified"
                    )))
                }
                None => {
                    return Err(PackageError::Signature(format!(
                        "file `{name}` is not covered by the signature"
                    )))
                }
            }
        }

        let key_bytes = base64::engine::general_purpose::STANDARD
            .decode(&signature.public_key)
            .map_err(|e| PackageError::Signature(format!("public key is not base64: {e}")))?;
        let key_array: [u8; 32] = key_bytes
            .as_slice()
            .try_into()
            .map_err(|_| PackageError::Signature("public key is not 32 bytes".to_string()))?;
        let verifying_key = VerifyingKey::from_bytes(&key_array)
            .map_err(|e| PackageError::Signature(format!("public key is not on the curve: {e}")))?;

        let sig_bytes = base64::engine::general_purpose::STANDARD
            .decode(&signature.signature)
            .map_err(|e| PackageError::Signature(format!("signature is not base64: {e}")))?;
        let sig_array: [u8; SIGNATURE_LENGTH] = sig_bytes
            .as_slice()
            .try_into()
            .map_err(|_| PackageError::Signature("signature is not 64 bytes".to_string()))?;
        let sig = Signature::from_bytes(&sig_array);

        verifying_key
            .verify(&canonical_listing(&actual), &sig)
            .map_err(|_| {
                PackageError::Signature("signature does not match the archive".to_string())
            })?;

        Ok(Some(signature.public_key.clone()))
    }

    /// Write the package contents into `target`, replacing whatever is there.
    pub fn extract_to(&self, target: &Path) -> Result<(), PackageError> {
        if target.exists() {
            std::fs::remove_dir_all(target)?;
        }
        std::fs::create_dir_all(target)?;
        for (name, bytes) in &self.files {
            let destination = target.join(name);
            // `enclosed_name` plus `validate_relative_path` already rejected
            // traversal, this is the belt to that pair of braces.
            if !destination.starts_with(target) {
                return Err(PackageError::Invalid(format!("unsafe path `{name}`")));
            }
            if let Some(parent) = destination.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(&destination, bytes)?;
        }
        Ok(())
    }
}

/// The exact bytes an Ed25519 publisher key signs. This must remain byte-for-
/// byte compatible with `sdk/cli/src/signing.ts`.
pub fn canonical_listing(hashes: &BTreeMap<String, String>) -> Vec<u8> {
    let mut out = SIGNING_PREAMBLE.as_bytes().to_vec();
    for (name, hash) in hashes {
        out.extend_from_slice(hash.to_ascii_lowercase().as_bytes());
        out.extend_from_slice(b"  ");
        out.extend_from_slice(name.as_bytes());
        out.push(b'\n');
    }
    out
}

pub fn hex_sha256(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut out = String::with_capacity(64);
    for byte in digest {
        out.push_str(&format!("{byte:02x}"));
    }
    out
}
