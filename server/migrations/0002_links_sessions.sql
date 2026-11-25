-- One-time links (the QR code on the board) and the sessions they are traded for. Codes and session ids are
-- 128 random bits written in base32; only their SHA-256 is stored, so a copy of the database holds no
-- usable link and no usable cookie.

CREATE TABLE links (
    code_hash  BLOB    PRIMARY KEY CHECK (length(code_hash) = 32),
    kind       TEXT    NOT NULL CHECK (kind IN ('quest', 'event', 'home')),
    expires_at INTEGER NOT NULL,
    used       INTEGER NOT NULL DEFAULT 0 CHECK (used IN (0, 1))
);

CREATE TABLE sessions (
    id_hash    BLOB    PRIMARY KEY CHECK (length(id_hash) = 32),
    csrf       TEXT    NOT NULL CHECK (length(csrf) = 26),
    expires_at INTEGER NOT NULL
);
