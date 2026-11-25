//! The quest calls of `docs/api.md`, through the whole stack (router, auth, parsing, rules, database).

mod common;

use std::time::{Duration, Instant};

use common::*;
use poem::http::StatusCode;
use serde_json::json;

fn error_code(r: &Reply) -> String {
    r.json()["error"].as_str().unwrap().to_string()
}

#[tokio::test]
async fn health_needs_no_token_and_everything_else_does() {
    let e = env().await;
    let r = e.send("GET", "/health", None, None).await;
    assert_eq!(r.status, StatusCode::OK);
    assert_eq!(r.json(), json!({"ok": true}));
    // only the GET is open: any other method on it needs the token too
    assert_eq!(e.send("POST", "/health", Some("{}"), None).await.status, StatusCode::UNAUTHORIZED);
    assert_eq!(e.send("DELETE", "/health", None, None).await.status, StatusCode::UNAUTHORIZED);
    for (m, p) in [
        ("GET", "/quests"),
        ("POST", "/quests"),
        ("GET", "/quests/1"),
        ("PATCH", "/quests/1"),
        ("DELETE", "/quests/1"),
        ("GET", "/nothing"),
    ] {
        let none = e.send(m, p, Some("{}"), None).await;
        let wrong = e.send(m, p, Some("{}"), Some("not-the-token-not-the-token-not-the")).await;
        assert_eq!(none.status, StatusCode::UNAUTHORIZED, "{m} {p}");
        assert_eq!(wrong.status, StatusCode::UNAUTHORIZED, "{m} {p}");
        assert_eq!(none.bytes, wrong.bytes, "a missing token and a wrong one must look the same");
        assert_eq!(error_code(&none), "unauthorized");
    }
}

#[tokio::test]
async fn authorization_must_be_a_bearer_with_the_exact_token() {
    let e = env().await;
    for header in [
        TOKEN.to_string(),
        format!("bearer {TOKEN}"),
        format!("Basic {TOKEN}"),
        format!("Bearer  {TOKEN}"),
        format!("Bearer {TOKEN} "),
        format!("Bearer {}", &TOKEN[..31]),
        format!("Bearer {TOKEN}x"),
    ] {
        let resp = e.cli.get("/quests").header("Authorization", header.clone()).send().await;
        assert_eq!(resp.0.status(), StatusCode::UNAUTHORIZED, "{header:?}");
    }
    let r = e.send("GET", "/quests", None, Some(TOKEN)).await;
    assert_eq!(r.status, StatusCode::OK);
}

#[tokio::test]
async fn a_wrong_token_costs_the_caller_at_least_the_delay() {
    let e = env_with_delay(Duration::from_millis(200)).await;
    let t = Instant::now();
    let r = e.send("GET", "/quests", None, Some("wrong")).await;
    assert_eq!(r.status, StatusCode::UNAUTHORIZED);
    assert!(t.elapsed() >= Duration::from_millis(200), "{:?}", t.elapsed());
    let t = Instant::now(); // the right token is not slowed down
    e.send("GET", "/quests", None, Some(TOKEN)).await;
    assert!(t.elapsed() < Duration::from_millis(150));
}

