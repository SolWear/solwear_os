//! Device keystore for the Solana signer.
//!
//! The private key is generated on first start, stored in the daemon's data
//! directory with owner-only permissions, and never leaves this module: it is
//! not returned by any RPC method and is never written to a log.

use crate::error::{RpcError, INTERNAL_ERROR};
use ed25519_dalek::{Signer, SigningKey};
use std::path::{Path, PathBuf};

const SEED_LEN: usize = 32;

pub struct Wallet {
    signing_key: SigningKey,
    path: PathBuf,
}

impl std::fmt::Debug for Wallet {
    /// Deliberately opaque so a stray `{:?}` can never leak key material.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Wallet")
            .field("path", &self.path)
            .field("publicKey", &self.public_key())
            .finish_non_exhaustive()
    }
}

impl Wallet {
    /// Load the key at `path`, generating and persisting one if absent.
    pub fn load_or_create(path: &Path) -> Result<Wallet, RpcError> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(|e| {
                RpcError::new(
                    INTERNAL_ERROR,
                    format!("cannot create keystore directory: {e}"),
                )
            })?;
        }

        if path.exists() {
            let bytes = std::fs::read(path).map_err(|e| {
                RpcError::new(INTERNAL_ERROR, format!("cannot read wallet key: {e}"))
            })?;
            let seed: [u8; SEED_LEN] = bytes.as_slice().try_into().map_err(|_| {
                RpcError::new(
                    INTERNAL_ERROR,
                    format!("wallet key at {} is not {SEED_LEN} bytes", path.display()),
                )
            })?;
            enforce_permissions(path)?;
            return Ok(Wallet {
                signing_key: SigningKey::from_bytes(&seed),
                path: path.to_path_buf(),
            });
        }

        let signing_key = SigningKey::generate(&mut rand::rngs::OsRng);
        write_private(path, &signing_key.to_bytes())?;
        Ok(Wallet {
            signing_key,
            path: path.to_path_buf(),
        })
    }

    /// Base58 encoded Ed25519 public key, the device's Solana address.
    pub fn public_key(&self) -> String {
        bs58::encode(self.signing_key.verifying_key().to_bytes()).into_string()
    }

    /// Sign raw message bytes. Callers must have obtained user confirmation
    /// before reaching this point; the check lives in the RPC layer so that
    /// there is exactly one path to a signature.
    pub fn sign(&self, message: &[u8]) -> String {
        bs58::encode(self.signing_key.sign(message).to_bytes()).into_string()
    }
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
            .map_err(|e| RpcError::new(INTERNAL_ERROR, format!("cannot create wallet key: {e}")))?;
        file.write_all(bytes)
            .map_err(|e| RpcError::new(INTERNAL_ERROR, format!("cannot write wallet key: {e}")))?;
        file.sync_all().ok();
    }
    #[cfg(not(unix))]
    {
        std::fs::write(path, bytes)
            .map_err(|e| RpcError::new(INTERNAL_ERROR, format!("cannot write wallet key: {e}")))?;
    }
    Ok(())
}

/// A key file that has drifted to a wider mode is tightened back to 0600.
fn enforce_permissions(path: &Path) -> Result<(), RpcError> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let metadata = std::fs::metadata(path)
            .map_err(|e| RpcError::new(INTERNAL_ERROR, format!("cannot stat wallet key: {e}")))?;
        let mode = metadata.permissions().mode() & 0o777;
        if mode != 0o600 {
            tracing::warn!("wallet key had mode {mode:o}, tightening to 600");
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600)).map_err(
                |e| {
                    RpcError::new(
                        INTERNAL_ERROR,
                        format!("cannot fix wallet key permissions: {e}"),
                    )
                },
            )?;
        }
    }
    #[cfg(not(unix))]
    let _ = path;
    Ok(())
}
