//! A server that misbehaves on purpose, so that the firmware's defences can be tested against something real.
//!
//! Only with `ALLOW_FAULTS=1` (the configuration refuses a `FAULT` otherwise). A fault changes **what the device API
//! answers** (never `/health`, never the phone pages, never the answer to a wrong token) and nothing else: the
//! database is written as usual, so the firmware can be watched surviving a server that lies about its own state.

use std::io;
use std::time::Duration;

use futures_util::stream::{self, StreamExt};
use poem::http::header::{CONTENT_LENGTH, CONTENT_TYPE};
use poem::{Body, Response};

use crate::http_error::ApiError;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Fault {
    /// The right answer, after a long wait (`slow_for`).
    Slow,
    /// A correct `Content-Length`, and the connection closed half way through the body.
    Truncate,
    /// A 200 KB body of valid JSON.
    Huge,
    /// 200, `application/json`, and bytes that are not JSON.
    Garbage,
    /// A 500 with the usual error body.
    Error500,
    /// The right body, but chunked: no `Content-Length`.
    Chunked,
    /// Valid JSON nested a thousand deep.
    Deep,
    /// Valid JSON in which every field has the wrong type.
    WrongType,
}

impl Fault {
    pub const ALL: [(&'static str, Fault); 8] = [
        ("slow", Fault::Slow),
        ("truncate", Fault::Truncate),
        ("huge", Fault::Huge),
        ("garbage", Fault::Garbage),
        ("error500", Fault::Error500),
        ("chunked", Fault::Chunked),
        ("deep", Fault::Deep),
        ("wrongtype", Fault::WrongType),
    ];

    pub fn parse(name: &str) -> Option<Fault> {
        Self::ALL.iter().find(|(n, _)| *n == name).map(|(_, f)| *f)
    }

    pub fn names() -> String {
        Self::ALL.iter().map(|(n, _)| *n).collect::<Vec<_>>().join(", ")
    }
}

/// How long a `slow` answer takes by default: longer than the board's 15 second timeout.
pub const SLOW_DEFAULT: Duration = Duration::from_secs(20);

fn json(status: poem::http::StatusCode, bytes: Vec<u8>) -> Response {
    Response::builder().status(status).header(CONTENT_TYPE, "application/json").body(bytes)
}

/// What the device API answers instead of `resp` (which is the genuine answer, already computed).
pub async fn apply(fault: Fault, slow_for: Duration, resp: Response) -> Response {
    match fault {
        Fault::Slow => {
            tokio::time::sleep(slow_for).await;
            resp
        }
        Fault::Truncate | Fault::Chunked => {
            let (parts, body) = resp.into_parts();
            let bytes = body.into_bytes().await.unwrap_or_default();
            let mid = bytes.len() / 2;
            let (first, second) = (bytes.slice(..mid), bytes.slice(mid..));
            let mut out = Response::builder().status(parts.status).header(CONTENT_TYPE, "application/json");
            if fault == Fault::Truncate {
                // the promise is the whole body; the stream breaks after the first half
                out = out.header(CONTENT_LENGTH, bytes.len());
                // a short pause before the break, so that the headers and the first half are on the wire when it comes
                let broken = stream::once(async {
                    tokio::time::sleep(Duration::from_millis(100)).await;
                    Err(io::Error::new(io::ErrorKind::ConnectionAborted, "fault: truncated"))
                });
                out.body(Body::from_bytes_stream(stream::once(async move { Ok(first) }).chain(broken)))
            } else {
                let s = stream::iter(vec![Ok::<_, io::Error>(first), Ok(second)]);
                out.body(Body::from_bytes_stream(s))
            }
        }
        Fault::Huge => {
            let mut b = br#"{"pad":""#.to_vec();
            b.extend(std::iter::repeat_n(b'x', 200_000));
            b.extend_from_slice(br#""}"#);
            json(poem::http::StatusCode::OK, b)
        }
        Fault::Garbage => {
            let mut x: u32 = 0x1234_5678;
            let b: Vec<u8> = (0..600)
                .map(|_| {
                    x = x.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                    (x >> 24) as u8
                })
                .collect();
            json(poem::http::StatusCode::OK, b)
        }
        Fault::Error500 => ApiError::internal().to_response(),
        Fault::Deep => {
            let mut b = vec![b'['; 1000];
            b.extend(std::iter::repeat_n(b']', 1000));
            json(poem::http::StatusCode::OK, b)
        }
        Fault::WrongType => json(
            poem::http::StatusCode::OK,
            br#"{"rev":"seven","xp":null,"open":-1,"done":1.5,"truncated":"no","quests":"none","events":7,"changed":"maybe","url":9,"expires_in":"soon"}"#.to_vec(),
        ),
    }
}
