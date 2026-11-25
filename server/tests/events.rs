//! The event calls and `GET /changes`, and the biggest bodies the server can be made to build.

mod common;

use common::*;
use poem::http::StatusCode;
use serde_json::json;

const DAY: i64 = 86_400;

fn ev(title: &str, start: i64, end: i64) -> String {
    format!(r#"{{"title":"{title}","start":{start},"end":{end}}}"#)
}

#[tokio::test]
async fn a_new_event_has_the_documented_shape() {
    let e = env().await;
    let r = e.post("/events", &ev("Dentist", T0, T0 + 3600)).await;
    assert_eq!(r.status, StatusCode::CREATED);
    assert_eq!(r.json(), json!({"id":1,"title":"Dentist","description":"","start":T0,"end":T0+3600,"updated_at":T0}));
    let d = e
        .post(
            "/events",
            &format!(r#"{{"title":"x","description":"bring the\nX-rays","start":{T0},"end":{}}}"#, T0 + 60),
        )
        .await;
    assert_eq!(d.json()["description"], "bring the\nX-rays");
}

#[tokio::test]
async fn event_rules_are_422s_and_shape_mistakes_are_400s() {
    let e = env().await;
    for (title, start, end) in [
        ("ok", T0, T0),
        ("ok", T0, T0 - 1),
        ("", T0, T0 + 1),
        ("ok", 0, 100),
        ("ok", T0, T0 + 31 * DAY + 1),
        ("ok", 4_102_444_800, 4_102_444_900),
    ] {
        let r = e.post("/events", &ev(title, start, end)).await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{title} {start} {end}");
    }
    assert_eq!(e.post("/events", &ev("ok", T0, T0 + 31 * DAY)).await.status, StatusCode::CREATED);
    for body in [
        r#"{"title":"x"}"#,
        r#"{"title":"x","start":"a","end":2}"#,
        r#"{"title":"x","start":1.5,"end":2}"#,
        &format!(r#"{{"title":"x","start":{T0},"end":{},"extra":1}}"#, T0 + 1),
    ] {
        assert_eq!(e.post("/events", body).await.status, StatusCode::BAD_REQUEST, "{body}");
    }
}

#[tokio::test]
async fn patching_an_event_checks_the_pair_after_the_merge() {
    let e = env().await;
    e.post("/events", &ev("a", T0, T0 + 3600)).await;
    let p = e.patch("/events/1", r#"{"title":"b"}"#).await;
    assert_eq!((p.json()["title"].clone(), p.json()["start"].clone()), (json!("b"), json!(T0)));
    assert_eq!(
        e.patch("/events/1", &format!(r#"{{"end":{}}}"#, T0 - 5)).await.status,
        StatusCode::UNPROCESSABLE_ENTITY
    );
    assert_eq!(
        e.patch("/events/1", &format!(r#"{{"start":{}}}"#, T0 + 3600)).await.status,
        StatusCode::UNPROCESSABLE_ENTITY
    );
    let both = e.patch("/events/1", &format!(r#"{{"start":{},"end":{}}}"#, T0 + 7200, T0 + 9000)).await;
    assert_eq!(both.status, StatusCode::OK);
    assert_eq!(e.patch("/events/1", "{}").await.status, StatusCode::BAD_REQUEST);
    for member in ["title", "description", "start", "end"] {
        let r = e.patch("/events/1", &format!(r#"{{"title":"fine","{member}":null}}"#)).await;
        assert_eq!(r.status, StatusCode::BAD_REQUEST, "{member}");
    }
    assert_eq!(e.get("/events/1").await.json()["title"], "b");
    assert_eq!(e.delete("/events/1").await.status, StatusCode::NO_CONTENT);
    assert_eq!(e.get("/events/1").await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.delete("/events/1").await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.get("/events/zero").await.status, StatusCode::NOT_FOUND);
}

fn window(from: i64, to: i64) -> String {
    format!("/events?from={from}&to={to}")
}

#[tokio::test]
async fn the_list_returns_what_overlaps_in_order() {
    let e = env().await;
    e.post("/events", &ev("ends at from", T0 - 100, T0)).await;
    e.post("/events", &ev("late", T0 + 500, T0 + 600)).await;
    e.post("/events", &ev("starts at to", T0 + 1000, T0 + 1100)).await;
    e.post("/events", &ev("early", T0 + 10, T0 + 20)).await;
    e.post("/events", &ev("long", T0 - 5000, T0 + 5000)).await;
    let v = e.get(&window(T0, T0 + 1000)).await.json();
    let titles: Vec<&str> = v["events"].as_array().unwrap().iter().map(|x| x["title"].as_str().unwrap()).collect();
    assert_eq!(titles, ["long", "early", "late"]);
    assert_eq!(v["rev"], 5);
    assert_eq!(v["truncated"], false);
    let empty = e.get(&window(T0 + 90 * DAY, T0 + 91 * DAY)).await.json();
    assert_eq!(empty["events"].as_array().unwrap().len(), 0);
}

#[tokio::test]
async fn the_window_is_required_forward_and_at_most_62_days() {
    let e = env().await;
    let max = 62 * DAY;
    assert_eq!(e.get(&window(T0, T0 + max)).await.status, StatusCode::OK);
    assert_eq!(e.get(&window(T0, T0 + max + 1)).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get(&window(T0, T0)).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get(&window(T0 + 5, T0)).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get(&window(0, 100)).await.status, StatusCode::UNPROCESSABLE_ENTITY);
    for bad in [
        "/events",
        "/events?from=1",
        "/events?to=1",
        "/events?from=a&to=b",
        "/events?from=1.5&to=2",
        "/events?from=1&to=2&extra=3",
        "/events?from=-1&to=2",
    ] {
        let r = e.get(bad).await;
        assert!(
            r.status == StatusCode::BAD_REQUEST || r.status == StatusCode::UNPROCESSABLE_ENTITY,
            "{bad}: {}",
            r.status
        );
        assert!(r.json()["error"].is_string());
    }
}

#[tokio::test]
async fn more_than_48_events_are_cut_earliest_first_and_say_so() {
    let e = env().await;
    for i in 0..60 {
        e.post("/events", &ev(&format!("e{i:02}"), T0 + i * 60, T0 + i * 60 + 30)).await;
    }
    let v = e.get(&window(T0, T0 + DAY)).await.json();
    let list = v["events"].as_array().unwrap();
    assert_eq!(list.len(), 48);
    assert_eq!(list[0]["title"], "e00");
    assert_eq!(list[47]["title"], "e47");
    assert_eq!(v["truncated"], true);
}

#[tokio::test]
async fn changes_follows_the_revision() {
    let e = env().await;
    assert_eq!(e.get("/changes").await.json(), json!({"rev":0,"changed":true}), "no since: changed");
    assert_eq!(e.get("/changes?since=0").await.json(), json!({"rev":0,"changed":false}));
    e.post("/quests", r#"{"title":"a"}"#).await;
    assert_eq!(e.get("/changes?since=0").await.json(), json!({"rev":1,"changed":true}));
    assert_eq!(e.get("/changes?since=1").await.json(), json!({"rev":1,"changed":false}));
    e.post("/events", &ev("e", T0, T0 + 1)).await;
    e.delete("/quests/1").await;
    assert_eq!(e.get("/changes?since=1").await.json(), json!({"rev":3,"changed":true}));
    assert_eq!(e.get("/changes?since=99").await.json(), json!({"rev":3,"changed":true}), "a rev from the future");
    assert_eq!(e.get("/changes?since=-4").await.json(), json!({"rev":3,"changed":true}));
    assert_eq!(e.get("/changes?since=abc").await.status, StatusCode::BAD_REQUEST);
    assert_eq!(e.get("/changes?other=1").await.status, StatusCode::BAD_REQUEST);
    // a failed write does not change the answer
    e.post("/quests", r#"{"title":""}"#).await;
    assert_eq!(e.get("/changes?since=3").await.json()["changed"], false);
    // the call is tiny
    assert!(e.get("/changes").await.bytes.len() < 40);
}

// ---- the worst bodies ----

#[tokio::test]
async fn the_worst_quest_list_still_fits_what_the_device_reads() {
    let e = env().await;
    let title = "\"".repeat(64); // every character is escaped in JSON
    let notes = "\n".repeat(256);
    for _ in 0..48 {
        let body = json!({"title": title, "notes": notes, "difficulty": 5, "due": "2026-12-31"}).to_string();
        assert_eq!(e.post("/quests", &body).await.status, StatusCode::CREATED);
    }
    let r = e.get("/quests").await;
    assert_eq!(r.status, StatusCode::OK);
    assert!(r.bytes.len() <= 14 * 1024, "{} bytes", r.bytes.len());
    let v = r.json();
    let n = v["quests"].as_array().unwrap().len();
    assert!(n > 10 && n < 48, "{n}");
    assert_eq!(v["truncated"], true);
    assert_eq!(v["open"], 48, "the totals are all-time, not the page");
    assert_eq!(v["quests"][0]["title"], title.as_str());
}

#[tokio::test]
async fn the_worst_event_list_still_fits_what_the_device_reads() {
    let e = env().await;
    let title = "\\".repeat(64);
    let desc = "\"".repeat(256);
    for i in 0..48 {
        let body = json!({"title": title, "description": desc, "start": T0 + i, "end": T0 + 1000 + i}).to_string();
        assert_eq!(e.post("/events", &body).await.status, StatusCode::CREATED);
    }
    let r = e.get(&window(T0, T0 + 3600)).await;
    assert!(r.bytes.len() <= 14 * 1024, "{} bytes", r.bytes.len());
    let v = r.json();
    let n = v["events"].as_array().unwrap().len();
    assert!(n > 10 && n < 48, "{n}");
    assert_eq!(v["truncated"], true);
}

#[tokio::test]
async fn a_single_item_is_always_far_under_the_limit() {
    let e = env().await;
    let body =
        json!({"title": "\"".repeat(64), "notes": "\n".repeat(256), "difficulty": 5, "due": "2026-12-31"}).to_string();
    let r = e.post("/quests", &body).await;
    assert!(r.bytes.len() < 1000, "{} bytes", r.bytes.len());
}
