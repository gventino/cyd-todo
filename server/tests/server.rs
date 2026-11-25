//! The app on a real socket: what the board will actually see on the wire.

mod common;

use std::sync::Arc;
use std::time::Duration;

use common::TOKEN;
use cyd_todo_server::app::{AppState, serve};
use cyd_todo_server::db::Db;
use poem::listener::{Acceptor, Listener, TcpListener};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

struct Wire {
    status: u16,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}

impl Wire {
    fn header(&self, name: &str) -> Option<&str> {
        self.headers.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
    }
}

async fn start() -> std::net::SocketAddr {
    let db = Db::memory().await.unwrap();
    let state = Arc::new(AppState {
        db,
        token: TOKEN.as_bytes().to_vec(),
        public_url: "http://127.0.0.1:8080".into(),
        tz_offset_min: 0,
        csp: cyd_todo_server::html::csp(),
        clock: cyd_todo_server::app::system_clock(),
        log: Arc::new(|_| {}),
        auth_delay: Duration::ZERO,
    });
    let acceptor = TcpListener::bind("127.0.0.1:0").into_acceptor().await.unwrap();
    let addr = acceptor.local_addr().remove(0).0.as_socket_addr().copied().unwrap();
    tokio::spawn(serve(acceptor, state, std::future::pending()));
    addr
}

async fn request(addr: std::net::SocketAddr, method: &str, path: &str, body: &str) -> Wire {
    let mut s = TcpStream::connect(addr).await.unwrap();
    let req = format!(
        "{method} {path} HTTP/1.1\r\nHost: test\r\nAuthorization: Bearer {TOKEN}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    );
    s.write_all(req.as_bytes()).await.unwrap();
    let mut raw = Vec::new();
    s.read_to_end(&mut raw).await.unwrap();
    let split = raw.windows(4).position(|w| w == b"\r\n\r\n").expect("a header end");
    let head = String::from_utf8_lossy(&raw[..split]).into_owned();
    let mut lines = head.lines();
    let status: u16 = lines.next().unwrap().split_whitespace().nth(1).unwrap().parse().unwrap();
    let headers =
        lines.filter_map(|l| l.split_once(':')).map(|(k, v)| (k.trim().to_string(), v.trim().to_string())).collect();
    Wire { status, headers, body: raw[split + 4..].to_vec() }
}

fn assert_plain_length(w: &Wire) {
    assert!(w.header("transfer-encoding").is_none(), "chunked reply: the board cannot bound it");
    if w.status == 204 {
        // by the HTTP rules a 204 has no Content-Length and no body
        assert!(w.body.is_empty());
        return;
    }
    let len: usize = w.header("content-length").expect("a Content-Length").parse().unwrap();
    assert_eq!(len, w.body.len());
}

#[tokio::test]
async fn every_kind_of_answer_has_a_content_length_and_is_never_chunked() {
    let addr = start().await;
    let created = request(addr, "POST", "/quests", r#"{"title":"a"}"#).await;
    assert_eq!(created.status, 201);
    assert_plain_length(&created);
    for (m, p, b, status) in [
        ("GET", "/quests", "", 200),
        ("GET", "/quests/1", "", 200),
        ("GET", "/events?from=1796230800&to=1796234400", "", 200),
        ("GET", "/changes", "", 200),
        ("GET", "/health", "", 200),
        ("GET", "/quests/9", "", 404),
        ("POST", "/quests", "garbage", 400),
        ("POST", "/quests", r#"{"title":""}"#, 422),
        ("DELETE", "/quests/1", "", 204),
    ] {
        let w = request(addr, m, p, b).await;
        assert_eq!(w.status, status, "{m} {p}");
        assert_plain_length(&w);
    }
}

#[tokio::test]
async fn a_big_list_is_one_body_with_its_length_and_under_16_kb() {
    let addr = start().await;
    let title = "\\".repeat(64);
    for _ in 0..48 {
        let body = serde_json::json!({"title": title, "notes": "\n".repeat(256), "difficulty": 5}).to_string();
        assert_eq!(request(addr, "POST", "/quests", &body).await.status, 201);
    }
    let w = request(addr, "GET", "/quests", "").await;
    assert_eq!(w.status, 200);
    assert_plain_length(&w);
    assert!(w.body.len() < 16 * 1024, "{}", w.body.len());
    assert_eq!(w.header("content-type"), Some("application/json"));
}
