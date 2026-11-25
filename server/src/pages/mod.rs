//! The phone pages: HTML for a person, reached through a one-time link and a session cookie.
//! Everything user-controlled goes through `html::Html`, so it is escaped; nothing here builds markup
//! with `format!` (`scripts/audit.sh` checks).

use std::sync::Arc;

use poem::http::{HeaderMap, StatusCode};
use poem::web::Data;
use poem::{Body, Endpoint, EndpointExt, IntoResponse, Request, Response, Route, get, handler, post};
use serde::Deserialize;
use serde::de::DeserializeOwned;
use subtle::ConstantTimeEq;

use crate::app::AppState;
use crate::db::Session;
use crate::html::{Html, page};
use crate::limits::{self, BODY_MAX, SESSION_TTL};
use crate::token;

/// What a page handler knows about the visitor.
#[derive(Clone)]
pub struct Visitor {
    pub sid_hash: Vec<u8>,
    pub session: Session,
}

pub mod events;
pub mod quests;

pub fn routes() -> impl Endpoint {
    Route::new()
        .at("/", get(home))
        .at("/logout", post(logout))
        .at("/quests", get(quests::list).post(quests::create))
        .at("/quests/new", get(quests::new_form))
        .at("/quests/:id/edit", get(quests::edit_form))
        .at("/quests/:id", post(quests::update))
        .at("/quests/:id/toggle", post(quests::toggle))
        .at("/quests/:id/delete", get(quests::confirm_delete).post(quests::delete))
        .at("/events", get(events::list).post(events::create))
        .at("/events/new", get(events::new_form))
        .at("/events/:id/edit", get(events::edit_form))
        .at("/events/:id", post(events::update))
        .at("/events/:id/delete", get(events::confirm_delete).post(events::delete))
        .around(session_guard)
}

/// The links at the top of every page.
pub fn nav() -> Html {
    Html::tag(
        "nav",
        &[],
        Html::join([
            Html::tag("a", &[("href", "/app")], Html::lit("Home")),
            Html::tag("a", &[("href", "/app/quests")], Html::lit("Quests")),
            Html::tag("a", &[("href", "/app/events")], Html::lit("Calendar")),
        ]),
    )
}

/// A page with the navigation above `body`.
pub fn shell(title: &str, body: Html) -> String {
    page(title, Html::join([nav(), body]))
}

// ---- responses ----

pub fn html_response(status: StatusCode, body: String) -> Response {
    Response::builder().status(status).content_type("text/html; charset=utf-8").body(body)
}

/// A small page with a title and a sentence (always ours, never the client's words).
pub fn message_page(status: StatusCode, title: &'static str, text: &'static str) -> Response {
    let body = Html::join([Html::tag("h1", &[], Html::lit(title)), Html::tag("p", &[], Html::lit(text))]);
    html_response(status, page(title, body))
}

pub fn expired_page() -> Response {
    message_page(
        StatusCode::UNAUTHORIZED,
        "This link has expired",
        "Tap the button on the board again to get a new one.",
    )
}

pub fn not_found_page() -> Response {
    message_page(
        StatusCode::NOT_FOUND,
        "Nothing here",
        "This page does not exist, or the link no longer works. Tap the button on the board again.",
    )
}

/// Any error that poem raised inside the pages, as a page.
pub fn error_page(status: StatusCode) -> Response {
    match status {
        StatusCode::NOT_FOUND => not_found_page(),
        StatusCode::METHOD_NOT_ALLOWED => message_page(status, "Not allowed", "That cannot be done here."),
        StatusCode::PAYLOAD_TOO_LARGE => message_page(status, "Too large", "That was more than this page accepts."),
        StatusCode::FORBIDDEN => {
            message_page(status, "That form has expired", "Go back, reload the page and try again.")
        }
        s if s.is_client_error() => message_page(s, "That did not work", "The request could not be understood."),
        _ => message_page(StatusCode::INTERNAL_SERVER_ERROR, "Something went wrong", "Please try again."),
    }
}

pub fn redirect(to: &str) -> Response {
    Response::builder().status(StatusCode::SEE_OTHER).header("Location", to).finish()
}

/// The headers every page, redirect and error of the phone side carries.
pub fn harden(mut resp: Response, state: &AppState) -> Response {
    let h = resp.headers_mut();
    if let Ok(v) = state.csp.parse() {
        h.insert("Content-Security-Policy", v);
    }
    h.insert("Cache-Control", "no-store".parse().expect("static header"));
    h.insert("X-Content-Type-Options", "nosniff".parse().expect("static header"));
    h.insert("Referrer-Policy", "no-referrer".parse().expect("static header"));
    resp
}

pub fn session_cookie(sid: &str, secure: bool) -> String {
    let mut c = format!("sid={sid}; HttpOnly; SameSite=Strict; Path=/; Max-Age={SESSION_TTL}");
    if secure {
        c.push_str("; Secure");
    }
    c
}

pub fn clear_cookie(secure: bool) -> String {
    let mut c = String::from("sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    if secure {
        c.push_str("; Secure");
    }
    c
}

