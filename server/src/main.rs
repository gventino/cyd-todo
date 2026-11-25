#![forbid(unsafe_code)]

use std::sync::Arc;
use std::time::Duration;

use cyd_todo_server::app::{AppState, serve, system_clock, tracing_sink};
use cyd_todo_server::config::Config;
use cyd_todo_server::db::Db;
use cyd_todo_server::seed::seed;
use poem::listener::{Listener, TcpListener};

const USAGE: &str = "usage: cyd-todo-server [serve | seed [--force] | link [quest|event|home]]\n\
the configuration is in the environment: CYD_TOKEN, PUBLIC_URL, BIND, DATABASE_URL, TZ_OFFSET_MIN (see README.md)";

fn fail(code: i32, what: &str, why: impl std::fmt::Display) -> ! {
    eprintln!("{what}: {why}");
    std::process::exit(code);
}

#[tokio::main]
async fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let command = args.first().map(String::as_str).unwrap_or("serve");
    let force = args.iter().any(|a| a == "--force");
    let kind = args.get(1).map(String::as_str).unwrap_or("home");
    let good = match command {
        "serve" => args.len() <= 1,
        "seed" => args.iter().skip(1).all(|a| a == "--force"),
        "link" => args.len() <= 2 && matches!(kind, "quest" | "event" | "home"),
        _ => false,
    };
    if !good {
        fail(2, "bad arguments", USAGE);
    }

    let cfg = Config::from_env().unwrap_or_else(|e| fail(2, "configuration error", e));
    tracing_subscriber::fmt().with_target(false).with_max_level(tracing::Level::INFO).init();
    let db = Db::connect(&cfg.database_url).await.unwrap_or_else(|e| fail(1, "database error", e));

    if command == "link" {
        // a one-time link made by hand, to open the phone pages before the board can show a QR code
        let kind = match kind {
            "quest" => cyd_todo_server::db::LinkKind::Quest,
            "event" => cyd_todo_server::db::LinkKind::Event,
            _ => cyd_todo_server::db::LinkKind::Home,
        };
        let code = cyd_todo_server::token::new_code();
        db.create_link(&cyd_todo_server::token::hash(&code), kind, (system_clock())())
            .await
            .unwrap_or_else(|e| fail(1, "link", e));
        println!("{}/m/{code}", cfg.public_url);
        println!("(valid once, for 10 minutes)");
        return;
    }

    if command == "seed" {
        let empty = db.is_empty().await.unwrap_or_else(|e| fail(1, "database error", e));
        if !empty && !force {
            fail(1, "seed", "the database already has rows (use --force to add the demo data anyway)");
        }
        seed(&db, (system_clock())()).await.unwrap_or_else(|e| fail(1, "seed", e));
        println!("demo data added");
        return;
    }

    let state = Arc::new(AppState {
        db,
        token: cfg.token.clone().into_bytes(),
        public_url: cfg.public_url.clone(),
        tz_offset_min: cfg.tz_offset_min,
        csp: cyd_todo_server::html::csp(),
        clock: system_clock(),
        log: tracing_sink(),
        auth_delay: Duration::from_millis(200),
        fault: cfg.fault.as_deref().and_then(cyd_todo_server::faults::Fault::parse),
        fault_slow: cyd_todo_server::faults::SLOW_DEFAULT,
    });
    if let Some(f) = &cfg.fault {
        tracing::warn!(
            "FAULT INJECTION IS ON ({f}): the device API will misbehave on purpose. Never run this for real use."
        );
    }
    let acceptor =
        TcpListener::bind(cfg.bind.clone()).into_acceptor().await.unwrap_or_else(|e| fail(1, "cannot listen", e));
    tracing::info!("listening on {} (public url {})", cfg.bind, cfg.public_url);
    if let Err(e) = serve(acceptor, state, shutdown()).await {
        fail(1, "server error", e);
    }
    tracing::info!("stopped");
}

/// Ctrl-C or SIGTERM (systemd, docker stop).
async fn shutdown() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };
    #[cfg(unix)]
    let term = async {
        if let Ok(mut s) = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()) {
            s.recv().await;
        }
    };
    #[cfg(not(unix))]
    let term = std::future::pending::<()>();
    tokio::select! {
        () = ctrl_c => {},
        () = term => {},
    }
}
