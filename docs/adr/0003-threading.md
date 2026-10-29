# ADR 0003: Threading

- **Status:** Accepted
- **Date:** 2026-10-29
- **Task:** 2.1

## Context

Typing must never stutter (Plan §10: < 16 ms per keystroke), yet the app saves
continuously (autosave 500 ms after typing stops), searches 10,000 notes, and
later syncs in the background (Plan §6, §9). Disk and SQLite calls can take
milliseconds, or far longer on a busy or slow disk. Qt widgets may only be used
from the main (UI) thread.

A SQLite connection is safest used from one thread at a time, and each
connection must be keyed and configured on open (SQLCipher key, WAL, foreign
keys, cached statements), so connections should be few and long-lived.

## Options

- **A: Database calls on the UI thread.** Simplest, but any slow write freezes
  the window. Ruled out by the typing budget.
- **B: A thread pool with a connection per thread** (WAL allows concurrent
  readers). Parallel reads, but several connections to key and configure, harder ordering
  guarantees (a save and a following read can run out of order), and every
  repository must be thread-safe.
- **C: One dedicated database thread** that owns the only connection and runs
  jobs from a queue, in the order they were posted.

## Decision

**Option C.**

- `DbThread` (task 2.3) owns the single `Database`. All SQL runs there, and
  nowhere else.
- Services (`NoteService`, `FolderService`, ...) expose asynchronous calls
  that post a job and return a `QFuture<T>` (fulfilled with a `QPromise` on the
  DB thread). Callers continue with `.then(context, ...)`, which runs the
  continuation on the context object's thread (the UI thread for widgets).
- Services announce changes with Qt signals (`noteCreated`, `noteUpdated`, ...)
  delivered as queued connections, so the UI updates on its own thread.
- Jobs from one caller run in order (FIFO); a save followed by a load always
  sees the save.
- Results and errors are plain values (`DbError`, ADR 0002); no exceptions
  cross threads.
- **The UI thread never waits on a `QFuture`** (`result()`, `waitForFinished()`)
  except at shutdown, where pending saves are flushed before exit.
- The sync engine runs on its own thread (Plan §6) and reaches the database
  only through the same services, so it shares the queue.
- Database rows are never handed to widgets by pointer; jobs copy results into
  value types (`Note`, `Folder`, task 2.2).

## Consequences

- No locking in repositories: only one thread touches them.
- Reads queue behind writes. Each job is short (single-row writes, paged list
  loads of 200 rows), so this stays within budget; TC-PERF-005 (search under
  30 ms) and TC-PERF-008 (typing during a 1,000-note sync) will show if it
  doesn't. If it does, add a second, read-only connection for search, recorded
  in a new ADR.
- Long jobs (sync batches, export) must be split into small jobs so they don't
  block saves.
- Shutdown order matters: stop the sync engine, drain the DB queue, close the
  database, then let the event loop exit.
- Tests can run services against a real `DbThread` on a temp database (no
  mocks needed); `TC-STO-006` stresses the queue from four threads, and is run
  under ThreadSanitizer once by hand.

## Verification

- `TC-STO-006`: 1,000 jobs from 4 threads all run, in order per caller, without data races.
- Task 2.9: saves run on the DB thread; the UI never waits.
- `TC-PERF-002`, `TC-PERF-008`: typing stays under 16 ms per keystroke, also during sync.
