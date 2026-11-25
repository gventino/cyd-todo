//! One-time links, sessions, CSRF: how a phone gets in, and what keeps everybody else out.

mod common;

use std::time::{Duration, Instant};

use common::*;
use poem::http::StatusCode;

#[tokio::test]
async fn a_link_is_created_with_the_device_token_and_has_the_documented_shape() {
    let e = env().await;
    let r = e.post("/link", r#"{"kind":"quest"}"#).await;
    assert_eq!(r.status, StatusCode::CREATED);
    let v = r.json();
    let url = v["url"].as_str().unwrap();
    assert!(url.starts_with(&format!("{PUBLIC_URL}/m/")), "{url}");
    assert_eq!(url.len(), PUBLIC_URL.len() + 3 + 26);
    assert!(url.len() <= 61);
    assert_eq!(v["expires_in"], 600);
    // the device token is needed
    assert_eq!(e.send("POST", "/link", Some(r#"{"kind":"quest"}"#), None).await.status, StatusCode::UNAUTHORIZED);
    // bad bodies
    for bad in ["", "{}", r#"{"kind":"other"}"#, r#"{"kind":"quest","x":1}"#, r#"{"kind":5}"#] {
        assert_eq!(e.post("/link", bad).await.status, StatusCode::BAD_REQUEST, "{bad:?}");
    }
    // a link does not move the revision of the data
    assert_eq!(e.db.rev().await.unwrap(), 0);
}

#[tokio::test]
async fn the_longest_public_url_still_gives_a_link_that_fits_a_qr_code() {
    let e = env().await;
    // PUBLIC_URL is capped at 32 bytes by the configuration; the app refuses anything that would not fit
    assert!(PUBLIC_URL.len() <= 32);
    let (_, url) = e.new_link("home").await;
    assert!(url.len() <= 61, "{}", url.len());
}

#[tokio::test]
async fn a_link_works_once_and_lands_on_the_page_of_its_kind() {
    let e = env().await;
    for (kind, to) in [("quest", "/app/quests/new"), ("event", "/app/events/new"), ("home", "/app")] {
        let (code, _) = e.new_link(kind).await;
        let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
        assert_eq!(r.status, StatusCode::SEE_OTHER, "{kind}");
        assert_eq!(r.headers.get("location").unwrap(), to);
        let again = e.raw("GET", &format!("/m/{code}"), &[], None).await;
        assert_eq!(again.status, StatusCode::NOT_FOUND, "{kind} used twice");
    }
}

#[tokio::test]
async fn every_way_a_link_can_fail_looks_the_same() {
    let e = env().await;
    let (used, _) = e.new_link("home").await;
    e.raw("GET", &format!("/m/{used}"), &[], None).await;
    let (stale, _) = e.new_link("home").await;
    e.set_now(T0 + 601);
    let cases = [
        used,
        stale,
        "00000000000000000000000000".to_string(), // well formed, unknown
        "short".to_string(),
        "0000000000000000000000000I".to_string(),
        "7ZZZZZZZZZZZZZZZZZZZZZZZZZ".to_string(),
        "%2e%2e".to_string(),
        "A".repeat(500),
    ];
    let mut bodies = Vec::new();
    for c in &cases {
        let r = e.raw("GET", &format!("/m/{c}"), &[], None).await;
        assert_eq!(r.status, StatusCode::NOT_FOUND, "{c}");
        assert!(r.headers.get("set-cookie").is_none(), "{c}");
        bodies.push(r.bytes);
    }
    assert!(bodies.windows(2).all(|w| w[0] == w[1]), "the pages differ: a code could be told from another");
    assert!(!String::from_utf8_lossy(&bodies[0]).contains("expired"), "the page must not say which failure it was");
}

#[tokio::test]
async fn a_link_that_expires_exactly_at_ten_minutes() {
    let e = env().await;
    let (code, _) = e.new_link("home").await;
    e.set_now(T0 + 599);
    assert_eq!(e.raw("GET", &format!("/m/{code}"), &[], None).await.status, StatusCode::SEE_OTHER);
    let (code, _) = e.new_link("home").await;
    e.set_now(T0 + 599 + 600);
    assert_eq!(e.raw("GET", &format!("/m/{code}"), &[], None).await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn a_wrong_code_costs_the_same_delay_as_a_wrong_token() {
    let e = env_with_delay(Duration::from_millis(200)).await;
    let t = Instant::now();
    e.raw("GET", "/m/00000000000000000000000000", &[], None).await;
    assert!(t.elapsed() >= Duration::from_millis(200), "{:?}", t.elapsed());
    let t = Instant::now();
    e.raw("GET", "/m/short", &[], None).await;
    assert!(t.elapsed() >= Duration::from_millis(200), "{:?}", t.elapsed());
}

#[tokio::test]
async fn at_most_six_links_are_alive_and_a_slot_frees_when_one_is_used_or_expires() {
    let e = env().await;
    let mut codes = Vec::new();
    for _ in 0..6 {
        codes.push(e.new_link("quest").await.0);
    }
    let r = e.post("/link", r#"{"kind":"quest"}"#).await;
    assert_eq!(r.status, StatusCode::TOO_MANY_REQUESTS);
    assert_eq!(r.json()["error"], "too_many");
    e.raw("GET", &format!("/m/{}", codes[0]), &[], None).await; // using one frees its place
    assert_eq!(e.post("/link", r#"{"kind":"quest"}"#).await.status, StatusCode::CREATED);
    assert_eq!(e.post("/link", r#"{"kind":"quest"}"#).await.status, StatusCode::TOO_MANY_REQUESTS);
    e.set_now(T0 + 601); // the rest expire
    assert_eq!(e.post("/link", r#"{"kind":"quest"}"#).await.status, StatusCode::CREATED);
}

#[tokio::test]
async fn the_session_cookie_has_the_attributes_that_protect_it() {
    let e = env().await;
    let (code, _) = e.new_link("home").await;
    let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
    let set = r.headers.get("set-cookie").unwrap().to_str().unwrap();
    assert!(set.starts_with("sid="), "{set}");
    let sid = set.split(';').next().unwrap().trim_start_matches("sid=");
    assert_eq!(sid.len(), 26);
    for attr in ["HttpOnly", "SameSite=Strict", "Path=/", "Max-Age=1800"] {
        assert!(set.contains(attr), "{attr} missing in {set}");
    }
    assert!(!set.contains("Secure"), "plain HTTP in development: Secure comes with https");
    assert_ne!(sid, code, "the session id is not the link code");
}

#[tokio::test]
async fn the_pages_need_a_live_session() {
    let e = env().await;
    // no cookie, a made-up cookie, a malformed cookie
    for headers in [
        vec![],
        vec![("Cookie", "sid=00000000000000000000000000")],
        vec![("Cookie", "sid=nope")],
        vec![("Cookie", "other=1")],
    ] {
        let r = e.raw("GET", "/app", &headers, None).await;
        assert_eq!(r.status, StatusCode::UNAUTHORIZED);
        assert!(r.text().contains("expired"));
    }
    let v = e.visit("home").await;
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::OK);
    e.set_now(T0 + 1799);
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::OK);
    e.set_now(T0 + 1800);
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::UNAUTHORIZED, "30 minutes, not sliding");
    // nothing under /app is open without a session, whatever the path
    for p in ["/app/anything", "/app/quests", "/app/logout"] {
        assert_eq!(e.raw("GET", p, &[], None).await.status, StatusCode::UNAUTHORIZED, "{p}");
    }
}

#[tokio::test]
async fn the_device_token_is_not_a_session_and_a_session_is_not_a_device_token() {
    let e = env().await;
    let v = e.visit("home").await;
    // a phone session on the device API
    let r = e.raw("GET", "/quests", &[("Cookie", &v.cookie)], None).await;
    assert_eq!(r.status, StatusCode::UNAUTHORIZED);
    // the Bearer token on the phone side
    let r = e.raw("GET", "/app", &[("Authorization", &format!("Bearer {TOKEN}"))], None).await;
    assert_eq!(r.status, StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn a_post_needs_the_csrf_token_of_the_session_and_an_origin_that_is_ours() {
    let e = env().await;
    let v = e.visit("home").await;
    let ck = [("Cookie", v.cookie.as_str())];
    let good = format!("csrf={}", v.csrf);
    // no token, a wrong one, one of the right length, another field
    for body in
        ["", "x=1", "csrf=", "csrf=wrong", "csrf=00000000000000000000000000", &format!("csrf={}&extra=1", v.csrf)]
    {
        let r = e.raw("POST", "/app/logout", &ck, Some(body)).await;
        assert!(r.status == StatusCode::FORBIDDEN || r.status == StatusCode::BAD_REQUEST, "{body:?}: {}", r.status);
    }
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::OK, "no refused post ended the session");
    // an Origin that is not ours, with the right token
    for origin in ["http://evil.example", "null", "http://192.168.1.50:8081", "http://192.168.1.50:8080/"] {
        let r = e.raw("POST", "/app/logout", &[("Cookie", &v.cookie), ("Origin", origin)], Some(&good)).await;
        assert_eq!(r.status, StatusCode::FORBIDDEN, "{origin}");
    }
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::OK);
    // our own Origin, or none (older browsers), with the token: fine
    let r = e.raw("POST", "/app/logout", &[("Cookie", &v.cookie), ("Origin", PUBLIC_URL)], Some(&good)).await;
    assert_eq!(r.status, StatusCode::OK);
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::UNAUTHORIZED, "the session is gone");
    let r = e.raw("POST", "/app/logout", &ck, Some(&good)).await;
    assert_eq!(r.status, StatusCode::UNAUTHORIZED, "and it cannot be used again");
}

#[tokio::test]
async fn the_csrf_token_belongs_to_its_session() {
    let e = env().await;
    let a = e.visit("home").await;
    let b = e.visit("home").await;
    assert_ne!(a.csrf, b.csrf);
    let r = e.raw("POST", "/app/logout", &[("Cookie", &a.cookie)], Some(&format!("csrf={}", b.csrf))).await;
    assert_eq!(r.status, StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn a_form_over_4096_bytes_is_refused() {
    let e = env().await;
    let v = e.visit("home").await;
    let big = format!("csrf={}&pad={}", v.csrf, "a".repeat(4100));
    let r = e.raw("POST", "/app/logout", &[("Cookie", &v.cookie)], Some(&big)).await;
    assert_eq!(r.status, StatusCode::PAYLOAD_TOO_LARGE);
}

#[tokio::test]
async fn a_get_never_changes_anything() {
    let e = env().await;
    let v = e.visit("home").await;
    let before = e.db.rev().await.unwrap();
    e.page(&v, "/app").await;
    e.page(&v, "/app/logout").await; // a GET on a POST route
    assert_eq!(e.page(&v, "/app").await.status, StatusCode::OK, "still in");
    assert_eq!(e.db.rev().await.unwrap(), before);
}

#[tokio::test]
async fn every_phone_side_answer_is_hardened_and_the_device_side_does_not_get_a_policy() {
    let e = env().await;
    let v = e.visit("home").await;
    let (code, _) = e.new_link("home").await;
    let csp = cyd_todo_server::html::csp();
    let answers = [
        e.page(&v, "/app").await,
        e.raw("GET", "/app", &[], None).await,                           // 401 page
        e.page(&v, "/app/nope").await,                                   // 404 inside the session
        e.raw("GET", "/m/short", &[], None).await,                       // 404 page
        e.raw("GET", &format!("/m/{code}"), &[], None).await,            // the 303
        e.raw("POST", "/app", &[("Cookie", &v.cookie)], Some("")).await, // 405
    ];
    for r in answers {
        let h = &r.headers;
        assert_eq!(h.get("content-security-policy").unwrap().to_str().unwrap(), csp, "{}", r.status);
        assert_eq!(h.get("x-content-type-options").unwrap(), "nosniff");
        assert_eq!(h.get("referrer-policy").unwrap(), "no-referrer");
        assert_eq!(h.get("cache-control").unwrap(), "no-store");
        if r.status != StatusCode::SEE_OTHER {
            assert!(h.get("content-type").unwrap().to_str().unwrap().starts_with("text/html"));
        }
    }
    assert!(e.get("/quests").await.headers.get("content-security-policy").is_none());
    // a page of the phone side is never JSON, an answer of the device side never HTML
    assert!(e.raw("GET", "/app/zzz", &[("Cookie", &v.cookie)], None).await.text().contains("<h1>"));
    assert!(e.get("/quests/zzz").await.text().starts_with('{'));
}

#[tokio::test]
async fn the_access_log_never_has_a_link_code_a_cookie_or_a_form() {
    let e = env().await;
    let (code, _) = e.new_link("home").await;
    let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
    let sid = r.headers.get("set-cookie").unwrap().to_str().unwrap().split(';').next().unwrap().to_string();
    let v = Visit {
        cookie: sid.clone(),
        csrf: field_value(&e.page(&Visit { cookie: sid.clone(), csrf: String::new() }, "/app").await.text(), "csrf")
            .unwrap(),
    };
    e.submit(&v, "/app/logout", &[]).await;
    e.raw("GET", "/m/00000000000000000000000000", &[], None).await;
    let log = e.log.lock().unwrap().join("\n");
    assert!(!log.contains(&code), "{log}");
    assert!(!log.contains(sid.trim_start_matches("sid=")), "{log}");
    assert!(!log.contains(&v.csrf), "{log}");
    assert!(!log.contains("00000000000000000000000000"), "{log}");
    assert!(log.contains("GET /m/:code 303"), "{log}");
}

#[tokio::test]
async fn the_database_holds_only_hashes() {
    let e = env().await;
    let (code, _) = e.new_link("home").await;
    let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
    let sid = r
        .headers
        .get("set-cookie")
        .unwrap()
        .to_str()
        .unwrap()
        .split(';')
        .next()
        .unwrap()
        .trim_start_matches("sid=")
        .to_string();
    let dump = e.db.dump_secrets_for_tests().await;
    assert!(!dump.contains(&code) && !dump.contains(&sid), "a code or a session id is stored in the clear");
}
