//! JSON-RPC 2.0 request handling: parsing, capability enforcement, dispatch.

use crate::error::{RpcError, RpcResult, CAPABILITY_DENIED, INVALID_REQUEST, PARSE_ERROR};
use crate::hal::KNOWN_SENSORS;
use crate::state::{AppState, Caller, SHELL_APP_ID};
use base64::Engine;
use serde_json::{json, Map, Value};
use std::sync::Arc;

pub const DAEMON_VERSION: &str = env!("CARGO_PKG_VERSION");

/// A parsed JSON-RPC request together with the app id the transport stamped
/// on it. Apps never talk to the socket directly; the shell brokers their
/// calls and sets `appId`, so an app cannot forge another app's identity.
#[derive(Debug)]
pub struct Request {
    pub id: Option<Value>,
    pub method: String,
    pub params: Value,
    pub app_id: Option<String>,
}

pub fn parse_request(text: &str) -> Result<Request, RpcError> {
    let value: Value = serde_json::from_str(text)
        .map_err(|e| RpcError::new(PARSE_ERROR, format!("invalid JSON: {e}")))?;
    let object = value
        .as_object()
        .ok_or_else(|| RpcError::new(INVALID_REQUEST, "request must be a JSON object"))?;

    if object.get("jsonrpc").and_then(Value::as_str) != Some("2.0") {
        return Err(RpcError::new(INVALID_REQUEST, "`jsonrpc` must be \"2.0\""));
    }
    let method = object
        .get("method")
        .and_then(Value::as_str)
        .ok_or_else(|| RpcError::new(INVALID_REQUEST, "`method` must be a string"))?
        .to_string();

    let params = match object.get("params") {
        None | Some(Value::Null) => Value::Object(Map::new()),
        Some(Value::Object(map)) => Value::Object(map.clone()),
        Some(_) => {
            return Err(RpcError::new(
                INVALID_REQUEST,
                "`params` must be an object; positional arrays are not supported",
            ))
        }
    };

    Ok(Request {
        id: object.get("id").cloned(),
        method,
        params,
        app_id: object
            .get("appId")
            .and_then(Value::as_str)
            .map(str::to_string),
    })
}

pub fn success(id: Option<Value>, result: Value) -> Value {
    json!({ "jsonrpc": "2.0", "id": id.unwrap_or(Value::Null), "result": result })
}

pub fn failure(id: Option<Value>, error: &RpcError) -> Value {
    json!({ "jsonrpc": "2.0", "id": id.unwrap_or(Value::Null), "error": error.to_json() })
}

/// The capability that guards a method, taken from its namespace prefix.
pub fn required_capability(method: &str) -> Option<&str> {
    method.split_once('.').map(|(prefix, _)| prefix)
}

/// Reject any call the caller is not entitled to make.
pub fn check_capability(state: &AppState, caller: &Caller, method: &str) -> RpcResult<()> {
    let capability =
        required_capability(method).ok_or_else(|| RpcError::method_not_found(method))?;

    // The `shell` namespace drives the system UI itself and is never reachable
    // from an app, whatever that app declares in its manifest.
    if capability == "shell" {
        return if caller.is_shell() {
            Ok(())
        } else {
            Err(RpcError::capability_denied(caller.app_id(), "shell"))
        };
    }

    if caller.is_shell() {
        return Ok(());
    }

    let granted = state.capabilities_for(caller).ok_or_else(|| {
        RpcError::new(
            CAPABILITY_DENIED,
            format!("app `{}` is not installed", caller.app_id()),
        )
        .with_data(json!({ "appId": caller.app_id(), "capability": capability }))
    })?;

    if granted.iter().any(|c| c == capability) {
        Ok(())
    } else {
        Err(RpcError::capability_denied(caller.app_id(), capability))
    }
}

