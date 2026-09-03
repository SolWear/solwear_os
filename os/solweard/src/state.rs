//! Shared daemon state: the HAL, the app store, the wallet, notifications,
//! and the set of connected shell clients that receive pushed events.

use crate::apps::AppStore;
use crate::config::Config;
use crate::error::{RpcError, SHELL_UNAVAILABLE, USER_REJECTED};
use crate::hal::Hal;
use crate::wallet::Wallet;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use tokio::sync::{mpsc, oneshot};

/// How long a wallet confirmation prompt stays on screen before it is treated
/// as a refusal. A signature must never happen without a deliberate tap.
pub const CONFIRM_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(90);

/// Most recent notifications kept in memory; older ones are dropped.
const MAX_NOTIFICATIONS: usize = 64;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Notification {
    pub id: String,
    pub title: String,
    pub body: String,
    pub app_id: String,
    pub timestamp_ms: u64,
}

/// Who is making a call. The shell is privileged; everything else is an app
/// whose capabilities are read from its installed manifest.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Caller {
    Shell,
    App(String),
}

impl Caller {
    pub fn app_id(&self) -> &str {
        match self {
            Caller::Shell => SHELL_APP_ID,
            Caller::App(id) => id,
        }
    }

    pub fn is_shell(&self) -> bool {
        matches!(self, Caller::Shell)
    }
}

/// Reserved id of the system shell. An app may not claim it.
pub const SHELL_APP_ID: &str = "tech.solwear.shell";

pub struct AppState {
    pub config: Config,
    pub hal: Arc<dyn Hal>,
    pub apps: AppStore,
    pub wallet: Wallet,
    /// Where the two listeners actually bound. A configured port of `0` means
    /// the kernel picks one, so anything that reports a URL — the shell's
    /// discovery document, the asset Content-Security-Policy, the runtime file
    /// — has to read the resolved address rather than the configured one.
    bound: Mutex<BoundAddrs>,
    notifications: Mutex<Vec<Notification>>,
    next_id: AtomicU64,
    shells: Mutex<Vec<mpsc::UnboundedSender<String>>>,
    pending_confirms: Mutex<HashMap<String, oneshot::Sender<bool>>>,
}

impl AppState {
    pub fn new(config: Config, hal: Arc<dyn Hal>) -> Result<Arc<AppState>, RpcError> {
        let apps = AppStore::new(config.apps_dir())
            .map_err(|e| RpcError::internal(format!("cannot open app store: {e}")))?;
        let wallet = Wallet::load_or_create(&config.wallet_path())?;
        let bound = BoundAddrs {
            rpc: config.rpc_addr,
            http: config.http_addr,
        };
        Ok(Arc::new(AppState {
            bound: Mutex::new(bound),
            config,
            hal,
            apps,
            wallet,
            notifications: Mutex::new(Vec::new()),
            next_id: AtomicU64::new(1),
            shells: Mutex::new(Vec::new()),
            pending_confirms: Mutex::new(HashMap::new()),
        }))
    }

    /// Record the addresses the listeners actually bound to.
    pub fn set_bound(&self, rpc: SocketAddr, http: SocketAddr) {
        *self.bound.lock().expect("bound") = BoundAddrs { rpc, http };
    }

    pub fn rpc_addr(&self) -> SocketAddr {
        self.bound.lock().expect("bound").rpc
    }

    pub fn http_addr(&self) -> SocketAddr {
        self.bound.lock().expect("bound").http
    }

    /// The WebSocket URL the shell should open. Served to the shell so that it
    /// never has to assume a port number.
    pub fn rpc_url(&self) -> String {
        format!("ws://{}/rpc", self.rpc_addr())
    }

    /// What the daemon reports about itself once both listeners are up.
    pub fn runtime_document(&self) -> Value {
        json!({
            "version": env!("CARGO_PKG_VERSION"),
            "pid": std::process::id(),
            "rpcAddr": self.rpc_addr().to_string(),
            "httpAddr": self.http_addr().to_string(),
            "rpcUrl": self.rpc_url(),
            "httpUrl": format!("http://{}/", self.http_addr()),
        })
    }

    fn next_sequence(&self) -> u64 {
        self.next_id.fetch_add(1, Ordering::Relaxed)
    }

    // --- shell connections -------------------------------------------------

