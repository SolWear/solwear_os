//! Runtime configuration, resolved once at startup from the environment.

use std::net::SocketAddr;
use std::path::{Path, PathBuf};

/// Default location of mutable daemon state on a device.
pub const DEFAULT_DATA_DIR: &str = "/var/lib/solwear";
/// Default location of the built shell bundle on a device image.
pub const DEFAULT_SHELL_DIR: &str = "/usr/share/solwear/shell";
/// The JSON-RPC WebSocket address from section 4 of the specification.
pub const DEFAULT_RPC_ADDR: &str = "127.0.0.1:8730";
/// The static asset address from section 4 of the specification.
pub const DEFAULT_HTTP_ADDR: &str = "127.0.0.1:8731";
/// File the daemon writes on startup recording where it actually bound.
pub const RUNTIME_FILE: &str = "runtime.json";

#[derive(Debug, Clone)]
pub struct Config {
    /// Root of the daemon's mutable state. Apps live in `<data_dir>/apps`,
    /// the wallet key in `<data_dir>/wallet.key`.
    pub data_dir: PathBuf,
    /// Directory containing the built shell (`index.html` and friends).
    pub shell_dir: PathBuf,
    pub rpc_addr: SocketAddr,
    pub http_addr: SocketAddr,
}

impl Config {
    pub fn from_env() -> Self {
        let data_dir = std::env::var_os("SOLWEAR_DATA_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(DEFAULT_DATA_DIR));

        let shell_dir = std::env::var_os("SOLWEAR_SHELL_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(default_shell_dir);

        Config {
            data_dir,
            shell_dir,
            rpc_addr: addr_from_env("SOLWEAR_RPC_ADDR", DEFAULT_RPC_ADDR),
            http_addr: addr_from_env("SOLWEAR_HTTP_ADDR", DEFAULT_HTTP_ADDR),
        }
    }

    pub fn apps_dir(&self) -> PathBuf {
        self.data_dir.join("apps")
    }

    pub fn wallet_path(&self) -> PathBuf {
        self.data_dir.join("wallet.key")
    }

    pub fn runtime_path(&self) -> PathBuf {
        self.data_dir.join(RUNTIME_FILE)
    }
}

/// Read a listen address from the environment.
///
/// The device always uses the two ports the specification names, so the
/// defaults are the whole story there. The override exists so that tests and a
/// second developer instance can ask the operating system for a free port with
/// `127.0.0.1:0` instead of colliding with a daemon that is already running.
/// An unparseable value is a configuration mistake worth failing loudly on,
/// rather than silently falling back to a port someone else may hold.
fn addr_from_env(variable: &str, default: &str) -> SocketAddr {
    match std::env::var(variable) {
        Ok(value) if !value.trim().is_empty() => value.trim().parse().unwrap_or_else(|error| {
            panic!("{variable}=`{value}` is not a valid socket address: {error}")
        }),
        _ => default.parse().expect("static address"),
    }
}

/// On a device the shell is installed at a fixed path. During development the
/// daemon is usually run from the repository, so fall back to the sibling
/// `os/shell/dist` directory when it exists.
fn default_shell_dir() -> PathBuf {
    let installed = Path::new(DEFAULT_SHELL_DIR);
    if installed.is_dir() {
        return installed.to_path_buf();
    }
    if let Ok(exe) = std::env::current_exe() {
        // target/<profile>/solweard -> ../../../shell/dist
        for ancestor in exe.ancestors().skip(1).take(5) {
            let candidate = ancestor.join("shell").join("dist");
            if candidate.is_dir() {
                return candidate;
            }
        }
    }
    let cwd_relative = PathBuf::from("../shell/dist");
    if cwd_relative.is_dir() {
        return cwd_relative;
    }
    installed.to_path_buf()
}
