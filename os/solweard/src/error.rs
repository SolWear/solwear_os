//! JSON-RPC error taxonomy.
//!
//! The application specific codes are stable and part of the API contract:
//! `-32001` capability denied is mandated by the architecture specification.

use serde_json::{json, Value};

pub const PARSE_ERROR: i32 = -32700;
pub const INVALID_REQUEST: i32 = -32600;
pub const METHOD_NOT_FOUND: i32 = -32601;
pub const INVALID_PARAMS: i32 = -32602;
pub const INTERNAL_ERROR: i32 = -32603;

/// The calling app does not hold the capability required by the method.
pub const CAPABILITY_DENIED: i32 = -32001;
/// The user declined a confirmation prompt, or it timed out.
pub const USER_REJECTED: i32 = -32002;
/// The shell is not connected, so no confirmation can be shown.
pub const SHELL_UNAVAILABLE: i32 = -32003;
/// The hardware backing a HAL call is not present on this device.
pub const HAL_UNAVAILABLE: i32 = -32004;
/// Package install, verification, or removal failed.
pub const PACKAGE_ERROR: i32 = -32005;

#[derive(Debug, Clone)]
pub struct RpcError {
    pub code: i32,
    pub message: String,
    pub data: Option<Value>,
}

impl RpcError {
    pub fn new(code: i32, message: impl Into<String>) -> Self {
        RpcError {
            code,
            message: message.into(),
            data: None,
        }
    }

    pub fn with_data(mut self, data: Value) -> Self {
        self.data = Some(data);
        self
    }

    pub fn method_not_found(method: &str) -> Self {
        RpcError::new(METHOD_NOT_FOUND, format!("unknown method `{method}`"))
    }

    pub fn invalid_params(message: impl Into<String>) -> Self {
        RpcError::new(INVALID_PARAMS, message)
    }

    pub fn internal(message: impl Into<String>) -> Self {
        RpcError::new(INTERNAL_ERROR, message)
    }

    pub fn capability_denied(app_id: &str, capability: &str) -> Self {
        RpcError::new(
            CAPABILITY_DENIED,
            format!("app `{app_id}` does not hold the `{capability}` capability"),
        )
        .with_data(json!({ "appId": app_id, "capability": capability }))
    }

    pub fn to_json(&self) -> Value {
        let mut v = json!({ "code": self.code, "message": self.message });
        if let Some(data) = &self.data {
            v["data"] = data.clone();
        }
        v
    }
}

impl std::fmt::Display for RpcError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] {}", self.code, self.message)
    }
}

impl std::error::Error for RpcError {}

pub type RpcResult<T> = Result<T, RpcError>;
