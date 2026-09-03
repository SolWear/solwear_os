//! Encrypted device keystore for the Solana signer.
//!
//! A new development wallet starts as a legacy-compatible owner-only raw seed.
//! `wallet.setPassphrase` upgrades it in place to an authenticated encrypted
//! document using Argon2id and ChaCha20-Poly1305. Once protected, locking drops
//! the decrypted signing key from memory and signing cannot resume until a
//! correct passphrase is supplied.

use crate::error::{RpcError, INTERNAL_ERROR, USER_REJECTED};
use argon2::Argon2;
use base64::Engine;
use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Nonce};
use ed25519_dalek::{Signer, SigningKey};
use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use zeroize::Zeroize;

const SEED_LEN: usize = 32;
const FORMAT_VERSION: u8 = 1;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct EncryptedWallet {
    version: u8,
    algorithm: String,
    name: String,
    public_key: String,
    salt: String,
    nonce: String,
    ciphertext: String,
}

pub struct Wallet {
    signing_key: Mutex<Option<SigningKey>>,
    encrypted: Mutex<Option<EncryptedWallet>>,
    public_key: String,
    path: PathBuf,
}

impl std::fmt::Debug for Wallet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Wallet")
            .field("path", &self.path)
            .field("publicKey", &self.public_key)
            .field("locked", &self.is_locked())
            .field("protected", &self.is_protected())
            .finish_non_exhaustive()
    }
}

impl Wallet {
    pub fn load_or_create(path: &Path) -> Result<Wallet, RpcError> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(internal("cannot create keystore directory"))?;
        }

        if path.exists() {
            let bytes = std::fs::read(path).map_err(internal("cannot read wallet key"))?;
            enforce_permissions(path)?;
            if bytes.len() == SEED_LEN {
                let seed: [u8; SEED_LEN] = bytes
                    .as_slice()
                    .try_into()
                    .map_err(|_| RpcError::internal("invalid wallet seed"))?;
                let key = SigningKey::from_bytes(&seed);
                let public_key = bs58::encode(key.verifying_key().to_bytes()).into_string();
                return Ok(Wallet {
                    signing_key: Mutex::new(Some(key)),
                    encrypted: Mutex::new(None),
                    public_key,
                    path: path.to_path_buf(),
                });
            }
            let document: EncryptedWallet = serde_json::from_slice(&bytes).map_err(|error| {
                RpcError::new(
                    INTERNAL_ERROR,
                    format!("wallet file is neither a seed nor encrypted JSON: {error}"),
                )
            })?;
            if document.version != FORMAT_VERSION
                || document.algorithm != "argon2id+chacha20poly1305"
            {
                return Err(RpcError::new(
                    INTERNAL_ERROR,
                    "unsupported encrypted wallet format",
                ));
            }
            return Ok(Wallet {
                signing_key: Mutex::new(None),
                public_key: document.public_key.clone(),
                encrypted: Mutex::new(Some(document)),
                path: path.to_path_buf(),
            });
        }

        let signing_key = SigningKey::generate(&mut rand::rngs::OsRng);
        write_private(path, &signing_key.to_bytes())?;
        let public_key = bs58::encode(signing_key.verifying_key().to_bytes()).into_string();
        Ok(Wallet {
            signing_key: Mutex::new(Some(signing_key)),
            encrypted: Mutex::new(None),
            public_key,
            path: path.to_path_buf(),
        })
    }

    pub fn public_key(&self) -> String {
        self.public_key.clone()
    }
    pub fn is_locked(&self) -> bool {
        self.signing_key.lock().expect("wallet key").is_none()
    }
    pub fn is_protected(&self) -> bool {
        self.encrypted.lock().expect("wallet document").is_some()
    }
    pub fn name(&self) -> String {
        self.encrypted
            .lock()
            .expect("wallet document")
            .as_ref()
            .map(|doc| doc.name.clone())
            .unwrap_or_else(|| "SolWear".to_string())
    }

    pub fn set_passphrase(&self, passphrase: &str, name: &str) -> Result<(), RpcError> {
        if passphrase.chars().count() < 8 {
            return Err(RpcError::invalid_params(
                "passphrase must contain at least 8 characters",
            ));
        }
        let guard = self.signing_key.lock().expect("wallet key");
        let signing_key = guard
            .as_ref()
            .ok_or_else(|| RpcError::new(USER_REJECTED, "wallet is locked"))?;
        let mut salt = [0u8; 16];
        let mut nonce = [0u8; 12];
        rand::rngs::OsRng.fill_bytes(&mut salt);
        rand::rngs::OsRng.fill_bytes(&mut nonce);
        let mut encryption_key = [0u8; 32];
        Argon2::default()
            .hash_password_into(passphrase.as_bytes(), &salt, &mut encryption_key)
            .map_err(|error| {
                RpcError::new(INTERNAL_ERROR, format!("cannot derive wallet key: {error}"))
            })?;
        let cipher = ChaCha20Poly1305::new((&encryption_key).into());
        let ciphertext = cipher
            .encrypt(Nonce::from_slice(&nonce), signing_key.to_bytes().as_ref())
            .map_err(|_| RpcError::new(INTERNAL_ERROR, "cannot encrypt wallet seed"))?;
        encryption_key.zeroize();
        let document = EncryptedWallet {
            version: FORMAT_VERSION,
            algorithm: "argon2id+chacha20poly1305".to_string(),
            name: if name.trim().is_empty() {
                "SolWear".to_string()
            } else {
                name.trim().chars().take(32).collect()
            },
            public_key: self.public_key.clone(),
            salt: b64(&salt),
            nonce: b64(&nonce),
            ciphertext: b64(&ciphertext),
        };
        let bytes = serde_json::to_vec_pretty(&document).map_err(|error| {
            RpcError::new(
                INTERNAL_ERROR,
                format!("cannot encode encrypted wallet: {error}"),
            )
        })?;
        replace_private(&self.path, &bytes)?;
        *self.encrypted.lock().expect("wallet document") = Some(document);
        Ok(())
    }

    pub fn lock(&self) -> Result<(), RpcError> {
        if !self.is_protected() {
            return Err(RpcError::invalid_params(
                "set a passphrase before locking the wallet",
            ));
        }
        *self.signing_key.lock().expect("wallet key") = None;
        Ok(())
    }

    pub fn unlock(&self, passphrase: &str) -> Result<(), RpcError> {
        if !self.is_locked() {
            return Ok(());
        }
        let document = self
            .encrypted
            .lock()
            .expect("wallet document")
            .clone()
            .ok_or_else(|| RpcError::new(INTERNAL_ERROR, "wallet has no encrypted document"))?;
        let salt = decode(&document.salt)?;
        let nonce = decode(&document.nonce)?;
        let ciphertext = decode(&document.ciphertext)?;
        if salt.len() != 16 || nonce.len() != 12 {
            return Err(RpcError::new(
                INTERNAL_ERROR,
                "encrypted wallet parameters are malformed",
            ));
        }
        let mut encryption_key = [0u8; 32];
        Argon2::default()
            .hash_password_into(passphrase.as_bytes(), &salt, &mut encryption_key)
            .map_err(|error| {
                RpcError::new(INTERNAL_ERROR, format!("cannot derive wallet key: {error}"))
            })?;
        let cipher = ChaCha20Poly1305::new((&encryption_key).into());
        let mut seed = cipher
            .decrypt(Nonce::from_slice(&nonce), ciphertext.as_ref())
            .map_err(|_| RpcError::new(USER_REJECTED, "incorrect passphrase"))?;
        encryption_key.zeroize();
        let seed_array: [u8; 32] = seed.as_slice().try_into().map_err(|_| {
            RpcError::new(INTERNAL_ERROR, "decrypted wallet seed has the wrong size")
        })?;
        let signing_key = SigningKey::from_bytes(&seed_array);
        seed.zeroize();
        if bs58::encode(signing_key.verifying_key().to_bytes()).into_string() != self.public_key {
            return Err(RpcError::new(
                INTERNAL_ERROR,
                "decrypted wallet does not match its public key",
            ));
        }
        *self.signing_key.lock().expect("wallet key") = Some(signing_key);
        Ok(())
    }

    pub fn sign(&self, message: &[u8]) -> Result<String, RpcError> {
        let guard = self.signing_key.lock().expect("wallet key");
        let key = guard
            .as_ref()
            .ok_or_else(|| RpcError::new(USER_REJECTED, "wallet is locked"))?;
        Ok(bs58::encode(key.sign(message).to_bytes()).into_string())
    }
}

