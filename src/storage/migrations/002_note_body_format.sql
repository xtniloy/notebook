-- Schema version 2: note bodies in our own format (task 3.1, ADR 0006).
--
-- `body` holds the note in the encoding named by `format_version`:
--   1 = Qt HTML, as the Phase 2 plain editor saved it (every existing row);
--   2 = the note format's JSON (editor/NoteFormat), written from now on.
-- The editor reads both; a body is rewritten in the current format the next
-- time the note is saved.

ALTER TABLE notes RENAME COLUMN body_html TO body;
ALTER TABLE notes ADD COLUMN format_version INTEGER NOT NULL DEFAULT 1;
