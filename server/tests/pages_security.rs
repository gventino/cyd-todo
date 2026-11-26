//! Stored cross-site scripting, the way it is tested here: every field a person controls gets a set of hostile
//! strings, every page that can show them is fetched, and the *structure* of what comes back is checked against
//! an allowlist (the tags, the attributes, where a link may go). A payload that got out unescaped would show up
//! as a tag or an attribute that we never write.

mod common;

use common::*;
use cyd_todo_server::html::escape;
use poem::http::StatusCode;

const PAYLOADS: &[&str] = &[
    "<script>alert(1)</script>",
    "\"><img src=x onerror=alert(1)>",
    "' onmouseover='alert(1)",
    "\" onfocus=\"alert(1)\" autofocus=\"",
    "</textarea><script>alert(1)</script>",
    "</title><script>alert(1)</script>",
    "</style><script>alert(1)</script>",
    "<a href=\"javascript:alert(1)\">click</a>",
    "javascript:alert(1)",
    "<svg onload=alert(1)>",
    "<iframe src=//evil.example></iframe>",
    "<meta http-equiv=\"refresh\" content=\"0;url=//evil.example\">",
    "<base href=\"//evil.example/\">",
    "<style>*{display:none}</style>",
    "<!-- comment",
    "<![CDATA[ x ]]>",
    "&lt;b&gt; &amp; &#60;script&#62; &#x3c;",
    "\u{202e}gnp.exe\u{202c}",
    "emoji \u{1F600} and \u{00E9}",
    "{{7*7}} ${7*7} <%= 7*7 %>",
    "%3Cscript%3Ealert(1)%3C/script%3E",
    "\\u003cscript\\u003e",
];

const TAGS: &[&str] = &[
    "html", "head", "meta", "title", "style", "body", "main", "nav", "a", "h1", "h2", "p", "div", "span", "strong",
    "form", "input", "label", "textarea", "select", "option", "button",
];
const ATTRS: &[&str] = &[
    "lang",
    "charset",
    "name",
    "content",
    "class",
    "href",
    "method",
    "action",
    "type",
    "id",
    "for",
    "value",
    "maxlength",
    "selected",
];

/// Walks the tags of a page. Text is escaped, so every `<` that is left starts one of ours.
fn check_structure(html: &str) -> Result<(), String> {
    let mut rest = html.strip_prefix("<!doctype html>").ok_or("no doctype")?;
    // the stylesheet is the one place with raw `<`-free CSS text; skip its body
    while let Some(at) = rest.find('<') {
        let after = &rest[at + 1..];
        let end = after.find('>').ok_or("a tag that never ends")?;
        let tag = &after[..end];
        rest = &after[end + 1..];
        if let Some(name) = tag.strip_prefix('/') {
            if !TAGS.contains(&name) {
                return Err(format!("closing tag </{name}>"));
            }
            continue;
        }
        let mut chars = tag.char_indices().peekable();
        let name: String =
            std::iter::from_fn(|| chars.next_if(|(_, c)| c.is_ascii_alphanumeric()).map(|(_, c)| c)).collect();
        if !TAGS.contains(&name.as_str()) {
            return Err(format!("tag <{name}>"));
        }
        let mut attrs = &tag[name.len()..];
        while !attrs.trim_start().is_empty() {
            attrs = attrs.trim_start();
            let eq = attrs.find('=').unwrap_or(attrs.len());
            let key = attrs[..eq].trim();
            if !ATTRS.contains(&key) {
                return Err(format!("attribute {key:?} on <{name}>"));
            }
            if eq == attrs.len() {
                break;
            }
            let val = attrs[eq + 1..].strip_prefix('"').ok_or(format!("unquoted value of {key}"))?;
            let close = val.find('"').ok_or("a value that never ends")?;
            let value = &val[..close];
            if value.contains(['<', '>', '"']) {
                return Err(format!("a raw metacharacter in {key}={value:?}"));
            }
            if (key == "href" || key == "action") && !(value.starts_with("/app") && !value.starts_with("//")) {
                return Err(format!("{key} goes somewhere else: {value:?}"));
            }
            attrs = &val[close + 1..];
        }
    }
    Ok(())
}

#[test]
fn the_checker_itself_catches_what_it_is_for() {
    let page = |body: &str| format!("<!doctype html><html><head></head><body><main>{body}</main></body></html>");
    assert!(check_structure(&page("<h1>fine &lt;script&gt;</h1>")).is_ok());
    for bad in [
        "<script>alert(1)</script>",
        "<img src=x onerror=alert(1)>",
        "<a href=\"javascript:alert(1)\">x</a>",
        "<a href=\"//evil.example\">x</a>",
        "<a href=\"http://evil.example\">x</a>",
        "<input value=\"x\" onfocus=\"y\">",
        "<input value=\"x\"><iframe></iframe>",
        "<div class=\"a\" autofocus>x</div>",
    ] {
        assert!(check_structure(&page(bad)).is_err(), "{bad}");
    }
}

