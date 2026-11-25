//! The limits of `docs/api.md` and the rules that check them. Pure functions, no I/O.
//!
//! Every message is a fixed string and never contains what the client sent (CT-17, CT-20). The
//! phone pages call the same functions, so the two faces of the server cannot disagree.

use std::fmt;

pub const TITLE_MAX: usize = 64;
pub const NOTES_MAX: usize = 256;
pub const DESCRIPTION_MAX: usize = 256;
pub const DIFFICULTY_MIN: i64 = 1;
pub const DIFFICULTY_MAX: i64 = 5;
/// Items in one list.
pub const LIST_MAX: usize = 48;
/// A request body, in bytes.
pub const BODY_MAX: usize = 4096;
/// A list response stops growing here (the device reads at most 16 KB).
pub const RESPONSE_SOFT_CAP: usize = 14 * 1024;
/// The least and the most epoch seconds accepted: 2000-01-01 and 2100-01-01 (UTC).
pub const EPOCH_MIN: i64 = 946_684_800;
pub const EPOCH_MAX: i64 = 4_102_444_800;
pub const WINDOW_MAX_SECS: i64 = 62 * 86_400;
pub const EVENT_MAX_SECS: i64 = 31 * 86_400;
/// Active one-time links, and how long a link and a session live (seconds).
pub const LINKS_MAX: usize = 6;
pub const LINK_TTL: i64 = 600;
pub const SESSION_TTL: i64 = 1800;
/// The longest link URL: `PUBLIC_URL` (32) + "/m/" + a 26-character code, which fits a QR code of version 4.
pub const LINK_URL_MAX: usize = 61;
pub const TOKEN_MIN: usize = 32;
pub const PUBLIC_URL_MAX: usize = 32;

/// A rule that was broken. The text says which, never what was sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Invalid(pub &'static str);

impl fmt::Display for Invalid {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.0)
    }
}

type Check = Result<(), Invalid>;

fn text(s: &str, min: usize, max: usize, allow_newline: bool, msg: &'static str) -> Check {
    let n = s.len();
    if n < min || n > max {
        return Err(Invalid(msg));
    }
    if s.chars().any(|c| c.is_control() && !(allow_newline && c == '\n')) {
        return Err(Invalid(msg));
    }
    Ok(())
}

pub fn title(s: &str) -> Check {
    text(s, 1, TITLE_MAX, false, "title must be 1 to 64 bytes without control characters")
}

pub fn notes(s: &str) -> Check {
    text(s, 0, NOTES_MAX, true, "notes must be at most 256 bytes without control characters")
}

pub fn description(s: &str) -> Check {
    text(s, 0, DESCRIPTION_MAX, true, "description must be at most 256 bytes without control characters")
}

pub fn difficulty(d: i64) -> Check {
    if (DIFFICULTY_MIN..=DIFFICULTY_MAX).contains(&d) { Ok(()) } else { Err(Invalid("difficulty must be 1 to 5")) }
}

fn leap(y: i64) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}

fn days_in_month(y: i64, m: i64) -> i64 {
    match m {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        _ if leap(y) => 29,
        _ => 28,
    }
}

/// A calendar date written `YYYY-MM-DD`, from the year 2000 to 2099. The server never turns it into
/// an instant: it means that day in the user's own zone.
pub fn due(s: &str) -> Check {
    const MSG: &str = "due must be a date written YYYY-MM-DD between 2000 and 2099";
    let b = s.as_bytes();
    if b.len() != 10 || b[4] != b'-' || b[7] != b'-' {
        return Err(Invalid(MSG));
    }
    let digits = |r: std::ops::Range<usize>| -> Option<i64> {
        let part = &b[r];
        if part.iter().all(u8::is_ascii_digit) {
            Some(part.iter().fold(0i64, |a, d| a * 10 + i64::from(d - b'0')))
        } else {
            None
        }
    };
    let (Some(y), Some(m), Some(d)) = (digits(0..4), digits(5..7), digits(8..10)) else {
        return Err(Invalid(MSG));
    };
    if !(2000..=2099).contains(&y) || !(1..=12).contains(&m) || d < 1 || d > days_in_month(y, m) {
        return Err(Invalid(MSG));
    }
    Ok(())
}

pub fn epoch(t: i64) -> Check {
    if (EPOCH_MIN..EPOCH_MAX).contains(&t) {
        Ok(())
    } else {
        Err(Invalid("a time must be epoch seconds between 2000-01-01 and 2100-01-01"))
    }
}

/// The pair of an event, validated as a whole.
pub fn event_times(start: i64, end: i64) -> Check {
    epoch(start)?;
    epoch(end)?;
    if end <= start {
        return Err(Invalid("end must be after start"));
    }
    if end - start > EVENT_MAX_SECS {
        return Err(Invalid("an event can last at most 31 days"));
    }
    Ok(())
}