    pub fn register_shell(&self) -> mpsc::UnboundedReceiver<String> {
        let (tx, rx) = mpsc::unbounded_channel();
        self.shells.lock().expect("shells").push(tx);
        rx
    }

    pub fn shell_connected(&self) -> bool {
        let mut shells = self.shells.lock().expect("shells");
        shells.retain(|tx| !tx.is_closed());
        !shells.is_empty()
    }

    /// Push a JSON-RPC notification (a request without an id) to every
    /// connected shell.
    pub fn push_event(&self, method: &str, params: Value) {
        let payload = json!({ "jsonrpc": "2.0", "method": method, "params": params }).to_string();
        let mut shells = self.shells.lock().expect("shells");
        shells.retain(|tx| tx.send(payload.clone()).is_ok());
    }

    // --- notifications -----------------------------------------------------

    pub fn post_notification(&self, title: String, body: String, app_id: String) -> Notification {
        let notification = Notification {
            id: format!("n{}", self.next_sequence()),
            title,
            body,
            app_id,
            timestamp_ms: self.hal.now_ms(),
        };
        {
            let mut list = self.notifications.lock().expect("notifications");
            list.push(notification.clone());
            let overflow = list.len().saturating_sub(MAX_NOTIFICATIONS);
            if overflow > 0 {
                list.drain(0..overflow);
            }
        }
        self.push_event(
            "notifications.posted",
            json!({ "notification": notification }),
        );
        notification
    }

    pub fn notifications(&self) -> Vec<Notification> {
        let list = self.notifications.lock().expect("notifications");
        let mut items = list.clone();
        items.reverse(); // newest first
        items
    }

    // --- wallet confirmation ----------------------------------------------

    /// Ask the shell to prompt the user, then wait for the answer.
    ///
    /// Returns `Ok(())` only on an affirmative response. A timeout, a refusal,
    /// or a shell that disappears mid-prompt all deny the request.
    pub async fn request_confirmation(&self, app_id: &str, summary: Value) -> Result<(), RpcError> {
        if !self.shell_connected() {
            return Err(RpcError::new(
                SHELL_UNAVAILABLE,
                "no shell is connected, so the confirmation prompt cannot be shown",
            ));
        }

        let request_id = format!("c{}", self.next_sequence());
        let (tx, rx) = oneshot::channel();
        self.pending_confirms
            .lock()
            .expect("confirms")
            .insert(request_id.clone(), tx);

        self.push_event(
            "wallet.confirmRequest",
            json!({ "requestId": request_id, "appId": app_id, "summary": summary }),
        );

        let outcome = tokio::time::timeout(CONFIRM_TIMEOUT, rx).await;
        self.pending_confirms
            .lock()
            .expect("confirms")
            .remove(&request_id);

        match outcome {
            Ok(Ok(true)) => Ok(()),
            Ok(Ok(false)) => Err(RpcError::new(
                USER_REJECTED,
                "the user declined the request",
            )),
            Ok(Err(_)) => Err(RpcError::new(
                SHELL_UNAVAILABLE,
                "the shell disconnected before the user answered",
            )),
            Err(_) => {
                self.push_event(
                    "wallet.confirmCancelled",
                    json!({ "requestId": request_id }),
                );
                Err(RpcError::new(
                    USER_REJECTED,
                    "the confirmation prompt timed out",
                ))
            }
        }
    }

    /// Deliver the user's answer from the shell. Returns false when the
    /// request is unknown, which happens if it already timed out.
    pub fn resolve_confirmation(&self, request_id: &str, approved: bool) -> bool {
        let sender = self
            .pending_confirms
            .lock()
            .expect("confirms")
            .remove(request_id);
        match sender {
            Some(tx) => tx.send(approved).is_ok(),
            None => false,
        }
    }

    /// Capabilities granted to a caller. The shell holds all of them.
    pub fn capabilities_for(&self, caller: &Caller) -> Option<Vec<String>> {
        match caller {
            Caller::Shell => Some(
                crate::manifest::CAPABILITIES
                    .iter()
                    .map(|c| (*c).to_string())
                    .collect(),
            ),
            Caller::App(id) => self.apps.manifest(id).map(|m| m.capabilities),
        }
    }
}

#[derive(Debug, Clone, Copy)]
struct BoundAddrs {
    rpc: SocketAddr,
    http: SocketAddr,
}