/// Every page that can show stored data, for the given ids.
fn pages(quest: i64, event: i64) -> Vec<String> {
    vec![
        "/app".into(),
        "/app/quests".into(),
        format!("/app/quests/{quest}/edit"),
        format!("/app/quests/{quest}/delete"),
        "/app/events?month=2026-12".into(),
        format!("/app/events/{event}/edit"),
        format!("/app/events/{event}/delete"),
    ]
}

#[tokio::test]
async fn stored_text_never_becomes_markup_on_any_page() {
    let e = env().await;
    let v = e.visit("home").await;
    for payload in PAYLOADS {
        // stored through the device API (a title, notes, a description: all attacker-controlled text)
        let mut title = String::new();
        for c in payload.chars() {
            if title.len() + c.len_utf8() > 64 {
                break;
            }
            title.push(c);
        }
        let mut long = String::new();
        for c in payload.chars().cycle() {
            if long.len() + c.len_utf8() > 250 {
                break;
            }
            long.push(c);
        }
        let q =
            e.post("/quests", &serde_json::json!({"title": title, "notes": long, "difficulty": 3}).to_string()).await;
        assert_eq!(q.status, StatusCode::CREATED, "{payload}");
        let ev = e
            .post(
                "/events",
                &serde_json::json!({"title": title, "description": long, "start": 1_796_230_800, "end": 1_796_234_400})
                    .to_string(),
            )
            .await;
        assert_eq!(ev.status, StatusCode::CREATED, "{payload}");
        let (qid, eid) = (q.json()["id"].as_i64().unwrap(), ev.json()["id"].as_i64().unwrap());
        for p in pages(qid, eid) {
            let r = e.page(&v, &p).await;
            assert_eq!(r.status, StatusCode::OK, "{p}");
            let html = r.text();
            check_structure(&html).unwrap_or_else(|why| panic!("{p} with {payload:?}: {why}"));
        }
        // and the text is there, escaped (the person must still be able to read it)
        let list = e.page(&v, "/app/quests").await.text();
        assert!(list.contains(&escape(&title)), "{payload:?} is not shown, escaped");
    }
}

