//! One error type for the data layer. The HTTP layer turns it into the JSON of `docs/api.md`.

use std::fmt;

use crate::limits::Invalid;

#[derive(Debug)]
pub enum Error {
    /// A rule of `limits.rs` was broken (422).
    Invalid(Invalid),
    /// A request that is well formed but empty or contradictory in a way the rules do not name (400).
    BadRequest(&'static str),
    /// No such row (404).
    NotFound,
    /// The database failed (500; the cause is logged, never sent).
    Db(sqlx::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Invalid(i) => write!(f, "invalid: {i}"),
            Error::BadRequest(m) => write!(f, "bad request: {m}"),
            Error::NotFound => f.write_str("not found"),
            Error::Db(e) => write!(f, "database error: {e}"),
        }
    }
}

impl std::error::Error for Error {}

impl From<Invalid> for Error {
    fn from(i: Invalid) -> Self {
        Error::Invalid(i)
    }
}

impl From<sqlx::Error> for Error {
    fn from(e: sqlx::Error) -> Self {
        Error::Db(e)
    }
}

pub type Result<T> = std::result::Result<T, Error>;
