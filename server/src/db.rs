//! The data layer: SQLite through sqlx with runtime-checked queries (no database is needed to build).
//!
//! Every write is one transaction that also bumps the revision counter, so `GET /changes` can never
//! miss a change. Rules are checked here, on the merged values, so a PATCH cannot sneak past them.

use std::str::FromStr;
use std::time::Duration;

use serde::Serialize;
use sqlx::sqlite::{SqliteConnectOptions, SqliteJournalMode, SqlitePoolOptions};
use sqlx::{FromRow, SqlitePool};

use crate::error::{Error, Result};
use crate::limits;

#[derive(Clone)]
pub struct Db {
    pool: SqlitePool,
}

/// A quest, in the order and with the names of `docs/api.md`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, FromRow)]
pub struct Quest {
    pub id: i64,
    pub title: String,
    pub notes: String,
    pub difficulty: i64,
    pub due: Option<String>,
    pub state: String,
    pub done_at: Option<i64>,
    pub updated_at: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, FromRow)]
pub struct Event {
    pub id: i64,
    pub title: String,
    pub description: String,
    pub start: i64,
    pub end: i64,
    pub updated_at: i64,
}

/// Where a one-time link lands the phone.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LinkKind {
    Quest,
    Event,
    Home,
}

impl LinkKind {
    pub fn as_str(self) -> &'static str {
        match self {
            LinkKind::Quest => "quest",
            LinkKind::Event => "event",
            LinkKind::Home => "home",
        }
    }
    fn parse(s: &str) -> Option<LinkKind> {
        match s {
            "quest" => Some(LinkKind::Quest),
            "event" => Some(LinkKind::Event),
            "home" => Some(LinkKind::Home),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Session {
    pub csrf: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QuestState {
    Open,
    Done,
}

impl QuestState {
    fn as_str(self) -> &'static str {
        match self {
            QuestState::Open => "open",
            QuestState::Done => "done",
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct NewQuest {
    pub title: String,
    pub notes: Option<String>,
    pub difficulty: Option<i64>,
    pub due: Option<String>,
}

/// `Option<Option<T>>`: the outer is "was it given", the inner is the value (`None` is JSON `null`).
#[derive(Debug, Clone, Default)]
pub struct QuestPatch {
    pub title: Option<String>,
    pub notes: Option<String>,
    pub difficulty: Option<i64>,
    pub due: Option<Option<String>>,
    pub state: Option<QuestState>,
}

#[derive(Debug, Clone)]
pub struct NewEvent {
    pub title: String,
    pub description: Option<String>,
    pub start: i64,
    pub end: i64,
}

#[derive(Debug, Clone, Default)]
pub struct EventPatch {
    pub title: Option<String>,
    pub description: Option<String>,
    pub start: Option<i64>,
    pub end: Option<i64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Stats {
    pub xp: i64,
    pub open: i64,
    pub done: i64,
}

// The column lists are macros, not constants, so that every query is one static string (concat!):
// sqlx 0.9 refuses a string built at run time, and a string built at run time is how SQL injection happens.
macro_rules! quest_cols {
    () => {
        "id, title, notes, difficulty, due, state, done_at, updated_at"
    };
}
macro_rules! event_cols {
    () => {
        "id, title, description, start_ts AS start, end_ts AS end, updated_at"
    };
}

impl Db {
    /// Opens (and creates) the database file, applies the migrations.
    pub async fn connect(url: &str) -> Result<Db> {
        let opts = SqliteConnectOptions::from_str(url)?
            .create_if_missing(true)
            .journal_mode(SqliteJournalMode::Wal)
            .foreign_keys(true)
            .busy_timeout(Duration::from_secs(5));
        let pool = SqlitePoolOptions::new().max_connections(4).connect_with(opts).await?;
        Db::migrated(pool).await
    }

    /// A private in-memory database (one connection: each connection would be its own database).
    pub async fn memory() -> Result<Db> {
        let opts = SqliteConnectOptions::from_str("sqlite::memory:")?.foreign_keys(true);
        let pool = SqlitePoolOptions::new()
            .max_connections(1)
            .idle_timeout(None)
            .max_lifetime(None)
            .connect_with(opts)
            .await?;
        Db::migrated(pool).await
    }

    async fn migrated(pool: SqlitePool) -> Result<Db> {
        sqlx::migrate!("./migrations").run(&pool).await.map_err(|e| Error::Db(e.into()))?;
        Ok(Db { pool })
    }

    pub async fn rev(&self) -> Result<i64> {
        let (rev,): (i64,) = sqlx::query_as("SELECT value FROM meta WHERE key = 'rev'").fetch_one(&self.pool).await?;
        Ok(rev)
    }

    async fn bump(tx: &mut sqlx::Transaction<'_, sqlx::Sqlite>) -> Result<()> {
        sqlx::query("UPDATE meta SET value = value + 1 WHERE key = 'rev'").execute(&mut **tx).await?;
        Ok(())
    }

    // ---- quests ----

    pub async fn create_quest(&self, q: NewQuest, now: i64) -> Result<Quest> {
        limits::title(&q.title)?;
        let notes = q.notes.unwrap_or_default();
        limits::notes(&notes)?;
        let difficulty = q.difficulty.unwrap_or(limits::DIFFICULTY_MIN);
        limits::difficulty(difficulty)?;
        if let Some(d) = &q.due {
            limits::due(d)?;
        }
        let mut tx = self.pool.begin().await?;
        let id = sqlx::query("INSERT INTO quests (title, notes, difficulty, due, state, done_at, updated_at) VALUES (?, ?, ?, ?, 'open', NULL, ?)")
            .bind(&q.title)
            .bind(&notes)
            .bind(difficulty)
            .bind(&q.due)
            .bind(now)
            .execute(&mut *tx)
            .await?
            .last_insert_rowid();
        Db::bump(&mut tx).await?;
        let quest = Db::fetch_quest(&mut tx, id).await?.ok_or(Error::NotFound)?;
        tx.commit().await?;
        Ok(quest)
    }

    async fn fetch_quest(tx: &mut sqlx::Transaction<'_, sqlx::Sqlite>, id: i64) -> Result<Option<Quest>> {
        Ok(sqlx::query_as(concat!("SELECT ", quest_cols!(), " FROM quests WHERE id = ?"))
            .bind(id)
            .fetch_optional(&mut **tx)
            .await?)
    }

    pub async fn get_quest(&self, id: i64) -> Result<Quest> {
        sqlx::query_as(concat!("SELECT ", quest_cols!(), " FROM quests WHERE id = ?"))
            .bind(id)
            .fetch_optional(&self.pool)
            .await?
            .ok_or(Error::NotFound)
    }

    pub async fn update_quest(&self, id: i64, p: QuestPatch, now: i64) -> Result<Quest> {
        if p.title.is_none() && p.notes.is_none() && p.difficulty.is_none() && p.due.is_none() && p.state.is_none() {
            return Err(Error::BadRequest("nothing to change"));
        }
        let mut tx = self.pool.begin().await?;
        let cur = Db::fetch_quest(&mut tx, id).await?.ok_or(Error::NotFound)?;
        let title = p.title.unwrap_or(cur.title);
        let notes = p.notes.unwrap_or(cur.notes);
        let difficulty = p.difficulty.unwrap_or(cur.difficulty);
        let due = p.due.unwrap_or(cur.due);
        limits::title(&title)?;
        limits::notes(&notes)?;
        limits::difficulty(difficulty)?;
        if let Some(d) = &due {
            limits::due(d)?;
        }
        let (state, done_at) = match p.state {
            None => (cur.state, cur.done_at),
            Some(QuestState::Done) if cur.state == "done" => (cur.state, cur.done_at), // already done: keep when
            Some(QuestState::Done) => ("done".to_string(), Some(now)),
            Some(QuestState::Open) => (QuestState::Open.as_str().to_string(), None),
        };
        sqlx::query("UPDATE quests SET title = ?, notes = ?, difficulty = ?, due = ?, state = ?, done_at = ?, updated_at = ? WHERE id = ?")
            .bind(&title)
            .bind(&notes)
            .bind(difficulty)
            .bind(&due)
            .bind(&state)
            .bind(done_at)
            .bind(now)
            .bind(id)
            .execute(&mut *tx)
            .await?;
        Db::bump(&mut tx).await?;
        let quest = Db::fetch_quest(&mut tx, id).await?.ok_or(Error::NotFound)?;
        tx.commit().await?;
        Ok(quest)
    }

    pub async fn delete_quest(&self, id: i64) -> Result<()> {
        let mut tx = self.pool.begin().await?;
        let n = sqlx::query("DELETE FROM quests WHERE id = ?").bind(id).execute(&mut *tx).await?.rows_affected();
        if n == 0 {
            return Err(Error::NotFound);
        }
        Db::bump(&mut tx).await?;
        tx.commit().await?;
        Ok(())
    }

    /// Open quests first (by due date, the ones without a date last, then by id), then the done ones,
    /// the most recently done first. At most `limit` rows.
    pub async fn list_quests(&self, limit: i64) -> Result<Vec<Quest>> {
        Ok(sqlx::query_as(concat!(
            "SELECT ",
            quest_cols!(),
            " FROM quests ORDER BY \
             (state = 'done'), \
             CASE WHEN state = 'open' THEN due IS NULL END, \
             CASE WHEN state = 'open' THEN due END, \
             CASE WHEN state = 'done' THEN -done_at END, \
             id LIMIT ?"
        ))
        .bind(limit)
        .fetch_all(&self.pool)
        .await?)
    }

    /// All-time numbers: the XP is `difficulty * 10` of every done quest.
    pub async fn stats(&self) -> Result<Stats> {
        let (xp, open, done): (i64, i64, i64) = sqlx::query_as(
            "SELECT COALESCE(SUM(CASE WHEN state = 'done' THEN difficulty * 10 END), 0), \
                    COALESCE(SUM(state = 'open'), 0), COALESCE(SUM(state = 'done'), 0) FROM quests",
        )
        .fetch_one(&self.pool)
        .await?;
        Ok(Stats { xp, open, done })
    }

    // ---- events ----

    pub async fn create_event(&self, e: NewEvent, now: i64) -> Result<Event> {
        limits::title(&e.title)?;
        let description = e.description.unwrap_or_default();
        limits::description(&description)?;
        limits::event_times(e.start, e.end)?;
        let mut tx = self.pool.begin().await?;
        let id =
            sqlx::query("INSERT INTO events (title, description, start_ts, end_ts, updated_at) VALUES (?, ?, ?, ?, ?)")
                .bind(&e.title)
                .bind(&description)
                .bind(e.start)
                .bind(e.end)
                .bind(now)
                .execute(&mut *tx)
                .await?
                .last_insert_rowid();
        Db::bump(&mut tx).await?;
        let ev = Db::fetch_event(&mut tx, id).await?.ok_or(Error::NotFound)?;
        tx.commit().await?;
        Ok(ev)
    }

    async fn fetch_event(tx: &mut sqlx::Transaction<'_, sqlx::Sqlite>, id: i64) -> Result<Option<Event>> {
        Ok(sqlx::query_as(concat!("SELECT ", event_cols!(), " FROM events WHERE id = ?"))
            .bind(id)
            .fetch_optional(&mut **tx)
            .await?)
    }

    pub async fn get_event(&self, id: i64) -> Result<Event> {
        sqlx::query_as(concat!("SELECT ", event_cols!(), " FROM events WHERE id = ?"))
            .bind(id)
            .fetch_optional(&self.pool)
            .await?
            .ok_or(Error::NotFound)
    }

    pub async fn update_event(&self, id: i64, p: EventPatch, now: i64) -> Result<Event> {
        if p.title.is_none() && p.description.is_none() && p.start.is_none() && p.end.is_none() {
            return Err(Error::BadRequest("nothing to change"));
        }
        let mut tx = self.pool.begin().await?;
        let cur = Db::fetch_event(&mut tx, id).await?.ok_or(Error::NotFound)?;
        let title = p.title.unwrap_or(cur.title);
        let description = p.description.unwrap_or(cur.description);
        let (start, end) = (p.start.unwrap_or(cur.start), p.end.unwrap_or(cur.end));
        limits::title(&title)?;
        limits::description(&description)?;
        limits::event_times(start, end)?; // the pair as a whole, after the merge
        sqlx::query(
            "UPDATE events SET title = ?, description = ?, start_ts = ?, end_ts = ?, updated_at = ? WHERE id = ?",
        )
        .bind(&title)
        .bind(&description)
        .bind(start)
        .bind(end)
        .bind(now)
        .bind(id)
        .execute(&mut *tx)
        .await?;
        Db::bump(&mut tx).await?;
        let ev = Db::fetch_event(&mut tx, id).await?.ok_or(Error::NotFound)?;
        tx.commit().await?;
        Ok(ev)
    }

    pub async fn delete_event(&self, id: i64) -> Result<()> {
        let mut tx = self.pool.begin().await?;
        let n = sqlx::query("DELETE FROM events WHERE id = ?").bind(id).execute(&mut *tx).await?.rows_affected();
        if n == 0 {
            return Err(Error::NotFound);
        }
        Db::bump(&mut tx).await?;
        tx.commit().await?;
        Ok(())
    }

    /// The events that overlap `[from, to)`, by start then id, at most `limit` rows.
    pub async fn list_events(&self, from: i64, to: i64, limit: i64) -> Result<Vec<Event>> {
        Ok(sqlx::query_as(concat!(
            "SELECT ",
            event_cols!(),
            " FROM events WHERE start_ts < ? AND end_ts > ? ORDER BY start_ts, id LIMIT ?"
        ))
        .bind(to)
        .bind(from)
        .bind(limit)
        .fetch_all(&self.pool)
        .await?)
    }

    // ---- one-time links and sessions ----

    /// Stores the hash of a new link. At most `LINKS_MAX` can be alive: used and expired ones are swept first.
    pub async fn create_link(&self, code_hash: &[u8], kind: LinkKind, now: i64) -> Result<()> {
        let mut tx = self.pool.begin().await?;
        sqlx::query("DELETE FROM links WHERE used = 1 OR expires_at <= ?").bind(now).execute(&mut *tx).await?;
        let (alive,): (i64,) = sqlx::query_as("SELECT COUNT(*) FROM links").fetch_one(&mut *tx).await?;
        if alive >= limits::LINKS_MAX as i64 {
            return Err(Error::TooMany);
        }
        sqlx::query("INSERT INTO links (code_hash, kind, expires_at, used) VALUES (?, ?, ?, 0)")
            .bind(code_hash)
            .bind(kind.as_str())
            .bind(now + limits::LINK_TTL)
            .execute(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(())
    }

    /// Spends a link: one atomic statement, so two phones with the same code cannot both win.
    pub async fn use_link(&self, code_hash: &[u8], now: i64) -> Result<Option<LinkKind>> {
        let row: Option<(String,)> = sqlx::query_as(
            "UPDATE links SET used = 1 WHERE code_hash = ? AND used = 0 AND expires_at > ? RETURNING kind",
        )
        .bind(code_hash)
        .bind(now)
        .fetch_optional(&self.pool)
        .await?;
        Ok(row.and_then(|(k,)| LinkKind::parse(&k)))
    }

    pub async fn create_session(&self, id_hash: &[u8], csrf: &str, now: i64) -> Result<()> {
        sqlx::query("DELETE FROM sessions WHERE expires_at <= ?").bind(now).execute(&self.pool).await?;
        sqlx::query("INSERT INTO sessions (id_hash, csrf, expires_at) VALUES (?, ?, ?)")
            .bind(id_hash)
            .bind(csrf)
            .bind(now + limits::SESSION_TTL)
            .execute(&self.pool)
            .await?;
        Ok(())
    }

    pub async fn session(&self, id_hash: &[u8], now: i64) -> Result<Option<Session>> {
        let row: Option<(String,)> = sqlx::query_as("SELECT csrf FROM sessions WHERE id_hash = ? AND expires_at > ?")
            .bind(id_hash)
            .bind(now)
            .fetch_optional(&self.pool)
            .await?;
        Ok(row.map(|(csrf,)| Session { csrf }))
    }

    pub async fn delete_session(&self, id_hash: &[u8]) -> Result<()> {
        sqlx::query("DELETE FROM sessions WHERE id_hash = ?").bind(id_hash).execute(&self.pool).await?;
        Ok(())
    }

    /// Everything the two secret tables hold, as one string (for a test that looks for a code in it).
    #[doc(hidden)]
    pub async fn dump_secrets_for_tests(&self) -> String {
        let links: Vec<(Vec<u8>, String)> =
            sqlx::query_as("SELECT code_hash, kind FROM links").fetch_all(&self.pool).await.unwrap_or_default();
        let sessions: Vec<(Vec<u8>, String)> =
            sqlx::query_as("SELECT id_hash, csrf FROM sessions").fetch_all(&self.pool).await.unwrap_or_default();
        format!("{links:?} {sessions:?}")
    }

    /// Row counts, for the seed command.
    pub async fn is_empty(&self) -> Result<bool> {
        let (n,): (i64,) = sqlx::query_as("SELECT (SELECT COUNT(*) FROM quests) + (SELECT COUNT(*) FROM events)")
            .fetch_one(&self.pool)
            .await?;
        Ok(n == 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const T: i64 = 1_796_230_800; // 2026-12-02 17:00 UTC

    fn nq(title: &str) -> NewQuest {
        NewQuest { title: title.into(), ..Default::default() }
    }

    fn ne(title: &str, start: i64, end: i64) -> NewEvent {
        NewEvent { title: title.into(), description: None, start, end }
    }

    #[tokio::test]
    async fn every_write_bumps_the_revision_once_and_a_refused_one_does_not() {
        let db = Db::memory().await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 0);
        let q = db.create_quest(nq("one"), T).await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 1);
        db.update_quest(q.id, QuestPatch { title: Some("two".into()), ..Default::default() }, T).await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 2);
        let e = db.create_event(ne("ev", T, T + 60), T).await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 3);
        db.update_event(e.id, EventPatch { title: Some("ev2".into()), ..Default::default() }, T).await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 4);
        db.delete_event(e.id).await.unwrap();
        db.delete_quest(q.id).await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 6);
        // refused writes leave the counter alone
        assert!(db.create_quest(nq(""), T).await.is_err());
        assert!(db.delete_quest(999).await.is_err());
        assert!(db.update_quest(999, QuestPatch { title: Some("x".into()), ..Default::default() }, T).await.is_err());
        assert!(db.update_quest(q.id, QuestPatch::default(), T).await.is_err());
        assert!(db.create_event(ne("e", T, T), T).await.is_err());
        assert_eq!(db.rev().await.unwrap(), 6);
        // reads do not bump
        db.list_quests(48).await.unwrap();
        db.list_events(T, T + 100, 48).await.unwrap();
        db.stats().await.unwrap();
        assert_eq!(db.rev().await.unwrap(), 6);
    }

    #[tokio::test]
    async fn a_new_quest_has_the_defaults() {
        let db = Db::memory().await.unwrap();
        let q = db.create_quest(nq("Slay the inbox"), T).await.unwrap();
        assert_eq!(
            q,
            Quest {
                id: 1,
                title: "Slay the inbox".into(),
                notes: "".into(),
                difficulty: 1,
                due: None,
                state: "open".into(),
                done_at: None,
                updated_at: T
            }
        );
    }

    #[tokio::test]
    async fn patch_distinguishes_absent_null_and_a_value() {
        let db = Db::memory().await.unwrap();
        let q = db
            .create_quest(
                NewQuest {
                    title: "a".into(),
                    notes: Some("n".into()),
                    difficulty: Some(3),
                    due: Some("2026-12-24".into()),
                },
                T,
            )
            .await
            .unwrap();
        // absent: untouched
        let r =
            db.update_quest(q.id, QuestPatch { title: Some("b".into()), ..Default::default() }, T + 1).await.unwrap();
        assert_eq!(
            (r.title.as_str(), r.notes.as_str(), r.difficulty, r.due.as_deref()),
            ("b", "n", 3, Some("2026-12-24"))
        );
        assert_eq!(r.updated_at, T + 1);
        // null: cleared
        let r = db.update_quest(q.id, QuestPatch { due: Some(None), ..Default::default() }, T + 2).await.unwrap();
        assert_eq!(r.due, None);
        // a value: set
        let r = db
            .update_quest(q.id, QuestPatch { due: Some(Some("2027-01-02".into())), ..Default::default() }, T + 3)
            .await
            .unwrap();
        assert_eq!(r.due.as_deref(), Some("2027-01-02"));
        // a refused value changes nothing
        assert!(
            db.update_quest(q.id, QuestPatch { due: Some(Some("2027-02-29".into())), ..Default::default() }, T + 4)
                .await
                .is_err()
        );
        assert_eq!(db.get_quest(q.id).await.unwrap().due.as_deref(), Some("2027-01-02"));
        assert_eq!(db.get_quest(q.id).await.unwrap().updated_at, T + 3);
    }

    #[tokio::test]
    async fn done_sets_and_open_clears_done_at_and_done_again_keeps_it() {
        let db = Db::memory().await.unwrap();
        let q = db.create_quest(nq("a"), T).await.unwrap();
        let r = db
            .update_quest(q.id, QuestPatch { state: Some(QuestState::Done), ..Default::default() }, T + 10)
            .await
            .unwrap();
        assert_eq!((r.state.as_str(), r.done_at), ("done", Some(T + 10)));
        let r = db
            .update_quest(q.id, QuestPatch { state: Some(QuestState::Done), ..Default::default() }, T + 20)
            .await
            .unwrap();
        assert_eq!(r.done_at, Some(T + 10));
        let r = db
            .update_quest(q.id, QuestPatch { state: Some(QuestState::Open), ..Default::default() }, T + 30)
            .await
            .unwrap();
        assert_eq!((r.state.as_str(), r.done_at), ("open", None));
    }

    #[tokio::test]
    async fn quests_are_listed_open_first_by_due_then_done_latest_first() {
        let db = Db::memory().await.unwrap();
        let mk =
            |t: &str, due: Option<&str>| NewQuest { title: t.into(), due: due.map(String::from), ..Default::default() };
        let a = db.create_quest(mk("no date 1", None), T).await.unwrap();
        let b = db.create_quest(mk("late", Some("2026-12-31")), T).await.unwrap();
        let c = db.create_quest(mk("early", Some("2026-12-05")), T).await.unwrap();
        let d = db.create_quest(mk("no date 2", None), T).await.unwrap();
        let e = db.create_quest(mk("done first", Some("2026-01-01")), T).await.unwrap();
        let f = db.create_quest(mk("done second", None), T).await.unwrap();
        let done = |db: &Db, id, at| {
            let db = db.clone();
            async move {
                db.update_quest(id, QuestPatch { state: Some(QuestState::Done), ..Default::default() }, at)
                    .await
                    .unwrap()
            }
        };
        done(&db, e.id, T + 100).await;
        done(&db, f.id, T + 200).await;
        let ids: Vec<i64> = db.list_quests(48).await.unwrap().iter().map(|q| q.id).collect();
        assert_eq!(ids, vec![c.id, b.id, a.id, d.id, f.id, e.id]);
        assert_eq!(db.list_quests(2).await.unwrap().len(), 2);
    }

    #[tokio::test]
    async fn stats_are_all_time_and_not_limited_by_the_page() {
        let db = Db::memory().await.unwrap();
        assert_eq!(db.stats().await.unwrap(), Stats { xp: 0, open: 0, done: 0 });
        for (i, diff) in [3i64, 5, 1, 2].iter().enumerate() {
            let q = db
                .create_quest(NewQuest { title: format!("q{i}"), difficulty: Some(*diff), ..Default::default() }, T)
                .await
                .unwrap();
            if i < 3 {
                db.update_quest(q.id, QuestPatch { state: Some(QuestState::Done), ..Default::default() }, T)
                    .await
                    .unwrap();
            }
        }
        assert_eq!(db.stats().await.unwrap(), Stats { xp: 90, open: 1, done: 3 });
    }

    #[tokio::test]
    async fn events_overlap_a_half_open_window() {
        let db = Db::memory().await.unwrap();
        let (from, to) = (T, T + 1000);
        let ends_at_from = db.create_event(ne("before", T - 100, T), T).await.unwrap();
        let starts_at_to = db.create_event(ne("after", T + 1000, T + 1100), T).await.unwrap();
        let inside = db.create_event(ne("inside", T + 10, T + 20), T).await.unwrap();
        let crosses_from = db.create_event(ne("crosses start", T - 50, T + 1), T).await.unwrap();
        let crosses_to = db.create_event(ne("crosses end", T + 999, T + 5000), T).await.unwrap();
        let around = db.create_event(ne("around", T - 5000, T + 5000), T).await.unwrap();
        let got: Vec<i64> = db.list_events(from, to, 48).await.unwrap().iter().map(|e| e.id).collect();
        assert!(!got.contains(&ends_at_from.id) && !got.contains(&starts_at_to.id));
        assert_eq!(got, vec![around.id, crosses_from.id, inside.id, crosses_to.id]);
        assert_eq!(db.list_events(from, to, 2).await.unwrap().len(), 2);
    }

    #[tokio::test]
    async fn an_event_pair_is_checked_after_the_merge() {
        let db = Db::memory().await.unwrap();
        let e = db.create_event(ne("e", T, T + 3600), T).await.unwrap();
        // moving only the end before the start is refused
        assert!(db.update_event(e.id, EventPatch { end: Some(T - 1), ..Default::default() }, T).await.is_err());
        // moving only the start past the end is refused
        assert!(db.update_event(e.id, EventPatch { start: Some(T + 3600), ..Default::default() }, T).await.is_err());
        // moving both is fine
        let r = db
            .update_event(e.id, EventPatch { start: Some(T + 7200), end: Some(T + 9000), ..Default::default() }, T)
            .await
            .unwrap();
        assert_eq!((r.start, r.end), (T + 7200, T + 9000));
        // too long
        assert!(
            db.update_event(
                e.id,
                EventPatch { end: Some(T + 7200 + limits::EVENT_MAX_SECS + 1), ..Default::default() },
                T
            )
            .await
            .is_err()
        );
        assert_eq!(db.get_event(e.id).await.unwrap().end, T + 9000);
    }

    #[tokio::test]
    async fn the_database_refuses_what_the_rules_refuse_even_if_a_handler_forgot() {
        let db = Db::memory().await.unwrap();
        let long = "x".repeat(65);
        let r = sqlx::query("INSERT INTO quests (title, updated_at) VALUES (?, 0)").bind(&long).execute(&db.pool).await;
        assert!(r.is_err(), "title over 64 bytes");
        let r = sqlx::query("INSERT INTO quests (title, difficulty, updated_at) VALUES ('a', 9, 0)")
            .execute(&db.pool)
            .await;
        assert!(r.is_err(), "difficulty 9");
        let r = sqlx::query("INSERT INTO quests (title, state, updated_at) VALUES ('a', 'done', 0)")
            .execute(&db.pool)
            .await;
        assert!(r.is_err(), "done without done_at");
        let r = sqlx::query("INSERT INTO events (title, start_ts, end_ts, updated_at) VALUES ('a', 5, 5, 0)")
            .execute(&db.pool)
            .await;
        assert!(r.is_err(), "end not after start");
    }

    #[tokio::test]
    async fn a_link_works_once_and_not_after_it_expires() {
        let db = Db::memory().await.unwrap();
        let h = crate::token::hash("00000000000000000000000001");
        db.create_link(&h, LinkKind::Event, T).await.unwrap();
        assert_eq!(db.use_link(&h, T + 599).await.unwrap(), Some(LinkKind::Event));
        assert_eq!(db.use_link(&h, T + 600).await.unwrap(), None, "used");
        let h2 = crate::token::hash("00000000000000000000000002");
        db.create_link(&h2, LinkKind::Home, T).await.unwrap();
        assert_eq!(db.use_link(&h2, T + 600).await.unwrap(), None, "expired at exactly the ttl");
        assert_eq!(db.use_link(&crate::token::hash("nope"), T).await.unwrap(), None);
    }

    #[tokio::test]
    async fn at_most_six_links_are_alive_and_used_or_expired_ones_free_their_place() {
        let db = Db::memory().await.unwrap();
        let hs: Vec<Vec<u8>> = (0..8).map(|i| crate::token::hash(&format!("{i:026}"))).collect();
        for h in &hs[..6] {
            db.create_link(h, LinkKind::Quest, T).await.unwrap();
        }
        assert!(matches!(db.create_link(&hs[6], LinkKind::Quest, T).await, Err(Error::TooMany)));
        db.use_link(&hs[0], T + 1).await.unwrap(); // spending one frees its place
        db.create_link(&hs[6], LinkKind::Quest, T + 2).await.unwrap();
        assert!(matches!(db.create_link(&hs[7], LinkKind::Quest, T + 3).await, Err(Error::TooMany)));
        db.create_link(&hs[7], LinkKind::Quest, T + 601).await.unwrap(); // the first six have expired
    }

    #[tokio::test]
    async fn a_session_lives_for_thirty_minutes_and_can_be_ended() {
        let db = Db::memory().await.unwrap();
        let h = crate::token::hash("00000000000000000000000009");
        db.create_session(&h, "00000000000000000000000008", T).await.unwrap();
        assert_eq!(db.session(&h, T + 1799).await.unwrap().unwrap().csrf, "00000000000000000000000008");
        assert!(db.session(&h, T + 1800).await.unwrap().is_none());
        db.delete_session(&h).await.unwrap();
        assert!(db.session(&h, T).await.unwrap().is_none());
    }

    #[tokio::test]
    async fn ids_are_never_reused() {
        let db = Db::memory().await.unwrap();
        let a = db.create_quest(nq("a"), T).await.unwrap();
        db.delete_quest(a.id).await.unwrap();
        let b = db.create_quest(nq("b"), T).await.unwrap();
        assert!(b.id > a.id);
    }
}