pub async fn dispatch(
    state: &Arc<AppState>,
    caller: &Caller,
    method: &str,
    params: &Value,
) -> RpcResult<Value> {
    check_capability(state, caller, method)?;

    match method {
        "system.info" => {
            let screen = state.hal.screen();
            Ok(json!({
                "version": DAEMON_VERSION,
                "device": state.hal.device(),
                "screen": screen,
            }))
        }
        "system.time" => Ok(json!({
            "epochMs": state.hal.now_ms(),
            "timezone": state.hal.timezone(),
        })),
        // Extension beyond section 4.2: the settings screen needs to show
        // whether the watch is online. Guarded by the `system` capability.
        "system.network" => Ok(serde_json::to_value(state.hal.network())
            .map_err(|e| RpcError::internal(e.to_string()))?),

        "power.status" => Ok(serde_json::to_value(state.hal.power())
            .map_err(|e| RpcError::internal(e.to_string()))?),

        "display.setBrightness" => {
            let percent = require_percent(params, "percent")?;
            state.hal.set_brightness(percent)?;
            state.push_event("display.brightnessChanged", json!({ "percent": percent }));
            Ok(json!({}))
        }
        // Extension: reading back the current level lets the settings slider
        // start in the right place.
        "display.getBrightness" => Ok(json!({ "percent": state.hal.brightness() })),

        "sensors.read" => {
            let sensor = require_string(params, "sensor")?;
            let reading = state.hal.read_sensor(&sensor)?;
            serde_json::to_value(reading).map_err(|e| RpcError::internal(e.to_string()))
        }

        "notifications.list" => Ok(json!({ "items": state.notifications() })),
        "notifications.post" => {
            let title = require_string(params, "title")?;
            let body = optional_string(params, "body").unwrap_or_default();
            // An app may only post as itself; the shell may post on behalf of
            // any app, which is how system notifications are attributed.
            let app_id = match caller {
                Caller::App(id) => id.clone(),
                Caller::Shell => {
                    optional_string(params, "appId").unwrap_or_else(|| SHELL_APP_ID.to_string())
                }
            };
            let notification = state.post_notification(title, body, app_id);
            Ok(json!({ "id": notification.id }))
        }

        "apps.list" => Ok(json!({ "apps": state.apps.list() })),
        "apps.install" => {
            let source = require_string(params, "source")?;
            let is_remote = source.starts_with("http://") || source.starts_with("https://");
            let allow_unsigned = match params.get("allowUnsigned") {
                None => !is_remote,
                Some(Value::Bool(value)) => *value,
                Some(_) => {
                    return Err(RpcError::invalid_params(
                        "`allowUnsigned` must be a boolean",
                    ))
                }
            };
            let expected_sha256 = optional_string(params, "expectedSha256")
                .or_else(|| optional_string(params, "sha256"));
            let expected_publisher = optional_string(params, "expectedPublisherKey")
                .or_else(|| optional_string(params, "publisherKey"));
            let now = state.hal.now_ms();
            let record = state.apps.install_verified(
                &source,
                now,
                allow_unsigned,
                expected_sha256.as_deref(),
                expected_publisher.as_deref(),
            )?;
            state.push_event(
                "apps.changed",
                json!({ "reason": "installed", "appId": record.id }),
            );
            Ok(json!({ "appId": record.id, "version": record.version }))
        }
        "apps.uninstall" => {
            let app_id = require_string(params, "appId")?;
            state.apps.uninstall(&app_id)?;
            state.push_event(
                "apps.changed",
                json!({ "reason": "uninstalled", "appId": app_id }),
            );
            Ok(json!({}))
        }
        "apps.launch" => {
            let app_id = require_string(params, "appId")?;
            let manifest = state.apps.manifest(&app_id).ok_or_else(|| {
                RpcError::invalid_params(format!("app `{app_id}` is not installed"))
            })?;
            state.push_event(
                "apps.launch",
                json!({
                    "appId": manifest.id,
                    "name": manifest.name,
                    "type": manifest.app_type,
                    "url": format!("/apps/{}/{}", manifest.id, manifest.entry),
                    "capabilities": manifest.capabilities,
                }),
            );
            Ok(json!({}))
        }

        "wallet.publicKey" => Ok(json!({ "publicKey": state.wallet.public_key() })),
        "wallet.signTransaction" => sign_transaction(state, caller, params).await,

        // Privileged shell plumbing.
        "shell.ready" => {
            state.push_event("shell.acknowledged", json!({}));
            Ok(json!({
                "version": DAEMON_VERSION,
                "screen": state.hal.screen(),
                "apps": state.apps.list(),
            }))
        }
        "shell.confirmResponse" => {
            let request_id = require_string(params, "requestId")?;
            let approved = params
                .get("approved")
                .and_then(Value::as_bool)
                .ok_or_else(|| RpcError::invalid_params("`approved` must be a boolean"))?;
            let delivered = state.resolve_confirmation(&request_id, approved);
            Ok(json!({ "delivered": delivered }))
        }
        "shell.sensors" => Ok(json!({ "sensors": KNOWN_SENSORS })),

        other => Err(RpcError::method_not_found(other)),
    }
}

