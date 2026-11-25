//! The quest pages: list, new, edit, done/reopen, and delete in two steps.

use std::sync::Arc;

use poem::http::StatusCode;
use poem::web::{Data, Path};
use poem::{Body, Request, Response, handler};
use serde::Deserialize;

use super::{Csrf, Visitor, csrf_field, error_page, html_response, not_found_page, read_form, redirect, shell};
use crate::app::{AppState, parse_id};
use crate::db::{Db, NewQuest, Quest, QuestPatch, QuestState};
use crate::error::Error;
use crate::html::Html;
use crate::limits;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct QuestForm {
    csrf: String,
    title: String,
    #[serde(default)]
    notes: String,
    #[serde(default)]
    difficulty: String,
    #[serde(default)]
    due: String,
}

impl Csrf for QuestForm {
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

/// What the form holds (typed by the person, or loaded from a quest).
struct Values {
    title: String,
    notes: String,
    difficulty: String,
    due: String,
}

impl Values {
    fn blank() -> Values {
        Values { title: String::new(), notes: String::new(), difficulty: "1".into(), due: String::new() }
    }
    fn of(q: &Quest) -> Values {
        Values {
            title: q.title.clone(),
            notes: q.notes.clone(),
            difficulty: q.difficulty.to_string(),
            due: q.due.clone().unwrap_or_default(),
        }
    }
    fn from_form(f: &QuestForm) -> Values {
        Values { title: f.title.clone(), notes: f.notes.clone(), difficulty: f.difficulty.clone(), due: f.due.clone() }
    }
}

fn internal(e: Error) -> Response {
    tracing::error!("database error: {e}");
    error_page(StatusCode::INTERNAL_SERVER_ERROR)
}

fn stars(n: i64) -> String {
    "★".repeat(usize::try_from(n.clamp(0, 5)).unwrap_or(0))
}

/// One quest as a card: the title, stars, the date and the notes (all escaped).
pub fn summary(q: &Quest) -> Html {
    let class = if q.state == "done" { "card done" } else { "card" };
    let mut inner = Html::tag("strong", &[], Html::text(&q.title));
    inner.push(&Html::lit(" "));
    inner.push(&Html::tag("span", &[], Html::text(&stars(q.difficulty))));
    if let Some(d) = &q.due {
        inner.push(&Html::tag("div", &[("class", "muted")], Html::join([Html::lit("Due "), Html::text(d)])));
    }
    if !q.notes.is_empty() {
        inner.push(&Html::tag("div", &[("class", "notes muted")], Html::text(&q.notes)));
    }
    Html::tag("div", &[("class", class)], inner)
}

fn card_with_actions(q: &Quest, v: &Visitor) -> Html {
    let id = q.id.to_string();
    let toggle_label = if q.state == "done" { "Reopen" } else { "Done" };
    let actions = Html::tag(
        "div",
        &[("class", "row")],
        Html::join([
            Html::tag(
                "form",
                &[("method", "post"), ("action", &format!("/app/quests/{id}/toggle"))],
                Html::join([csrf_field(v), Html::tag("button", &[("type", "submit")], Html::text(toggle_label))]),
            ),
            Html::tag("a", &[("class", "button"), ("href", &format!("/app/quests/{id}/edit"))], Html::lit("Edit")),
            Html::tag(
                "a",
                &[("class", "button danger"), ("href", &format!("/app/quests/{id}/delete"))],
                Html::lit("Delete"),
            ),
        ]),
    );
    let mut card = summary(q);
    // the actions go inside the card, after the text
    card = Html::tag("div", &[("class", "card-wrap")], Html::join([card, actions]));
    card
}

#[handler]
pub async fn list(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>) -> Response {
    let mut rows = match state.db.list_quests(limits::LIST_MAX as i64 + 1).await {
        Ok(r) => r,
        Err(e) => return internal(e),
    };
    let more = rows.len() > limits::LIST_MAX;
    rows.truncate(limits::LIST_MAX);
    let mut body = Html::tag("h1", &[], Html::lit("Quests"));
    body.push(&Html::tag("a", &[("class", "button"), ("href", "/app/quests/new")], Html::lit("New quest")));
    if rows.is_empty() {
        body.push(&Html::tag("p", &[("class", "muted")], Html::lit("No quests yet.")));
    }
    for q in &rows {
        body.push(&card_with_actions(q, v));
    }
    if more {
        body.push(&Html::tag("p", &[("class", "muted")], Html::lit("More quests exist than are shown here.")));
    }
    html_response(StatusCode::OK, shell("Quests", body))
}

fn form_page(
    title: &'static str,
    action: &str,
    label: &'static str,
    vals: &Values,
    error: Option<&str>,
    v: &Visitor,
) -> Html {
    let mut f = Html::new();
    f.push(&csrf_field(v));
    f.push(&Html::tag("label", &[("for", "title")], Html::lit("Title")));
    f.push(&Html::void(
        "input",
        &[("id", "title"), ("name", "title"), ("type", "text"), ("maxlength", "64"), ("value", &vals.title)],
    ));
    f.push(&Html::tag("label", &[("for", "notes")], Html::lit("Notes")));
    f.push(&Html::tag("textarea", &[("id", "notes"), ("name", "notes")], Html::text(&vals.notes)));
    f.push(&Html::tag("label", &[("for", "difficulty")], Html::lit("Difficulty")));
    let mut options = Html::new();
    for d in ["1", "2", "3", "4", "5"] {
        let attrs: Vec<(&'static str, &str)> =
            if vals.difficulty == d { vec![("value", d), ("selected", "selected")] } else { vec![("value", d)] };
        options.push(&Html::tag("option", &attrs, Html::text(&format!("{d} {}", stars(d.parse().unwrap_or(1))))));
    }
    f.push(&Html::tag("select", &[("id", "difficulty"), ("name", "difficulty")], options));
    f.push(&Html::tag("label", &[("for", "due")], Html::lit("Due date (optional)")));
    f.push(&Html::void("input", &[("id", "due"), ("name", "due"), ("type", "date"), ("value", &vals.due)]));
    f.push(&Html::tag("button", &[("type", "submit")], Html::text(label)));
    f.push(&Html::tag("a", &[("class", "button danger"), ("href", "/app/quests")], Html::lit("Cancel")));
    let mut body = Html::tag("h1", &[], Html::text(title));
    if let Some(e) = error {
        body.push(&Html::tag("div", &[("class", "error")], Html::text(e)));
    }
    body.push(&Html::tag("form", &[("method", "post"), ("action", action)], f));
    body
}

fn show_form(
    status: StatusCode,
    title: &'static str,
    action: &str,
    label: &'static str,
    vals: &Values,
    error: Option<&str>,
    v: &Visitor,
) -> Response {
    html_response(status, shell(title, form_page(title, action, label, vals, error, v)))
}

#[handler]
pub async fn new_form(Data(v): Data<&Visitor>) -> Response {
    show_form(StatusCode::OK, "New quest", "/app/quests", "Create", &Values::blank(), None, v)
}

/// The form as the database wants it: a trimmed title, `\n` line ends, an optional date, a number.
fn parse(f: &QuestForm) -> Result<(String, String, i64, Option<String>), &'static str> {
    let difficulty: i64 = if f.difficulty.trim().is_empty() {
        1
    } else {
        f.difficulty.trim().parse().map_err(|_| "difficulty must be 1 to 5")?
    };
    let due = if f.due.is_empty() { None } else { Some(f.due.clone()) };
    Ok((f.title.trim().to_string(), f.notes.replace("\r\n", "\n"), difficulty, due))
}

#[handler]
pub async fn create(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>, req: &Request, body: Body) -> Response {
    let form: QuestForm = match read_form(state, v, req, body).await {
        Ok(f) => f,
        Err(r) => return *r,
    };
    let (title, notes, difficulty, due) = match parse(&form) {
        Ok(p) => p,
        Err(m) => {
            return show_form(
                StatusCode::UNPROCESSABLE_ENTITY,
                "New quest",
                "/app/quests",
                "Create",
                &Values::from_form(&form),
                Some(m),
                v,
            );
        }
    };
    match state
        .db
        .create_quest(NewQuest { title, notes: Some(notes), difficulty: Some(difficulty), due }, (state.clock)())
        .await
    {
        Ok(_) => redirect("/app/quests"),
        Err(Error::Invalid(i)) => show_form(
            StatusCode::UNPROCESSABLE_ENTITY,
            "New quest",
            "/app/quests",
            "Create",
            &Values::from_form(&form),
            Some(i.0),
            v,
        ),
        Err(e) => internal(e),
    }
}

async fn load(db: &Db, id: &str) -> Result<Quest, Box<Response>> {
    let id = parse_id(id).map_err(|_| Box::new(not_found_page()))?;
    match db.get_quest(id).await {
        Ok(q) => Ok(q),
        Err(Error::NotFound) => Err(Box::new(not_found_page())),
        Err(e) => Err(Box::new(internal(e))),
    }
}

#[handler]
pub async fn edit_form(Data(state): Data<&Arc<AppState>>, Data(v): Data<&Visitor>, Path(id): Path<String>) -> Response {
    let q = match load(&state.db, &id).await {
        Ok(q) => q,
        Err(r) => return *r,
    };
    show_form(StatusCode::OK, "Edit quest", &format!("/app/quests/{}", q.id), "Save", &Values::of(&q), None, v)
}

#[handler]
pub async fn update(
    Data(state): Data<&Arc<AppState>>,
    Data(v): Data<&Visitor>,
    Path(id): Path<String>,
    req: &Request,
    body: Body,
) -> Response {
    let form: QuestForm = match read_form(state, v, req, body).await {
        Ok(f) => f,
        Err(r) => return *r,
    };
    let q = match load(&state.db, &id).await {
        Ok(q) => q,
        Err(r) => return *r,
    };
    let action = format!("/app/quests/{}", q.id);
    let (title, notes, difficulty, due) = match parse(&form) {
        Ok(p) => p,
        Err(m) => {
            return show_form(
                StatusCode::UNPROCESSABLE_ENTITY,
                "Edit quest",
                &action,
                "Save",
                &Values::from_form(&form),
                Some(m),
                v,
            );
        }
    };
    let patch = QuestPatch {
        title: Some(title),
        notes: Some(notes),
        difficulty: Some(difficulty),
        due: Some(due),
        state: None,
    };
    match state.db.update_quest(q.id, patch, (state.clock)()).await {
        Ok(_) => redirect("/app/quests"),
        Err(Error::Invalid(i)) => show_form(
            StatusCode::UNPROCESSABLE_ENTITY,
            "Edit quest",
            &action,
            "Save",
            &Values::from_form(&form),
            Some(i.0),
            v,
        ),
        Err(Error::NotFound) => not_found_page(),
        Err(e) => internal(e),
    }
}

#[handler]
pub async fn toggle(
    Data(state): Data<&Arc<AppState>>,
    Data(v): Data<&Visitor>,
    Path(id): Path<String>,
    req: &Request,
    body: Body,
) -> Response {
    if let Err(r) = read_form::<CsrfOnly>(state, v, req, body).await {
        return *r;
    }
    let q = match load(&state.db, &id).await {
        Ok(q) => q,
        Err(r) => return *r,
    };
    let next = if q.state == "done" { QuestState::Open } else { QuestState::Done };
    match state.db.update_quest(q.id, QuestPatch { state: Some(next), ..Default::default() }, (state.clock)()).await {
        Ok(_) => redirect("/app/quests"),
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
    let q = match load(&state.db, &id).await {
        Ok(q) => q,
        Err(r) => return *r,
    };
    let mut body = Html::tag("h1", &[], Html::lit("Delete this quest?"));
    body.push(&summary(&q));
    body.push(&Html::tag(
        "form",
        &[("method", "post"), ("action", &format!("/app/quests/{}/delete", q.id))],
        Html::join([
            csrf_field(v),
            Html::tag("button", &[("type", "submit"), ("class", "danger")], Html::lit("Yes, delete it")),
            Html::tag("a", &[("class", "button"), ("href", "/app/quests")], Html::lit("No, keep it")),
        ]),
    ));
    html_response(StatusCode::OK, shell("Delete quest", body))
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
    match state.db.delete_quest(id).await {
        Ok(()) => redirect("/app/quests"),
        Err(Error::NotFound) => not_found_page(),
        Err(e) => internal(e),
    }
}
