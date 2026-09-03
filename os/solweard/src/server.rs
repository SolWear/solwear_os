//! The two listeners: the JSON-RPC WebSocket on 8730 and the static asset
//! server on 8731.

use crate::error::{RpcError, INVALID_REQUEST};
use crate::rpc;
use crate::state::{AppState, Caller, SHELL_APP_ID};
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{Query, State};
use axum::http::{header, StatusCode, Uri};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::Router;
use futures_util::{SinkExt, StreamExt};
use serde::Deserialize;
use std::path::{Path, PathBuf};
use std::sync::Arc;

// --- JSON-RPC WebSocket ----------------------------------------------------

/// A connection is privileged (the shell) unless it names an app.
///
/// The shell connects without parameters and stamps `appId` on the calls it
/// brokers for sandboxed apps. A client that connects with `?appId=<id>` is
/// permanently bound to that app and cannot raise its own privileges.
#[derive(Debug, Deserialize)]
struct ConnectParams {
    #[serde(default)]
    app_id: Option<String>,
    #[serde(default, rename = "appId")]
    app_id_camel: Option<String>,
}

pub fn rpc_router(state: Arc<AppState>) -> Router {
    Router::new()
        .route("/", get(ws_handler))
        .route("/rpc", get(ws_handler))
        .with_state(state)
}

async fn ws_handler(
    ws: WebSocketUpgrade,
    Query(params): Query<ConnectParams>,
    State(state): State<Arc<AppState>>,
) -> Response {
    let requested = params.app_id.or(params.app_id_camel);
    let bound = match requested {
        None => None,
        Some(id) if id.is_empty() || id == SHELL_APP_ID => {
            return (
                StatusCode::BAD_REQUEST,
                "an app-bound connection needs a non-reserved app id",
            )
                .into_response()
        }
        Some(id) => Some(id),
    };
    ws.on_upgrade(move |socket| connection(socket, state, bound))
}

async fn connection(socket: WebSocket, state: Arc<AppState>, bound_app: Option<String>) {
    let privileged = bound_app.is_none();
    let (mut sink, mut stream) = socket.split();

    // Only the shell receives pushed events.
    let mut events = if privileged {
        tracing::info!("shell connected to the JSON-RPC socket");
        Some(state.register_shell())
    } else {
        tracing::info!(app = %bound_app.clone().unwrap_or_default(), "app connected to the JSON-RPC socket");
        None
    };

    let (out_tx, mut out_rx) = tokio::sync::mpsc::unbounded_channel::<String>();

    // One task owns the sink: replies and pushed events are interleaved here.
    let writer = tokio::spawn(async move {
        loop {
            let next = match events.as_mut() {
                Some(events) => tokio::select! {
                    reply = out_rx.recv() => reply,
                    event = events.recv() => event,
                },
                None => out_rx.recv().await,
            };
            match next {
                Some(text) => {
                    if sink.send(Message::Text(text)).await.is_err() {
                        break;
                    }
                }
                None => break,
            }
        }
    });

    while let Some(Ok(message)) = stream.next().await {
        let text = match message {
            Message::Text(text) => text,
            Message::Binary(bytes) => match String::from_utf8(bytes) {
                Ok(text) => text,
                Err(_) => continue,
            },
            Message::Close(_) => break,
            Message::Ping(_) | Message::Pong(_) => continue,
        };

        let state = state.clone();
        let bound_app = bound_app.clone();
        let out_tx = out_tx.clone();
        // Handle each call on its own task: `wallet.signTransaction` blocks for
        // as long as the confirmation prompt is on screen, and that must not
        // stall the rest of the connection.
        tokio::spawn(async move {
            let response = handle_message(&state, bound_app.as_deref(), &text).await;
            if let Some(response) = response {
                let _ = out_tx.send(response.to_string());
            }
        });
    }

    drop(out_tx);
    let _ = writer.await;
    tracing::info!("JSON-RPC connection closed");
}

async fn handle_message(
    state: &Arc<AppState>,
    bound_app: Option<&str>,
    text: &str,
) -> Option<serde_json::Value> {
    let request = match rpc::parse_request(text) {
        Ok(request) => request,
        Err(error) => return Some(rpc::failure(None, &error)),
    };

    let caller = match resolve_caller(bound_app, request.app_id.as_deref()) {
        Ok(caller) => caller,
        Err(error) => return Some(rpc::failure(request.id.clone(), &error)),
    };

    let result = rpc::dispatch(state, &caller, &request.method, &request.params).await;

    // A request without an id is a notification: no response is sent.
    request.id.as_ref()?;
    Some(match result {
        Ok(value) => rpc::success(request.id, value),
        Err(error) => {
            tracing::debug!(method = %request.method, "rpc error: {error}");
            rpc::failure(request.id, &error)
        }
    })
}

