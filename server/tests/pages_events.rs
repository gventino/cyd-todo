//! The calendar pages: a month, new, edit, delete, and the zone the times are read in.

mod common;

use common::*;
use poem::http::StatusCode;

// 2026-12-24 23:30 in UTC-3 is 2026-12-25 02:30 UTC
const LOCAL_START: &str = "2026-12-24T23:30";
const LOCAL_END: &str = "2026-12-25T00:45";

fn utc(y: i64, m: i64, d: i64, hh: i64, mm: i64) -> i64 {
    cyd_todo_server::timeutil::days_from_civil(y, m, d) * 86_400 + hh * 3600 + mm * 60
}

#[tokio::test]
async fn the_link_of_an_event_lands_on_the_new_event_form_with_a_start_in_the_future() {
    let e = env_tz(-180).await;
    let (code, _) = e.new_link("event").await;
    let r = e.raw("GET", &format!("/m/{code}"), &[], None).await;
    let cookie = r.headers.get("set-cookie").unwrap().to_str().unwrap().split(';').next().unwrap().to_string();
    let page = e.raw("GET", r.headers.get("location").unwrap().to_str().unwrap(), &[("Cookie", &cookie)], None).await;
    assert_eq!(page.status, StatusCode::OK);
    let t = page.text();
    assert!(t.contains("<h1>New event</h1>") && t.contains("type=\"datetime-local\"") && t.contains("UTC-03:00"));
    // T0 is 2026-12-02 17:00 UTC = 14:00 local: the next full hour is 15:00, for an hour
    assert_eq!(field_value(&t, "start").unwrap(), "2026-12-02T15:00");
    assert_eq!(field_value(&t, "end").unwrap(), "2026-12-02T16:00");
}

#[tokio::test]
async fn an_event_is_read_in_the_zone_of_the_server_and_stored_in_utc() {
    let e = env_tz(-180).await;
    let v = e.visit("home").await;
    let rev = e.db.rev().await.unwrap();
    let r = e
        .submit(
            &v,
            "/app/events",
            &[("title", "Christmas eve"), ("description", "Bring\r\nwine"), ("start", LOCAL_START), ("end", LOCAL_END)],
        )
        .await;
    assert_eq!(r.status, StatusCode::SEE_OTHER, "{}", r.text());
    assert_eq!(r.headers.get("location").unwrap(), "/app/events?month=2026-12");
    assert_eq!(e.db.rev().await.unwrap(), rev + 1);
    let ev = e.get("/events/1").await.json();
    assert_eq!(ev["start"], utc(2026, 12, 25, 2, 30), "23:30 at UTC-3 is 02:30 UTC the next day");
    assert_eq!(ev["end"], utc(2026, 12, 25, 3, 45));
    assert_eq!(ev["description"], "Bring\nwine");
    let list = e.page(&v, "/app/events?month=2026-12").await.text();
    assert!(
        list.contains("Christmas eve")
            && list.contains("2026-12-24 23:30 - 2026-12-25 00:45")
            && list.contains("UTC-03:00")
    );
}

#[tokio::test]
async fn an_event_that_shares_its_day_shows_only_the_end_time() {
    let e = env_tz(0).await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/events", &[("title", "Dentist"), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T10:45")])
        .await;
    let list = e.page(&v, "/app/events?month=2026-12").await.text();
    assert!(list.contains("2026-12-09 10:00 - 10:45"), "{list}");
}