async fn sign_transaction(
    state: &Arc<AppState>,
    caller: &Caller,
    params: &Value,
) -> RpcResult<Value> {
    let requested_app = require_string(params, "appId")?;
    // A stamped app id is authoritative. If an app names someone else in the
    // parameters, the call is malformed rather than merely mistaken.
    if let Caller::App(id) = caller {
        if id != &requested_app {
            return Err(RpcError::invalid_params(
                "`appId` does not match the calling application",
            ));
        }
    }

    let message = require_string(params, "message")?;
    let encoding = optional_string(params, "encoding").unwrap_or_else(|| "base64".to_string());
    let bytes = decode_message(&message, &encoding)?;
    if bytes.is_empty() {
        return Err(RpcError::invalid_params("`message` decoded to zero bytes"));
    }

    let summary = json!({
        "appId": requested_app,
        "byteLength": bytes.len(),
        "encoding": encoding,
        "digest": crate::package::hex_sha256(&bytes),
        "publicKey": state.wallet.public_key(),
        "label": optional_string(params, "label"),
    });

    // Blocks until the user answers on the device. There is no other path to
    // a signature.
    state.request_confirmation(&requested_app, summary).await?;

    let signature = state.wallet.sign(&bytes);
    tracing::info!(app = %requested_app, bytes = bytes.len(), "transaction signed after user confirmation");
    Ok(json!({ "signature": signature }))
}

fn decode_message(message: &str, encoding: &str) -> RpcResult<Vec<u8>> {
    match encoding.to_ascii_lowercase().as_str() {
        "base64" => base64::engine::general_purpose::STANDARD
            .decode(message)
            .map_err(|e| RpcError::invalid_params(format!("`message` is not valid base64: {e}"))),
        "base58" => bs58::decode(message)
            .into_vec()
            .map_err(|e| RpcError::invalid_params(format!("`message` is not valid base58: {e}"))),
        "hex" => {
            if !message.len().is_multiple_of(2) {
                return Err(RpcError::invalid_params("`message` hex has an odd length"));
            }
            (0..message.len())
                .step_by(2)
                .map(|i| {
                    u8::from_str_radix(&message[i..i + 2], 16)
                        .map_err(|e| RpcError::invalid_params(format!("`message` is not hex: {e}")))
                })
                .collect()
        }
        other => Err(RpcError::invalid_params(format!(
            "unsupported encoding `{other}`"
        ))),
    }
}

fn require_string(params: &Value, field: &str) -> RpcResult<String> {
    params
        .get(field)
        .and_then(Value::as_str)
        .map(str::to_string)
        .filter(|s| !s.is_empty())
        .ok_or_else(|| RpcError::invalid_params(format!("`{field}` must be a non-empty string")))
}

fn optional_string(params: &Value, field: &str) -> Option<String> {
    params
        .get(field)
        .and_then(Value::as_str)
        .map(str::to_string)
}

fn require_percent(params: &Value, field: &str) -> RpcResult<u8> {
    let value = params
        .get(field)
        .and_then(Value::as_f64)
        .ok_or_else(|| RpcError::invalid_params(format!("`{field}` must be a number")))?;
    if !(0.0..=100.0).contains(&value) {
        return Err(RpcError::invalid_params(format!(
            "`{field}` must be between 0 and 100"
        )));
    }
    Ok(value.round() as u8)
}
