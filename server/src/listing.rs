//! Bodies of lists. The device reads at most 16 KB, and the worst item (a title and notes made of
//! characters that must be escaped) is about 740 bytes, so 48 items can be more than that. The size
//! is therefore decided while the body is built: items are added until the next one would pass the
//! soft cap, and `"truncated": true` says that some were left out (`docs/api.md`).

use serde::Serialize;

use crate::limits::{LIST_MAX, RESPONSE_SOFT_CAP};

/// `{head, "truncated": <bool>, "<key>": [ items ]}`. `head` is a fragment of JSON members without
/// the braces (`"rev":41,"xp":270`). `more_exist` says the query found more rows than it was asked for.
pub fn page<T: Serialize>(head: &str, key: &str, items: &[T], more_exist: bool) -> Vec<u8> {
    // what the envelope itself costs, with room for the longest "truncated" value
    let overhead = head.len() + key.len() + 40;
    let mut array: Vec<u8> = Vec::new();
    let mut truncated = more_exist;
    for (count, item) in items.iter().enumerate() {
        let Ok(one) = serde_json::to_vec(item) else {
            truncated = true;
            break;
        };
        let comma = usize::from(count > 0);
        if count >= LIST_MAX || overhead + array.len() + comma + one.len() > RESPONSE_SOFT_CAP {
            truncated = true;
            break;
        }
        if count > 0 {
            array.push(b',');
        }
        array.extend_from_slice(&one);
    }
    let mut out = Vec::with_capacity(overhead + array.len());
    out.push(b'{');
    out.extend_from_slice(head.as_bytes());
    out.extend_from_slice(if head.is_empty() { b"\"truncated\":" } else { b",\"truncated\":" });
    out.extend_from_slice(if truncated { b"true" } else { b"false" });
    out.extend_from_slice(b",\"");
    out.extend_from_slice(key.as_bytes());
    out.extend_from_slice(b"\":[");
    out.extend_from_slice(&array);
    out.extend_from_slice(b"]}");
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::Quest;

    fn quest(i: i64, title: &str, notes: &str) -> Quest {
        Quest {
            id: i,
            title: title.into(),
            notes: notes.into(),
            difficulty: 5,
            due: Some("2026-12-31".into()),
            state: "open".into(),
            done_at: None,
            updated_at: 1_796_230_800,
        }
    }

    #[test]
    fn an_empty_list_is_valid_json() {
        let body = page::<Quest>(r#""rev":1"#, "quests", &[], false);
        let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(v["rev"], 1);
        assert_eq!(v["truncated"], false);
        assert_eq!(v["quests"].as_array().unwrap().len(), 0);
    }

    #[test]
    fn the_flag_follows_the_query_when_nothing_is_cut() {
        let items = vec![quest(1, "a", "b")];
        let body = page(r#""rev":1"#, "quests", &items, true);
        let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(v["truncated"], true);
        assert_eq!(v["quests"].as_array().unwrap().len(), 1);
    }

    #[test]
    fn normal_items_stop_at_48() {
        let items: Vec<Quest> = (0..60).map(|i| quest(i, "short", "")).collect();
        let body = page("", "quests", &items, false);
        let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(v["quests"].as_array().unwrap().len(), 48);
        assert_eq!(v["truncated"], true);
        assert!(body.len() < RESPONSE_SOFT_CAP);
    }

    #[test]
    fn hostile_items_stop_at_the_byte_cap_and_the_body_is_still_valid_json() {
        // 64 bytes of quote characters become 128 in the title, 256 newlines 512 in the notes
        let title = "\"".repeat(64);
        let notes = "\n".repeat(256);
        let items: Vec<Quest> = (0..48).map(|i| quest(i, &title, &notes)).collect();
        let body = page(r#""rev":123456,"xp":99999,"open":48,"done":0"#, "quests", &items, false);
        assert!(body.len() <= RESPONSE_SOFT_CAP, "{} bytes", body.len());
        assert!(body.len() < 16 * 1024);
        let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
        let n = v["quests"].as_array().unwrap().len();
        assert!(n > 10 && n < 48, "{n} items");
        assert_eq!(v["truncated"], true);
        assert_eq!(v["quests"][0]["title"], title);
    }
}