#[tokio::test]
async fn a_month_shows_what_overlaps_it_and_moves_with_the_links() {
    let e = env_tz(0).await;
    let v = e.visit("home").await;
    for (t, s, en) in [
        ("Nov only", "2026-11-10T10:00", "2026-11-10T11:00"),
        ("Dec only", "2026-12-10T10:00", "2026-12-10T11:00"),
        ("Crosses", "2026-11-30T22:00", "2026-12-01T02:00"),
        ("Jan only", "2027-01-05T10:00", "2027-01-05T11:00"),
    ] {
        assert_eq!(
            e.submit(&v, "/app/events", &[("title", t), ("start", s), ("end", en)]).await.status,
            StatusCode::SEE_OTHER
        );
    }
    let dec = e.page(&v, "/app/events?month=2026-12").await.text();
    assert!(
        dec.contains("Dec only") && dec.contains("Crosses") && !dec.contains("Nov only") && !dec.contains("Jan only")
    );
    assert!(dec.contains("href=\"/app/events?month=2026-11\"") && dec.contains("href=\"/app/events?month=2027-01\""));
    let jan = e.page(&v, "/app/events?month=2027-01").await.text();
    assert!(
        jan.contains("Jan only")
            && jan.contains("href=\"/app/events?month=2026-12\"")
            && jan.contains("href=\"/app/events?month=2027-02\"")
    );
    // without a month: the month of now (T0 is in December 2026)
    assert!(e.page(&v, "/app/events").await.text().contains("Dec only"));
    assert!(e.page(&v, "/app/events?month=2026-03").await.text().contains("No events this month."));
    // a month that is not one
    for bad in [
        "/app/events?month=2026-13",
        "/app/events?month=abc",
        "/app/events?month=2026-1",
        "/app/events?other=1",
        "/app/events?month=2100-01",
    ] {
        assert_eq!(e.page(&v, bad).await.status, StatusCode::BAD_REQUEST, "{bad}");
    }
}

#[tokio::test]
async fn the_month_edges_follow_the_zone() {
    // 2026-12-01 01:00 UTC is still November 30th at 22:00 in UTC-3
    let e = env_tz(-180).await;
    let v = e.visit("home").await;
    e.submit(
        &v,
        "/app/events",
        &[("title", "Late November"), ("start", "2026-11-30T22:00"), ("end", "2026-11-30T23:00")],
    )
    .await;
    assert!(e.page(&v, "/app/events?month=2026-11").await.text().contains("Late November"));
    assert!(!e.page(&v, "/app/events?month=2026-12").await.text().contains("Late November"));
}

