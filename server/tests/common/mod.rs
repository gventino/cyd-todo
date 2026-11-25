//! A running app for the integration tests: in-memory database, a clock the test controls, a log it can read.

#![allow(dead_code)]

use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use cyd_todo_server::app::{AppState, app};
use cyd_todo_server::db::Db;
use poem::http::{HeaderMap, StatusCode};
use poem::test::TestClient;
use serde_json::Value;

pub const TOKEN: &str = "0123456789abcdef0123456789abcdef";
pub const T0: i64 = 1_796_230_800; // 2026-12-02 17:00 UTC

pub struct Env {
    pub cli: TestClient<poem::endpoint::BoxEndpoint<'static, poem::Response>>,
    pub now: Arc<AtomicI64>,
    pub log: Arc<Mutex<Vec<String>>>,
    pub db: Db,
}

pub struct Reply {
    pub status: StatusCode,
    pub headers: HeaderMap,
    pub bytes: Vec<u8>,
}

impl Reply {
    pub fn json(&self) -> Value {
        serde_json::from_slice(&self.bytes)
            .unwrap_or_else(|e| panic!("not JSON ({e}): {:?}", String::from_utf8_lossy(&self.bytes)))
    }
    pub fn text(&self) -> String {
        String::from_utf8_lossy(&self.bytes).into_owned()
    }
}

pub async fn env() -> Env {
    env_with_delay(Duration::ZERO).await
}

pub async fn env_with_delay(auth_delay: Duration) -> Env {
    let db = Db::memory().await.unwrap();
    let now = Arc::new(AtomicI64::new(T0));
    let log = Arc::new(Mutex::new(Vec::new()));
    let (n, l) = (now.clone(), log.clone());
    let state = Arc::new(AppState {
        db: db.clone(),
        token: TOKEN.as_bytes().to_vec(),
        clock: Arc::new(move || n.load(Ordering::SeqCst)),
        log: Arc::new(move |line| l.lock().unwrap().push(line)),
        auth_delay,
    });
    let ep = poem::EndpointExt::boxed(poem::EndpointExt::map_to_response(app(state)));
    Env { cli: TestClient::new(ep), now, log, db }
}

impl Env {
    pub async fn send(&self, method: &str, path: &str, body: Option<&str>, token: Option<&str>) -> Reply {
        let mut b = match method {
            "GET" => self.cli.get(path),
            "POST" => self.cli.post(path),
            "PATCH" => self.cli.patch(path),
            "DELETE" => self.cli.delete(path),
            "PUT" => self.cli.put(path),
            other => panic!("method {other}"),
        };
        if let Some(t) = token {
            b = b.header("Authorization", format!("Bearer {t}"));
        }
        if let Some(body) = body {
            b = b.content_type("application/json").body(body.to_string());
        }
        let resp = b.send().await;
        let status = resp.0.status();
        let headers = resp.0.headers().clone();
        let bytes = resp.0.into_body().into_bytes().await.unwrap().to_vec();
        Reply { status, headers, bytes }
    }

    /// With the right token.
    pub async fn get(&self, path: &str) -> Reply {
        self.send("GET", path, None, Some(TOKEN)).await
    }
    pub async fn post(&self, path: &str, body: &str) -> Reply {
        self.send("POST", path, Some(body), Some(TOKEN)).await
    }
    pub async fn patch(&self, path: &str, body: &str) -> Reply {
        self.send("PATCH", path, Some(body), Some(TOKEN)).await
    }
    pub async fn delete(&self, path: &str) -> Reply {
        self.send("DELETE", path, None, Some(TOKEN)).await
    }
    pub fn set_now(&self, t: i64) {
        self.now.store(t, Ordering::SeqCst);
    }
}
