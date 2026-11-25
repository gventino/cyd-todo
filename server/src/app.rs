//! The router: the device API of `docs/api.md`.

use std::sync::Arc;
use std::time::{Duration, Instant};

use poem::http::{Method, StatusCode};
use poem::web::{Data, Path};
use poem::{Body, Endpoint, EndpointExt, IntoResponse, Request, Response, Route, get, handler};
use serde::de::DeserializeOwned;
use serde::{Deserialize, Deserializer, Serialize};
use subtle::ConstantTimeEq;

use crate::db::{Db, NewQuest, QuestPatch, QuestState};
use crate::http_error::ApiError;
use crate::limits::{BODY_MAX, LIST_MAX};
use crate::listing;

/// Where access-log lines go (a closure, so a test can read them).
pub type LogSink = Arc<dyn Fn(String) + Send + Sync>;
/// Seconds since the epoch (a closure, so a test controls time).
pub type Clock = Arc<dyn Fn() -> i64 + Send + Sync>;

pub struct AppState {
    pub db: Db,
    pub token: Vec<u8>,
    pub clock: Clock,
    pub log: LogSink,
    /// What a wrong token costs the caller (200 ms; not a lockout).
    pub auth_delay: Duration,
}

pub fn system_clock() -> Clock {
    Arc::new(|| {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| i64::try_from(d.as_secs()).unwrap_or(i64::MAX))
            .unwrap_or(0)
    })
}

pub fn tracing_sink() -> LogSink {
    Arc::new(|line| tracing::info!("{line}"))
}

pub fn app(state: Arc<AppState>) -> impl Endpoint {
    let routes = Route::new()
        .at("/health", get(health))
        .at("/quests", get(list_quests).post(create_quest))
        .at("/quests/:id", get(get_quest).patch(patch_quest).delete(delete_quest));
    routes.around(guard).data(state).catch_all_error(|err: poem::Error| async move {
        match err.downcast_ref::<ApiError>() {
            Some(api) => api.to_response(),
            None => ApiError::from_status(err.status()).to_response(),
        }
    })
}

/// What one request leaves in the log: the method, the path made printable, the status, how long.
fn log_line(method: &Method, path: &str, status: StatusCode, started: Instant) -> String {
    let clean: String = path.chars().take(64).map(|c| if c.is_ascii_graphic() { c } else { '?' }).collect();
    format!("{method} {clean} {} {}ms", status.as_u16(), started.elapsed().as_millis())
}

/// Auth for everything but `GET /health`, and the access log (no query, no headers, no bodies).
async fn guard<E: Endpoint>(next: E, req: Request) -> poem::Result<Response> {
    let state = req.data::<Arc<AppState>>().cloned().ok_or_else(ApiError::internal)?;
    let started = Instant::now();
    let method = req.method().clone();
    let path = req.uri().path().to_string();

    let open = method == Method::GET && path == "/health";
    let resp = if open || authorized(&req, &state.token) {
        match next.call(req).await {
            Ok(r) => r.into_response(),
            Err(e) => match e.downcast_ref::<ApiError>() {
                Some(api) => api.to_response(),
                None => ApiError::from_status(e.status()).to_response(),
            },
        }
    } else {
        tokio::time::sleep(state.auth_delay).await;
        ApiError::unauthorized().to_response()
    };
    (state.log)(log_line(&method, &path, resp.status(), started));
    Ok(resp)
}

fn authorized(req: &Request, token: &[u8]) -> bool {
    let Some(value) = req.headers().get("Authorization").and_then(|v| v.to_str().ok()) else {
        return false;
    };
    let Some(given) = value.strip_prefix("Bearer ") else {
        return false;
    };
    given.as_bytes().ct_eq(token).into()
}

/// An id in a path: digits only, a positive number that fits. Anything else names nothing, so it is a 404.
fn parse_id(s: &str) -> Result<i64, ApiError> {
    if s.is_empty() || s.len() > 19 || !s.bytes().all(|b| b.is_ascii_digit()) {
        return Err(ApiError::not_found());
    }
    match s.parse::<i64>() {
        Ok(n) if n > 0 => Ok(n),
        _ => Err(ApiError::not_found()),
    }
}

fn json<T: Serialize>(status: StatusCode, value: &T) -> Result<Response, ApiError> {
    let body = serde_json::to_vec(value).map_err(|_| ApiError::internal())?;
    Ok(Response::builder()
        .status(status)
        .content_type("application/json")
        .header("Cache-Control", "no-store")
        .body(body))
}

fn raw_json(status: StatusCode, body: Vec<u8>) -> Response {
    Response::builder().status(status).content_type("application/json").header("Cache-Control", "no-store").body(body)
}