fn resolve_caller(bound_app: Option<&str>, stamped: Option<&str>) -> Result<Caller, RpcError> {
    match (bound_app, stamped) {
        // App-bound connection: the stamp is redundant and must agree.
        (Some(bound), Some(stamped)) if bound != stamped => Err(RpcError::new(
            INVALID_REQUEST,
            "`appId` does not match the identity this connection was opened with",
        )),
        (Some(bound), _) => Ok(Caller::App(bound.to_string())),
        // Shell connection brokering for an app.
        (None, Some(stamped)) if stamped != SHELL_APP_ID => Ok(Caller::App(stamped.to_string())),
        (None, _) => Ok(Caller::Shell),
    }
}

// --- static assets ---------------------------------------------------------

pub fn http_router(state: Arc<AppState>) -> Router {
    Router::new()
        .route("/healthz", get(|| async { "ok" }))
        .route("/system.json", get(system_document))
        .fallback(static_handler)
        .with_state(state)
}

/// Where the shell finds the JSON-RPC socket.
///
/// The shell is served from this port and has to open the other one. Hard
/// coding 8730 into the bundle works on a device and breaks the moment the
/// daemon is asked to bind somewhere else, so the daemon publishes the answer
/// and the shell reads it.
async fn system_document(State(state): State<Arc<AppState>>) -> Response {
    let mut response = axum::Json(state.runtime_document()).into_response();
    response
        .headers_mut()
        .insert(header::CACHE_CONTROL, "no-store".parse().expect("static"));
    response
}

async fn static_handler(State(state): State<Arc<AppState>>, uri: Uri) -> Response {
    let path = percent_decode(uri.path());
    let segments: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
    if segments.iter().any(|s| *s == ".." || *s == ".") {
        return (StatusCode::BAD_REQUEST, "bad path").into_response();
    }

    if segments.first() == Some(&"apps") {
        return serve_app_asset(&state, &segments[1..]);
    }
    let rpc_addr = state.rpc_addr();

    let shell_dir = state.config.shell_dir.clone();
    let relative = if segments.is_empty() {
        "index.html".to_string()
    } else {
        segments.join("/")
    };
    if let Some(response) = serve_file(&shell_dir, &relative) {
        return secure_asset(response, ShellCsp(rpc_addr));
    }
    // Single page shell: unknown paths fall back to the entry document.
    serve_file(&shell_dir, "index.html")
        .map(|response| secure_asset(response, ShellCsp(rpc_addr)))
        .unwrap_or_else(|| {
            (
                StatusCode::NOT_FOUND,
                format!(
                    "shell bundle not found in {}. Build it with `npm run build` in os/shell.",
                    shell_dir.display()
                ),
            )
                .into_response()
        })
}

fn serve_app_asset(state: &AppState, segments: &[&str]) -> Response {
    let Some(app_id) = segments.first() else {
        return (StatusCode::NOT_FOUND, "no app id").into_response();
    };
    let Ok(dir) = state.apps.app_dir(app_id) else {
        return (StatusCode::BAD_REQUEST, "invalid app id").into_response();
    };
    let relative = if segments.len() > 1 {
        segments[1..].join("/")
    } else {
        state
            .apps
            .manifest(app_id)
            .map(|m| m.entry)
            .unwrap_or_else(|| "index.html".to_string())
    };
    if relative == "install.json" {
        return (StatusCode::NOT_FOUND, "asset not found").into_response();
    }
    serve_file(&dir, &relative)
        .map(|response| secure_asset(response, AppCsp(state.http_addr())))
        .map(allow_opaque_origin)
        .unwrap_or_else(|| (StatusCode::NOT_FOUND, "asset not found").into_response())
}

/// The Content-Security-Policy for a sandboxed app document, which is served
/// from the given HTTP address.
pub struct AppCsp(pub std::net::SocketAddr);

/// The Content-Security-Policy for the shell, which may open the JSON-RPC
/// socket at the address the daemon actually bound.
pub struct ShellCsp(pub std::net::SocketAddr);

trait AssetPolicy {
    fn policy(&self) -> String;
}

