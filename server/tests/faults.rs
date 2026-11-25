//! A server that misbehaves on purpose: what each fault looks like on a real socket, and what it leaves alone.

mod common;

use std::sync::Arc;
use std::time::{Duration, Instant};

use common::TOKEN;
use cyd_todo_server::app::{AppState, serve};
use cyd_todo_server::db::Db;
use cyd_todo_server::faults::Fault;
use poem::listener::{Acceptor, Listener, TcpListener};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

struct Wire {
    status: u16,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
    /// the connection ended before the body was complete (or with an error)
    broken: bool,
}

impl Wire {
    fn header(&self, name: &str) -> Option<&str> {
        self.headers.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
    }
}

async fn start(fault: Fault, slow_for: Duration) -> (std::net::SocketAddr, Db) {
    let db = Db::memory().await.unwrap();
    let state = Arc::new(AppState {
        db: db.clone(),
        token: TOKEN.as_bytes().to_vec(),
        public_url: "http://127.0.0.1:8080".into(),
        tz_offset_min: 0,
        csp: cyd_todo_server::html::csp(),
        clock: cyd_todo_server::app::system_clock(),
        log: Arc::new(|_| {}),
        auth_delay: Duration::ZERO,
        fault: Some(fault),
        fault_slow: slow_for,
    });
    let acceptor = TcpListener::bind("127.0.0.1:0").into_acceptor().await.unwrap();
    let addr = acceptor.local_addr().remove(0).0.as_socket_addr().copied().unwrap();
    tokio::spawn(serve(acceptor, state, std::future::pending()));
    (addr, db)
}