fn no_content() -> Response {
    Response::builder().status(StatusCode::NO_CONTENT).header("Cache-Control", "no-store").finish()
}

/// Reads the body with the limit of `api.md` and parses it as JSON. Nothing of the body is ever
/// put in an error.
async fn read_json<T: DeserializeOwned>(body: Body) -> Result<T, ApiError> {
    let bytes = body.into_bytes_limit(BODY_MAX).await.map_err(|e| match e {
        poem::error::ReadBodyError::PayloadTooLarge => ApiError::too_large(),
        _ => ApiError::bad_request("the request body could not be read"),
    })?;
    serde_json::from_slice(&bytes).map_err(|_| ApiError::bad_request("the body is not the JSON this call expects"))
}

/// `"x": null` and an absent `x` are different in a PATCH.
fn double_option<'de, D, T>(d: D) -> Result<Option<Option<T>>, D::Error>
where
    D: Deserializer<'de>,
    T: Deserialize<'de>,
{
    Option::<T>::deserialize(d).map(Some)
}

/// A member that can be absent but not null (a title cannot be cleared).
fn not_null<T>(given: Option<Option<T>>, msg: &'static str) -> Result<Option<T>, ApiError> {
    match given {
        None => Ok(None),
        Some(None) => Err(ApiError::bad_request(msg)),
        Some(Some(v)) => Ok(Some(v)),
    }
}

#[handler]
fn health() -> Response {
    raw_json(StatusCode::OK, br#"{"ok":true}"#.to_vec())
}

// ---- quests ----

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct QuestIn {
    title: String,
    notes: Option<String>,
    difficulty: Option<i64>,
    due: Option<String>,
}

#[derive(Deserialize, Default)]
#[serde(deny_unknown_fields)]
struct QuestPatchIn {
    #[serde(default, deserialize_with = "double_option")]
    title: Option<Option<String>>,
    #[serde(default, deserialize_with = "double_option")]
    notes: Option<Option<String>>,
    #[serde(default, deserialize_with = "double_option")]
    difficulty: Option<Option<i64>>,
    #[serde(default, deserialize_with = "double_option")]
    due: Option<Option<String>>,
    #[serde(default, deserialize_with = "double_option")]
    state: Option<Option<StateIn>>,
}

#[derive(Deserialize, Clone, Copy)]
#[serde(rename_all = "lowercase")]
enum StateIn {
    Open,
    Done,
}

#[handler]
async fn list_quests(Data(state): Data<&Arc<AppState>>) -> Result<Response, ApiError> {
    // the revision first: a write that lands between the reads can only make the device ask again
    let rev = state.db.rev().await?;
    let stats = state.db.stats().await?;
    let mut rows = state.db.list_quests(LIST_MAX as i64 + 1).await?;
    let more = rows.len() > LIST_MAX;
    rows.truncate(LIST_MAX);
    let head = format!(r#""rev":{rev},"xp":{},"open":{},"done":{}"#, stats.xp, stats.open, stats.done);
    Ok(raw_json(StatusCode::OK, listing::page(&head, "quests", &rows, more)))
}

#[handler]
async fn create_quest(Data(state): Data<&Arc<AppState>>, body: Body) -> Result<Response, ApiError> {
    let input: QuestIn = read_json(body).await?;
    let quest = state
        .db
        .create_quest(
            NewQuest { title: input.title, notes: input.notes, difficulty: input.difficulty, due: input.due },
            (state.clock)(),
        )
        .await?;
    json(StatusCode::CREATED, &quest)
}

#[handler]
async fn get_quest(Data(state): Data<&Arc<AppState>>, Path(id): Path<String>) -> Result<Response, ApiError> {
    json(StatusCode::OK, &state.db.get_quest(parse_id(&id)?).await?)
}

#[handler]
async fn patch_quest(
    Data(state): Data<&Arc<AppState>>,
    Path(id): Path<String>,
    body: Body,
) -> Result<Response, ApiError> {
    let input: QuestPatchIn = read_json(body).await?;
    let patch = QuestPatch {
        title: not_null(input.title, "title cannot be null")?,
        notes: not_null(input.notes, "notes cannot be null")?,
        difficulty: not_null(input.difficulty, "difficulty cannot be null")?,
        due: input.due,
        state: not_null(input.state, "state cannot be null")?.map(|s| match s {
            StateIn::Open => QuestState::Open,
            StateIn::Done => QuestState::Done,
        }),
    };
    json(StatusCode::OK, &state.db.update_quest(parse_id(&id)?, patch, (state.clock)()).await?)
}

#[handler]
async fn delete_quest(Data(state): Data<&Arc<AppState>>, Path(id): Path<String>) -> Result<Response, ApiError> {
    state.db.delete_quest(parse_id(&id)?).await?;
    Ok(no_content())
}