impl AssetPolicy for AppCsp {
    fn policy(&self) -> String {
        // An app runs in a sandbox without `allow-same-origin`, so its document
        // has an opaque origin and `'self'` matches nothing at all: under a
        // `'self'` policy the app cannot even load its own bundle. The origin it
        // is actually served from has to be named explicitly instead.
        //
        // This does let one app request another's static files, which are not
        // secrets. The boundary that matters is `connect-src 'none'`: an app
        // still cannot reach the JSON-RPC socket, so every privileged call goes
        // through the shell's broker, which stamps the calling app id.
        let origin = format!("http://{}", self.0);
        format!(
            "default-src {origin} data: blob:; script-src {origin} 'unsafe-inline' blob:; style-src {origin} 'unsafe-inline'; img-src {origin} data: blob:; font-src {origin} data:; connect-src 'none'; object-src 'none'; base-uri 'none'; frame-ancestors 'self'"
        )
    }
}

impl AssetPolicy for ShellCsp {
    fn policy(&self) -> String {
        // The port is whatever the RPC listener bound to, which is not 8730
        // when a test or a second instance asked for an ephemeral one.
        format!(
            "default-src 'self' data: blob:; script-src 'self'; style-src 'self';              connect-src ws://127.0.0.1:{port} ws://localhost:{port};              frame-src 'self'; object-src 'none'; base-uri 'none'",
            port = self.0.port()
        )
    }
}

/// Browser-enforced half of the capability boundary. App documents run in an
/// opaque sandbox in the shell and may only use postMessage; denying network
/// connections here prevents an app from bypassing that broker by opening the
/// privileged localhost WebSocket itself.
fn secure_asset(mut response: Response, policy: impl AssetPolicy) -> Response {
    if let Ok(value) = policy.policy().parse() {
        response
            .headers_mut()
            .insert("Content-Security-Policy", value);
    }
    response
}

/// Let an opaque-origin app document fetch its own subresources.
///
/// A module script is always fetched with CORS, and the sandboxed frame sends
/// `Origin: null`, so without this header the browser blocks the app's own
/// bundle. The assets are static files the browser could already request
/// directly, so allowing any origin to read them gives nothing away.
fn allow_opaque_origin(mut response: Response) -> Response {
    if let Ok(value) = "*".parse() {
        response
            .headers_mut()
            .insert(header::ACCESS_CONTROL_ALLOW_ORIGIN, value);
    }
    response
}

fn serve_file(root: &Path, relative: &str) -> Option<Response> {
    let candidate: PathBuf = root.join(relative);
    let canonical_root = root.canonicalize().ok()?;
    let canonical = candidate.canonicalize().ok()?;
    if !canonical.starts_with(&canonical_root) {
        return None;
    }
    if canonical.is_dir() {
        return serve_file(
            root,
            &format!("{}/index.html", relative.trim_end_matches('/')),
        );
    }
    let bytes = std::fs::read(&canonical).ok()?;
    let mime = mime_for(&canonical);
    let mut response = (StatusCode::OK, bytes).into_response();
    let headers = response.headers_mut();
    headers.insert(header::CONTENT_TYPE, mime.parse().ok()?);
    // The shell and apps are local content; nothing here should be cached
    // across an install, and nothing should be embedded by a remote page.
    headers.insert(header::CACHE_CONTROL, "no-cache".parse().ok()?);
    headers.insert("X-Content-Type-Options", "nosniff".parse().ok()?);
    Some(response)
}

fn mime_for(path: &Path) -> &'static str {
    match path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase()
        .as_str()
    {
        "html" | "htm" => "text/html; charset=utf-8",
        "js" | "mjs" => "text/javascript; charset=utf-8",
        "css" => "text/css; charset=utf-8",
        "json" => "application/json; charset=utf-8",
        "svg" => "image/svg+xml",
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "webp" => "image/webp",
        "gif" => "image/gif",
        "ico" => "image/x-icon",
        "woff2" => "font/woff2",
        "woff" => "font/woff",
        "ttf" => "font/ttf",
        "txt" | "md" => "text/plain; charset=utf-8",
        "wasm" => "application/wasm",
        _ => "application/octet-stream",
    }
}

/// Minimal percent decoding: asset names on device are ASCII, but a space or
/// an escaped character in a path should still resolve.
fn percent_decode(input: &str) -> String {
    let bytes = input.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or("");
            if let Ok(value) = u8::from_str_radix(hex, 16) {
                out.push(value);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).to_string()
}
