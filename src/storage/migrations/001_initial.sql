-- Schema version 1: Plan §7.
--
-- Conventions: times are UTC milliseconds since the epoch (ADR 0004); `id` is
-- local only and `uuid` is the identity across devices (ADR 0005).
--
-- Not here yet: `notes_fts`. An external-content FTS5 table needs triggers to
-- stay in sync with `notes`; both arrive together in the Phase 5 migration
-- (task 5.1).
--
-- Rules for every migration: never edit a migration that has shipped; add a new
-- numbered file instead. The Migrator runs each file in one transaction and
-- records the version itself, so files don't touch meta.schema_version.

-- Folders (nested via parent_id)
CREATE TABLE folders (
    id           INTEGER PRIMARY KEY,
    uuid         TEXT UNIQUE NOT NULL,
    parent_id    INTEGER REFERENCES folders(id),
    name         TEXT NOT NULL,
    sort_order   INTEGER DEFAULT 0,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,                 -- tombstone for sync
    version      INTEGER DEFAULT 1,
    sync_state   INTEGER DEFAULT 0        -- 0 = dirty, 1 = synced
);

-- Notes
CREATE TABLE notes (
    id            INTEGER PRIMARY KEY,
    uuid          TEXT UNIQUE NOT NULL,
    folder_id     INTEGER REFERENCES folders(id),
    title         TEXT NOT NULL DEFAULT '',
    snippet       TEXT,                   -- preview line (NULL if locked)
    body_html     TEXT,                   -- NULL if locked
    body_text     TEXT,                   -- for FTS (NULL if locked)
    is_pinned     INTEGER DEFAULT 0,
    is_locked     INTEGER DEFAULT 0,
    locked_blob   BLOB,                   -- encrypted body when locked (Phase 6)
    locked_nonce  BLOB,
    wrapped_key   BLOB,                   -- per-note key, wrapped by lock key
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL,
    trashed_at    INTEGER,                -- Recently Deleted
    deleted_at    INTEGER,                -- permanent tombstone for sync
    version       INTEGER DEFAULT 1,
    sync_state    INTEGER DEFAULT 0
);

-- Attachments (images, files)
CREATE TABLE attachments (
    id          INTEGER PRIMARY KEY,
    uuid        TEXT UNIQUE NOT NULL,
    note_id     INTEGER REFERENCES notes(id) ON DELETE CASCADE,
    file_name   TEXT,
    mime_type   TEXT,
    size_bytes  INTEGER,
    sha256      TEXT,
    data        BLOB,                     -- encrypted separately if note is locked
    thumbnail   BLOB,
    created_at  INTEGER NOT NULL
);

-- Tags. note_tags also references both sides (not in Plan §7), so deleting a
-- note or tag can't leave orphaned rows.
CREATE TABLE tags (
    id    INTEGER PRIMARY KEY,
    name  TEXT UNIQUE NOT NULL
);

CREATE TABLE note_tags (
    note_id  INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
    tag_id   INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (note_id, tag_id)
);

-- App settings, sync cursor and meta.schema_version
CREATE TABLE meta (
    key    TEXT PRIMARY KEY,
    value  TEXT
);

CREATE INDEX idx_notes_folder  ON notes(folder_id, updated_at DESC);
CREATE INDEX idx_notes_pinned  ON notes(is_pinned, updated_at DESC);
CREATE INDEX idx_notes_sync    ON notes(sync_state);
