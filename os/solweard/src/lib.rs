//! SolWear OS system daemon.
//!
//! `solweard` owns everything the web-based shell cannot do for itself: it
//! speaks JSON-RPC 2.0 over a localhost WebSocket, serves the shell and the
//! installed app bundles over HTTP, abstracts the hardware, manages `.swa`
//! packages, and holds the signing key behind a mandatory user confirmation.

pub mod apps;
pub mod config;
pub mod error;
pub mod hal;
pub mod manifest;
pub mod package;
pub mod rpc;
pub mod server;
pub mod state;
pub mod wallet;