// ---- the session ----

fn sid_from(headers: &HeaderMap) -> Option<String> {
    for value in headers.get_all("Cookie") {
        let Ok(v) = value.to_str() else { continue };
        for part in v.split(';') {
            if let Some(sid) = part.trim().strip_prefix("sid=")
                && token::well_formed(sid)
            {
                return Some(sid.to_string());
            }
        }
    }
    None
}

async fn session_guard<E: Endpoint>(next: E, mut req: Request) -> poem::Result<Response> {
    let Some(state) = req.data::<Arc<AppState>>().cloned() else {
        return Ok(error_page(StatusCode::INTERNAL_SERVER_ERROR));
    };
    let Some(sid) = sid_from(req.headers()) else {
        return Ok(expired_page());
    };
    let hash = token::hash(&sid);
    match state.db.session(&hash, (state.clock)()).await {
        Ok(Some(session)) => {
            req.set_data(Visitor { sid_hash: hash, session });
            Ok(next.call(req).await?.into_response())
        }
        Ok(None) => Ok(expired_page()),
        Err(e) => {
            tracing::error!("database error: {e}");
            Ok(error_page(StatusCode::INTERNAL_SERVER_ERROR))
        }
    }
}

// ---- forms ----

/// A form that carries the CSRF token.
pub trait Csrf {
    fn csrf(&self) -> &str;
}

/// Reads a POSTed form: the `Origin` (when the browser sends one) must be ours, the body is limited, the
/// fields are exactly the struct's, and the CSRF token must be the session's. A failure is the page to send.
pub async fn read_form<T: DeserializeOwned + Csrf>(
    state: &AppState,
    v: &Visitor,
    req: &Request,
    body: Body,
) -> Result<T, Box<Response>> {
    if let Some(origin) = req.headers().get("Origin")
        && origin.to_str().map(|o| o != state.public_url).unwrap_or(true)
    {
        return Err(Box::new(error_page(StatusCode::FORBIDDEN)));
    }
    let bytes = body.into_bytes_limit(BODY_MAX).await.map_err(|e| match e {
        poem::error::ReadBodyError::PayloadTooLarge => Box::new(error_page(StatusCode::PAYLOAD_TOO_LARGE)),
        _ => Box::new(error_page(StatusCode::BAD_REQUEST)),
    })?;
    let form: T = serde_urlencoded::from_bytes(&bytes).map_err(|_| Box::new(error_page(StatusCode::BAD_REQUEST)))?;
    let ok: bool = form.csrf().as_bytes().ct_eq(v.session.csrf.as_bytes()).into();
    if !ok {
        return Err(Box::new(error_page(StatusCode::FORBIDDEN)));
    }
    Ok(form)
}

/// A hidden field with the session's CSRF token, for every form.
pub fn csrf_field(v: &Visitor) -> Html {
    Html::void("input", &[("type", "hidden"), ("name", "csrf"), ("value", &v.session.csrf)])
}

// ---- the pages of this step ----

#[handler]
async fn home(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>) -> Response {
    let rows = match state.db.list_quests(limits::LIST_MAX as i64).await {
        Ok(r) => r,
        Err(e) => {
            tracing::error!("database error: {e}");
            return error_page(StatusCode::INTERNAL_SERVER_ERROR);
        }
    };
    let open: Vec<Html> = rows.iter().filter(|q| q.state == "open").map(quests::summary).collect();
    let mut body = Html::tag("h1", &[], Html::lit("Quest log"));
    body.push(&Html::tag("h2", &[], Html::lit("Open quests")));
    if open.is_empty() {
        body.push(&Html::tag("p", &[("class", "muted")], Html::lit("No open quests. Time to write one.")));
    }
    for q in open {
        body.push(&q);
    }
    body.push(&Html::tag("a", &[("class", "button"), ("href", "/app/quests/new")], Html::lit("New quest")));
    body.push(&Html::tag(
        "form",
        &[("method", "post"), ("action", "/app/logout")],
        Html::join([
            csrf_field(v),
            Html::tag("button", &[("type", "submit"), ("class", "danger")], Html::lit("Leave")),
        ]),
    ));
    html_response(StatusCode::OK, shell("Quest log", body))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct LogoutForm {
    csrf: String,
}

impl Csrf for LogoutForm {
    fn csrf(&self) -> &str {
        &self.csrf
    }
}

#[handler]
async fn logout(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>, req: &Request, body: Body) -> Response {
    if let Err(page) = read_form::<LogoutForm>(state, v, req, body).await {
        return *page;
    }
    if let Err(e) = state.db.delete_session(&v.sid_hash).await {
        tracing::error!("database error: {e}");
        return error_page(StatusCode::INTERNAL_SERVER_ERROR);
    }
    let mut resp =
        message_page(StatusCode::OK, "Bye", "The session is closed. Tap the button on the board for a new link.");
    if let Ok(c) = clear_cookie(state.secure_cookie()).parse() {
        resp.headers_mut().insert("Set-Cookie", c);
    }
    resp
}
