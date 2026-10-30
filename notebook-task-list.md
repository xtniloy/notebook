# Task List: Secure Notes App for Ubuntu

> Companion to [`notebook-project-plan.md`](notebook-project-plan.md). Section references like **(Plan §8.4)** point to that document.
> Every task lists its **subtasks**, its **test cases**, and a **Done when** line. A task is only complete when its tests exist, pass in CI, and are clean under ASan/UBSan.

---

## Table of Contents

- [How to Use This List](#how-to-use-this-list)
- [Test Strategy (applies to every phase)](#test-strategy-applies-to-every-phase)
- [Phase 0: Learning & Setup (Weeks 1–3)](#phase-0-learning--setup-weeks-13)
- [Phase 1: Skeleton UI (Weeks 4–6)](#phase-1-skeleton-ui-weeks-46)
- [Phase 2: Storage MVP (Weeks 7–9)](#phase-2-storage-mvp-weeks-79)
- [Phase 3: Rich Editor (Weeks 10–15)](#phase-3-rich-editor-weeks-1015)
- [Phase 4: Encryption at Rest (Weeks 16–18)](#phase-4-encryption-at-rest-weeks-1618)
- [Phase 5: Search (Weeks 19–20)](#phase-5-search-weeks-1920)
- [Phase 6: Locked Notes (Weeks 21–23)](#phase-6-locked-notes-weeks-2123)
- [🎯 MVP Release: Private Beta (Week 24)](#-mvp-release-private-beta-week-24)
- [Phase 7: Editor P1 Features (Weeks 25–29)](#phase-7-editor-p1-features-weeks-2529)
- [Phase 8: Gallery, Smart Folders & Export (Weeks 30–31)](#phase-8-gallery-smart-folders--export-weeks-3031)
- [Phase 9: Google Sync (Weeks 32–38)](#phase-9-google-sync-weeks-3238)
- [Phase 10: Hardening (Weeks 39–42)](#phase-10-hardening-weeks-3942)
- [Phase 11: Packaging & Launch Prep (Weeks 43–44)](#phase-11-packaging--launch-prep-weeks-4344)
- [🚀 v1.0 Public Release (Week 45)](#-v10-public-release-week-45)
- [Backlog: P2 (after v1.0)](#backlog-p2-after-v10)
- [Feature → Task Traceability](#feature--task-traceability)

---

## How to Use This List

**Task IDs:** `<phase>.<task>`, e.g. `3.6` = Phase 3, task 6. Subtasks are checkboxes, so you can tick them off in git.

**Sizes**

| Size | Effort | Rule |
|---|---|---|
| **S** | ≤ ½ day | — |
| **M** | 1–2 days | — |
| **L** | 3–5 days | Must be broken into S/M subtasks (already done below) |

**Test case IDs:** `TC-<AREA>-<NNN>` with a type tag:

| Tag | Type | Where |
|---|---|---|
| **(U)** | Unit test | `tests/unit/` |
| **(I)** | Integration test (real DB, mock network) | `tests/integration/` |
| **(UI)** | Widget test with Qt Test (`QTest::keyClick`, `QSignalSpy`) | `tests/ui/` |
| **(P)** | Performance benchmark | `tests/bench/` |
| **(F)** | Fuzz target | `tests/fuzz/` |
| **(M)** | Manual check (on the release QA checklist) | `docs/qa-checklist.md` |

**Area codes:** `ENV` setup · `BLD` build/CI · `UI` interface · `STO` storage · `MIG` migrations · `EDT` editor · `CRY` crypto · `KEY` keyring · `ENC` encrypted DB · `SRC` search · `LCK` locked notes · `OAU` OAuth · `DRV` Drive client · `SYN` sync · `EXP` export · `PERF` performance · `SEC` security · `PKG` packaging.

**Workflow per task**
1. Write the test cases first (or at the same time). They should fail.
2. Implement until they pass.
3. Run the `debug-asan` preset locally.
4. Open a PR. CI must be green before merging.
5. Tick the checkboxes.

---

## Test Strategy (applies to every phase)

| Rule | Detail |
|---|---|
| **Framework** | **Qt Test** for everything (unit, UI, benchmarks via `QBENCHMARK`). It handles widgets and signals natively and runs under CTest. |
| **Layout** | One test executable per class/module: `tests/unit/storage/tst_noterepository.cpp`, etc. |
| **CTest labels** | `unit`, `integration`, `ui`, `bench`, `slow`. CI runs `unit integration ui` on every push and `bench` nightly. |
| **Headless** | All UI tests run with `QT_QPA_PLATFORM=offscreen`. |
| **Isolation** | Each test gets a fresh temp dir (`QTemporaryDir`) and its own DB. No test touches `~/.local/share` or the real keyring. |
| **Fakes** | `FakeClock` (time travel for 30-day purge, auto-relock), `InMemoryKeyStore` (instead of GNOME Keyring), `MockDriveServer` (instead of Google). Code depends on interfaces (`IClock`, `IKeyStore`, `IDriveApi`) so the fakes can be swapped in. |
| **Models** | Every `QAbstractItemModel` subclass is tested with **`QAbstractItemModelTester`**, which catches most model bugs automatically. |
| **Bugs** | Every bug fix starts with a failing regression test named after the issue (`TC-REG-<issue#>`). |
| **Sanitizers** | Every debug build in CI runs the full suite with ASan + UBSan. Any report fails the build. |
| **Coverage** | Target: ≥ 90 % line coverage for `src/crypto/` and `src/sync/`, ≥ 75 % for `src/storage/` and `src/services/`. No target for `src/ui/` (covered by UI tests + manual QA). |
| **Privacy in tests** | Tests use a marker string like `SECRET-MARKER-7f3a` in note bodies so later tests can prove it never appears on disk, in logs, or on the network. |

---

## Phase 0: Learning & Setup (Weeks 1–3)

**Goal:** A CMake project that builds an empty window on Ubuntu, with tests and CI already running.
**Exit criteria:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` passes locally and in GitHub Actions.

### 0.1 Decide minimum Ubuntu and Qt version `S`

Ubuntu 24.04 ships **Qt 6.4**, which is missing APIs this plan relies on: `QStyleHints::colorScheme()` (Qt 6.5+) for dark mode and built-in PKCE in Qt Network Authorization (Qt 6.8+). Decide now, because it affects code from Phase 1 on.

- [x] Check Qt versions shipped by Ubuntu 24.04 LTS and 26.04 LTS (`apt policy qt6-base-dev`)
- [x] Check the Qt version in the current KDE Flatpak runtime
- [x] Choose one option and record it in `docs/adr/0001-minimum-platform.md`:
  - **A:** `.deb` targets Ubuntu 26.04+ only; Flatpak covers older systems *(recommended)*
  - **B:** Support Qt 6.4 and write fallbacks (manual PKCE, D-Bus theme detection)
- [x] Set `find_package(Qt6 <version> REQUIRED ...)` to match

**Tests**
- `TC-ENV-001` (M) Build on the chosen minimum Ubuntu version in a clean container succeeds.

**Done when:** ADR merged and CMake enforces the minimum Qt version.

### 0.2 Development environment `S`

- [x] Write `scripts/setup-dev.sh` that installs: `build-essential cmake ninja-build clang clang-tidy cppcheck gdb valgrind heaptrack linux-tools-generic`, `qt6-base-dev qt6-tools-dev qt6-svg-dev qt6-networkauth-dev qt6-httpserver-dev`, `libsqlcipher-dev libsodium-dev qtkeychain-qt6-dev`, `pkgconf ccache libclang-rt-dev` *(package names corrected for Ubuntu 26.04: `libqt6networkauth6-dev` does not exist; `libclang-rt-dev` is needed for sanitizers/libFuzzer with clang)*
- [x] Make the script idempotent (safe to run twice)
- [ ] Install Qt Creator or configure CLion/VS Code with clangd
- [x] Document the steps in `README.md` → "Building"

**Tests**
- `TC-ENV-002` (M) Run `setup-dev.sh` in a fresh `ubuntu` Docker container, then build. Succeeds with no manual steps.
- `TC-ENV-003` (M) Run the script a second time. Exits 0, changes nothing.

**Done when:** a new contributor can build from README instructions alone.

### 0.3 Learning sprint `L` → broken down

Keep exercises in a throwaway `sandbox/` folder (git-ignored or a separate repo).

- [ ] **C++20 basics (M):** RAII, `std::unique_ptr`/`std::shared_ptr`, move semantics, `std::optional`, `std::expected`-style error handling, `std::span`
- [ ] **Qt core (M):** `QObject` parent/child ownership, signals & slots, event loop, `QTimer`
- [ ] **Qt Widgets (M):** layouts, `QSplitter`, `QMainWindow`, `QAction`, style sheets
- [ ] **Model/View (M):** build a tiny `QAbstractListModel` + custom delegate
- [ ] **Threads (S):** `QThread` worker pattern, `QtConcurrent::run`, why widgets must stay on the main thread
- [ ] **CMake (S):** targets, `target_link_libraries`, presets
- [ ] **Qt Test (S):** write a test with `QCOMPARE`, `QSignalSpy`, data-driven `_data()` functions

**Tests**
- `TC-ENV-004` (M) Each exercise has at least one passing Qt Test.

**Done when:** you can build a list model + delegate with tests without looking at a tutorial.

### 0.4 Repository skeleton `S`

- [x] Create the folder layout from **Plan §11** (`src/{app,ui,editor,models,services,storage,crypto,sync}`, `tests/{unit,integration,ui,bench,fuzz}`, `resources/`, `packaging/`, `docs/`, `cmake/`, `tools/`)
- [ ] Add `LICENSE` (choose: GPLv3, MIT, or proprietary; Qt is LGPL and dynamically linked either way, see Plan §5)
- [x] Add `.clang-format`, `.clang-tidy`, `.editorconfig`
- [x] Add `docs/contributing.md`: naming, smart pointers only (except Qt parent-owned widgets), wrap all user-visible strings in `tr()`, **never log note content**

**Tests**
- `TC-BLD-001` (U) `clang-format --dry-run --Werror` on all sources passes (runs in CI).

**Done when:** skeleton committed.

### 0.5 CMake project `M`

- [x] Top-level `CMakeLists.txt` with C++20, `-Wall -Wextra -Wpedantic -Werror` (debug), `-O2` + LTO (release)
- [x] `cmake/FindSQLCipher.cmake` and `cmake/FindSodium.cmake` (or `pkg_check_modules`)
- [x] Targets:
  - `notesapp_core` static library: storage, crypto, services, sync (**no widgets**, so tests link without a GUI)
  - `notesapp_ui` static library: widgets
  - `notesapp` executable: `main.cpp` only
- [x] Options: `NOTES_ENABLE_TESTS`, `NOTES_ENABLE_ASAN`, `NOTES_ENABLE_FUZZ`, `NOTES_ENABLE_BENCH`
- [x] `CMakePresets.json` with presets: `debug`, `debug-asan`, `release`, `fuzz` (clang)
- [x] `main.cpp` shows an empty `QMainWindow`

**Tests**
- `TC-BLD-002` (I) Each preset configures and builds with zero warnings.
- `TC-BLD-003` (M) `./build/release/notesapp` opens an empty window.
- `TC-BLD-004` (U) Release binary is stripped; size reported in CI log (baseline for Plan §10 budget).

**Done when:** all four presets build.

### 0.6 Test harness `S`

- [x] `tests/CMakeLists.txt` with helper `notes_add_test(NAME tst_x SOURCES ... LABELS unit)`
- [x] Set `QT_QPA_PLATFORM=offscreen` for all tests via CTest environment
- [x] `tests/common/`: `TestHelpers.h` (temp DB factory, marker string constant), stubs for `FakeClock`, `InMemoryKeyStore`
- [x] First smoke test `tst_smoke.cpp`

**Tests**
- `TC-BLD-005` (U) `ctest -L unit` runs and passes the smoke test.
- `TC-BLD-006` (U) A deliberately failing test makes `ctest` exit non-zero (verify once, then remove).

**Done when:** adding a new test is one line in CMake.

### 0.7 Continuous integration `M`

- [x] `.github/workflows/ci.yml` with jobs:
  - `build-test`: debug build + `ctest -L "unit|integration|ui"`
  - `sanitizers`: `debug-asan` preset + full test suite
  - `static-analysis`: `clang-tidy` + `cppcheck`, warnings as errors
  - `format`: `clang-format` check
- [x] Cache apt packages and the build dir *(build dir cached via ccache)*
- [x] Upload test logs as artifacts on failure
- [ ] Branch protection on `main`: CI must pass

**Tests**
- `TC-BLD-007` (M) Open a PR with a failing test → CI is red. Open a PR with a memory leak → `sanitizers` job is red.

**Done when:** CI is green on `main` and blocks broken PRs.

---

## Phase 1: Skeleton UI (Weeks 4–6)

**Goal:** The three-pane window from **Plan §3.1** with toolbar, sidebar, note list, and a plain editor, using fake in-memory data. Light and dark themes work.
**Exit criteria:** You can click around a realistic-looking app; all UI tests pass; nothing is saved yet.

### 1.1 Application bootstrap `S`

- [x] `Application` class: sets org/app name, app ID (e.g. `io.augmex.NotesApp`), loads `QSettings`
- [x] Single-instance guard with `QLocalServer`: a second launch focuses the existing window (prevents two processes writing the same DB and running two sync engines)
- [x] Central `AppContext` that owns services (empty for now) and hands them to the UI (dependency injection, no globals)

**Tests**
- `TC-UI-001` (I) Starting a second instance sends an "activate" message and the second process exits with code 0.
- `TC-UI-002` (U) `QSettings` uses the configured org/app name.

**Done when:** app launches once, second launch focuses the first.

### 1.2 Resources: fonts and icons `S`

- [x] Add **Inter** and **JetBrains Mono** (SIL OFL) to `resources/fonts/`, load with `QFontDatabase::addApplicationFont` *(static TTFs, not variable: Qt 6.10 ignores the `wght` axis of application fonts and fakes bold; see `resources/SOURCES.md`)*
- [x] Add the needed **Lucide** (or Phosphor) SVG icons to `resources/icons/` *(needs `qt6-svg-plugins` at runtime; add it to the `.deb` dependencies in Phase 11)*
- [x] Put font/icon license texts in `resources/licenses/` and an "About → Licenses" entry (stub)
- [x] `resources.qrc` with aliases *(done with `qt_add_resources()` in CMake instead: a `.qrc` in a static library needs a manual `Q_INIT_RESOURCE()` in every executable, including each test; fonts are zstd-compressed to keep the binary at 1.2 MB)*

**Tests**
- `TC-UI-003` (U) Both font families are available after loading.
- `TC-UI-004` (U) Every icon path referenced in code resolves (`QIcon(path).isNull() == false`). Use a single `Icons.h` list so the test can iterate it.

**Done when:** fonts and icons render.

### 1.3 Main window with three panes `M`

- [x] `MainWindow` with a `QSplitter`: sidebar (220 px), note list (300 px), editor (flexible)
- [x] Minimum widths so panes can't collapse to zero by accident
- [x] Save/restore window geometry and splitter sizes in `QSettings`
- [x] Sidebar toggle (`Ctrl+Alt+S`)

**Tests**
- `TC-UI-005` (UI) Window opens with default widths 220/300/rest.
- `TC-UI-006` (UI) Change splitter sizes, destroy window, create new one → sizes restored.
- `TC-UI-007` (UI) `Ctrl+Alt+S` hides the sidebar; pressing again shows it at the previous width.

**Done when:** layout matches Plan §3.1.

### 1.4 Toolbar and actions `M`

- [x] Create all actions from **Plan §3.2** as `QAction`s in an `ActionRegistry`, in the documented order *(also listed in File/Edit/Format/View menus; the search box is backed by two actions, Find in Note `Ctrl+F` and Search All Notes `Ctrl+Alt+F`)*
- [x] Icons, tooltips that include the shortcut
- [x] Actions that aren't implemented yet are disabled *(only Sidebar toggle and Search All Notes, which focuses the search box, are enabled)*
- [x] Search box on the right side of the toolbar

**Tests**
- `TC-UI-008` (UI) Toolbar action order equals the order in Plan §3.2.
- `TC-UI-009` (UI) Data-driven test: for each shortcut in the table, `QTest::keySequence` triggers the matching action (`QSignalSpy` on `triggered`).
- `TC-UI-010` (U) No two actions share the same shortcut.

**Done when:** every toolbar button exists with correct shortcut.

### 1.5 Sidebar `M`

- [x] `Sidebar` widget with `QTreeView` *(works with any model that provides the roles in `models/SidebarRoles.h`, so `FolderTreeModel` (2.8) can replace the fake one; All Notes and Recently Deleted use the reserved folder ids `-1`/`-2`)*
- [x] Sections: account header ("On My Computer"), All Notes, Notes, user folders (nested), Tags (placeholder), Recently Deleted *(tags are display-only until Phase 7; Recently Deleted sits below Tags, as in the Plan §3.1 sketch)*
- [x] "+ Folder" button at the bottom *(disabled until the FolderService in 2.7)*
- [x] Count badges drawn by a delegate
- [x] Fake model with sample folders

**Tests**
- `TC-UI-011` (UI) Selecting a folder emits `folderSelected(id)`.
- `TC-UI-012` (UI) Nested folders expand/collapse; state is remembered for the session.
- `TC-UI-013` (U) Fake sidebar model passes `QAbstractItemModelTester`.

**Done when:** sidebar looks like Plan §3.3 with fake data.

### 1.6 Note list view and delegate `M`

- [x] `NoteListView` (`QListView`, `setUniformItemSizes(true)`) *(works with any model that provides the roles in `models/NoteListRoles.h`)*
- [x] `NoteListDelegate`: bold title, time + first line of preview, thumbnail slot, pin icon, lock icon *(the slot is a placeholder until attachments; locked notes show "Locked" and their model never holds a preview)*
- [x] Section headers: Pinned, Today, Yesterday, Previous 7 Days, Previous 30 Days, month names, years *(headers are disabled rows that clicks and arrow keys skip; since the 1.9 UI review they are compact rather than note-height, so item sizes aren't uniform (the delegate's sizeHint() is constant-time and TC-PERF-001 still passes). Previous 7 Days = 2–7 calendar days ago, Previous 30 Days = 8–30; months only for the current year)*
- [x] Fake model that can generate **10,000** items, to prove virtualization early *(the app shows 10,000; generating them takes ~11 ms)*

**Tests**
- `TC-UI-014` (U) `DateGrouping::sectionFor(timestamp, now)` returns the correct section for boundary times (midnight, exactly 7 days ago, year change). Use `FakeClock`.
- `TC-UI-015` (UI) Selecting a row emits `noteSelected(id)`.
- `TC-PERF-001` (P) Scrolling 10,000 rows from top to bottom: each frame paints in < 16 ms.

**Done when:** the list looks like Plan §3.4 and scrolls smoothly with 10k fake notes.

### 1.7 Editor view (plain) `S`

- [x] `EditorView`: date/time header label (centered, light gray) + `NoteTextEdit` (subclass of `QTextEdit`) *(plain text: `acceptRichText(false)` until the Phase 3 document model)*
- [x] Empty state ("No note selected")
- [x] Shows fake note content when a list row is selected *(text comes from `FakeNoteListModel::note(id)`, standing in for `NoteService::get(id)`; list rows never carry bodies. Locked notes show "This note is locked." until the lock screen in 6.4)*

**Tests**
- `TC-UI-016` (UI) Selecting a note in the list shows its text in the editor and its date in the header.
- `TC-UI-017` (UI) With no selection, the empty state is shown and the editor is read-only.

**Done when:** list → editor wiring works with fake data.

### 1.8 Themes (light/dark) `M`

- [x] `resources/themes/light.qss` and `dark.qss` using shared color tokens (accent color from Plan §3.6) *(one shared `theme.qss` with `${token}` placeholders, filled from each theme's `ThemeColors`, so the two themes can't drift apart; the same tokens build the `QPalette` that custom-painted delegates use. Accent: amber `#F3BE33` (was `#F2A900`; lightened in the 1.9 UI review), dark text on it. Icons are tinted with the palette at paint time, so they stay visible in dark mode)*
- [x] `ThemeManager` follows the system setting and reacts live to changes (`QStyleHints::colorSchemeChanged`, or D-Bus fallback per ADR 0001) *(no D-Bus fallback needed: ADR 0001 requires Qt 6.8)*
- [x] Manual override in settings: System / Light / Dark *(View → Appearance; saved as `appearance/theme`)*
- [x] 150 ms fade limit for any animation *(`ThemeManager::kMaxAnimationMs`; theme switches cross-fade in 150 ms; Qt's own menu/combo/tooltip animations are turned off)*

**Tests**
- `TC-UI-018` (UI) Forcing dark mode applies `dark.qss` (check a known widget's palette color).
- `TC-UI-019` (UI) Emitting a color-scheme change switches theme without restart.
- `TC-UI-020` (M) Visual check in both themes: text contrast is readable, selection highlight is rounded.

**Done when:** theme follows Ubuntu's setting.

### 1.9 Header bar and window chrome `M`

*Added 2026-10-30 from UI feedback: on GNOME, Qt's Adwaita title bar ignores the app theme and draws no shadow, so the window blended into light backgrounds. Layout follows macOS Notes' grouped toolbar.*

- [x] Replace the system title bar: the menu row (File, Edit, Format, View, Help) is the header, with minimise / maximise / close at its right end; no title text *(`WindowChrome`: frameless, translucent window; the "Notes" title still names the window for the dock and Alt+Tab)*
- [x] Drag empty header or toolbar space to move the window; double-click to maximise / restore
- [x] Soft drop shadow and 1 px border around the window, removed when maximised or full screen; the shadow margin is the resize border *(Qt can't tell when GNOME snaps a window to half the screen, so snapped windows keep the margin)*
- [x] Rounded window corners (12 px, as GNOME) *(an overlay above the content erases the corners outside the curve and draws the border along it; square when maximised)*
- [x] Spacing per UI review: padding around the header row and toolbar, window buttons centred on the menu titles, search box as tall as the button groups, lighter shadow
- [x] Second UI review: gap between the menu row and the toolbar buttons equal to the gap below them; window buttons at GNOME size (22 px, 14 px icons); roomier note list rows (more padding and line spacing, as macOS Notes)
- [x] Third UI review: lighter accent `#F3BE33`; compact section headers in the note list; equal toolbar insets at both ends (QToolBar ignores its own side margins, so fixed spacers at each end); app logo (`resources/app/logo.svg`) as window icon and in About; a dark ring along the window's curved corners removed (the shadow was painted over the half-erased edge pixels instead of behind them)
- [x] Note list, as macOS Notes: note rows indented 14 px past their section header ("Pinned", "Today", ...) so headers stand out; headers and the hairlines between notes keep their position
- [x] Bug fix (seen at 125 % display scale): thin light line under the window and at the corners, and a shadow that looked heavy. The shadow is now nested translucent rounded shapes (no seams, no unshaded strip under the window, lighter); the window edge is a faint translucent line instead of light grey; content widgets overhang the rounded edge by 1 px and the overlay trims them, so no background strip shows at fractional scales
- [x] Toolbar buttons in joined, rounded groups as in macOS Notes: sidebar · list/gallery · delete/new note · format/checklist/table/attachment/lock · share; editing tools centred
- [x] Search placeholder (magnifier + "Search") centred until the box is focused or has text

**Tests**
- `TC-UI-040` (UI) Window is frameless and translucent, with rounded corners; header row has the five menus and the window buttons on the right.
- `TC-UI-041` (UI) Maximise button toggles maximised state and removes the shadow margin; minimise and close buttons work.
- `TC-UI-042` (UI) Double-click on empty header space maximises; on a menu title it doesn't.
- `TC-UI-043` (UI) Toolbar groups and their order; search placeholder centred until used; search box as tall as the groups; toolbar layout identical at startup and after a theme switch.
- `TC-UI-044` (M) Window stays distinguishable on light and dark backgrounds in both themes; move, resize and snapping behave normally on GNOME Wayland.

**Done when:** the window has one custom header row and a visible edge on any background.

---

## Phase 2: Storage MVP (Weeks 7–9)

**Goal:** Real folders and notes saved in SQLite, with autosave, pinning, Recently Deleted, sorting, and drag & drop.
**Exit criteria:** Create, edit, move, pin, delete, and restore notes; restart the app and everything is still there.

### 2.1 Architecture decisions `S`

Record each in `docs/adr/`:

- [x] **ADR 0002: Database access.** Use the **SQLite C API through a thin C++ wrapper, linked against SQLCipher from day one** (unkeyed until Phase 4). Reason: Qt's `QSQLITE` driver links the system SQLite, not SQLCipher, so using `QSqlDatabase` now would mean rewriting storage in Phase 4.
- [x] **ADR 0003: Threading.** All DB access runs on one dedicated DB thread; services expose async calls returning `QFuture`. The UI thread never blocks on disk.
- [x] **ADR 0004: Time.** Timestamps are UTC milliseconds since epoch (`int64`). All code reads time through `IClock`. *(also decided: local time only at the UI edge; auto-relock treats a backwards clock jump as expired)*
- [x] **ADR 0005: IDs.** `uuid` (`QUuid::createUuid()`, string form) is the stable identity across devices; integer `id` is local only. *(also decided: lowercase without braces; the default "Notes" folder gets a fixed well-known UUID so devices don't end up with two "Notes" folders after sync)*

**Done when:** ADRs merged.

### 2.2 Domain models and clock `S`

- [x] Value types in `src/models/`: `Note`, `Folder`, `Attachment`, `Tag` (plain structs, no Qt widgets) *(plus `SyncState` and the ADR 0005 helpers `newUuid()` / `parseUuid()`. The locked-note columns join `Note` in Phase 6 with `SecureBuffer`. "Used by the repositories" completes in 2.5/2.6)*
- [x] `IClock` with `SystemClock` and `FakeClock` (in `tests/common/`) *(`IClock::nowMs()`; `AppContext::clock()` owns it; test contexts use `FakeClock`. The fake notes in `MainWindow` now use it, so `SystemClock` is the only reader of system time in `src/`)*

**Tests**
- `TC-STO-001` (U) `FakeClock::advance(30 days)` changes `now()` by exactly that amount.

**Done when:** models compile and are used by the repositories.

### 2.3 Database wrapper `M`

- [x] `Database` class: open/close, `exec`, prepared `Statement` class with typed `bind()`/`column()` *(`QString`/`QByteArray` always bind as non-NULL; `std::optional` for NULL. Open also refuses to run without SQLCipher)*
- [x] Prepared-statement cache *(`prepare()` hands out a `ScopedStatement` that resets on scope exit, so a half-read statement can't hold a WAL read snapshot; nested use of the same SQL gets a throwaway statement)*
- [x] RAII `Transaction` guard (commits on `commit()`, rolls back in destructor otherwise) *(`BEGIN IMMEDIATE`; no nesting)*
- [x] On open: `PRAGMA journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`
- [x] Errors returned as a `DbError` type (no exceptions crossing threads) *(`DbResult<T>` / `DbStatus`, extended SQLite codes)*
- [x] `DbThread` that owns the `Database` and runs jobs from a queue *(`post()` returns a `QFuture`; `stop()` drains the queue before closing)*

**Tests**
- `TC-STO-002` (U) Open a new DB in a temp dir → `journal_mode` is `wal`, `foreign_keys` is `1`.
- `TC-STO-003` (U) `Transaction` destroyed without `commit()` → inserted row is gone.
- `TC-STO-004` (U) Binding text containing quotes and `'; DROP TABLE notes; --` is stored literally (no SQL injection).
- `TC-STO-005` (U) Invalid SQL returns a `DbError` with a message; does not crash.
- `TC-STO-006` (I) 1,000 jobs posted from 4 threads all run, in order per caller, without data races (run under TSan once manually).

**Done when:** wrapper covered by tests, no Qt SQL module used.

### 2.4 Migrations `M`

- [x] `migrations/001_initial.sql` = schema from **Plan §7** *(`src/storage/migrations/`. Without `notes_fts`: task 5.1 adds it together with the sync triggers an external-content FTS table needs. `note_tags` also gets foreign keys with `ON DELETE CASCADE`)*
- [x] `Migrator`: reads `meta.schema_version`, applies newer numbered scripts in order, each in a transaction *(the version update is in the same transaction; foreign keys are off while migrating and `PRAGMA foreign_key_check` runs before each commit; files must be `NNN_name.sql` with no gaps)*
- [x] Migration scripts embedded via `.qrc` *(`qt_add_resources()` on `notesapp_core`, as in 1.2)*
- [ ] If the DB's version is **newer** than the app knows: refuse to open and show a clear error (prevents an old app version from damaging data) *(`Migrator` side done: returns `TooNew` and writes nothing. The error dialog needs the app's startup path that opens the database, which no task lists yet; it belongs with 2.11's wiring)*

**Tests**
- `TC-MIG-001` (U) Empty DB → after migrate, `schema_version` = latest and all tables/indexes from Plan §7 exist.
- `TC-MIG-002` (U) DB at v1 with data → migrate to latest → data preserved.
- `TC-MIG-003` (U) A migration with a syntax error rolls back: version unchanged, tables unchanged.
- `TC-MIG-004` (U) DB with `schema_version` = latest + 1 → `Migrator` returns `TooNew` and writes nothing.
- `TC-MIG-005` (U) Running migrate twice is a no-op the second time.

**Done when:** every future schema change is just a new `.sql` file + a test.

### 2.5 Folder repository `M`

- [x] `FolderRepository`: `create`, `rename`, `move(newParent)`, `softDelete`, `listTree`, `reorder` *(names are trimmed; `softDelete` tombstones the whole subtree and moves its notes to Recently Deleted, keeping their `folder_id` for restore. Errors carry a `DbError::Kind`: NotFound, InvalidArgument, Cycle, Protected)*
- [x] Every change bumps `version`, sets `updated_at`, and sets `sync_state = 0` *(times from `IClock`; `reorder` only touches rows whose position changes)*
- [x] Prevent cycles (moving a folder into its own descendant)
- [x] Create default **Notes** folder on first run *(`ensureDefaultFolder()`, with the fixed UUID `d5cf1c5f-0bc7-419e-b2f2-096709ed4f5b` from ADR 0005; it can't be deleted, moved or renamed, as in macOS Notes. Calling it at startup comes with the 2.11 wiring)*

**Tests**
- `TC-STO-007` (U) Create nested folders 3 levels deep → `listTree` returns the correct hierarchy.
- `TC-STO-008` (U) Moving folder A into its child B fails with `CycleError`; tree unchanged.
- `TC-STO-009` (U) Rename bumps `version` by 1 and sets `sync_state = 0`.
- `TC-STO-010` (U) Deleting a folder moves its notes to Recently Deleted (not lost).
- `TC-STO-011` (U) Default "Notes" folder exists after first run and cannot be deleted.
- `TC-STO-012` (U) Folder names: empty and whitespace-only rejected; Unicode/emoji accepted.

**Done when:** all folder operations tested.

### 2.6 Note repository `M`

- [x] `NoteRepository`: `create`, `get`, `updateBody`, `listForFolder(sort)` (title + snippet only, **no body**), `listAll`, `setPinned`, `moveToFolder`, `trash`, `restore`, `purge` *(lists return `NoteSummary`, which has no body fields, and take `limit`/`offset` for 2.8's paging; plus `listTrashed()`. `restore` falls back to the default folder; `purge` deletes attachments and tags and leaves a content-free tombstone for sync)*
- [x] Sort modes: date edited, date created, title (case-insensitive, locale-aware) *(a `LOCALE` collation backed by `QCollator`, registered on every connection; numbers sort by value)*
- [x] Every change bumps `version`, `updated_at`, `sync_state = 0` *(changed: every change bumps `version` and `sync_state`, but only content changes set `updated_at`, the "date edited" the list sorts and groups by. Pinning, moving or trashing a note doesn't make it look newly edited)*

**Tests**
- `TC-STO-013` (U) Create → get returns identical fields.
- `TC-STO-014` (U) `listForFolder` does not load `body_html` (assert via a query counter or column list).
- `TC-STO-015` (U) Pinned notes come first in every sort mode.
- `TC-STO-016` (U) Sort by title: "apple", "Banana", "Élan" ordered correctly.
- `TC-STO-017` (U) `updateBody` bumps `version` and `updated_at`; `created_at` unchanged.
- `TC-STO-018` (U) Note with 1 MB body saves and loads unchanged.

**Done when:** repository tested for all operations.

### 2.7 Note and folder services `M`

- [ ] `NoteService`, `FolderService` wrap repositories, run on DB thread, emit signals (`noteCreated`, `noteUpdated`, `noteMoved`, `noteTrashed`, ...)
- [ ] **Title derivation:** first non-empty line of plain text, trimmed, max 200 chars
- [ ] **Snippet derivation:** next non-empty line(s), max 120 chars
- [ ] New note is created in the selected folder (or "Notes" when "All Notes" is selected)

**Tests**
- `TC-STO-019` (U) Data-driven title test: `"Shopping\nMilk"` → `"Shopping"`; `"\n\n  Hi  \n"` → `"Hi"`; empty → `""` (UI shows "New Note"); 500-char line → 200 chars.
- `TC-STO-020` (U) Snippet skips the title line and blank lines.
- `TC-STO-021` (I) Creating a note emits `noteCreated` exactly once (`QSignalSpy`).

**Done when:** UI can use services without touching repositories.

### 2.8 Qt models `M`

- [ ] `FolderTreeModel` (`QAbstractItemModel`) backed by `FolderService`, updates on signals
- [ ] `NoteListModel` (`QAbstractListModel`) with **lazy loading** (`canFetchMore`/`fetchMore`, pages of 200)
- [ ] Date-group sections exposed as a role
- [ ] Count badges per folder

**Tests**
- `TC-STO-022` (U) Both models pass `QAbstractItemModelTester` while notes are added, moved, and deleted.
- `TC-STO-023` (U) With 1,000 notes, initial load fetches ≤ 200 rows.
- `TC-STO-024` (U) Updating a note's title emits `dataChanged` for that row only.

**Done when:** fake models from Phase 1 are replaced.

### 2.9 Autosave `M`

- [ ] Debounce: save 500 ms after the last keystroke (`QTimer` restart on each change)
- [ ] Flush immediately on: switching notes, closing the window, app quit
- [ ] Saves run on the DB thread; UI never waits
- [ ] Skip saving when content is unchanged (compare hash)

**Tests**
- `TC-STO-025` (UI) Type 10 characters within 400 ms → exactly 1 save after 500 ms.
- `TC-STO-026` (UI) Type, then switch note within 100 ms → the first note is saved before the switch.
- `TC-STO-027` (I) Type, then quit → reopen → text is there.
- `TC-STO-028` (UI) Select a note without editing → no save happens, `version` unchanged.

**Done when:** no "Save" button needed and no lost keystrokes.

### 2.10 Recently Deleted `M`

- [ ] Trash sets `trashed_at`; note leaves its folder and appears in Recently Deleted
- [ ] Restore returns it to its original folder (or "Notes" if that folder is gone)
- [ ] "Delete Permanently" with confirmation
- [ ] Auto-purge items older than 30 days: on startup and once per day
- [ ] List shows "N days remaining"

**Tests**
- `TC-STO-029` (U) Trash → restore → note back in original folder.
- `TC-STO-030` (U) Original folder deleted → restore puts note in "Notes".
- `TC-STO-031` (U) With `FakeClock` +29 days → still present; +30 days + 1 ms → purged.
- `TC-STO-032` (U) Purge also deletes the note's attachments (cascade).

**Done when:** matches Plan §3.3.

### 2.11 Wire UI to real data `M`

- [ ] Sidebar, note list, and editor use the real models and services
- [ ] New Note (`Ctrl+N`), Delete (`Delete` key), Pin from context menu
- [ ] Right-click menus: note (Pin, Move to, Duplicate, Delete), folder (Rename, Delete, New Subfolder)
- [ ] Sort options menu, saved in `QSettings`

**Tests**
- `TC-UI-021` (UI) `Ctrl+N` creates a note in the selected folder and focuses the editor.
- `TC-UI-022` (UI) `Delete` key on a selected note moves it to Recently Deleted and selects the next note.
- `TC-UI-023` (UI) Duplicate creates "<title> copy" with identical body.
- `TC-UI-024` (I) Full restart test: create folder + 3 notes, pin one, restart → same state.

**Done when:** the app is usable for plain-text notes.

### 2.12 Drag & drop `S`

- [ ] Drag notes from list onto sidebar folders (custom MIME type `application/x-notesapp-note-uuids`)
- [ ] Drag folders to nest/reorder
- [ ] Drop highlight on hover

**Tests**
- `TC-UI-025` (UI) Simulated drop of 2 notes on folder B → both moved, counts updated.
- `TC-UI-026` (UI) Dropping a folder on its own child is refused.

**Done when:** drag & drop works for notes and folders.

---

## Phase 3: Rich Editor (Weeks 10–15)

**Goal:** All P0 editing features from **Plan §4 → Editing**, built on `QTextEdit`.
**Exit criteria:** Round-trip test suite passes (save → load → identical document) for every format.

### 3.1 Spike: document format round-trip `M`

**Do this first.** Custom `QTextFormat` properties are **not** written by `QTextDocument::toHtml()`, so anything stored only as a custom property (checklist state, collapsed headings, attachment IDs) will be lost on save unless you design for it.

- [ ] Build a test document with every planned feature (headings, lists, checklist items via `QTextBlockFormat::MarkerType`, monospace, image, table)
- [ ] Save with `toHtml()`, load with `setHtml()`, compare block-by-block
- [ ] Record which properties survive and which don't
- [ ] Decide in `docs/adr/0006-note-format.md`:
  - **A:** Qt HTML + a small post-processor that adds `data-*` attributes for lost properties
  - **B:** Own versioned JSON format for the document tree *(more work, full control)*
- [ ] Add `format_version` to the stored body so the format can change later

**Tests**
- `TC-EDT-001` (U) Round-trip harness: `serialize(deserialize(serialize(doc))) == serialize(doc)` for a fixture document. This harness is reused by every later editor task.

**Done when:** ADR merged and harness exists.

### 3.2 Paragraph styles `M`

- [ ] `StyleManager` with Title, Heading, Subheading, Body, Monospaced (mapped to heading levels 1/2/3, none, and JetBrains Mono)
- [ ] **Aa** Format popup menu (Plan §3.2) showing the current style checked
- [ ] Shortcuts: `Ctrl+Shift+T` Title, `Ctrl+Shift+H` Heading, `Ctrl+Shift+J` Subheading, `Ctrl+Shift+B` Body *(mirrors macOS; check against Plan §3.2 table for conflicts)*
- [ ] Each style change is one undo step (`beginEditBlock`/`endEditBlock`)

**Tests**
- `TC-EDT-002` (UI) Apply each style → block has correct heading level / font.
- `TC-EDT-003` (UI) Format menu shows the current block's style.
- `TC-EDT-004` (U) Round-trip preserves all 5 styles.
- `TC-EDT-005` (UI) Style change + `Ctrl+Z` → original style restored in one step.

**Done when:** all paragraph styles work and survive save.

### 3.3 First line becomes title `S`

- [ ] The first block is always styled as Title when the note is new
- [ ] Pressing Enter after the title switches the next block to Body
- [ ] If the user deletes the first line, the new first line becomes the title (service re-derives on save)

**Tests**
- `TC-EDT-006` (UI) New note, type "Groceries⏎Milk" → block 0 is Title, block 1 is Body, note title = "Groceries".
- `TC-EDT-007` (UI) Delete the first line → title becomes "Milk" after save.

**Done when:** behaves like Plan §3.5.

### 3.4 Inline formatting `S`

- [ ] Bold `Ctrl+B`, Italic `Ctrl+I`, Underline `Ctrl+U`, Strikethrough `Ctrl+Shift+X`
- [ ] Toolbar/menu toggle states follow the cursor position
- [ ] Applying with no selection sets the format for the next typed text

**Tests**
- `TC-EDT-008` (UI) Select word, `Ctrl+B` → bold; again → not bold.
- `TC-EDT-009` (UI) Cursor inside bold text → Bold action is checked.
- `TC-EDT-010` (U) Round-trip preserves mixed inline formats in one paragraph.

**Done when:** all four inline styles work.

### 3.5 Lists `M`

- [ ] Bulleted, dashed, numbered lists from the Format menu
- [ ] Auto-format at line start: `- `, `* ` → list; `1. ` → numbered
- [ ] `Tab` / `Shift+Tab` indent/outdent
- [ ] `Enter` on an empty item exits the list
- [ ] `Backspace` at the start of an item removes the bullet first

**Tests**
- `TC-EDT-011` (UI) Type `- item` → becomes a dashed list item with text "item".
- `TC-EDT-012` (UI) `Enter` twice → list ends, cursor in a Body paragraph.
- `TC-EDT-013` (UI) `Tab` on item 2 → indent level 2; numbering restarts at the nested level.
- `TC-EDT-014` (U) Round-trip preserves list type and nesting.

**Done when:** list behavior matches Plan §3.5.

### 3.6 Checklists `L` → broken down

- [ ] **(M)** Insert/toggle checklist with `Ctrl+Shift+L`; Enter creates a new unchecked item
- [ ] **(M)** Click on the checkbox toggles it (hit-test in `mousePressEvent`; clicking the text must not toggle)
- [ ] **(S)** Checked items rendered with a filled box and grayed text
- [ ] **(S)** Toggle is one undo step and triggers autosave
- [ ] **(M)** Setting "Move checked items to bottom" (Plan §3.5)

**Tests**
- `TC-EDT-015` (UI) `Ctrl+Shift+L` on a paragraph → unchecked checklist item.
- `TC-EDT-016` (UI) Click inside the checkbox rect → checked; click on the text → unchanged.
- `TC-EDT-017` (UI) Toggle + `Ctrl+Z` → unchecked again.
- `TC-EDT-018` (UI) With auto-move on, checking item 1 of 3 moves it below items 2 and 3.
- `TC-EDT-019` (U) Round-trip preserves checked/unchecked state (this is the case the Phase 3.1 spike must solve).

**Done when:** checklists work like Plan §3.1 mockup.

### 3.7 Images `L` → broken down

- [ ] **(M)** Insert by paste, drag & drop, and file picker
- [ ] **(M)** Store image bytes in the `attachments` table; reference in the document as `attachment://<uuid>`; resolve with a `loadResource` override
- [ ] **(S)** Scale images to fit editor width; keep aspect ratio
- [ ] **(S)** Generate a thumbnail once at insert time, store in `attachments.thumbnail` (Plan §10)
- [ ] **(S)** Limits: max file size (e.g. 25 MB), `QImageReader::setAllocationLimit` against decompression bombs
- [ ] **(S)** Orphan cleanup: attachments no longer referenced by the note body are deleted on save

**Tests**
- `TC-EDT-020` (UI) Paste a PNG from the clipboard → one `attachments` row; editor shows the image.
- `TC-EDT-021` (I) Restart → image still displays (loaded from DB, no loose files on disk).
- `TC-EDT-022` (U) Thumbnail created with max dimension ≤ 256 px.
- `TC-EDT-023` (U) A 30 MB file is rejected with a user message.
- `TC-EDT-024` (U) Image with a header claiming 100,000 × 100,000 px is rejected without allocating the memory.
- `TC-EDT-025` (I) Delete the image from text, save → attachment row removed.
- `TC-EDT-026` (I) Note list shows the thumbnail for a note with an image.

**Done when:** images are fully stored inside the DB.

### 3.8 Safe paste `M`

- [ ] Paste from web pages/other apps: keep supported formatting (bold, italic, lists, headings), strip everything else (fonts, colors, scripts, styles)
- [ ] **Never fetch remote images or URLs** from pasted HTML (privacy: prevents tracking pixels); convert remote `<img>` to a link or drop it
- [ ] `Ctrl+Shift+V` = paste as plain text

**Tests**
- `TC-EDT-027` (UI) Paste HTML with `<img src="http://127.0.0.1:<port>/pixel.png">` → local test server receives **zero** requests.
- `TC-EDT-028` (U) Pasted `<script>`, `<style>`, `onclick` attributes are removed.
- `TC-EDT-029` (UI) `Ctrl+Shift+V` pastes plain text only.

**Done when:** paste is safe and clean.

### 3.9 Links `S`

- [ ] Auto-detect URLs while typing
- [ ] `Ctrl+Click` opens with `QDesktopServices::openUrl`
- [ ] Only allow `http`, `https`, `mailto` schemes; ignore everything else

**Tests**
- `TC-EDT-030` (UI) Typing `https://example.com ` turns it into a link.
- `TC-EDT-031` (U) Data-driven: `file:///etc/passwd`, `javascript:alert(1)`, `smb://x` → not opened.

**Done when:** links work and are restricted.

### 3.10 Undo/redo `S`

- [ ] Verify `Ctrl+Z` / `Ctrl+Shift+Z` for every editor operation above
- [ ] Undo history is per note and cleared when switching notes (avoids undoing into another note)

**Tests**
- `TC-EDT-032` (UI) Data-driven: for each operation (style, list, checklist, image insert, paste) → undo restores the exact previous document.
- `TC-EDT-033` (UI) Switch note A → B → A: undo stack of A does not contain B's edits.

**Done when:** undo is reliable.

### 3.11 Editor performance `S`

- [ ] Benchmark typing in a 50,000-word note with images
- [ ] Profile with `perf` if slow; common fixes: avoid re-deriving title on every keystroke, defer heavy work to autosave

**Tests**
- `TC-PERF-002` (P) Keystroke-to-paint < 16 ms in a 50,000-word note.
- `TC-PERF-003` (P) Opening a note < 50 ms (Plan §10).

**Done when:** budgets met.

---

## Phase 4: Encryption at Rest (Weeks 16–18)

**Goal:** The local database is always encrypted with a random Device Key stored in GNOME Keyring (**Plan §8.3**).
**Exit criteria:** Copying the DB file to another machine reveals nothing; the app starts with no password prompt on a normal desktop.

### 4.1 Crypto module foundation `M`

- [ ] `src/crypto/` with `sodium_init()` called once at startup; abort with an error dialog if it fails
- [ ] `SecureBuffer`: allocated with `sodium_malloc` (guarded, `mlock`ed), move-only (copy deleted), `sodium_memzero` + `sodium_free` on destroy
- [ ] `Random::bytes(n)` → `SecureBuffer` via `randombytes_buf`
- [ ] Constant-time compare via `sodium_memcmp`
- [ ] Hex/base64 helpers that write into `SecureBuffer` (no plain `QString` copies of keys)

**Tests**
- `TC-CRY-001` (U) `SecureBuffer` move leaves the source empty; copy constructor does not compile (`static_assert(!std::is_copy_constructible_v<SecureBuffer>)`).
- `TC-CRY-002` (U) Two `Random::bytes(32)` calls produce different values.
- `TC-CRY-003` (U) `constantTimeEqual` returns correct results for equal, different, and different-length inputs.

**Done when:** all key material in the app lives in `SecureBuffer`.

### 4.2 Key store (GNOME Keyring) `M`

- [ ] `IKeyStore` interface: `read(name)`, `write(name, secret)`, `remove(name)` (async)
- [ ] `KeychainKeyStore` using QtKeychain (libsecret backend)
- [ ] `InMemoryKeyStore` for tests
- [ ] Error handling: no secret service running, keyring locked, user cancelled unlock prompt

**Tests**
- `TC-KEY-001` (U) `InMemoryKeyStore` write → read returns same bytes; remove → read returns `NotFound`.
- `TC-KEY-002` (I) `KeychainKeyStore` round-trip on a real session (CI: run inside `dbus-run-session` with `gnome-keyring-daemon --unlock`).
- `TC-KEY-003` (U) Each error type maps to a distinct, user-readable message.

**Done when:** keys can be stored and read.

### 4.3 Encrypted database `M`

- [ ] On first launch: generate 32-byte Device Key (DK), store via `IKeyStore`
- [ ] Open DB with SQLCipher using the **raw key form** `PRAGMA key = "x'<64 hex chars>'"`. Because the DK is already random, this skips SQLCipher's PBKDF2 step, which saves noticeable startup time (Plan §10: < 500 ms cold start).
- [ ] Verify with `PRAGMA cipher_version` that SQLCipher (not plain SQLite) is linked
- [ ] Wrong/missing key → clear error, never create a new empty DB over the old one

**Tests**
- `TC-ENC-001` (I) Create DB with key K, add note containing `SECRET-MARKER` → file bytes do not contain the marker, and the first 16 bytes are not `"SQLite format 3\0"`.
- `TC-ENC-002` (I) Open with wrong key → `NotADatabase` error; file unchanged (hash equal before/after).
- `TC-ENC-003` (U) `PRAGMA cipher_version` returns a non-empty value.
- `TC-ENC-004` (I) Key missing from keyring but DB exists → app shows recovery message, does **not** overwrite the DB.
- `TC-PERF-004` (P) Opening the encrypted DB < 50 ms.

**Done when:** DB is always encrypted.

### 4.4 Migrate existing plain DB to encrypted `M`

For beta testers who used Phase 2/3 builds.

- [ ] Detect plaintext DB (header check)
- [ ] `ATTACH` new encrypted file with key, `SELECT sqlcipher_export('encrypted')`, verify row counts and `PRAGMA integrity_check`
- [ ] Atomic swap via rename; delete plaintext copy only after successful verification
- [ ] Copy `schema_version` / `user_version`

**Tests**
- `TC-ENC-005` (I) Plain DB with 100 notes + 5 images → encrypted DB with identical content.
- `TC-ENC-006` (I) Simulate crash after export but before swap (inject failure) → on next start, original plain DB is intact and migration retries.
- `TC-ENC-007` (I) After migration, no plaintext DB file remains in the data dir.

**Done when:** upgrade path is safe.

### 4.5 File permissions `S`

- [ ] Data dir (`~/.local/share/<app>/`) created with `0700`
- [ ] DB, `-wal`, `-shm` files `0600`; set `umask(077)` at startup
- [ ] Fix permissions on startup if they were loosened

**Tests**
- `TC-ENC-008` (I) After first run, `stat` shows `0700` for dir and `0600` for all DB files.
- `TC-ENC-009` (I) Chmod DB to `0644`, start app → back to `0600`.

**Done when:** other users on the machine can't read the files.

### 4.6 Fallback: startup password when keyring is unavailable `M`

- [ ] If `IKeyStore` is unavailable, show "Set a startup password" (first run) or "Enter startup password"
- [ ] Derive DK from password with Argon2id (`crypto_pwhash`, `OPSLIMIT_MODERATE`/`MEMLIMIT_MODERATE`)
- [ ] Store salt + params in a small sidecar file (`db.salt`, `0600`); it can't live inside the encrypted DB
- [ ] Setting to switch between keyring mode and password mode (re-keys the DB with `PRAGMA rekey`)

**Tests**
- `TC-ENC-010` (I) Keyring unavailable → password set → restart → correct password opens DB.
- `TC-ENC-011` (I) Wrong password → error, DB unchanged.
- `TC-ENC-012` (I) Switch keyring → password → keyring: notes intact throughout.

**Done when:** app works on systems without a keyring (Plan §15 risk).

### 4.7 Runtime hygiene `S`

- [ ] Release builds: `prctl(PR_SET_DUMPABLE, 0)` and `setrlimit(RLIMIT_CORE, 0)` (Plan §8.5). Debug builds keep them off so `gdb` still works.
- [ ] Custom `qInstallMessageHandler`: release builds log only categories/IDs, never note text
- [ ] Code review rule + CI grep: no `qDebug() <<` on note body/title variables

**Tests**
- `TC-SEC-001` (I) Release build: `prctl(PR_GET_DUMPABLE)` returns 0 and `getrlimit(RLIMIT_CORE)` is 0.
- `TC-SEC-002` (I) Run full test suite with debug logging enabled, capture all output → `SECRET-MARKER` never appears.

**Done when:** no secrets in logs or core dumps.

---

## Phase 5: Search (Weeks 19–20)

**Goal:** Instant full-text search across all notes (**Plan §4 → Search**).
**Exit criteria:** < 30 ms search across 10,000 notes.

### 5.1 FTS5 index `M`

- [ ] Verify the linked SQLCipher has FTS5: `PRAGMA compile_options` includes `ENABLE_FTS5`. If not, build SQLCipher from source in `third_party/` with FTS5 enabled.
- [ ] Migration adds `notes_fts` (Plan §7) **plus the triggers** an external-content FTS table needs (insert, update, delete)
- [ ] Locked notes: only title indexed (body columns are NULL); trashed notes excluded from normal search
- [ ] Command to rebuild the index (`INSERT INTO notes_fts(notes_fts) VALUES('rebuild')`) for recovery

**Tests**
- `TC-SRC-001` (U) `compile_options` contains `ENABLE_FTS5`.
- `TC-SRC-002` (U) Insert, update, delete a note → FTS results reflect each change.
- `TC-SRC-003` (U) Locked note: searching its body word returns nothing; searching its title finds it.
- `TC-SRC-004` (U) Trashed note not returned by default search.

**Done when:** index stays in sync automatically.

### 5.2 Safe query builder `S`

- [ ] Convert user input to an FTS5 query: split into terms, quote each term, add prefix `*` to the last term (search-as-you-type)
- [ ] Never pass raw user input into `MATCH`

**Tests**
- `TC-SRC-005` (U) Data-driven inputs `"`, `'`, `AND`, `OR NOT`, `*`, `-`, `NEAR(`, `col:x`, emoji, empty → no SQL error, sensible results.
- `TC-SRC-006` (U) `"shop"` matches "Shopping List" (prefix).
- `TC-SRC-007` (U) Unicode: `"café"` matches "Café"; diacritics folding via `tokenize='unicode61 remove_diacritics 2'`.
- `TC-SRC-008` (F) Fuzz target for the query builder (added to Phase 10 fuzz runs).

**Done when:** any typed input is safe.

### 5.3 Search service `S`

- [ ] `SearchService::search(text)` → async results (note id, title, snippet with match markers)
- [ ] Debounce 100 ms; cancel/ignore stale results when a newer query starts
- [ ] Ranking: `bm25()` combined with recency

**Tests**
- `TC-SRC-009` (U) Rapid queries "a", "ab", "abc" → only results for "abc" are delivered.
- `TC-SRC-010` (U) Title match ranks above body-only match.

**Done when:** service is fast and race-free.

### 5.4 Search UI `M`

- [ ] Toolbar search field; `Ctrl+Alt+F` focuses it (Plan §3.2)
- [ ] Results replace the note list while searching; `Esc` clears and returns to the previous folder
- [ ] Highlighted matches in result titles/snippets (`snippet()` / `highlight()` FTS functions)
- [ ] Opening a result highlights matches in the editor

**Tests**
- `TC-UI-027` (UI) `Ctrl+Alt+F`, type "milk" → list shows only matching notes.
- `TC-UI-028` (UI) `Esc` restores the previous folder and selection.
- `TC-UI-029` (UI) Opening a result highlights every occurrence in the editor.

**Done when:** search feels instant.

### 5.5 Find in note `S`

- [ ] `Ctrl+F` opens a find bar under the editor; Enter/`Shift+Enter` next/previous; match count "3 of 7"

**Tests**
- `TC-UI-030` (UI) Find "a" in "a b a" → "1 of 2"; Enter → "2 of 2"; Enter → wraps to "1 of 2".

**Done when:** find bar works.

### 5.6 Test data generator and benchmark `S`

- [ ] `tools/gen-notes`: creates N realistic notes (random words, lists, some images, some locked) in a given DB
- [ ] Benchmark suite runs nightly in CI

**Tests**
- `TC-PERF-005` (P) Search across 10,000 notes < 30 ms (p95 over 100 queries).
- `TC-PERF-006` (P) Cold start with 10,000 notes < 500 ms.

**Done when:** Plan §10 search budget met.

---

## Phase 6: Locked Notes (Weeks 21–23)

**Goal:** Individual notes protected by a notes password (**Plan §8.4**).
**Exit criteria:** A locked note's content can't be read from the open, unlocked app without the password, and auto-relock works.

### 6.1 Crypto primitives `M`

- [ ] `Kdf::derive(password, salt, ops, mem)` → `SecureBuffer` (Argon2id `crypto_pwhash`)
- [ ] `Aead::encrypt(key, plaintext, associatedData)` / `decrypt(...)` using XChaCha20-Poly1305 with a random 24-byte nonce
- [ ] Use the **note's UUID as associated data**, so an encrypted body can't be swapped onto another note
- [ ] `KeyWrap::wrap(kek, key)` / `unwrap(...)`
- [ ] Store KDF params (`opslimit`, `memlimit`, algorithm id) next to every salt so they can be raised later

**Tests**
- `TC-CRY-004` (U) Known-answer tests from libsodium's test vectors for Argon2id and XChaCha20-Poly1305.
- `TC-CRY-005` (U) Encrypt → decrypt round-trip for empty, 1-byte, and 10 MB inputs.
- `TC-CRY-006` (U) Flipping any single bit of nonce, ciphertext, or tag → decrypt fails (data-driven over positions).
- `TC-CRY-007` (U) Decrypt with a different associated data (other note UUID) → fails.
- `TC-CRY-008` (U) Wrong key → fails; no partial plaintext returned.

**Done when:** the crypto API is small, reviewed, and fully tested.

### 6.2 Notes password setup `M`

- [ ] First time the user locks a note → "Create notes password" dialog: password, confirm, optional hint (Plan §8.4 step 10)
- [ ] Minimum length (e.g. 8) and a clear warning that the password cannot be recovered
- [ ] Generate salt, derive Lock Key, store salt + params + **verifier** (encryption of a known constant) in `meta`

**Tests**
- `TC-LCK-001` (U) Setup → `meta` has salt, params, verifier; **no** password or hash of it.
- `TC-LCK-002` (U) Verifier check: correct password → OK; wrong → `WrongPassword`.
- `TC-LCK-003` (UI) Mismatched confirmation → dialog shows error, OK disabled.

**Done when:** password can be set and verified.

### 6.3 Lock service `L` → broken down

- [ ] **(M)** `lock(noteId)`: generate note key, encrypt `body_html` → `locked_blob`, wrap note key with Lock Key → `wrapped_key`, set `body_html`, `body_text`, `snippet` to NULL, update FTS (title only), all in one transaction
- [ ] **(M)** Encrypt the note's attachments with the note key
- [ ] **(S)** Enable `PRAGMA secure_delete = ON` so old plaintext pages are overwritten, not just marked free
- [ ] **(M)** `unlock(noteId)`: decrypt into memory only; editing an unlocked note re-encrypts on autosave (plaintext never written to disk)
- [ ] **(S)** `removeLock(noteId)`: decrypt and store as normal note (requires password)
- [ ] **(S)** Tags from a locked note's body are removed while locked (otherwise they leak content)

**Tests**
- `TC-LCK-004` (I) Lock a note with `SECRET-MARKER` in body and image → raw decrypted DB (opened with DK) contains no marker in any table, including FTS shadow tables.
- `TC-LCK-005` (I) Edit an unlocked locked-note, autosave → DB still contains no plaintext.
- `TC-LCK-006` (I) Lock → unlock → content identical, including images.
- `TC-LCK-007` (I) Crash injected mid-lock → note either fully locked or fully unlocked, never half.
- `TC-LCK-008` (U) Locked note's title stays searchable; tags removed.

**Done when:** locked notes are encrypted at the row level.

### 6.4 Locked notes UI `M`

- [ ] Lock toolbar button toggles lock for the current note
- [ ] Note list: lock icon + "Locked" instead of snippet (Plan §3.4)
- [ ] Editor: lock screen with password field and hint button when locked
- [ ] Unlocking one note unlocks all locked notes for the session (macOS behavior)

**Tests**
- `TC-UI-031` (UI) Lock current note → list row shows 🔒 and "Locked"; editor shows lock screen.
- `TC-UI-032` (UI) Enter correct password → content shown; wrong → shake/error and delay.
- `TC-UI-033` (UI) Hint button shows the saved hint.

**Done when:** UI matches Plan §3.4 and §8.4.

### 6.5 Session and auto-relock `M`

- [ ] Lock Key kept in a `SecureBuffer` while the session is unlocked
- [ ] Idle timer (default 5 min, configurable) reset by an app-wide event filter on key/mouse input
- [ ] Relock on screen lock: listen to D-Bus `org.freedesktop.login1.Session.Lock` and `org.gnome.ScreenSaver.ActiveChanged`
- [ ] Relock on app close and on suspend
- [ ] On relock: wipe Lock Key and decrypted buffers, clear the editor's document and undo stack

**Tests**
- `TC-LCK-009` (U) `FakeClock` +4:59 without input → still unlocked; +5:00 → locked.
- `TC-LCK-010` (U) Input event at 4:00 resets the timer.
- `TC-LCK-011` (I) Emit fake `ActiveChanged(true)` on a test D-Bus session → notes relock.
- `TC-LCK-012` (UI) After relock, `Ctrl+Z` in the editor cannot bring back plaintext.

**Done when:** notes relock automatically.

### 6.6 Brute-force protection `S`

- [ ] Exponential delay after failed attempts: 1 s, 2 s, 4 s … capped at 5 min
- [ ] Failure count stored in `meta`, so restarting the app does not reset it
- [ ] Reset on success

**Tests**
- `TC-LCK-013` (U) 3 failures → 4th attempt rejected until 4 s have passed (`FakeClock`).
- `TC-LCK-014` (I) 3 failures, restart → delay still applies.

**Done when:** guessing is slow.

### 6.7 Memory hygiene for plaintext `S`

`QString`/`QTextDocument` copy data internally, so plaintext can't be wiped perfectly. Do the best possible and document the limit honestly.

- [ ] Decrypted bytes go into `SecureBuffer` first; convert to `QString` only for display
- [ ] On relock: `QTextDocument::clear()`, clear undo stack, `sodium_memzero` every buffer we own
- [ ] Document the limitation in `docs/security.md` (Plan §8.1 already marks same-user malware as "partial")

**Tests**
- `TC-LCK-015` (U) After relock, the `SecureBuffer` holding the Lock Key reports empty.

**Done when:** limitation documented and best-effort wipe in place.

### 6.8 P1 lock extras (cheap to do now) `M`

- [ ] **Lock all now** `Ctrl+Shift+Alt+L`
- [ ] **Change notes password:** verify old, derive new Lock Key, **re-wrap every note key** in one transaction (note bodies are not re-encrypted, see Plan §8.2)
- [ ] **Clipboard auto-clear:** text copied from a locked note is cleared 30 s later, only if the clipboard still holds that text

**Tests**
- `TC-LCK-016` (UI) `Ctrl+Shift+Alt+L` → all notes locked, editor shows lock screen.
- `TC-LCK-017` (I) Change password on 50 locked notes → old password fails, new password opens all 50; `locked_blob` bytes unchanged.
- `TC-LCK-018` (I) Change password with crash injected mid-way → either all notes use old password or all use new.
- `TC-LCK-019` (UI) Copy from locked note, wait 30 s (`FakeClock`) → clipboard empty. Copy something else meanwhile → not cleared.

**Done when:** all lock features from Plan §4 → Security work.

---

## 🎯 MVP Release: Private Beta (Week 24)

**Goal:** All **P0** features in the hands of 5–10 testers.

- [ ] Every P0 row in [Feature → Task Traceability](#feature--task-traceability) is done
- [ ] Full test suite green, ASan/UBSan clean
- [ ] Quick `.deb` build for testers (full packaging comes in Phase 11)
- [ ] `docs/qa-checklist.md` written and run once by you: fresh install, create/edit/delete, images, lock/unlock, restart, search
- [ ] Beta notes for testers: what to try, how to report bugs (GitHub issues template), **no telemetry** is collected
- [ ] Warn testers: beta data format may change; migrations will handle it, but export important notes
- [ ] Tag `v0.1.0-beta`

**Tests**
- `TC-REL-001` (M) Complete QA checklist on a clean Ubuntu VM.
- `TC-REL-002` (I) Upgrade test: DB created by each earlier phase build migrates to the beta schema.

---

## Phase 7: Editor P1 Features (Weeks 25–29)

**Goal:** Tables, attachments, colors, note links, collapsible sections, tags, find & replace.

### 7.1 Tables `L` → broken down

- [ ] **(M)** Insert table `Ctrl+Alt+T` (default 3 × 3); `Tab`/`Shift+Tab` move between cells; `Tab` in the last cell adds a row
- [ ] **(M)** Context menu: add row above/below, add column left/right, delete row/column, delete table
- [ ] **(S)** Table styling for light and dark themes
- [ ] **(S)** Round-trip support

**Tests**
- `TC-EDT-034` (UI) `Ctrl+Alt+T` → 3 × 3 table, cursor in cell (0,0).
- `TC-EDT-035` (UI) `Tab` in last cell → 4 rows.
- `TC-EDT-036` (UI) Delete column 2 of 3 → 2 columns, content of others kept.
- `TC-EDT-037` (U) Round-trip preserves table structure and cell formatting.

### 7.2 File attachments `L` → broken down

- [ ] **(M)** Attach file `Ctrl+Shift+A` or drag & drop; stored in `attachments` table
- [ ] **(M)** Render as a card (icon, name, size) via `QTextObjectInterface`
- [ ] **(S)** Actions: Open, Save As…, Remove
- [ ] **(S)** "Open" writes to a private temp dir (`0700`) and deletes the file when the app closes; for locked notes, warn that the opened copy is unencrypted
- [ ] **(S)** Size limit (e.g. 100 MB) with clear message

**Tests**
- `TC-EDT-038` (I) Attach PDF → card shows name and size; restart → still there.
- `TC-EDT-039` (I) Save As → output file hash equals original.
- `TC-EDT-040` (I) Open, then quit → temp file deleted.
- `TC-EDT-041` (I) Attachment of a locked note is encrypted in DB (marker test).

### 7.3 Highlight and text colors `S`

- [ ] Small fixed palette (6–8 colors) stored as **semantic names** (e.g. `yellow`), mapped to theme-appropriate colors at render time so they stay readable in dark mode

**Tests**
- `TC-EDT-042` (U) Round-trip preserves semantic color names.
- `TC-EDT-043` (UI) Switch theme → highlighted text color changes to the dark-mode variant.

### 7.4 Links to other notes `M`

- [ ] Typing `>>` opens a completer popup searching note titles (uses `SearchService`)
- [ ] Inserted link stores `note://<uuid>`; displays the **current** title of the target (rename-safe)
- [ ] Click opens the target note; deleted target shows as "Note not found" (strikethrough)

**Tests**
- `TC-EDT-044` (UI) `>>shop` → popup lists "Shopping List"; Enter inserts link.
- `TC-EDT-045` (I) Rename target note → link text updates.
- `TC-EDT-046` (I) Trash target → link shows broken state; restore → works again.

### 7.5 Collapsible sections `M`

- [ ] Fold arrow appears on hover next to Title/Heading/Subheading blocks
- [ ] Folding hides blocks until the next heading of the same or higher level (`QTextBlock::setVisible` + relayout)
- [ ] Decide whether fold state is saved (needs the format from ADR 0006) or session-only

**Tests**
- `TC-EDT-047` (UI) Fold Heading A → its body blocks hidden; Heading B (same level) still visible.
- `TC-EDT-048` (UI) Search match inside a folded section → section unfolds.

### 7.6 Tags `M`

- [ ] Parser: `#tag` in plain text (Unicode letters, digits, `_`, `-`); ignore inside URLs, code/monospace blocks, and `#` alone
- [ ] Update `tags` / `note_tags` on save; delete unused tags
- [ ] Sidebar "Tags" section lists tags; click filters the note list
- [ ] Tags not extracted from locked notes (see 6.3)

**Tests**
- `TC-EDT-049` (U) Data-driven parser: `"#work"` ✓, `"#über"` ✓, `"https://x.com/#anchor"` ✗, `"# heading"` ✗, `"C#"` ✗, `"#a-b_c"` ✓.
- `TC-EDT-050` (I) Remove last `#home` from all notes → tag disappears from sidebar.

### 7.7 Find & replace `S`

- [ ] Extend find bar (5.5) with Replace / Replace All; Replace All is one undo step

**Tests**
- `TC-EDT-051` (UI) Replace All "a"→"b" in "a a a" → "b b b"; one `Ctrl+Z` → "a a a".

### 7.8 Search P1 extras `S`

- [ ] Search attachment file names (Plan §4 → Search)

**Tests**
- `TC-SRC-011` (I) Note with attachment "invoice-2026.pdf" found by searching "invoice".

**Phase 7 done when:** all P1 editing features pass round-trip tests.

---

## Phase 8: Gallery, Smart Folders & Export (Weeks 30–31)

### 8.1 Gallery view `M`

- [ ] `QListView` in `IconMode` with a card delegate: thumbnail (first image) or text preview
- [ ] `Ctrl+1` list / `Ctrl+2` gallery; choice saved per folder
- [ ] Locked notes show a lock card, no preview

**Tests**
- `TC-UI-034` (UI) `Ctrl+2` → gallery; `Ctrl+1` → list; restart → per-folder choice kept.
- `TC-PERF-007` (P) Gallery with 10,000 notes scrolls at < 16 ms/frame (thumbnails cached).

### 8.2 Smart folders `L` → broken down

- [ ] **(S)** Migration: `smart_folders` table (id, uuid, name, rules JSON, version, sync_state)
- [ ] **(M)** Rule model: tag is/isn't, created/edited within N days, has checklist, has attachments, is locked; match all/any
- [ ] **(M)** Rule → SQL builder with **bound parameters only**
- [ ] **(M)** Editor dialog to create/edit rules; smart folders shown in sidebar

**Tests**
- `TC-STO-033` (U) Data-driven rule sets → expected note IDs on a fixture DB.
- `TC-STO-034` (U) Tag value `x'); DROP TABLE notes;--` → no injection, zero results.
- `TC-STO-035` (I) New note matching a rule appears in the smart folder without restart.

### 8.3 Export `M`

- [ ] Export one note or all notes: **Markdown** (`toMarkdown()` + attachments in a folder), **PDF** (`QPdfWriter`), **plain text**
- [ ] Export all → folder structure mirrors the app's folders
- [ ] Locked notes: export only if unlocked; otherwise skipped with a summary
- [ ] Warning before export: "Exported files are not encrypted"

**Tests**
- `TC-EXP-001` (I) Export note with heading, list, checklist, image → Markdown contains `#`, `-`, `- [x]`, and a relative image link that exists.
- `TC-EXP-002` (I) PDF export produces a valid PDF (starts with `%PDF`, page count ≥ 1).
- `TC-EXP-003` (I) Export all with 2 locked notes while locked → summary says "2 skipped"; no locked content in output.
- `TC-EXP-004` (I) Note titles with `/`, `:`, `..` produce safe file names (no path traversal).

### 8.4 Print `S`

- [ ] `Ctrl+P` with `QPrintDialog`

**Tests**
- `TC-EXP-005` (M) Print a note with an image and a table on a real printer or "Print to File".

---

## Phase 9: Google Sync (Weeks 32–38)

**Goal:** End-to-end encrypted sync and restore via Google Drive `appDataFolder` (**Plan §9**).
**Exit criteria:** Two machines stay in sync; a third machine restores everything with sign-in + password or recovery key; Google only stores ciphertext.

### 9.0 Gaps in the plan to resolve first `S`

Record decisions in `docs/adr/0007-sync-design.md`:

- [ ] **Conflict detection needs a base version.** "Remote version > local" (Plan §9.5) can't tell "only remote changed" from "both changed". Store the last-synced version per item (`synced_version`) and compare local vs base vs remote.
- [ ] **Locked notes across devices.** The notes-password salt, KDF params, and verifier (6.2) must sync too, or the second device can't unlock locked notes. Put them in an encrypted `settings` record.
- [ ] **Drive file IDs.** Drive uses its own file IDs; add a `sync_map(uuid, kind, drive_file_id, synced_version)` table.
- [ ] **Smart folders and settings** sync as their own record kinds.
- [ ] **Per-item subkey ID.** `crypto_kdf_derive_from_key` takes a 64-bit subkey ID; derive it from the item UUID (first 8 bytes of BLAKE2b(uuid)) and also bind the UUID as associated data.

**Done when:** ADR merged; migration for `sync_map` and `synced_version` written.

### 9.1 Google Cloud project setup (manual) `S`

- [ ] Create project, enable Drive API, OAuth consent screen, **Desktop app** client, scope `drive.appdata` only (Plan §9.1)
- [ ] Keep client ID/secret out of git: pass via CMake variable / CI secret (`.gitignore` already blocks `client_secret*.json`)
- [ ] Write the privacy policy draft now (`docs/privacy-policy.md`); verification takes time (Plan §15)

**Tests**
- `TC-OAU-001` (M) Manual sign-in with a test Google account succeeds and the consent screen lists only the app-data scope.

### 9.2 Mock Drive server (build this before the client) `M`

- [ ] `tests/integration/MockDriveServer` using `QHttpServer` (or a small Python script started by CTest)
- [ ] Implements the endpoints used: files list/create/update/get/delete in `appDataFolder`, multipart + resumable upload, `changes.getStartPageToken`, `changes.list`, OAuth token endpoint
- [ ] Fault injection: HTTP 401, 429, 500, 503, slow responses, dropped connections, truncated bodies
- [ ] Inspection API for tests: list stored files, read raw bytes

**Tests**
- `TC-DRV-001` (U) Mock server self-test: upload then download returns same bytes; changes list returns the change.

### 9.3 OAuth 2.0 with PKCE `M`

- [ ] `QOAuth2AuthorizationCodeFlow` + `QOAuthHttpServerReplyHandler` on `127.0.0.1` with a random port
- [ ] PKCE S256 (built in from Qt 6.8; otherwise implement manually per ADR 0001)
- [ ] Opens the system browser; shows "Waiting for browser…" with Cancel
- [ ] Refresh token stored **only** in the keyring via `IKeyStore` (Plan §9.2)
- [ ] Automatic refresh on expiry; sign-out revokes the token and removes it

**Tests**
- `TC-OAU-002` (I) Against mock token endpoint: auth request contains `code_challenge` and `code_challenge_method=S256`; token request contains `code_verifier`.
- `TC-OAU-003` (I) Access token expired → client refreshes once and retries the request.
- `TC-OAU-004` (I) Refresh token revoked (mock returns `invalid_grant`) → UI state "Signed out, please sign in again"; local notes untouched.
- `TC-OAU-005` (I) After sign-in, the refresh token exists in `InMemoryKeyStore` and nowhere in the DB or `QSettings` (grep files).

### 9.4 Drive client `L` → broken down

- [ ] **(M)** `IDriveApi` interface + `DriveClient`: list, create, update, download, delete in `appDataFolder`
- [ ] **(M)** Multipart upload for ≤ 5 MB; resumable upload for larger attachments
- [ ] **(S)** `changes.getStartPageToken`, `changes.list` with paging
- [ ] **(M)** Retry policy: exponential backoff with jitter for 429/5xx/network errors, max attempts, respects `Retry-After`
- [ ] **(S)** Typed errors: `AuthError`, `QuotaError`, `NetworkError`, `NotFound`, `ServerError`

**Tests**
- `TC-DRV-002` (I) Upload/download/delete round-trip on mock server.
- `TC-DRV-003` (I) Mock returns 503 twice then 200 → succeeds; 3 requests observed; delays increase.
- `TC-DRV-004` (I) Mock returns 429 with `Retry-After: 2` → client waits ≥ 2 s (`FakeClock`).
- `TC-DRV-005` (I) 20 MB attachment uses resumable upload; connection dropped at 50 % → resumes and completes.
- `TC-DRV-006` (I) `changes.list` with 3 pages → all changes returned, final token saved.

### 9.5 Recovery key `M`

- [ ] Generate 256 bits of entropy, encode as **24 words** with a checksum (BIP-39-style English wordlist; confirm the wordlist's license before bundling)
- [ ] Parser tolerant of case, extra spaces, line breaks; rejects wrong checksum with "word N may be wrong"
- [ ] KEK from recovery key: fast KDF (BLAKE2b / `crypto_kdf`), since the input is already high-entropy

**Tests**
- `TC-CRY-009` (U) Encode → decode returns identical 32 bytes (1,000 random keys).
- `TC-CRY-010` (U) Changing any single word → checksum error.
- `TC-CRY-011` (U) `"  ABANDON ability\nable ..."` (case/whitespace variants) parses correctly.
- `TC-CRY-012` (F) Fuzz target for the recovery-key parser.

### 9.6 Account Master Key and `keys.bin` `M`

- [ ] Generate AMK (32 bytes)
- [ ] `keys.bin` format: `magic | version | kdf params | salt | AMK wrapped by sync-password KEK | AMK wrapped by recovery KEK`
- [ ] Encoder/decoder with strict length checks

**Tests**
- `TC-CRY-013` (U) Sync password path and recovery key path both unwrap the same AMK.
- `TC-CRY-014` (U) Wrong password → `WrongPassword`; wrong recovery key → `WrongRecoveryKey`.
- `TC-CRY-015` (U) Truncated, extended, or bit-flipped `keys.bin` → clean error, never a crash.
- `TC-CRY-016` (U) Unknown future `version` → `UnsupportedVersion`.
- `TC-CRY-017` (F) Fuzz target for the `keys.bin` decoder.

### 9.7 Record encryption `M`

- [ ] Serialize each record kind (folder, note, attachment, smart folder, settings) with `QCborValue`, including a schema version
- [ ] Encrypt: `version | nonce | XChaCha20-Poly1305(ciphertext)` with a per-item subkey from AMK; associated data = `kind | uuid | record version` (Plan §9.4)
- [ ] Locked notes: upload `locked_blob` + `wrapped_key` as-is (already encrypted with the note key)
- [ ] File names are UUIDs only

**Tests**
- `TC-SYN-001` (U) Encrypt → decrypt round-trip for every record kind.
- `TC-SYN-002` (U) Moving an encrypted note file to another UUID's name → decrypt fails (associated data check).
- `TC-SYN-003` (I) After full upload of notes containing `SECRET-MARKER` and their titles, **no file on the mock server** contains the marker or any note title.
- `TC-SYN-004` (F) Fuzz target for the record decoder.

### 9.8 Sync setup wizard `M`

- [ ] Steps: Sign in → choose sync password (+ confirm) → show recovery key (Print / Save / "I've written it down") → **retype 3 random words** to confirm (Plan §15 mitigation) → upload `keys.bin` → initial upload with progress
- [ ] Clear warning from Plan §9.6: losing both password and recovery key = data unrecoverable
- [ ] Save recovery key as file uses a file dialog and warns where it's being saved

**Tests**
- `TC-UI-035` (UI) Wizard can't proceed until the 3 words match.
- `TC-UI-036` (UI) Cancel at any step → nothing uploaded, no tokens left in key store.
- `TC-SYN-005` (I) Wizard completion → mock server has `keys.bin` and one file per note/folder.

### 9.9 Push `M`

- [ ] Find dirty rows (`sync_state = 0`) for all record kinds
- [ ] Encrypt + upload/update, record `drive_file_id` and `synced_version` in `sync_map`
- [ ] Mark `sync_state = 1` **only if the row wasn't edited during upload** (compare version)

**Tests**
- `TC-SYN-006` (I) 10 dirty notes → 10 uploads → all clean.
- `TC-SYN-007` (I) Edit a note while its upload is in flight → stays dirty and uploads again next cycle.
- `TC-SYN-008` (I) Network error mid-push → already-uploaded items clean, rest still dirty; next push finishes.

### 9.10 Pull `M`

- [ ] First sync: list all files; later syncs: `changes.list` from saved `pageToken` (stored in `meta`)
- [ ] Download + decrypt changed items; hand to the merge step
- [ ] Save the new `pageToken` only after changes are applied (in the same transaction)

**Tests**
- `TC-SYN-009` (I) Device B pulls 50 notes created on device A → identical content.
- `TC-SYN-010` (I) Crash after downloading but before commit → next pull re-applies the same changes; no duplicates.

### 9.11 Merge and conflict resolution `L` → broken down

- [ ] **(M)** `ConflictResolver` as a **pure function** `(local, base, remote) → action` so it's fully unit-testable
- [ ] **(S)** Remote changed, local not → apply remote
- [ ] **(S)** Local changed, remote not → keep local (push later)
- [ ] **(M)** Both changed → newer wins; the other saved as **"<title> (Conflicted copy)"** in the same folder; never lose data (Plan §9.5)
- [ ] **(M)** Edit vs delete → edit wins (note restored), deletion recorded as overridden
- [ ] **(S)** Note moved into a folder that was deleted remotely → note goes to "Notes"
- [ ] **(S)** Folder cycles created by concurrent moves → break the cycle deterministically

**Tests**
- `TC-SYN-011` (U) Table-driven test covering every `(local changed?, remote changed?, local deleted?, remote deleted?)` combination.
- `TC-SYN-012` (I) Two devices edit the same note offline, both sync → one note with the newer content + one "Conflicted copy" with the other; no text lost.
- `TC-SYN-013` (I) A deletes note, B edits it → after sync both devices have the edited note.
- `TC-SYN-014` (I) A moves folder X into Y; B moves Y into X → no cycle; both devices end in the same tree.
- `TC-SYN-015` (I) Conflicting locked notes: conflicted copy is also locked.

### 9.12 Tombstones `S`

- [ ] Permanent deletes become tombstones (`deleted_at`), synced, purged after 30 days (Plan §9.5 step 4)

**Tests**
- `TC-SYN-016` (I) Delete permanently on A → removed on B after sync.
- `TC-SYN-017` (U) Tombstones older than 30 days removed locally and remotely (`FakeClock`).

### 9.13 Sync engine and scheduling `M`

- [ ] `SyncEngine` on its own `QThread` (Plan §6); only one sync runs at a time
- [ ] Triggers: app start, 5 s after editing stops, every 5 min, network reconnect (`QNetworkInformation::reachabilityChanged`)
- [ ] Status shown in the sidebar account header: Synced / Syncing… / Offline / Error (with details)
- [ ] App fully usable offline (Plan §9.5 step 6)

**Tests**
- `TC-SYN-018` (U) Triggers during a running sync coalesce into one follow-up sync.
- `TC-SYN-019` (I) Edit → sync starts 5 s after last keystroke (`FakeClock`).
- `TC-SYN-020` (I) Offline → edits saved locally, status "Offline"; reconnect → syncs within seconds.
- `TC-PERF-008` (P) Typing latency unchanged (< 16 ms) during a sync of 1,000 notes.

### 9.14 Restore on a new device `M`

- [ ] After sign-in, if `keys.bin` exists → "Enter your sync password" or "Use recovery key" (Plan §9.6)
- [ ] Download and decrypt everything with progress; app usable once notes list is in (attachments can keep downloading)
- [ ] Local DB not empty → merge with remote (uses 9.11), never wipe local notes

**Tests**
- `TC-SYN-021` (I) Device A with 100 notes, 10 locked, 20 images → device C restores with sync password → identical content; locked notes unlock with the same notes password.
- `TC-SYN-022` (I) Restore with recovery key → same result.
- `TC-SYN-023` (I) Device with 5 local notes restores → 105 notes, nothing lost.
- `TC-SYN-024` (M) Real Google account: restore on a fresh Ubuntu VM.

### 9.15 Account management `M`

- [ ] Sign out (keep local notes, stop syncing)
- [ ] Change sync password (re-wrap AMK in `keys.bin` only)
- [ ] Show recovery key again? **No**; offer "Generate new recovery key" (re-wraps AMK, requires sync password)
- [ ] "Delete all cloud data" with strong confirmation

**Tests**
- `TC-SYN-025` (I) Change sync password → new password restores on a new device; old password fails.
- `TC-SYN-026` (I) New recovery key → old key fails, new key works.
- `TC-SYN-027` (I) Delete cloud data → mock server empty; local notes intact.

---

## Phase 10: Hardening (Weeks 39–42)

**Goal:** Prove security, robustness, and performance before public release.

### 10.1 Fuzzing `M`

- [ ] libFuzzer targets (clang `fuzz` preset): note HTML import/sanitizer, image decoding with allocation limits, sync record decoder, `keys.bin` decoder, recovery-key parser, FTS query builder
- [ ] Seed corpora in `tests/fuzz/corpus/` (git-ignored output, committed seeds)
- [ ] Nightly 1-hour CI run per target; one 24-hour run per target before v1.0 (Plan §16)

**Tests**
- `TC-SEC-003` (F) All targets run 24 h without crashes, leaks, or timeouts. Every crash found becomes a `TC-REG-*` unit test.

### 10.2 Security review `L` → broken down

- [ ] **(M)** Write `docs/security.md`: threat model (Plan §8.1), key hierarchy (Plan §8.2), file formats, known limitations
- [ ] **(M)** Self-review checklist: every crypto call, every place plaintext exists, every network request, every file written
- [ ] **(S)** Verify no secrets in: logs, `QSettings`, temp files, crash output, Drive files
- [ ] **(M)** External review of `src/crypto/` and `src/sync/` (Plan §8.6); fix findings

**Tests**
- `TC-SEC-004` (I) End-to-end "marker sweep": create notes with `SECRET-MARKER`, lock some, sync → scan the raw data dir, `~/.config`, `/tmp`, mock server storage, and captured logs → the marker appears nowhere.
- `TC-SEC-005` (M) Copy DB file to another machine → unreadable without the DK.

### 10.3 Robustness `M`

- [ ] Disk full during save → error shown, previous content intact
- [ ] DB corruption → `PRAGMA quick_check` on start; offer restore from sync or export of readable notes
- [ ] Keyring locked at startup → prompt to unlock, retry
- [ ] Power loss during write (WAL) → no corruption
- [ ] System clock wrong → sync still converges (versions, not timestamps, decide order where possible)

**Tests**
- `TC-STO-036` (I) Fill a small tmpfs, try saving → error surfaced; original note unchanged.
- `TC-STO-037` (I) Kill the process (`SIGKILL`) 100 times at random points during writes → DB always opens and passes `integrity_check`.
- `TC-SYN-028` (I) Device clock 1 day behind → conflicts resolved without data loss.

### 10.4 Performance tuning `M`

- [ ] Run all `TC-PERF-*` benchmarks on a mid-range laptop; profile with `perf` and `heaptrack`
- [ ] Fix regressions; CI fails nightly if a budget regresses by > 10 %

**Tests**
- `TC-PERF-009` (P) Idle RAM < 80 MB with 10,000 notes.
- `TC-PERF-010` (P) Stripped binary < 3 MB.
- All earlier `TC-PERF-*` within Plan §10 budgets.

### 10.5 Accessibility `M`

- [ ] Every action reachable by keyboard; visible focus indicator
- [ ] Accessible names on all buttons (`setAccessibleName`)
- [ ] Test with the Orca screen reader
- [ ] Text contrast ≥ 4.5:1 in both themes; UI respects system font scaling

**Tests**
- `TC-UI-037` (UI) Every toolbar widget has a non-empty accessible name.
- `TC-UI-038` (M) Complete "create, format, lock, search a note" using only the keyboard.
- `TC-UI-039` (M) Orca reads the note list and editor correctly.

### 10.6 Static analysis clean-up `S`

- [ ] Zero `clang-tidy` and `cppcheck` warnings; no `NOLINT` without a comment explaining why

---

## Phase 11: Packaging & Launch Prep (Weeks 43–44)

### 11.1 Desktop integration `S`

- [ ] `.desktop` file, app icon in hicolor sizes + SVG, AppStream `metainfo.xml` with screenshots
- [ ] Reverse-DNS app ID consistent everywhere (Flatpak, desktop file, `QSettings`)

**Tests**
- `TC-PKG-001` (U) `desktop-file-validate` and `appstreamcli validate` pass in CI.

### 11.2 `.deb` package `M`

- [ ] `packaging/debian/`: `control` (depends on system Qt 6, SQLCipher, libsodium, QtKeychain), `rules`, `changelog`, `copyright`
- [ ] Build in CI on tag

**Tests**
- `TC-PKG-002` (I) `lintian` reports no errors.
- `TC-PKG-003` (I) `.deb` size < 5 MB (Plan §10).
- `TC-PKG-004` (M) Install on a clean VM → app launches from the menu; uninstall leaves user data in place; `purge` docs explain where data lives.
- `TC-PKG-005` (M) Upgrade from beta `.deb` → data migrated.

### 11.3 Flatpak `M`

- [ ] Manifest `packaging/flatpak/<app-id>.yml` on the KDE runtime; build SQLCipher, libsodium, QtKeychain as modules
- [ ] Minimal permissions: `--share=network`, `--talk-name=org.freedesktop.secrets`, `--socket=wayland`, `--socket=fallback-x11`, `--device=dri`; **no** `--filesystem` (use the file chooser portal for attachments and export)
- [ ] Screen-lock D-Bus names for auto-relock (6.5)

**Tests**
- `TC-PKG-006` (I) `flatpak-builder` succeeds in CI.
- `TC-PKG-007` (M) In Flatpak: keyring works, attach file via portal works, export via portal works, sync works, auto-relock on screen lock works.

### 11.4 Release pipeline `S`

- [ ] GitHub Actions on `v*` tags: build `.deb` + Flatpak bundle, generate `SHA256SUMS`, GPG-sign it, create GitHub Release with changelog
- [ ] Version number in one place (CMake), shown in About dialog

**Tests**
- `TC-PKG-008` (I) Release dry-run on a test tag produces signed checksums that verify with `gpg --verify`.

### 11.5 Google verification, website, and docs `M`

- [ ] Publish privacy policy at a public URL (Plan §9.1)
- [ ] Submit Google OAuth verification (start as early as Phase 9)
- [ ] Simple website: features, download, privacy policy, security overview
- [ ] User guide: getting started, locked notes, sync setup, **recovery key warning**, restoring on a new computer

**Tests**
- `TC-REL-003` (M) Google consent screen shows the verified app name (no "unverified app" warning).

---

## 🚀 v1.0 Public Release (Week 45)

Maps to **Plan §16 Definition of Done**:

- [ ] Three-pane UI matching macOS Notes layout and workflow → Phases 1–3, 7–8
- [ ] All P0 and P1 features working → [traceability table](#feature--task-traceability) fully ticked
- [ ] DB encrypted at rest; copied file unreadable → `TC-ENC-001`, `TC-SEC-005`
- [ ] Locked notes require password; auto-relock works → `TC-LCK-004`, `TC-LCK-009`, `TC-LCK-011`
- [ ] Google stores only ciphertext → `TC-SYN-003`, `TC-SEC-004`
- [ ] Full restore on new machine → `TC-SYN-021`, `TC-SYN-022`, `TC-SYN-024`
- [ ] Performance budgets met → all `TC-PERF-*`
- [ ] `.deb` < 5 MB; Flatpak sandboxed → `TC-PKG-003`, `TC-PKG-007`
- [ ] No ASan/UBSan errors; fuzzers 24 h clean → CI + `TC-SEC-003`
- [ ] Privacy policy published; OAuth verified → `TC-REL-003`
- [ ] QA checklist run on a clean VM for `.deb` and Flatpak
- [ ] Tag `v1.0.0`

---

## Backlog: P2 (after v1.0)

Not scheduled. Each gets broken down like the tasks above when picked up.

| Feature | Notes |
|---|---|
| Sketch / drawing | Custom `QTextObjectInterface` + vector stroke storage |
| Audio recording + transcription | Local transcription only (privacy); large dependency, check size budget |
| Math results (`25*4=`) | Safe expression parser, **no** `eval`-style scripting |
| Import from Markdown / plain text | Reuse export format; fuzz the importer |
| Quick Note global shortcut | Wayland global shortcuts need the portal (`org.freedesktop.portal.GlobalShortcuts`) |
| Snap package | After Flatpak is stable |

---

## Feature → Task Traceability

Every feature from **Plan §4** and the task that delivers it.

| Feature | Priority | Task(s) |
|---|---|---|
| Folders + nested subfolders | P0 | 2.5, 2.8, 2.11 |
| All Notes view | P0 | 2.11 |
| Pin notes to top | P0 | 2.6, 2.11 |
| Recently Deleted (30 days) | P0 | 2.10 |
| Move notes via drag & drop | P0 | 2.12 |
| Sort by edited / created / title | P0 | 2.6, 2.11 |
| Tags | P1 | 7.6 |
| Smart Folders | P1 | 8.2 |
| Gallery view | P1 | 8.1 |
| Bold, italic, underline, strikethrough | P0 | 3.4 |
| Title / Heading / Subheading / Body / Monospaced | P0 | 3.2, 3.3 |
| Bulleted, dashed, numbered lists | P0 | 3.5 |
| Checklists | P0 | 3.6 |
| Autosave | P0 | 2.9 |
| Undo / redo | P0 | 3.10 |
| Images | P0 | 3.7 |
| Tables | P1 | 7.1 |
| File attachments | P1 | 7.2 |
| Highlight & text colors | P1 | 7.3 |
| Links to other notes | P1 | 7.4 |
| Collapsible sections | P1 | 7.5 |
| Find & replace in note | P1 | 5.5, 7.7 |
| Instant full-text search | P0 | 5.1–5.4, 5.6 |
| Search highlights | P1 | 5.4 |
| Search attachment file names | P1 | 7.8 |
| Encrypted database | P0 | 4.1–4.5 |
| Lock notes with password | P0 | 6.1–6.4 |
| Auto-relock after inactivity | P0 | 6.5 |
| Lock all notes now | P1 | 6.8 |
| Change notes password | P1 | 6.8 |
| Clipboard auto-clear | P1 | 6.8 |
| Sign in with Google | P1 | 9.1, 9.3 |
| E2E encrypted sync | P1 | 9.0, 9.2, 9.4, 9.6, 9.7, 9.9–9.13 |
| Recovery key (24 words) | P1 | 9.5, 9.8 |
| Restore on new device | P1 | 9.14 |
| Conflict handling | P1 | 9.11 |
| Export all notes (Markdown / PDF) | P1 | 8.3 |
| Dark / light mode | P0 | 1.8 |
| Keyboard shortcuts | P0 | 1.4 |
| Print / export to PDF | P1 | 8.3, 8.4 |
