# ADR 0002: Database access

- **Status:** Accepted
- **Date:** 2026-10-29
- **Task:** 2.1

## Context

Notes, folders, attachments and the search index live in one SQLite database
(Plan §7). From Phase 4 that database is encrypted with SQLCipher, keyed by a
key from the GNOME Keyring (Plan §8.3). The storage code is written in Phase 2,
before encryption exists.

Checked on 2026-10-29 in a clean `ubuntu:26.04` container:

| Package | Links against |
|---|---|
| `libqt6sql6-sqlite` (Qt's `QSQLITE` driver, 6.10.2) | `libsqlite3-0`: the **system SQLite**, no encryption |
| `libsqlcipher-dev` 4.13.0 | its own SQLite 3.51.2 with the SQLCipher codec; FTS5, JSON and WAL all work |

Qt ships no SQLCipher driver. A `QSqlDatabase` would open the database through
plain SQLite, which cannot read or write an encrypted file.

## Options

- **A: Qt SQL (`QSqlDatabase`, `QSQLITE`) now, switch later.** Familiar API, but
  everything in `src/storage/` gets rewritten in Phase 4, after it has tests and
  callers depending on its behaviour.
- **B: Qt SQL with a custom-built SQLCipher driver plugin.** Means building and
  shipping a Qt plugin against matching Qt private headers for every Qt update,
  in the `.deb` and the Flatpak. Qt SQL also hides SQLite features we need
  (explicit statement reuse, `sqlite3_changes`, extended error codes).
- **C: The SQLite C API through a thin C++ wrapper, linked against SQLCipher
  from day one.** The database stays unkeyed until Phase 4, when opening it
  gains a `PRAGMA key` and nothing else changes.

## Decision

**Option C.**

- `src/storage/` calls the SQLite C API (`sqlite3_*`) from SQLCipher's
  `<sqlite3.h>` (already set up in `cmake/Dependencies.cmake`). Plain SQLite
  must never be linked: `tst_smoke` checks `PRAGMA cipher_version`.
- A small wrapper (task 2.3) owns all raw handles: `Database`, a prepared
  `Statement` with typed `bind()`/`column()`, a statement cache, and an RAII
  `Transaction`. No other code calls `sqlite3_*` directly.
- Errors come back as values (`DbError` with the SQLite result code and
  message), not exceptions, so they can cross threads (ADR 0003).
- Every value goes through bound parameters. SQL text is never built from user
  input.
- On open: `PRAGMA journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`.
- The Qt SQL module is not used anywhere in the app.

## Consequences

- The Phase 4 change is small: key the connection right after opening it (raw
  key form, task 4.3), and migrate an existing unencrypted database once
  (`sqlcipher_export`, task 4.4).
- We own a little more code (the wrapper) and must free every handle; the
  wrapper uses RAII so callers can't leak, and ASan runs on every CI build.
- FTS5, JSON functions and WAL come from the SQLCipher build, so Ubuntu's
  `libsqlcipher` must keep them enabled. They are covered by tests from task
  2.3 (WAL) and Phase 5 (FTS5); a distro change that drops them fails CI.
- The Flatpak bundles its own SQLCipher (ADR 0001), built with the same options.

## Verification

- `tst_smoke`: `PRAGMA cipher_version` returns a version (the binary uses SQLCipher).
- `TC-STO-002`: a new database is in WAL mode with foreign keys on.
- `TC-STO-004`: SQL injection strings are stored literally.
- `TC-STO-005`: invalid SQL returns a `DbError`, no crash.
- Task 2.3 is done only when "no Qt SQL module used" holds.
