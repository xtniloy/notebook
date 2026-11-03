-- Schema version 3: note bodies are always the note format's JSON (ADR 0006).
--
-- Migration 002 kept Phase 2 bodies as Qt HTML, marked by format_version 1,
-- and the editor read both. Those notes were plain text, which body_text
-- still holds, so their HTML is dropped instead of converted: the editor shows
-- body_text when a note has no body, and the next save writes JSON. With one
-- encoding left, format_version goes too; a later JSON version is marked
-- inside the JSON ("v"), not in a column.

UPDATE notes SET body = NULL WHERE format_version = 1;
ALTER TABLE notes DROP COLUMN format_version;