/// The window of a list of events.
pub fn window(from: i64, to: i64) -> Check {
    epoch(from)?;
    epoch(to)?;
    if to <= from {
        return Err(Invalid("to must be after from"));
    }
    if to - from > WINDOW_MAX_SECS {
        return Err(Invalid("the window can be at most 62 days"));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bytes(n: usize) -> String {
        "a".repeat(n)
    }

    #[test]
    fn title_boundaries_are_in_bytes() {
        assert!(title("").is_err());
        assert!(title("a").is_ok());
        assert!(title(&bytes(64)).is_ok());
        assert!(title(&bytes(65)).is_err());
        // 32 two-byte characters are 64 bytes: fine; 33 are 66: refused (never cut)
        assert!(title(&"é".repeat(32)).is_ok());
        assert!(title(&"é".repeat(33)).is_err());
        // 63 ASCII bytes and one two-byte character is 65 bytes
        assert!(title(&format!("{}é", bytes(63))).is_err());
        // a four-byte character
        assert!(title(&"😀".repeat(16)).is_ok());
        assert!(title(&"😀".repeat(17)).is_err());
    }

    #[test]
    fn control_characters_are_refused_in_titles_and_only_newline_is_allowed_in_notes() {
        for c in ['\n', '\t', '\r', '\0', '\u{7f}', '\u{85}', '\u{1b}'] {
            assert!(title(&format!("a{c}b")).is_err(), "title with {c:?}");
        }
        assert!(notes("line one\nline two").is_ok());
        assert!(description("line one\nline two").is_ok());
        for c in ['\t', '\r', '\0', '\u{7f}', '\u{85}', '\u{1b}'] {
            assert!(notes(&format!("a{c}b")).is_err(), "notes with {c:?}");
            assert!(description(&format!("a{c}b")).is_err(), "description with {c:?}");
        }
    }

    #[test]
    fn notes_and_description_boundaries() {
        assert!(notes("").is_ok());
        assert!(notes(&bytes(256)).is_ok());
        assert!(notes(&bytes(257)).is_err());
        assert!(description("").is_ok());
        assert!(description(&bytes(256)).is_ok());
        assert!(description(&bytes(257)).is_err());
        assert!(notes(&"\n".repeat(256)).is_ok());
    }

    #[test]
    fn difficulty_is_one_to_five() {
        assert!(difficulty(0).is_err());
        assert!(difficulty(1).is_ok());
        assert!(difficulty(5).is_ok());
        assert!(difficulty(6).is_err());
        assert!(difficulty(-1).is_err());
        assert!(difficulty(i64::MAX).is_err());
    }

    #[test]
    fn due_dates_are_real_calendar_dates() {
        for ok in ["2000-01-01", "2026-12-31", "2028-02-29", "2000-02-29", "2099-12-31", "2026-04-30"] {
            assert!(due(ok).is_ok(), "{ok}");
        }
        for bad in [
            "2027-02-29",
            "2100-02-29",
            "2026-04-31",
            "2026-13-01",
            "2026-00-10",
            "2026-01-00",
            "1999-12-31",
            "2100-01-01",
            "2026-1-01",
            "2026-01-1",
            "26-01-01",
            "2026/01/01",
            "2026-01-01 ",
            " 2026-01-01",
            "2026-01-011",
            "",
            "abcd-ef-gh",
            "２０２６-01-01",
            "2026-01-0１",
        ] {
            assert!(due(bad).is_err(), "{bad:?}");
        }
    }

    #[test]
    fn epoch_range_is_half_open() {
        assert!(epoch(EPOCH_MIN - 1).is_err());
        assert!(epoch(EPOCH_MIN).is_ok());
        assert!(epoch(EPOCH_MAX - 1).is_ok());
        assert!(epoch(EPOCH_MAX).is_err());
        assert!(epoch(i64::MIN).is_err());
        assert!(epoch(i64::MAX).is_err());
        assert!(epoch(0).is_err());
        assert!(epoch(-1).is_err());
    }

    #[test]
    fn an_event_is_forward_and_at_most_31_days() {
        let t = 1_796_230_800;
        assert!(event_times(t, t + 1).is_ok());
        assert!(event_times(t, t).is_err());
        assert!(event_times(t, t - 1).is_err());
        assert!(event_times(t, t + EVENT_MAX_SECS).is_ok());
        assert!(event_times(t, t + EVENT_MAX_SECS + 1).is_err());
        assert!(event_times(EPOCH_MIN - 1, t).is_err());
        assert!(event_times(t, EPOCH_MAX).is_err());
    }

    #[test]
    fn a_window_is_forward_and_at_most_62_days() {
        let t = 1_796_230_800;
        assert!(window(t, t + 1).is_ok());
        assert!(window(t, t).is_err());
        assert!(window(t + 1, t).is_err());
        assert!(window(t, t + WINDOW_MAX_SECS).is_ok());
        assert!(window(t, t + WINDOW_MAX_SECS + 1).is_err());
        assert!(window(0, t).is_err());
    }

    #[test]
    fn messages_never_contain_what_was_sent() {
        let marker = "ZZ-MARKER-ZZ";
        let all = [title(&format!("{marker}\n")), notes(&format!("{marker}\t")), description(&bytes(300)), due(marker)];
        for r in all {
            assert!(!r.unwrap_err().0.contains(marker));
        }
    }
}
