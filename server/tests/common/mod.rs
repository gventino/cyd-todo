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
pub const PUBLIC_URL: &str = "http://192.168.1.50:8080";
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
    env_full(auth_delay, 0).await
}

/// An app that shows and reads times in a zone (minutes east of UTC).
pub async fn env_tz(offset_min: i32) -> Env {
    env_full(Duration::ZERO, offset_min).await
}

async fn env_full(auth_delay: Duration, tz_offset_min: i32) -> Env {
    let db = Db::memory().await.unwrap();
    let now = Arc::new(AtomicI64::new(T0));
    let log = Arc::new(Mutex::new(Vec::new()));
    let (n, l) = (now.clone(), log.clone());
    let state = Arc::new(AppState {
        db: db.clone(),
        token: TOKEN.as_bytes().to_vec(),
        public_url: PUBLIC_URL.to_string(),
        tz_offset_min,
        csp: cyd_todo_server::html::csp(),
        clock: Arc::new(move || n.load(Ordering::SeqCst)),
        log: Arc::new(move |line| l.lock().unwrap().push(line)),
        auth_delay,
        fault: None,
        fault_slow: Duration::ZERO,
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

// ---- the phone side ----

pub struct Visit {
    pub cookie: String, // "sid=..."
    pub csrf: String,
}

impl Env {
    /// Any request, with any headers (a cookie, an Origin) and a form body.
    pub async fn raw(&self, method: &str, path: &str, headers: &[(&str, &str)], form: Option<&str>) -> Reply {
        let mut b = match method {
            "GET" => self.cli.get(path),
            "POST" => self.cli.post(path),
            other => panic!("method {other}"),
        };
        for (k, v) in headers {
            b = b.header(*k, *v);
        }
        if let Some(f) = form {
            b = b.content_type("application/x-www-form-urlencoded").body(f.to_string());
        }
        let resp = b.send().await;
        let status = resp.0.status();
        let headers = resp.0.headers().clone();
        let bytes = resp.0.into_body().into_bytes().await.unwrap().to_vec();
        Reply { status, headers, bytes }
    }

    /// A fresh one-time link from the device API: (the code, the url).
    pub async fn new_link(&self, kind: &str) -> (String, String) {
        let r = self.post("/link", &format!(r#"{{"kind":"{kind}"}}"#)).await;
        assert_eq!(r.status, StatusCode::CREATED, "{}", r.text());
        let url = r.json()["url"].as_str().unwrap().to_string();
        let code = url.rsplit('/').next().unwrap().to_string();
        (code, url)
    }

    /// Trades a link for a session: the cookie and the CSRF token of the page.
    pub async fn visit(&self, kind: &str) -> Visit {
        let (code, _) = self.new_link(kind).await;
        let r = self.raw("GET", &format!("/m/{code}"), &[], None).await;
        assert_eq!(r.status, StatusCode::SEE_OTHER, "{}", r.text());
        let set = r.headers.get("set-cookie").unwrap().to_str().unwrap().to_string();
        let cookie = set.split(';').next().unwrap().to_string();
        let home = self.raw("GET", "/app", &[("Cookie", &cookie)], None).await;
        assert_eq!(home.status, StatusCode::OK);
        let csrf = field_value(&home.text(), "csrf").expect("a csrf field on the home page");
        Visit { cookie, csrf }
    }

    pub async fn page(&self, v: &Visit, path: &str) -> Reply {
        self.raw("GET", path, &[("Cookie", &v.cookie)], None).await
    }

    pub async fn submit(&self, v: &Visit, path: &str, fields: &[(&str, &str)]) -> Reply {
        let mut body = format!("csrf={}", v.csrf);
        for (k, val) in fields {
            body.push('&');
            body.push_str(k);
            body.push('=');
            body.push_str(&urlenc(val));
        }
        self.raw("POST", path, &[("Cookie", &v.cookie)], Some(&body)).await
    }
}

pub fn urlenc(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            out.push(char::from(b));
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

/// The `value` of the first `<input ... name="NAME" ... value="...">` of a page.
pub fn field_value(html: &str, name: &str) -> Option<String> {
    let marker = format!("name=\"{name}\"");
    let at = html.find(&marker)?;
    let tag_start = html[..at].rfind('<')?;
    let tag_end = at + html[at..].find('>')?;
    let tag = &html[tag_start..tag_end];
    let v = tag.find("value=\"")? + 7;
    Some(tag[v..v + tag[v..].find('"')?].to_string())
}
