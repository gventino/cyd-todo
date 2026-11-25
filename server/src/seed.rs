//! Demo data for `cyd-todo-server seed`: a few quests and a week of events, relative to now.

use crate::db::{Db, NewEvent, NewQuest, QuestPatch, QuestState};
use crate::error::Result;
use crate::timeutil::date_after;

const DAY: i64 = 86_400;

/// Adds the demo rows. The caller decides whether the database may already hold data.
pub async fn seed(db: &Db, now: i64) -> Result<()> {
    let quests = [
        ("Slay the inbox", "Zero unread. Coffee is allowed.", 3, Some(2)),
        ("Visit the dentist", "Ask about the crown.", 2, Some(5)),
        ("Pay the guild fees", "", 1, Some(1)),
        ("Clean the armory", "Garage, second shelf.", 4, None),
        ("Write the dragon report", "Two pages, with a map.", 5, Some(9)),
        ("Water the mandrake", "", 1, None),
    ];
    for (i, (title, notes, difficulty, due_in)) in quests.iter().enumerate() {
        let q = db
            .create_quest(
                NewQuest {
                    title: (*title).into(),
                    notes: Some((*notes).into()),
                    difficulty: Some(*difficulty),
                    due: due_in.map(|d| date_after(now, d)),
                },
                now,
            )
            .await?;
        if i == 2 || i == 5 {
            db.update_quest(q.id, QuestPatch { state: Some(QuestState::Done), ..Default::default() }, now).await?;
        }
    }
    let day_start = now - now.rem_euclid(DAY);
    let events = [
        ("Dentist", "Bring the X-rays.", 1, 10 * 3600, 45 * 60),
        ("Guild meeting", "Quarterly. Bring the ledger.", 2, 18 * 3600, 2 * 3600),
        ("Moonlight market", "", 3, 20 * 3600, 3 * 3600),
        ("Night watch", "Until the small hours.", 4, 22 * 3600, 6 * 3600),
    ];
    for (title, description, day, at, length) in events {
        let start = day_start + day * DAY + at;
        db.create_event(
            NewEvent { title: title.into(), description: Some(description.into()), start, end: start + length },
            now,
        )
        .await?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn the_demo_data_is_valid_and_the_numbers_add_up() {
        let db = Db::memory().await.unwrap();
        assert!(db.is_empty().await.unwrap());
        seed(&db, 1_796_230_800).await.unwrap();
        assert!(!db.is_empty().await.unwrap());
        let s = db.stats().await.unwrap();
        assert_eq!((s.open, s.done, s.xp), (4, 2, 20));
        let ev = db.list_events(1_796_230_800 - 86_400, 1_796_230_800 + 10 * 86_400, 48).await.unwrap();
        assert_eq!(ev.len(), 4);
    }
}
