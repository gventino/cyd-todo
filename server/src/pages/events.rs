//! The calendar pages: a month of events, new, edit, and delete in two steps. Times are shown and read in the
//! one zone of the server (`TZ_OFFSET_MIN`).

use std::sync::Arc;

use poem::http::StatusCode;
use poem::web::{Data, Path, Query};
use poem::{Body, Request, Response, handler};
use serde::Deserialize;

use super::{
    Csrf, Visitor, csrf_field, error_page, html_response, message_page, not_found_page, read_form, redirect, shell,
};
use crate::app::{AppState, parse_id};
use crate::db::{Db, Event, EventPatch, NewEvent};
use crate::error::Error;
use crate::html::Html;
use crate::limits;
use crate::timeutil as tz;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct EventForm {
    csrf: String,
    title: String,
    #[serde(default)]
    description: String,
    #[serde(default)]
    start: String,
    #[serde(default)]
    end: String,
}

impl Csrf for EventForm {
    fn csrf(&self) -> &str {
        &self.csrf
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CsrfOnly {
    csrf: String,
}

impl Csrf for CsrfOnly {
    fn csrf(&self) -> &str {
        &self.csrf
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct MonthQuery {
    month: Option<String>,
}

struct Values {
    title: String,
    description: String,
    start: String,
    end: String,
}

impl Values {
    fn of(e: &Event, off: i32) -> Values {
        Values {
            title: e.title.clone(),
            description: e.description.clone(),
            start: tz::format_local_datetime(e.start, off),
            end: tz::format_local_datetime(e.end, off),
        }
    }
    fn from_form(f: &EventForm) -> Values {
        Values {
            title: f.title.clone(),
            description: f.description.clone(),
            start: f.start.clone(),
            end: f.end.clone(),
        }
    }
    /// A new event starts at the next full hour and lasts an hour.
    fn fresh(now: i64, off: i32) -> Values {
        let next_hour = (now + 3600) - (now + 3600).rem_euclid(3600);
        Values {
            title: String::new(),
            description: String::new(),
            start: tz::format_local_datetime(next_hour, off),
            end: tz::format_local_datetime(next_hour + 3600, off),
        }
    }
}

fn internal(e: Error) -> Response {
    tracing::error!("database error: {e}");
    error_page(StatusCode::INTERNAL_SERVER_ERROR)
}

/// `UTC-03:00`, for the line that says which zone the times are in.
fn zone_label(off: i32) -> String {
    let sign = if off < 0 { '-' } else { '+' };
    format!("UTC{sign}{:02}:{:02}", off.abs() / 60, off.abs() % 60)
}

fn month_param(y: i64, m: i64) -> String {
    format!("{y:04}-{m:02}")
}

fn summary(e: &Event, off: i32) -> Html {
    let start = tz::display(e.start, off);
    let end = tz::display(e.end, off);
    let when = if start[..10] == end[..10] { format!("{start} - {}", &end[11..]) } else { format!("{start} - {end}") };
    let mut inner = Html::tag("strong", &[], Html::text(&e.title));
    inner.push(&Html::tag("div", &[("class", "muted")], Html::text(&when)));
    if !e.description.is_empty() {
        inner.push(&Html::tag("div", &[("class", "notes muted")], Html::text(&e.description)));
    }
    Html::tag("div", &[("class", "card")], inner)
}

#[handler]
pub async fn list(Data(state): Data<&Arc<AppState>>, Query(q): Query<MonthQuery>) -> Response {
    let off = state.tz_offset_min;
    let (y, m) = match q.month.as_deref() {
        None => tz::month_of((state.clock)(), off),
        Some(s) => match tz::parse_month(s) {
            Some(ym) => ym,
            None => {
                return message_page(
                    StatusCode::BAD_REQUEST,
                    "That month is not valid",
                    "Use the links on the page to move between months.",
                );
            }
        },
    };
    let (from, to) = tz::month_window(y, m, off);
    let mut rows = match state.db.list_events(from, to, limits::LIST_MAX as i64 + 1).await {
        Ok(r) => r,
        Err(e) => return internal(e),
    };
    let more = rows.len() > limits::LIST_MAX;
    rows.truncate(limits::LIST_MAX);
    let (py, pm) = tz::previous_month(y, m);
    let (ny, nm) = tz::next_month(y, m);
    let mut body = Html::tag("h1", &[], Html::lit("Calendar"));
    body.push(&Html::tag(
        "div",
        &[("class", "row")],
        Html::join([
            Html::tag(
                "a",
                &[("class", "button"), ("href", &format!("/app/events?month={}", month_param(py, pm)))],
                Html::lit("Previous"),
            ),
            Html::tag("strong", &[], Html::text(&month_param(y, m))),
            Html::tag(
                "a",
                &[("class", "button"), ("href", &format!("/app/events?month={}", month_param(ny, nm)))],
                Html::lit("Next"),
            ),
        ]),
    ));
    body.push(&Html::tag("a", &[("class", "button"), ("href", "/app/events/new")], Html::lit("New event")));
    if rows.is_empty() {
        body.push(&Html::tag("p", &[("class", "muted")], Html::lit("No events this month.")));
    }
    for e in &rows {
        let id = e.id.to_string();
        let actions = Html::tag(
            "div",
            &[("class", "row")],
            Html::join([
                Html::tag("a", &[("class", "button"), ("href", &format!("/app/events/{id}/edit"))], Html::lit("Edit")),
                Html::tag(
                    "a",
                    &[("class", "button danger"), ("href", &format!("/app/events/{id}/delete"))],
                    Html::lit("Delete"),
                ),
            ]),
        );
        body.push(&Html::tag("div", &[("class", "card-wrap")], Html::join([summary(e, off), actions])));
    }
    if more {
        body.push(&Html::tag(
            "p",
            &[("class", "muted")],
            Html::lit("More events exist this month than are shown here."),
        ));
    }
    body.push(&Html::tag(
        "p",
        &[("class", "muted")],
        Html::join([Html::lit("Times are in "), Html::text(&zone_label(off))]),
    ));
    html_response(StatusCode::OK, shell("Calendar", body))
}

fn form_page(
    title: &'static str,
    action: &str,
    label: &'static str,
    vals: &Values,
    error: Option<&str>,
    v: &Visitor,
    off: i32,
) -> Html {
    let mut f = Html::new();
    f.push(&csrf_field(v));
    f.push(&Html::tag("label", &[("for", "title")], Html::lit("Title")));
    f.push(&Html::void(
        "input",
        &[("id", "title"), ("name", "title"), ("type", "text"), ("maxlength", "64"), ("value", &vals.title)],
    ));
    f.push(&Html::tag("label", &[("for", "start")], Html::lit("Starts")));
    f.push(&Html::void(
        "input",
        &[("id", "start"), ("name", "start"), ("type", "datetime-local"), ("value", &vals.start)],
    ));
    f.push(&Html::tag("label", &[("for", "end")], Html::lit("Ends")));
    f.push(&Html::void("input", &[("id", "end"), ("name", "end"), ("type", "datetime-local"), ("value", &vals.end)]));
    f.push(&Html::tag("label", &[("for", "description")], Html::lit("Description (optional)")));
    f.push(&Html::tag("textarea", &[("id", "description"), ("name", "description")], Html::text(&vals.description)));
    f.push(&Html::tag(
        "p",
        &[("class", "muted")],
        Html::join([Html::lit("Times are in "), Html::text(&zone_label(off))]),
    ));
    f.push(&Html::tag("button", &[("type", "submit")], Html::text(label)));
    f.push(&Html::tag("a", &[("class", "button danger"), ("href", "/app/events")], Html::lit("Cancel")));
    let mut body = Html::tag("h1", &[], Html::text(title));
    if let Some(e) = error {
        body.push(&Html::tag("div", &[("class", "error")], Html::text(e)));
    }
    body.push(&Html::tag("form", &[("method", "post"), ("action", action)], f));
    body
}

#[allow(clippy::too_many_arguments)]
fn show_form(
    status: StatusCode,
    title: &'static str,
    action: &str,
    label: &'static str,
    vals: &Values,
    error: Option<&str>,
    v: &Visitor,
    off: i32,
) -> Response {
    html_response(status, shell(title, form_page(title, action, label, vals, error, v, off)))
}

#[handler]
pub async fn new_form(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>) -> Response {
    let off = state.tz_offset_min;
    show_form(StatusCode::OK, "New event", "/app/events", "Create", &Values::fresh((state.clock)(), off), None, v, off)
}

/// The form as the database wants it: UTC seconds for the two times, `\n` line ends.
fn parse(f: &EventForm, off: i32) -> Result<(String, String, i64, i64), &'static str> {
    let start = tz::parse_local_datetime(&f.start, off).ok_or("start must be a date and a time")?;
    let end = tz::parse_local_datetime(&f.end, off).ok_or("end must be a date and a time")?;
    Ok((f.title.trim().to_string(), f.description.replace("\r\n", "\n"), start, end))
}

#[handler]
pub async fn create(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>, req: &Request, body: Body) -> Response {
    let off = state.tz_offset_min;
    let form: EventForm = match read_form(state, v, req, body).await {
        Ok(f) => f,
        Err(r) => return *r,
    };
    let again = |status, msg: &str| {
        show_form(status, "New event", "/app/events", "Create", &Values::from_form(&form), Some(msg), v, off)
    };
    let (title, description, start, end) = match parse(&form, off) {
        Ok(p) => p,
        Err(m) => return again(StatusCode::UNPROCESSABLE_ENTITY, m),
    };
    match state.db.create_event(NewEvent { title, description: Some(description), start, end }, (state.clock)()).await {
        Ok(e) => {
            let (y, m) = tz::month_of(e.start, off);
            redirect(&format!("/app/events?month={}", month_param(y, m)))
        }
        Err(Error::Invalid(i)) => again(StatusCode::UNPROCESSABLE_ENTITY, i.0),
        Err(e) => internal(e),
    }
}

async fn load(db: &Db, id: &str) -> Result<Event, Box<Response>> {
    let id = parse_id(id).map_err(|_| Box::new(not_found_page()))?;
    match db.get_event(id).await {
        Ok(e) => Ok(e),
        Err(Error::NotFound) => Err(Box::new(not_found_page())),
        Err(e) => Err(Box::new(internal(e))),
    }
}

#[handler]
pub async fn edit_form(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>, Path(id): Path<String>) -> Response {
    let off = state.tz_offset_min;
    let e = match load(&state.db, &id).await {
        Ok(e) => e,
        Err(r) => return *r,
    };
    show_form(
        StatusCode::OK,
        "Edit event",
        &format!("/app/events/{}", e.id),
        "Save",
        &Values::of(&e, off),
        None,
        v,
        off,
    )
}

#[handler]
pub async fn update(
    Data(state): Data<&Arc<AppState>>,
    Data(v): Data<&Visitor>,
    Path(id): Path<String>,
    req: &Request,
    body: Body,
) -> Response {
    let off = state.tz_offset_min;
    let form: EventForm = match read_form(state, v, req, body).await {
        Ok(f) => f,
        Err(r) => return *r,
    };
    let ev = match load(&state.db, &id).await {
        Ok(e) => e,
        Err(r) => return *r,
    };
    let action = format!("/app/events/{}", ev.id);
    let again = |status, msg: &str| {
        show_form(status, "Edit event", &action, "Save", &Values::from_form(&form), Some(msg), v, off)
    };
    let (title, description, start, end) = match parse(&form, off) {
        Ok(p) => p,
        Err(m) => return again(StatusCode::UNPROCESSABLE_ENTITY, m),
    };
    let patch = EventPatch { title: Some(title), description: Some(description), start: Some(start), end: Some(end) };
    match state.db.update_event(ev.id, patch, (state.clock)()).await {
        Ok(e) => {
            let (y, m) = tz::month_of(e.start, off);
            redirect(&format!("/app/events?month={}", month_param(y, m)))
        }
        Err(Error::Invalid(i)) => again(StatusCode::UNPROCESSABLE_ENTITY, i.0),
        Err(Error::NotFound) => not_found_page(),
        Err(e) => internal(e),
    }
}

#[handler]
pub async fn confirm_delete(
    Data(state): Data<&Arc<AppState>>,
    Data(v): Data<&Visitor>,
    Path(id): Path<String>,
) -> Response {
    let e = match load(&state.db, &id).await {
        Ok(e) => e,
        Err(r) => return *r,
    };
    let mut body = Html::tag("h1", &[], Html::lit("Delete this event?"));
    body.push(&summary(&e, state.tz_offset_min));
    body.push(&Html::tag(
        "form",
        &[("method", "post"), ("action", &format!("/app/events/{}/delete", e.id))],
        Html::join([
            csrf_field(v),
            Html::tag("button", &[("type", "submit"), ("class", "danger")], Html::lit("Yes, delete it")),
            Html::tag("a", &[("class", "button"), ("href", "/app/events")], Html::lit("No, keep it")),
        ]),
    ));
    html_response(StatusCode::OK, shell("Delete event", body))
}

#[handler]
pub async fn delete(
    Data(state): Data<&Arc<AppState>>,
    Data(v): Data<&Visitor>,
    Path(id): Path<String>,
    req: &Request,
    body: Body,
) -> Response {
    if let Err(r) = read_form::<CsrfOnly>(state, v, req, body).await {
        return *r;
    }
    let Ok(id) = parse_id(&id) else { return not_found_page() };
    match state.db.delete_event(id).await {
        Ok(()) => redirect("/app/events"),
        Err(Error::NotFound) => not_found_page(),
        Err(e) => internal(e),
    }
}