async fn raw(addr: std::net::SocketAddr, method: &str, path: &str, body: &str, token: Option<&str>) -> Wire {
    let mut s = TcpStream::connect(addr).await.unwrap();
    let auth = token.map(|t| format!("Authorization: Bearer {t}\r\n")).unwrap_or_default();
    let req = format!(
        "{method} {path} HTTP/1.1\r\nHost: test\r\n{auth}Content-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    );
    s.write_all(req.as_bytes()).await.unwrap();
    let mut data = Vec::new();
    let mut broken = false;
    let mut buf = [0u8; 8192];
    loop {
        match s.read(&mut buf).await {
            Ok(0) => break,
            Ok(n) => data.extend_from_slice(&buf[..n]),
            Err(_) => {
                broken = true;
                break;
            }
        }
    }
    let Some(split) = data.windows(4).position(|w| w == b"\r\n\r\n") else {
        return Wire { status: 0, headers: vec![], body: data, broken: true }; // the connection died before any header
    };
    let head = String::from_utf8_lossy(&data[..split]).into_owned();
    let mut lines = head.lines();
    let status: u16 = lines.next().unwrap().split_whitespace().nth(1).unwrap().parse().unwrap();
    let headers =
        lines.filter_map(|l| l.split_once(':')).map(|(k, v)| (k.trim().to_string(), v.trim().to_string())).collect();
    Wire { status, headers, body: data[split + 4..].to_vec(), broken }
}

async fn get(addr: std::net::SocketAddr, path: &str) -> Wire {
    raw(addr, "GET", path, "", Some(TOKEN)).await
}

/// the body of a chunked answer, decoded
fn dechunk(mut b: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    loop {
        let nl = b.windows(2).position(|w| w == b"\r\n").expect("a chunk size line");
        let n = usize::from_str_radix(std::str::from_utf8(&b[..nl]).unwrap().trim(), 16).unwrap();
        b = &b[nl + 2..];
        if n == 0 {
            return out;
        }
        out.extend_from_slice(&b[..n]);
        b = &b[n + 2..];
    }
}

#[tokio::test]
async fn slow_answers_correctly_but_late() {
    let (addr, _) = start(Fault::Slow, Duration::from_millis(400)).await;
    let t = Instant::now();
    let w = get(addr, "/quests").await;
    assert!(t.elapsed() >= Duration::from_millis(400), "{:?}", t.elapsed());
    assert_eq!(w.status, 200);
    let v: serde_json::Value = serde_json::from_slice(&w.body).unwrap();
    assert!(v["quests"].is_array());
}

#[tokio::test]
async fn truncate_promises_the_whole_body_and_breaks_half_way() {
    let (addr, _) = start(Fault::Truncate, Duration::ZERO).await;
    // something to truncate: a list with a body of a few hundred bytes
    raw(addr, "POST", "/quests", r#"{"title":"a quest with a title"}"#, Some(TOKEN)).await;
    let w = get(addr, "/quests").await;
    assert_eq!(w.status, 200);
    let promised: usize = w.header("content-length").expect("a Content-Length").parse().unwrap();
    assert!(promised > 100, "{promised}");
    assert!(w.body.len() < promised, "got {} of {promised}", w.body.len());
    assert!(!w.body.is_empty(), "the first half does arrive");
    assert!(w.broken || w.body.len() < promised);
    assert!(serde_json::from_slice::<serde_json::Value>(&w.body).is_err(), "half of a JSON document is not one");
}

#[tokio::test]
async fn huge_is_valid_json_far_over_the_16_kb_limit() {
    let (addr, _) = start(Fault::Huge, Duration::ZERO).await;
    let w = get(addr, "/quests").await;
    assert_eq!(w.status, 200);
    let len: usize = w.header("content-length").unwrap().parse().unwrap();
    assert!(len > 200_000, "{len}");
    assert_eq!(len, w.body.len());
    assert!(serde_json::from_slice::<serde_json::Value>(&w.body).is_ok());
}

#[tokio::test]
async fn garbage_is_a_200_json_that_is_not_json() {
    let (addr, _) = start(Fault::Garbage, Duration::ZERO).await;
    let w = get(addr, "/changes").await;
    assert_eq!(w.status, 200);
    assert_eq!(w.header("content-type"), Some("application/json"));
    assert_eq!(w.body.len(), 600);
    assert!(serde_json::from_slice::<serde_json::Value>(&w.body).is_err());
    assert!(w.body.contains(&0), "binary, with NULs");
}

#[tokio::test]
async fn error500_is_the_usual_error_body_with_a_500() {
    let (addr, _) = start(Fault::Error500, Duration::ZERO).await;
    let w = get(addr, "/quests").await;
    assert_eq!(w.status, 500);
    let v: serde_json::Value = serde_json::from_slice(&w.body).unwrap();
    assert_eq!(v["error"], "internal");
}

#[tokio::test]
async fn chunked_is_the_right_body_without_a_content_length() {
    let (addr, _) = start(Fault::Chunked, Duration::ZERO).await;
    let w = get(addr, "/changes").await;
    assert_eq!(w.status, 200);
    assert!(w.header("content-length").is_none());
    assert_eq!(w.header("transfer-encoding"), Some("chunked"));
    let v: serde_json::Value = serde_json::from_slice(&dechunk(&w.body)).unwrap();
    assert!(v["changed"].is_boolean());
}

#[tokio::test]
async fn deep_is_a_thousand_brackets_nested() {
    let (addr, _) = start(Fault::Deep, Duration::ZERO).await;
    let w = get(addr, "/quests").await;
    assert_eq!(w.status, 200);
    assert_eq!(w.body.len(), 2000);
    assert!(w.body[..1000].iter().all(|b| *b == b'[') && w.body[1000..].iter().all(|b| *b == b']'));
}

#[tokio::test]
async fn wrongtype_is_valid_json_in_which_nothing_has_the_right_type() {
    let (addr, _) = start(Fault::WrongType, Duration::ZERO).await;
    let w = get(addr, "/quests").await;
    let v: serde_json::Value = serde_json::from_slice(&w.body).unwrap();
    assert!(v["rev"].is_string() && v["xp"].is_null() && v["quests"].is_string() && v["changed"].is_string());
    assert_eq!(v["open"], -1);
    assert!(v["done"].is_f64());
}

#[tokio::test]
async fn faults_touch_the_device_api_only() {
    for (name, fault) in Fault::ALL {
        let (addr, _) = start(fault, Duration::from_millis(50)).await;
        let health = raw(addr, "GET", "/health", "", None).await;
        assert_eq!(health.status, 200, "{name}");
        assert_eq!(health.body, br#"{"ok":true}"#, "{name}: /health is the real one");
        let wrong = raw(addr, "GET", "/quests", "", Some("not-the-token-not-the-token-not-the-token")).await;
        assert_eq!(wrong.status, 401, "{name}: a wrong token is still a 401, not a fault");
        let phone = raw(addr, "GET", "/m/ABCDEFGHJKMNPQRSTVWXYZ0123", "", None).await;
        assert_eq!(phone.status, 404, "{name}: the phone side is untouched");
        assert!(phone.header("content-security-policy").is_some(), "{name}");
    }
}

#[tokio::test]
async fn the_database_is_written_as_usual_whatever_the_answer_says() {
    let (addr, db) = start(Fault::Error500, Duration::ZERO).await;
    let w = raw(addr, "POST", "/quests", r#"{"title":"made, and denied"}"#, Some(TOKEN)).await;
    assert_eq!(w.status, 500, "the board is told the write failed ...");
    assert!(!w.broken);
    let quests = db.list_quests(10).await.unwrap();
    assert_eq!(quests.len(), 1, "... but it happened: the firmware must cope with that");
}