#[tokio::test]
async fn a_new_quest_has_the_documented_shape_and_defaults() {
    let e = env().await;
    let r = e.post("/quests", r#"{"title":"Slay the inbox"}"#).await;
    assert_eq!(r.status, StatusCode::CREATED);
    assert_eq!(
        r.json(),
        json!({"id":1,"title":"Slay the inbox","notes":"","difficulty":1,"due":null,"state":"open","done_at":null,"updated_at":T0})
    );
    // the members come in the order of the document
    let text = r.text();
    let order: Vec<usize> =
        ["\"id\"", "\"title\"", "\"notes\"", "\"difficulty\"", "\"due\"", "\"state\"", "\"done_at\"", "\"updated_at\""]
            .iter()
            .map(|k| text.find(k).unwrap())
            .collect();
    assert!(order.windows(2).all(|w| w[0] < w[1]));
    let full = e.post("/quests", r#"{"title":"Dentist","notes":"line\ntwo","difficulty":4,"due":"2026-12-24"}"#).await;
    assert_eq!(full.json()["difficulty"], 4);
    assert_eq!(full.json()["due"], "2026-12-24");
    assert_eq!(full.json()["notes"], "line\ntwo");
    let nulls = e.post("/quests", r#"{"title":"n","notes":null,"difficulty":null,"due":null}"#).await;
    assert_eq!(nulls.status, StatusCode::CREATED);
    assert_eq!(nulls.json()["difficulty"], 1);
}

#[tokio::test]
async fn responses_are_json_never_cached_and_errors_have_one_shape() {
    let e = env().await;
    let ok = e.get("/quests").await;
    assert_eq!(ok.headers.get("content-type").unwrap(), "application/json");
    assert_eq!(ok.headers.get("cache-control").unwrap(), "no-store");
    let bad = e.post("/quests", "nope").await;
    assert_eq!(bad.headers.get("content-type").unwrap(), "application/json");
    let v = bad.json();
    assert_eq!(v.as_object().unwrap().len(), 2);
    assert!(v["error"].is_string() && v["message"].is_string());
}

#[tokio::test]
async fn invalid_input_is_a_422_with_a_fixed_message() {
    let e = env().await;
    let cases = [
        r#"{"title":""}"#,
        r#"{"title":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"#,
        r#"{"title":"tab\there"}"#,
        r#"{"title":"ok","difficulty":0}"#,
        r#"{"title":"ok","difficulty":6}"#,
        r#"{"title":"ok","due":"2027-02-29"}"#,
        r#"{"title":"ok","due":"tomorrow"}"#,
        r#"{"title":"ok","notes":"bell\u0007"}"#,
    ];
    for c in cases {
        let r = e.post("/quests", c).await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{c}");
        assert_eq!(error_code(&r), "invalid");
    }
    let long_notes = format!(r#"{{"title":"ok","notes":"{}"}}"#, "n".repeat(257));
    assert_eq!(e.post("/quests", &long_notes).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    let max_notes = format!(r#"{{"title":"ok","notes":"{}"}}"#, "n".repeat(256));
    assert_eq!(e.post("/quests", &max_notes).await.status, StatusCode::CREATED);
}

#[tokio::test]
async fn malformed_input_is_a_400() {
    let e = env().await;
    let cases = [
        "",
        "nope",
        "[]",
        "null",
        r#"{"title":5}"#,
        r#"{"title":"ok","difficulty":"3"}"#,
        r#"{"title":"ok","difficulty":2.5}"#,
        r#"{"title":"ok","extra":1}"#,
        r#"{"title":null}"#,
        r#"{}"#,
        r#"{"title":"ok","due":20261224}"#,
    ];
    for c in cases {
        let r = e.post("/quests", c).await;
        assert_eq!(r.status, StatusCode::BAD_REQUEST, "{c:?}");
        assert_eq!(error_code(&r), "bad_request");
    }
    // invalid UTF-8 in the body
    let resp = e
        .cli
        .post("/quests")
        .header("Authorization", format!("Bearer {TOKEN}"))
        .body(vec![b'{', 0xff, 0xfe, b'}'])
        .send()
        .await;
    assert_eq!(resp.0.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn a_body_over_4096_bytes_is_a_413() {
    let e = env().await;
    let pad = |n: usize| format!(r#"{{"title":"ok","notes":"","x":"{}"}}"#, "a".repeat(n));
    let over = pad(4096);
    assert!(over.len() > 4096);
    let r = e.post("/quests", &over).await;
    assert_eq!(r.status, StatusCode::PAYLOAD_TOO_LARGE);
    assert_eq!(error_code(&r), "too_large");
    // exactly 4096 bytes is read (and then refused for its unknown field: a 400, not a 413)
    let base = pad(0);
    let exactly = format!(r#"{{"title":"ok","notes":"","x":"{}"}}"#, "a".repeat(4096 - base.len()));
    assert_eq!(exactly.len(), 4096);
    assert_eq!(e.post("/quests", &exactly).await.status, StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn get_patch_and_delete_a_quest() {
    let e = env().await;
    e.post("/quests", r#"{"title":"a","notes":"n","difficulty":3,"due":"2026-12-24"}"#).await;
    let g = e.get("/quests/1").await;
    assert_eq!(g.status, StatusCode::OK);
    assert_eq!(g.json()["title"], "a");

    e.set_now(T0 + 5);
    // absent members stay
    let p = e.patch("/quests/1", r#"{"title":"b"}"#).await;
    assert_eq!(p.status, StatusCode::OK);
    let v = p.json();
    assert_eq!(
        (v["title"].clone(), v["notes"].clone(), v["difficulty"].clone(), v["due"].clone()),
        (json!("b"), json!("n"), json!(3), json!("2026-12-24"))
    );
    assert_eq!(v["updated_at"], T0 + 5);
    // null clears a date, and only a date
    let p = e.patch("/quests/1", r#"{"due":null}"#).await;
    assert_eq!(p.json()["due"], json!(null));
    for member in ["title", "notes", "difficulty", "state"] {
        // alone, and next to a change that would be fine (so that the null itself is what is refused)
        let r = e.patch("/quests/1", &format!(r#"{{"{member}":null}}"#)).await;
        assert_eq!(r.status, StatusCode::BAD_REQUEST, "{member}");
        let r = e.patch("/quests/1", &format!(r#"{{"due":"2026-12-25","{member}":null}}"#)).await;
        assert_eq!(r.status, StatusCode::BAD_REQUEST, "{member} with a due date");
    }
    assert_eq!(e.get("/quests/1").await.json()["due"], json!(null), "a refused patch changes nothing");
    // nothing to change, an unknown member, bad values
    assert_eq!(e.patch("/quests/1", "{}").await.status, StatusCode::BAD_REQUEST);
    assert_eq!(e.patch("/quests/1", r#"{"colour":"red"}"#).await.status, StatusCode::BAD_REQUEST);
    assert_eq!(e.patch("/quests/1", r#"{"state":"finished"}"#).await.status, StatusCode::BAD_REQUEST);
    assert_eq!(e.patch("/quests/1", r#"{"difficulty":9}"#).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get("/quests/1").await.json()["difficulty"], 3);

    // done sets done_at, open clears it
    e.set_now(T0 + 100);
    let d = e.patch("/quests/1", r#"{"state":"done"}"#).await.json();
    assert_eq!((d["state"].clone(), d["done_at"].clone()), (json!("done"), json!(T0 + 100)));
    let o = e.patch("/quests/1", r#"{"state":"open"}"#).await.json();
    assert_eq!((o["state"].clone(), o["done_at"].clone()), (json!("open"), json!(null)));

    let del = e.delete("/quests/1").await;
    assert_eq!(del.status, StatusCode::NO_CONTENT);
    assert!(del.bytes.is_empty());
    assert_eq!(e.delete("/quests/1").await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.get("/quests/1").await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.patch("/quests/1", r#"{"title":"x"}"#).await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn ids_that_are_not_ids_and_methods_that_are_not_there() {
    let e = env().await;
    for p in ["/quests/abc", "/quests/-", "/quests/1.5", "/quests/99999999999999999999", "/quests/1/2", "/nothing", "/"]
    {
        let r = e.get(p).await;
        assert_eq!(r.status, StatusCode::NOT_FOUND, "{p}");
        assert_eq!(error_code(&r), "not_found", "{p}");
    }
    let r = e.send("PUT", "/quests", Some("{}"), Some(TOKEN)).await;
    assert_eq!(r.status, StatusCode::METHOD_NOT_ALLOWED);
    assert_eq!(error_code(&r), "method_not_allowed");
    assert_eq!(e.send("DELETE", "/quests", None, Some(TOKEN)).await.status, StatusCode::METHOD_NOT_ALLOWED);
}

#[tokio::test]
async fn the_list_has_the_envelope_the_order_and_all_time_numbers() {
    let e = env().await;
    let empty = e.get("/quests").await.json();
    assert_eq!(empty, json!({"rev":0,"xp":0,"open":0,"done":0,"truncated":false,"quests":[]}));
    e.post("/quests", r#"{"title":"late","difficulty":2,"due":"2026-12-31"}"#).await;
    e.post("/quests", r#"{"title":"early","difficulty":5,"due":"2026-12-05"}"#).await;
    e.post("/quests", r#"{"title":"undated","difficulty":3}"#).await;
    e.post("/quests", r#"{"title":"finished","difficulty":4}"#).await;
    e.set_now(T0 + 50);
    e.patch("/quests/4", r#"{"state":"done"}"#).await;
    let v = e.get("/quests").await.json();
    assert_eq!(v["rev"], 5);
    assert_eq!((v["xp"].clone(), v["open"].clone(), v["done"].clone()), (json!(40), json!(3), json!(1)));
    let titles: Vec<&str> = v["quests"].as_array().unwrap().iter().map(|q| q["title"].as_str().unwrap()).collect();
    assert_eq!(titles, ["early", "late", "undated", "finished"]);
}

#[tokio::test]
async fn more_than_48_quests_are_cut_and_say_so_while_the_totals_stay_whole() {
    let e = env().await;
    for i in 0..60 {
        e.post("/quests", &format!(r#"{{"title":"q{i}","difficulty":2}}"#)).await;
    }
    let v = e.get("/quests").await.json();
    assert_eq!(v["quests"].as_array().unwrap().len(), 48);
    assert_eq!(v["truncated"], true);
    assert_eq!((v["open"].clone(), v["done"].clone()), (json!(60), json!(0)));
}

#[tokio::test]
async fn a_failed_write_changes_nothing_and_the_revision_stays() {
    let e = env().await;
    e.post("/quests", r#"{"title":"a"}"#).await;
    let before = e.db.rev().await.unwrap();
    e.post("/quests", r#"{"title":""}"#).await;
    e.patch("/quests/1", r#"{"difficulty":9}"#).await;
    e.delete("/quests/77").await;
    assert_eq!(e.db.rev().await.unwrap(), before);
}

// ---- nothing the client sent comes back, and nothing secret is logged ----

const MARKER: &str = "ZZMARKERZZ";

#[tokio::test]
async fn no_error_ever_contains_what_the_client_sent() {
    let e = env().await;
    let evil_member = format!(r#"{{"title":"ok","{MARKER}":1}}"#);
    let evil_title = format!(r#"{{"title":"{MARKER}\u0007"}}"#);
    let evil_due = format!(r#"{{"title":"ok","due":"{MARKER}"}}"#);
    let evil_json = format!("{{{MARKER}");
    let evil_state = format!(r#"{{"state":"{MARKER}"}}"#);
    let bodies = [evil_member, evil_title, evil_due, evil_json, evil_state];
    let mut replies = Vec::new();
    for b in &bodies {
        replies.push(e.post("/quests", b).await);
        replies.push(e.patch("/quests/1", b).await);
    }
    replies.push(e.get(&format!("/quests/{MARKER}")).await);
    replies.push(e.get(&format!("/{MARKER}")).await);
    replies.push(e.send("GET", "/quests", None, Some(MARKER)).await);
    replies.push(e.send("GET", &format!("/quests?{MARKER}={MARKER}"), None, Some(MARKER)).await);
    for r in replies {
        assert!(!r.text().contains(MARKER), "{} echoed the client: {}", r.status, r.text());
        assert!(r.status.is_client_error());
    }
}

#[tokio::test]
async fn the_access_log_has_no_token_no_query_no_body() {
    let e = env().await;
    e.post("/quests", &format!(r#"{{"title":"{MARKER}"}}"#)).await;
    e.send("GET", "/quests", None, Some(MARKER)).await;
    e.get(&format!("/quests?filter={MARKER}")).await;
    e.get("/quests/1").await;
    e.send("GET", "/health", None, None).await;
    e.get("/quests/%0A%1B%5B31mred").await; // control characters in a path
    e.get(&format!("/{}", "p".repeat(300))).await;
    let log = e.log.lock().unwrap().clone();
    assert_eq!(log.len(), 7);
    assert!(log[6].len() < 100, "a long path is cut in the log: {}", log[6]);
    for line in &log {
        assert!(!line.contains(MARKER), "{line}");
        assert!(!line.contains(TOKEN), "{line}");
        assert!(!line.contains('\n') && !line.contains('\u{1b}'), "{line:?}");
    }
    assert!(log[0].starts_with("POST /quests 201 "), "{}", log[0]);
    assert!(log[1].starts_with("GET /quests 401 "), "{}", log[1]);
    assert!(log[2].starts_with("GET /quests 200 "), "{}", log[2]);
    assert!(log[4].starts_with("GET /health 200 "), "{}", log[4]);
}

#[tokio::test]
async fn a_refusal_is_logged_with_its_class_and_never_with_what_was_sent() {
    let e = env().await;
    e.get("/events?from=1700000000&to=1800000000").await; // a window of 1157 days
    e.get("/events?from=5&to=6").await; // before the year 2000
    e.send("GET", "/quests", None, Some("not-the-token-not-the-token-not-the-token")).await;
    e.get("/quests/999").await;
    e.get("/quests").await;
    let log = e.log.lock().unwrap().clone();
    assert_eq!(log.len(), 5);
    assert!(log[0].starts_with("GET /events 422 ") && log[0].ends_with(" invalid"), "{}", log[0]);
    assert!(log[1].ends_with(" invalid"), "{}", log[1]);
    assert!(log[2].ends_with(" unauthorized"), "{}", log[2]);
    assert!(log[3].ends_with(" not_found"), "{}", log[3]);
    assert!(log[4].ends_with("ms"), "a success says nothing more: {}", log[4]);
    for line in &log {
        assert!(!line.contains("1700000000") && !line.contains("from="), "the query is not logged: {line}");
    }
}
