//! Calendar arithmetic in integers (Howard Hinnant's algorithms) and the local time of the phone pages.
//! Times in the database are UTC epoch seconds; the pages show and read them in one fixed offset.

const DAY: i64 = 86_400;

/// Days since 1970-01-01 of a civil date.
pub fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = y.div_euclid(400);
    let yoe = y.rem_euclid(400);
    let mp = (m + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// The civil date of a day count.
pub fn civil_from_days(z: i64) -> (i64, i64, i64) {
    let z = z + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    (if m <= 2 { y + 1 } else { y }, m, d)
}

/// `YYYY-MM-DD` for `days` days after the UTC day of `now`.
pub fn date_after(now: i64, days: i64) -> String {
    let (y, m, d) = civil_from_days(now.div_euclid(DAY) + days);
    format!("{y:04}-{m:02}-{d:02}")
}

pub fn days_in_month(y: i64, m: i64) -> i64 {
    match m {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        _ if (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 => 29,
        _ => 28,
    }
}

fn num(b: &[u8]) -> Option<i64> {
    if b.is_empty() || !b.iter().all(u8::is_ascii_digit) {
        return None;
    }
    Some(b.iter().fold(0i64, |a, d| a * 10 + i64::from(d - b'0')))
}

/// `YYYY-MM-DDTHH:MM` or `YYYY-MM-DDTHH:MM:SS` (what `<input type="datetime-local">` sends), read as local
/// time in the zone `offset_min` minutes east of UTC; returns UTC epoch seconds.
pub fn parse_local_datetime(s: &str, offset_min: i32) -> Option<i64> {
    let b = s.as_bytes();
    if !(b.len() == 16 || b.len() == 19) || b[4] != b'-' || b[7] != b'-' || b[10] != b'T' || b[13] != b':' {
        return None;
    }
    let (y, m, d) = (num(&b[0..4])?, num(&b[5..7])?, num(&b[8..10])?);
    let (hh, mm) = (num(&b[11..13])?, num(&b[14..16])?);
    let ss = if b.len() == 19 {
        if b[16] != b':' {
            return None;
        }
        num(&b[17..19])?
    } else {
        0
    };
    if !(1..=12).contains(&m) || d < 1 || d > days_in_month(y, m) || hh > 23 || mm > 59 || ss > 59 {
        return None;
    }
    Some(days_from_civil(y, m, d) * DAY + hh * 3600 + mm * 60 + ss - i64::from(offset_min) * 60)
}

/// The inverse for a form: `YYYY-MM-DDTHH:MM` in the zone.
pub fn format_local_datetime(epoch: i64, offset_min: i32) -> String {
    let local = epoch + i64::from(offset_min) * 60;
    let (y, m, d) = civil_from_days(local.div_euclid(DAY));
    let s = local.rem_euclid(DAY);
    format!("{y:04}-{m:02}-{d:02}T{:02}:{:02}", s / 3600, s % 3600 / 60)
}

/// `YYYY-MM-DD HH:MM` for a page, in the zone.
pub fn display(epoch: i64, offset_min: i32) -> String {
    format_local_datetime(epoch, offset_min).replace('T', " ")
}

/// `YYYY-MM` -> (year, month), 2000..=2099.
pub fn parse_month(s: &str) -> Option<(i64, i64)> {
    let b = s.as_bytes();
    if b.len() != 7 || b[4] != b'-' {
        return None;
    }
    let (y, m) = (num(&b[0..4])?, num(&b[5..7])?);
    ((2000..=2099).contains(&y) && (1..=12).contains(&m)).then_some((y, m))
}

/// The month of an instant, in the zone.
pub fn month_of(epoch: i64, offset_min: i32) -> (i64, i64) {
    let (y, m, _) = civil_from_days((epoch + i64::from(offset_min) * 60).div_euclid(DAY));
    (y, m)
}

/// The UTC window `[from, to)` of a local month.
pub fn month_window(y: i64, m: i64, offset_min: i32) -> (i64, i64) {
    let off = i64::from(offset_min) * 60;
    let from = days_from_civil(y, m, 1) * DAY - off;
    let (ny, nm) = if m == 12 { (y + 1, 1) } else { (y, m + 1) };
    (from, days_from_civil(ny, nm, 1) * DAY - off)
}

pub fn previous_month(y: i64, m: i64) -> (i64, i64) {
    if m == 1 { (y - 1, 12) } else { (y, m - 1) }
}

pub fn next_month(y: i64, m: i64) -> (i64, i64) {
    if m == 12 { (y + 1, 1) } else { (y, m + 1) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn civil_dates_round_trip_over_a_long_range() {
        for z in (-800..60_000).step_by(1) {
            let (y, m, d) = civil_from_days(z);
            assert_eq!(days_from_civil(y, m, d), z, "{y}-{m}-{d}");
            assert!((1..=12).contains(&m) && d >= 1 && d <= days_in_month(y, m));
        }
        assert_eq!(days_from_civil(1970, 1, 1), 0);
        assert_eq!(days_from_civil(2000, 1, 1), 10_957);
        assert_eq!(civil_from_days(10_957), (2000, 1, 1));
    }

    #[test]
    fn leap_years_and_month_lengths() {
        assert_eq!(days_in_month(2024, 2), 29);
        assert_eq!(days_in_month(2100, 2), 28);
        assert_eq!(days_in_month(2000, 2), 29);
        assert_eq!(days_in_month(2026, 12), 31);
        assert_eq!(days_in_month(2026, 4), 30);
    }

    #[test]
    fn date_after_crosses_month_and_year_ends() {
        let t = 1_796_230_800; // 2026-12-02 17:00 UTC
        assert_eq!(date_after(t, 0), "2026-12-02");
        assert_eq!(date_after(t, 30), "2027-01-01");
        assert_eq!(date_after(1_709_164_800, 1), "2024-03-01");
    }

    #[test]
    fn local_times_convert_both_ways_and_cross_midnight() {
        // 23:30 on 2026-12-24 in UTC-3 is 02:30 UTC on the 25th
        let utc = parse_local_datetime("2026-12-24T23:30", -180).unwrap();
        assert_eq!(utc, days_from_civil(2026, 12, 25) * DAY + 2 * 3600 + 30 * 60);
        assert_eq!(format_local_datetime(utc, -180), "2026-12-24T23:30");
        assert_eq!(format_local_datetime(utc, 0), "2026-12-25T02:30");
        // 00:15 in UTC+5:30 is 18:45 UTC on the previous day
        let utc = parse_local_datetime("2027-01-01T00:15", 330).unwrap();
        assert_eq!(format_local_datetime(utc, 0), "2026-12-31T18:45");
        assert_eq!(display(utc, 330), "2027-01-01 00:15");
        // seconds are accepted and ignored by the form output
        assert_eq!(
            parse_local_datetime("2026-12-24T23:30:15", 0).unwrap()
                - parse_local_datetime("2026-12-24T23:30", 0).unwrap(),
            15
        );
    }

    #[test]
    fn a_datetime_must_be_exactly_what_the_browser_sends() {
        for bad in [
            "",
            "2026-12-24",
            "2026-12-24 23:30",
            "2026-12-24T23",
            "2026-12-24T24:00",
            "2026-12-24T23:60",
            "2026-13-01T10:00",
            "2026-02-29T10:00",
            "2026-12-24T23:30:60",
            "2026-12-24T23:30:",
            "2026-12-24T23:30Z",
            "26-12-24T23:30",
            "2026/12/24T23:30",
            "2026-12-24T2３:30",
            "2026-12-24t23:30",
            " 2026-12-24T23:30",
            "2026-12-24T23:30 ",
            "-026-12-24T23:30",
        ] {
            assert!(parse_local_datetime(bad, 0).is_none(), "{bad:?}");
        }
        assert!(parse_local_datetime("2028-02-29T00:00", 0).is_some());
    }

    #[test]
    fn months_parse_and_their_windows_follow_the_zone() {
        assert_eq!(parse_month("2026-12"), Some((2026, 12)));
        for bad in ["", "2026-13", "2026-00", "1999-12", "2100-01", "2026-1", "202612", "2026-12-01", "abcd-ef"] {
            assert_eq!(parse_month(bad), None, "{bad:?}");
        }
        let (from, to) = month_window(2026, 12, 0);
        assert_eq!(from, days_from_civil(2026, 12, 1) * DAY);
        assert_eq!(to - from, 31 * DAY);
        // in UTC-3 the local month starts three hours later in UTC
        let (from3, to3) = month_window(2026, 12, -180);
        assert_eq!(from3, from + 3 * 3600);
        assert_eq!(to3, to + 3 * 3600);
        assert_eq!(month_window(2026, 2, 0).1 - month_window(2026, 2, 0).0, 28 * DAY);
        assert_eq!(month_of(from3, -180), (2026, 12));
        assert_eq!(month_of(from3 - 1, -180), (2026, 11));
        assert_eq!(next_month(2026, 12), (2027, 1));
        assert_eq!(previous_month(2027, 1), (2026, 12));
    }
}
