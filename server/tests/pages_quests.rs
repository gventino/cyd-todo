//! The quest pages, flow by flow, through a real session.

mod common;

use common::*;
use poem::http::StatusCode;

async fn quest_count(e: &Env) -> usize {
    e.get("/quests").await.json()["quests"].as_array().unwrap().len()
}

#[tokio::test]
async fn the_link_of_a_quest_lands_on_the_new_quest_form() {
    let e = env().await;
    let (code, _) = e.new_link("quest").await;
    let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
    let cookie = r.headers.get("set-cookie").unwrap().to_str().unwrap().split(';').next().unwrap().to_string();
    let page = e.raw("GET", r.headers.get("location").unwrap().to_str().unwrap(), &[("Cookie", &cookie)], None).await;
    assert_eq!(page.status, StatusCode::OK);
    let t = page.text();
    for needle in [
        "<h1>New quest</h1>",
        "name=\"title\"",
        "name=\"notes\"",
        "name=\"difficulty\"",
        "type=\"date\"",
        "name=\"csrf\"",
        "action=\"/app/quests\"",
    ] {
        assert!(t.contains(needle), "{needle} missing");
    }
}

#[tokio::test]
async fn creating_a_quest_writes_it_moves_the_revision_and_shows_it() {
    let e = env().await;
    let v = e.visit("home").await;
    let rev0 = e.db.rev().await.unwrap();
    let r = e
        .submit(
            &v,
            "/app/quests",
            &[
                ("title", "Slay the inbox"),
                ("notes", "Zero unread.\r\nCoffee allowed."),
                ("difficulty", "3"),
                ("due", "2026-12-24"),
            ],
        )
        .await;
    assert_eq!(r.status, StatusCode::SEE_OTHER, "{}", r.text());
    assert_eq!(r.headers.get("location").unwrap(), "/app/quests");
    assert_eq!(e.db.rev().await.unwrap(), rev0 + 1, "the board would see this");
    let q = e.get("/quests/1").await.json();
    assert_eq!(
        (q["title"].clone(), q["difficulty"].clone(), q["due"].clone()),
        ("Slay the inbox".into(), 3.into(), "2026-12-24".into())
    );
    assert_eq!(q["notes"], "Zero unread.\nCoffee allowed.", "line ends are normalised");
    let list = e.page(&v, "/app/quests").await.text();
    assert!(
        list.contains("Slay the inbox")
            && list.contains("★★★")
            && list.contains("Due 2026-12-24")
            && list.contains("Coffee allowed.")
    );
    let home = e.page(&v, "/app").await.text();
    assert!(home.contains("Slay the inbox"));
}

#[tokio::test]
async fn a_title_is_trimmed_and_the_optional_fields_may_be_empty() {
    let e = env().await;
    let v = e.visit("home").await;
    let r =
        e.submit(&v, "/app/quests", &[("title", "  padded  "), ("notes", ""), ("difficulty", "1"), ("due", "")]).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
    let q = e.get("/quests/1").await.json();
    assert_eq!((q["title"].clone(), q["due"].clone()), ("padded".into(), serde_json::Value::Null));
    // difficulty and the rest default when absent
    let r = e.raw("POST", "/app/quests", &[("Cookie", &v.cookie)], Some(&format!("csrf={}&title=bare", v.csrf))).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
}

#[tokio::test]
async fn a_rejected_form_is_shown_again_with_what_was_typed_and_says_why() {
    let e = env().await;
    let v = e.visit("home").await;
    let before = e.db.rev().await.unwrap();
    let long = "n".repeat(257);
    let cases = [
        (vec![("title", ""), ("difficulty", "1")], "title must be 1 to 64 bytes"),
        (vec![("title", "ok"), ("difficulty", "9")], "difficulty must be 1 to 5"),
        (vec![("title", "ok"), ("difficulty", "x")], "difficulty must be 1 to 5"),
        (vec![("title", "ok"), ("difficulty", "2"), ("due", "2027-02-29")], "due must be a date"),
        (vec![("title", "ok"), ("difficulty", "2"), ("notes", long.as_str())], "notes must be at most 256 bytes"),
        (vec![("title", "bell\u{7}"), ("difficulty", "2")], "title must be 1 to 64 bytes"),
    ];
    for (fields, why) in cases {
        let r = e.submit(&v, "/app/quests", &fields).await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{fields:?}");
        let t = r.text();
        assert!(t.contains(why), "{why} not in the page");
        assert!(t.contains("class=\"error\""));
        if let Some((_, typed)) = fields.iter().find(|(k, val)| *k == "title" && *val == "ok") {
            assert!(t.contains(&format!("value=\"{typed}\"")), "the typed title is back in the form");
        }
    }
    assert_eq!(e.db.rev().await.unwrap(), before, "nothing was written");
    assert_eq!(quest_count(&e).await, 0);
}

