-- The database repeats the rules of src/limits.rs as CHECK constraints: the application validates first,
-- and this is the second line (a bug in a handler cannot store a row the device could not read).

CREATE TABLE quests (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT    NOT NULL CHECK (length(CAST(title AS BLOB)) BETWEEN 1 AND 64),
    notes      TEXT    NOT NULL DEFAULT '' CHECK (length(CAST(notes AS BLOB)) <= 256),
    difficulty INTEGER NOT NULL DEFAULT 1 CHECK (difficulty BETWEEN 1 AND 5),
    due        TEXT    CHECK (due IS NULL OR (length(due) = 10 AND due GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]')),
    state      TEXT    NOT NULL DEFAULT 'open' CHECK (state IN ('open', 'done')),
    done_at    INTEGER,
    updated_at INTEGER NOT NULL,
    CHECK ((state = 'done') = (done_at IS NOT NULL))
);

CREATE INDEX quests_by_state ON quests (state, due);

-- "start" and "end" are SQL keywords, hence the _ts names; the API calls them start and end.
CREATE TABLE events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    title       TEXT    NOT NULL CHECK (length(CAST(title AS BLOB)) BETWEEN 1 AND 64),
    description TEXT    NOT NULL DEFAULT '' CHECK (length(CAST(description AS BLOB)) <= 256),
    start_ts    INTEGER NOT NULL,
    end_ts      INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL,
    CHECK (end_ts > start_ts)
);

CREATE INDEX events_by_time ON events (start_ts, end_ts);

-- The revision counter: goes up by one in the same transaction as every write.
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value INTEGER NOT NULL
);

INSERT INTO meta (key, value) VALUES ('rev', 0);