fn b64(bytes: &[u8]) -> String {
    base64::engine::general_purpose::STANDARD.encode(bytes)
}
fn decode(text: &str) -> Result<Vec<u8>, RpcError> {
    base64::engine::general_purpose::STANDARD
        .decode(text)
        .map_err(|_| RpcError::new(INTERNAL_ERROR, "encrypted wallet contains invalid base64"))
}
fn internal(context: &'static str) -> impl FnOnce(std::io::Error) -> RpcError {
    move |error| RpcError::new(INTERNAL_ERROR, format!("{context}: {error}"))
}

fn replace_private(path: &Path, bytes: &[u8]) -> Result<(), RpcError> {
    let temporary = path.with_extension("key.new");
    if temporary.exists() {
        let _ = std::fs::remove_file(&temporary);
    }
    write_private(&temporary, bytes)?;
    std::fs::rename(&temporary, path).map_err(internal("cannot activate encrypted wallet"))?;
    enforce_permissions(path)
}

fn write_private(path: &Path, bytes: &[u8]) -> Result<(), RpcError> {
    #[cfg(unix)]
    {
        use std::io::Write;
        use std::os::unix::fs::OpenOptionsExt;
        let mut file = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(path)
            .map_err(internal("cannot create wallet key"))?;
        file.write_all(bytes)
            .map_err(internal("cannot write wallet key"))?;
        file.sync_all().ok();
    }
    #[cfg(not(unix))]
    {
        std::fs::write(path, bytes).map_err(internal("cannot write wallet key"))?;
    }
    Ok(())
}

fn enforce_permissions(path: &Path) -> Result<(), RpcError> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let metadata = std::fs::metadata(path).map_err(internal("cannot stat wallet key"))?;
        if metadata.permissions().mode() & 0o777 != 0o600 {
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600))
                .map_err(internal("cannot fix wallet key permissions"))?;
        }
    }
    #[cfg(not(unix))]
    let _ = path;
    Ok(())
}