#[tokio::test]
async fn text_typed_into_the_forms_never_becomes_markup_either() {
    let e = env().await;
    let v = e.visit("home").await;
    for payload in PAYLOADS {
        // a form that is refused shows the typed values again: the payload first, then too many bytes
        let refused_title = format!("{payload}{}", "x".repeat(70));
        let r =
            e.submit(&v, "/app/quests", &[("title", &refused_title), ("notes", payload), ("difficulty", "2")]).await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{payload}");
        check_structure(&r.text()).unwrap_or_else(|why| panic!("refused quest form with {payload:?}: {why}"));
        let r = e
            .submit(
                &v,
                "/app/events",
                &[
                    ("title", &refused_title),
                    ("description", payload),
                    ("start", "2026-12-09T10:00"),
                    ("end", "2026-12-09T11:00"),
                ],
            )
            .await;
        assert_eq!(r.status, StatusCode::UNPROCESSABLE_ENTITY, "{payload}");
        check_structure(&r.text()).unwrap_or_else(|why| panic!("refused event form with {payload:?}: {why}"));
        // and a form that is accepted, then shown on every page
        let mut title = String::new();
        for c in payload.chars() {
            if title.len() + c.len_utf8() > 60 {
                break;
            }
            title.push(c);
        }
        e.submit(&v, "/app/quests", &[("title", &title), ("notes", payload), ("difficulty", "2")]).await;
        e.submit(
            &v,
            "/app/events",
            &[("title", &title), ("description", payload), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T11:00")],
        )
        .await;
    }
    let count = e.get("/quests").await.json()["quests"].as_array().unwrap().len();
    assert!(count > 0);
    let last_q = e.get("/quests").await.json()["quests"][0]["id"].as_i64().unwrap();
    let last_e = e.get("/events?from=1796000000&to=1800000000").await.json()["events"][0]["id"].as_i64().unwrap();
    for p in pages(last_q, last_e) {
        let r = e.page(&v, &p).await;
        check_structure(&r.text()).unwrap_or_else(|why| panic!("{p}: {why}"));
    }
}

#[tokio::test]
async fn nothing_the_url_or_the_headers_carry_is_echoed_into_a_page() {
    let e = env().await;
    let v = e.visit("home").await;
    let marker = "ZZREFLECTZZ";
    let attempts = [
        format!("/app/quests/{marker}/edit"),
        format!("/app/events?month={marker}"),
        format!("/app/{marker}"),
        format!("/app/events?{marker}={marker}"),
        format!("/m/{marker}"),
    ];
    for p in attempts {
        let r = e
            .raw(
                "GET",
                &p,
                &[("Cookie", &v.cookie), ("Referer", marker), ("User-Agent", marker), ("Origin", marker)],
                None,
            )
            .await;
        assert!(!r.text().contains(marker), "{p} reflected the request: {}", r.text());
        check_structure(&r.text()).unwrap_or_else(|why| panic!("{p}: {why}"));
    }
    let r = e
        .raw("POST", "/app/logout", &[("Cookie", &v.cookie), ("Origin", marker)], Some(&format!("csrf={marker}")))
        .await;
    assert!(!r.text().contains(marker));
    let r = e.submit(&v, &format!("/app/quests/{marker}"), &[("title", "x")]).await;
    assert!(!r.text().contains(marker));
}

#[tokio::test]
async fn pages_have_no_script_no_inline_handlers_no_external_resources() {
    let e = env().await;
    let v = e.visit("home").await;
    e.submit(&v, "/app/quests", &[("title", "Quest"), ("difficulty", "2")]).await;
    e.submit(&v, "/app/events", &[("title", "Event"), ("start", "2026-12-09T10:00"), ("end", "2026-12-09T11:00")])
        .await;
    for p in [
        "/app",
        "/app/quests",
        "/app/quests/new",
        "/app/quests/1/edit",
        "/app/quests/1/delete",
        "/app/events?month=2026-12",
        "/app/events/new",
        "/app/events/1/edit",
        "/app/events/1/delete",
    ] {
        let html = e.page(&v, p).await.text();
        check_structure(&html).unwrap_or_else(|why| panic!("{p}: {why}"));
        let lower = html.to_ascii_lowercase();
        for banned in [
            "<script",
            "javascript:",
            " on",
            "http://",
            "https://",
            "//",
            "src=",
            "<iframe",
            "<object",
            "<embed",
            "url(",
        ] {
            let hit = match banned {
                " on" => {
                    lower.contains(" onclick=")
                        || lower.contains(" onload=")
                        || lower.contains(" onerror=")
                        || lower.contains(" onfocus=")
                }
                "//" => lower.contains("=\"//"),
                other => lower.contains(other),
            };
            assert!(!hit, "{p} contains {banned:?}");
        }
    }
}

#[tokio::test]
async fn the_code_and_the_session_never_appear_in_any_page() {
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
    let v = Visit { cookie: format!("sid={sid}"), csrf: String::new() };
    for p in ["/app", "/app/quests", "/app/quests/new", "/app/events", "/app/events/new"] {
        let t = e.page(&v, p).await.text();
        assert!(!t.contains(&code) && !t.contains(&sid), "{p}");
    }
    assert!(!r.text().contains(&code));
}

/// What a real phone sends. A browser posting a form under `Referrer-Policy: no-referrer` sends `Origin: null`, which this
/// server must (and does) refuse: so the pages must ask for `same-origin`, under which the browser sends the real origin.
#[tokio::test]
async fn a_form_posted_with_the_browsers_real_origin_works_and_a_null_origin_does_not() {
    let e = env().await;
    let v = e.visit("quest").await;
    let body = format!("csrf={}&title=From+a+phone&notes=&difficulty=2&due=", v.csrf);
    let headers_null = [("Cookie", v.cookie.as_str()), ("Origin", "null")];
    let r = e.raw("POST", "/app/quests", &headers_null, Some(&body)).await;
    assert_eq!(r.status, StatusCode::FORBIDDEN, "{}", r.text());
    let headers_ok = [("Cookie", v.cookie.as_str()), ("Origin", common::PUBLIC_URL)];
    let r = e.raw("POST", "/app/quests", &headers_ok, Some(&body)).await;
    assert_eq!(r.status, StatusCode::SEE_OTHER, "{}", r.text());
    // and the pages ask the browser to send the origin: same-origin, in the header and in the page
    let page = e.page(&v, "/app/quests/new").await;
    assert_eq!(page.headers.get("referrer-policy").unwrap(), "same-origin");
    assert!(page.text().contains("name=\"referrer\" content=\"same-origin\""), "{}", page.text());
}
