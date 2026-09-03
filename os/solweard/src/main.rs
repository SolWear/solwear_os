use solweard::config::Config;
use solweard::server;
use solweard::state::AppState;
use std::sync::Arc;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_env("SOLWEAR_LOG")
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    let arguments: Vec<String> = std::env::args().skip(1).collect();
    if !arguments.is_empty() {
        return run_command(&arguments);
    }

    let config = Config::from_env();
    tracing::info!(
        version = env!("CARGO_PKG_VERSION"),
        data_dir = %config.data_dir.display(),
        shell_dir = %config.shell_dir.display(),
        "starting solweard"
    );

    let hal = solweard::hal::from_env();
    let state = AppState::new(config.clone(), hal).map_err(|e| anyhow::anyhow!(e.to_string()))?;
    tracing::info!(public_key = %state.wallet.public_key(), "wallet ready");

    let rpc_listener = tokio::net::TcpListener::bind(config.rpc_addr).await?;
    let http_listener = tokio::net::TcpListener::bind(config.http_addr).await?;
    // Report what was actually bound, not what was asked for: a configured
    // port of 0 means the kernel chose one, and every consumer needs the real
    // number.
    state.set_bound(rpc_listener.local_addr()?, http_listener.local_addr()?);
    tracing::info!("JSON-RPC listening on {}", state.rpc_url());
    tracing::info!("assets listening on http://{}", state.http_addr());
    write_runtime_file(&config, &state);

    let rpc = axum::serve(
        rpc_listener,
        server::rpc_router(Arc::clone(&state)).into_make_service(),
    );
    let http = axum::serve(
        http_listener,
        server::http_router(state).into_make_service(),
    );

    let outcome = tokio::select! {
        result = rpc => result.map_err(anyhow::Error::from),
        result = http => result.map_err(anyhow::Error::from),
        _ = shutdown_signal() => {
            tracing::info!("shutting down");
            Ok(())
        }
    };
    let _ = std::fs::remove_file(config.runtime_path());
    outcome
}

/// Record where the daemon bound, so that a supervisor, a test harness or a
/// developer can find it without scraping the log. Written last, once both
/// listeners exist, and removed on shutdown so a stale file never points at a
/// daemon that is no longer running.
fn write_runtime_file(config: &Config, state: &AppState) {
    let path = config.runtime_path();
    let document = format!("{:#}\n", state.runtime_document());
    if let Err(error) = std::fs::write(&path, document) {
        tracing::warn!(path = %path.display(), "could not record the runtime address: {error}");
    }
}

/// Device-side package command used by first boot and `solwear install` over
/// SSH. It deliberately reuses AppStore, so daemon and command-line installs
/// have exactly the same verification and extraction path.
fn run_command(arguments: &[String]) -> anyhow::Result<()> {
    if arguments.first().map(String::as_str) != Some("install") {
        anyhow::bail!("usage: solweard install <package.swa> [--allow-unsigned] [--sha256 <hex>] [--publisher-key <base64>]");
    }

    let source = arguments.get(1).filter(|value| !value.starts_with('-')).ok_or_else(|| {
        anyhow::anyhow!("usage: solweard install <package.swa> [--allow-unsigned] [--sha256 <hex>] [--publisher-key <base64>]")
    })?;
    let mut allow_unsigned = false;
    let mut expected_sha256: Option<&str> = None;
    let mut expected_publisher: Option<&str> = None;
    let mut index = 2;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--allow-unsigned" => {
                allow_unsigned = true;
                index += 1;
            }
            "--sha256" | "--publisher-key" => {
                let flag = arguments[index].as_str();
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| anyhow::anyhow!("{flag} needs a value"))?;
                if flag == "--sha256" {
                    expected_sha256 = Some(value);
                } else {
                    expected_publisher = Some(value);
                }
                index += 2;
            }
            unknown => anyhow::bail!("unknown install option `{unknown}`"),
        }
    }

    let config = Config::from_env();
    let store = solweard::apps::AppStore::new(config.apps_dir())?;
    let now_ms = solweard::hal::system_now_ms();
    let record = store
        .install_verified(
            source,
            now_ms,
            allow_unsigned,
            expected_sha256,
            expected_publisher,
        )
        .map_err(|error| anyhow::anyhow!(error.to_string()))?;
    println!("{} {}", record.id, record.version);
    Ok(())
}

/// Systemd stops the unit with SIGTERM; Ctrl-C is for development.
async fn shutdown_signal() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };

    #[cfg(unix)]
    let terminate = async {
        if let Ok(mut signal) =
            tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
        {
            signal.recv().await;
        }
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }
}