#[tokio::test]
async fn a_rejected_event_form_is_shown_again_with_what_was_typed() {
    let e = env_tz(0).await;
    let v = e.visit("home").await;
    let before = e.db.rev().await.unwrap();
    let cases = [
        (vec![("title", "ok"), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T10:00")], "end must be after start"),
        (vec![("title", "ok"), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T09:00")], "end must be after start"),
        (
            vec![("title", "ok"), ("start", "2026-12-09T10:00"), ("end", "2027-01-20T10:00")],
            "an event can last at most 31 days",
        ),
        (
            vec![("title", ""), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T11:00")],
            "title must be 1 to 64 bytes",
        ),
        (vec![("title", "ok"), ("start", "tomorrow"), ("end", "2026-12-09T11:00")], "start must be a date and a time"),
        (vec![("title", "ok"), ("start", "2026-12-09T10:00"), ("end", "")], "end must be a date and a time"),
        (
            vec![("title", "ok"), ("start", "2026-02-30T10:00"), ("end", "2026-03-01T10:00")],
            "start must be a date and a time",
        ),
        (
            vec![("title", "ok"), ("start", "1999-12-31T10:00"), ("end", "2000-01-01T10:00")],
            "a time must be epoch seconds",
        ),
    ];
    for (fields, why) in cases {
        let r = e.submit(&v, "/app/events", &fields).await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{fields:?}");
        let t = r.text();
        assert!(t.contains(why), "{why} not in the page: {fields:?}");
        let start = fields.iter().find(|(k, _)| *k == "start").unwrap().1;
        assert!(t.contains(&format!("value=\"{start}\"")), "the typed start is back in the form");
    }
    assert_eq!(e.db.rev().await.unwrap(), before);
}

#[tokio::test]
async fn editing_shows_the_local_times_and_saves_them() {
    let e = env_tz(-180).await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/events", &[("title", "Old"), ("description", "d"), ("start", LOCAL_START), ("end", LOCAL_END)])
        .await;
    let t = e.page(&v, "/app/events/1/edit").await.text();
    assert_eq!(field_value(&t, "start").unwrap(), LOCAL_START, "the form shows the local time");
    assert_eq!(field_value(&t, "end").unwrap(), LOCAL_END);
    assert!(t.contains("value=\"Old\"") && t.contains(">d</textarea>") && t.contains("action=\"/app/events/1\""));
    e.set_now(T0 + 60);
    let r = e
        .submit(
            &v,
            "/app/events/1",
            &[("title", "New"), ("description", ""), ("start", "2026-12-25T09:00"), ("end", "2026-12-25T10:00")],
        )
        .await;
    assert_eq!(r.status, StatusCode::SEE_OTHER);
    let ev = e.get("/events/1").await.json();
    assert_eq!(
        (ev["title"].clone(), ev["start"].clone(), ev["updated_at"].clone()),
        ("New".into(), utc(2026, 12, 25, 12, 0).into(), (T0 + 60).into())
    );
    // a bad edit changes nothing
    let r = e
        .submit(&v, "/app/events/1", &[("title", "x"), ("start", "2026-12-25T11:00"), ("end", "2026-12-25T10:00")])
        .await;
    assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY);
    assert_eq!(e.get("/events/1").await.json()["title"], "New");
    assert_eq!(e.page(&v, "/app/events/9/edit").await.status, StatusCode::NOT_FOUND);
    assert_eq!(
        e.submit(&v, "/app/events/9", &[("title", "x"), ("start", LOCAL_START), ("end", LOCAL_END)]).await.status,
        StatusCode::NOT_FOUND
    );
}

#[tokio::test]
async fn delete_asks_twice_and_every_post_needs_the_csrf_token() {
    let e = env_tz(0).await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/events", &[("title", "Doomed"), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T11:00")])
        .await;
    let ask = e.page(&v, "/app/events/1/delete").await.text();
    assert!(ask.contains("Delete this event?") && ask.contains("Doomed") && ask.contains("Yes, delete it"));
    assert_eq!(e.get("/events/1").await.status, StatusCode::OK, "asking is not deleting");
    let rev = e.db.rev().await.unwrap();
    for path in ["/app/events", "/app/events/1", "/app/events/1/delete"] {
        for body in ["title=x&start=2026-12-09T10:00&end=2026-12-09T11:00", "csrf=wrong&title=x", ""] {
            let r = e.raw("POST", path, &[("Cookie", &v.cookie)], Some(body)).await;
            assert!(r.status == StatusCode::FORBIDDEN || r.status == StatusCode::BAD_REQUEST, "{path} {body:?}");
        }
        let r = e
            .raw(
                "POST",
                path,
                &[("Cookie", &v.cookie), ("Origin", "http://evil.example")],
                Some(&format!("csrf={}&title=x", v.csrf)),
            )
            .await;
        assert_eq!(r.status, StatusCode::FORBIDDEN, "{path}");
    }
    assert_eq!(e.db.rev().await.unwrap(), rev);
    assert_eq!(e.submit(&v, "/app/events/1/delete", &[]).await.status, StatusCode::SEE_OTHER);
    assert_eq!(e.get("/events/1").await.status, StatusCode::NOT_FOUND);
    assert_eq!(e.submit(&v, "/app/events/1/delete", &[]).await.status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn a_month_with_more_than_48_events_says_so() {
    let e = env_tz(0).await;
    let v = e.visit("home").await;
    for i in 0..50 {
        let h = 1 + i % 20;
        let d = 1 + i / 20;
        e.submit(
            &v,
            "/app/events",
            &[
                ("title", &format!("e{i:02}")),
                ("start", &format!("2026-12-{d:02}T{h:02}:00")),
                ("end", &format!("2026-12-{d:02}T{h:02}:30")),
            ],
        )
        .await;
    }
    let t = e.page(&v, "/app/events?month=2026-12").await.text();
    assert_eq!(t.matches("class=\"card-wrap\"").count(), 48);
    assert!(t.contains("More events exist this month than are shown here."));
}
