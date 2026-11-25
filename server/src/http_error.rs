//! The error answers of `docs/api.md`: a status and `{"error": "<code>", "message": "<fixed text>"}`.
//! A message is always one of ours and never contains what the client sent.

use std::fmt;

use poem::http::StatusCode;
use poem::{IntoResponse, Response};

use crate::error::Error;
use crate::limits::Invalid;

#[derive(Debug, Clone)]
pub struct ApiError {
    status: StatusCode,
    code: &'static str,
    message: &'static str,
}

impl ApiError {
    pub fn bad_request(message: &'static str) -> Self {
        Self { status: StatusCode::BAD_REQUEST, code: "bad_request", message }
    }
    pub fn unauthorized() -> Self {
        Self { status: StatusCode::UNAUTHORIZED, code: "unauthorized", message: "a valid token is required" }
    }
    pub fn not_found() -> Self {
        Self { status: StatusCode::NOT_FOUND, code: "not_found", message: "there is nothing here" }
    }
    pub fn method_not_allowed() -> Self {
        Self {
            status: StatusCode::METHOD_NOT_ALLOWED,
            code: "method_not_allowed",
            message: "that method is not allowed here",
        }
    }
    pub fn too_large() -> Self {
        Self {
            status: StatusCode::PAYLOAD_TOO_LARGE,
            code: "too_large",
            message: "the request body is over 4096 bytes",
        }
    }
    pub fn invalid(i: Invalid) -> Self {
        Self { status: StatusCode::UNPROCESSABLE_ENTITY, code: "invalid", message: i.0 }
    }
    pub fn too_many() -> Self {
        Self { status: StatusCode::TOO_MANY_REQUESTS, code: "too_many", message: "too many active one-time links" }
    }
    pub fn internal() -> Self {
        Self { status: StatusCode::INTERNAL_SERVER_ERROR, code: "internal", message: "something went wrong" }
    }

    /// For an error that poem raised itself (a path that does not parse, a wrong method, ...).
    pub fn from_status(status: StatusCode) -> Self {
        match status {
            StatusCode::NOT_FOUND => Self::not_found(),
            StatusCode::METHOD_NOT_ALLOWED => Self::method_not_allowed(),
            StatusCode::PAYLOAD_TOO_LARGE => Self::too_large(),
            StatusCode::UNAUTHORIZED => Self::unauthorized(),
            s if s.is_client_error() => Self::bad_request("the request could not be understood"),
            _ => Self::internal(),
        }
    }

    pub fn status(&self) -> StatusCode {
        self.status
    }

    pub fn code(&self) -> &'static str {
        self.code
    }

    pub fn to_response(&self) -> Response {
        let body = format!(r#"{{"error":"{}","message":"{}"}}"#, self.code, self.message);
        Response::builder()
            .status(self.status)
            .content_type("application/json")
            .header("Cache-Control", "no-store")
            .body(body)
    }
}

impl fmt::Display for ApiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.status.as_u16(), self.code)
    }
}

impl std::error::Error for ApiError {}

impl poem::error::ResponseError for ApiError {
    fn status(&self) -> StatusCode {
        self.status
    }
    fn as_response(&self) -> Response {
        self.to_response()
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        self.to_response()
    }
}

impl From<Error> for ApiError {
    fn from(e: Error) -> Self {
        match e {
            Error::Invalid(i) => ApiError::invalid(i),
            Error::BadRequest(m) => ApiError::bad_request(m),
            Error::NotFound => ApiError::not_found(),
            Error::TooMany => ApiError::too_many(),
            Error::Db(err) => {
                // The cause goes to the log (it holds no request data: sqlx reports the kind of failure);
                // the client gets nothing.
                tracing::error!("database error: {err}");
                ApiError::internal()
            }
        }
    }
}
