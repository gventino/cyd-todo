//! The calendar and time-zone arithmetic of the server against `docs/vectors/time.tsv`, the table that the
//! firmware's C code is checked against too (computed with Python's datetime, which is neither of them).

use cyd_todo_server::timeutil as tz;

fn lines(kind: &str) -> Vec<Vec<String>> {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../docs/vectors/time.tsv");
    let text = std::fs::read_to_string(path).expect("docs/vectors/time.tsv");
    text.lines()
        .filter(|l| !l.starts_with('#') && !l.is_empty())
        .map(|l| l.split('\t').map(String::from).collect::<Vec<_>>())
        .filter(|f| f[0] == kind)
        .map(|f| f[1..].to_vec())
        .collect()
}

#[test]
fn civil_dates_agree() {
    let v = lines("civil");
    assert!(v.len() > 1000);
    for f in v {
        let days: i64 = f[0].parse().unwrap();
        let (y, m, d): (i64, i64, i64) = (f[1].parse().unwrap(), f[2].parse().unwrap(), f[3].parse().unwrap());
        assert_eq!(tz::civil_from_days(days), (y, m, d), "day {days}");
        assert_eq!(tz::days_from_civil(y, m, d), days, "{y}-{m}-{d}");
    }
}

#[test]
fn local_times_agree() {
    let v = lines("local");
    assert!(v.len() > 300);
    for f in v {
        let (epoch, off): (i64, i32) = (f[0].parse().unwrap(), f[1].parse().unwrap());
        assert_eq!(tz::format_local_datetime(epoch, off), f[2], "epoch {epoch} offset {off}");
    }
}

#[test]
fn reading_what_a_browser_sends_agrees() {
    let v = lines("parse");
    assert!(v.len() > 50);
    for f in v {
        let off: i32 = f[1].parse().unwrap();
        let got = tz::parse_local_datetime(&f[0], off);
        match f[2].as_str() {
            "none" => assert_eq!(got, None, "{:?}", f[0]),
            e => assert_eq!(got, Some(e.parse::<i64>().unwrap()), "{:?} at {off}", f[0]),
        }
    }
}

#[test]
fn month_windows_agree() {
    let v = lines("window");
    assert!(v.len() > 200);
    for f in v {
        let (y, m, off): (i64, i64, i32) = (f[0].parse().unwrap(), f[1].parse().unwrap(), f[2].parse().unwrap());
        let (from, to): (i64, i64) = (f[3].parse().unwrap(), f[4].parse().unwrap());
        assert_eq!(tz::month_window(y, m, off), (from, to), "{y}-{m} at {off}");
    }
}