#[tokio::test]
async fn editing_a_quest_shows_its_values_and_saves_them() {
    let e = env().await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/quests", &[("title", "Old"), ("notes", "n"), ("difficulty", "4"), ("due", "2026-12-24")]).await;
    let form = e.page(&v, "/app/quests/1/edit").await;
    assert_eq!(form.status, StatusCode::OK);
    let t = form.text();
    assert!(
        t.contains("value=\"Old\"")
            && t.contains(">n</textarea>")
            && t.contains("value=\"4\" selected=\"selected\"")
            && t.contains("value=\"2026-12-24\"")
    );
    assert!(t.contains("action=\"/app/quests/1\""));
    e.set_now(T0 + 60);
    let r = e.submit(&v, "/app/quests/1", &[("title", "New"), ("notes", ""), ("difficulty", "2"), ("due", "")]).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
    let q = e.get("/quests/1").await.json();
    assert_eq!(
        (q["title"].clone(), q["notes"].clone(), q["difficulty"].clone(), q["due"].clone(), q["updated_at"].clone()),
        ("New".into(), "".into(), 2.into(), serde_json::Value::Null, (T0 + 60).into())
    );
    // a bad edit shows the form again and changes nothing
    let r = e.submit(&v, "/app/quests/1", &[("title", ""), ("difficulty", "2")]).await;
    assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get("/quests/1").await.json()["title"], "New");
    // a quest that is not there
    assert_eq!(e.page(&v, "/app/quests/9/edit").await.status, StatusCode::NOT_FOUND);
    assert_eq!(
        e.submit(&v, "/app/quests/9", &[("title", "x"), ("difficulty", "1")]).await.status,
        StatusCode::NOT_FOUND
    );
    assert_eq!(e.page(&v, "/app/quests/abc/edit").await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn done_and_reopen_flip_the_state_and_the_xp() {
    let e = env().await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/quests", &[("title", "Quest"), ("difficulty", "5")]).await;
    e.set_now(T0 + 100);
    let r = e.submit(&v, "/app/quests/1/toggle", &[]).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
    let q = e.get("/quests/1").await.json();
    assert_eq!((q["state"].clone(), q["done_at"].clone()), ("done".into(), (T0 + 100).into()));
    assert_eq!(e.get("/quests").await.json()["xp"], 50);
    let list = e.page(&v, "/app/quests").await.text();
    assert!(list.contains("card done") && list.contains("Reopen"));
    e.submit(&v, "/app/quests/1/toggle", &[]).await;
    assert_eq!(e.get("/quests/1").await.json()["state"], "open");
    assert_eq!(e.get("/quests").await.json()["xp"], 0);
    assert_eq!(e.submit(&v, "/app/quests/9/toggle", &[]).await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn delete_asks_twice_and_a_get_never_deletes() {
    let e = env().await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/quests", &[("title", "Doomed"), ("difficulty", "1")]).await;
    let ask = e.page(&v, "/app/quests/1/delete").await;
    assert_eq!(ask.status, StatusCode::OK);
    let t = ask.text();
    assert!(
        t.contains("Delete this quest?")
            && t.contains("Doomed")
            && t.contains("Yes, delete it")
            && t.contains("No, keep it")
    );
    assert_eq!(quest_count(&e).await, 1, "asking is not deleting");
    let rev = e.db.rev().await.unwrap();
    let r = e.submit(&v, "/app/quests/1/delete", &[]).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
    assert_eq!(quest_count(&e).await, 0);
    assert_eq!(e.db.rev().await.unwrap(), rev + 1);
    assert_eq!(e.submit(&v, "/app/quests/1/delete", &[]).await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.page(&v, "/app/quests/1/delete").await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn every_post_of_the_quest_pages_needs_the_csrf_token() {
    let e = env().await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/quests", &[("title", "Kept"), ("difficulty", "1")]).await;
    let rev = e.db.rev().await.unwrap();
    for path in ["/app/quests", "/app/quests/1", "/app/quests/1/toggle", "/app/quests/1/delete"] {
        for body in ["title=x&difficulty=1", "csrf=wrong&title=x&difficulty=1", ""] {
            let r = e.raw("POST", path, &[("Cookie", &v.cookie)], Some(body)).await;
            assert!(
                r.status == StatusCode::FORBIDDEN || r.status == StatusCode::BAD_REQUEST,
                "{path} {body:?} -> {}",
                r.status
            );
        }
        let r = e
            .raw(
                "POST",
                path,
                &[("Cookie", &v.cookie), ("Origin", "http://evil.example")],
                Some(&format!("csrf={}&title=x&difficulty=1", v.csrf)),
            )
            .await;
        assert_eq!(r.status, StatusCode::FORBIDDEN, "{path} with a foreign Origin");
        let r = e.raw("POST", path, &[], Some(&format!("csrf={}&title=x", v.csrf))).await;
        assert_eq!(r.status, StatusCode::UNAUTHORIZED, "{path} without a session");
    }
    assert_eq!(e.db.rev().await.unwrap(), rev, "nothing got through");
    assert_eq!(e.get("/quests/1").await.json()["title"], "Kept");
}

#[tokio::test]
async fn unknown_form_fields_and_big_forms_are_refused() {
    let e = env().await;
    let v = e.visit("home").await;
    let r = e.submit(&v, "/app/quests", &[("title", "x"), ("difficulty", "1"), ("admin", "1")]).await;
    assert_eq!(r.status, StatusCode::BAD_REQUEST);
    let r = e.submit(&v, "/app/quests", &[("title", "x"), ("difficulty", "1"), ("notes", &"n".repeat(4100))]).await;
    assert_eq!(r.status, StatusCode::PAYLOAD_TOO_LARGE);
    assert_eq!(quest_count(&e).await, 0);
}

#[tokio::test]
async fn the_list_stops_at_48_and_says_so() {
    let e = env().await;
    let v = e.visit("home").await;
    for i in 0..50 {
        e.submit(&v, "/app/quests", &[("title", &format!("q{i:02}")), ("difficulty", "1")]).await;
    }
    let t = e.page(&v, "/app/quests").await.text();
    assert_eq!(t.matches("class=\"card-wrap\"").count(), 48);
    assert!(t.contains("More quests exist than are shown here."));
}
