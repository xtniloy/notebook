# Contributing

## Workflow per task

From the [task list](../notebook-task-list.md):

1. Write the test cases first (or alongside). They should fail.
2. Implement until they pass.
3. Run the `debug-asan` preset locally.
4. Open a PR. CI must be green before merging.
5. Tick the task's checkboxes in the same PR.

Bug fixes start with a failing regression test named `TC-REG-<issue#>`.

## Hard rules

- **Never log note content.** No titles, bodies, snippets, search queries, passwords or keys in `qDebug()`, `qWarning()`, exceptions, or crash reports. Log IDs and categories only. Tests use `SECRET-MARKER-7f3a` (`notes::test::kSecretMarker`) to prove this.
- **No raw `new`/`delete`** except for Qt objects that get a parent (`new QLabel(this)`), where the parent owns them. Everything else uses `std::unique_ptr`, `std::shared_ptr`, or values.
- **Wrap every user-visible string in `tr()`.**
- **All crypto lives in `src/crypto/`** and only calls libsodium. Never implement crypto algorithms.
- **UI code never touches the database or crypto directly**; it goes through services (Plan §6).
- **Key material lives in `SecureBuffer`** (task 4.1), never in `QString`/`QByteArray`.

## Naming

| Thing | Style | Example |
|---|---|---|
| Files | `PascalCase.h` / `.cpp`, one class per file | `NoteRepository.h` |
| Classes, structs, enums | `PascalCase` | `NoteListModel` |
| Functions, variables | `camelCase` | `listForFolder()` |
| Member variables | `m_` prefix | `m_database` |
| Constants | `k` prefix | `kAutosaveDelayMs` |
| Namespaces | lowercase, per module | `notes::storage` |
| Interfaces | `I` prefix | `IClock`, `IKeyStore` |
| Test files / classes | `tst_<lowercase>.cpp` / `tst_PascalCase` | `tst_noterepository.cpp` |

Includes use paths relative to `src/`: `#include "storage/Database.h"`.

## Formatting and static analysis

`.clang-format` and `.clang-tidy` are the source of truth. Before pushing:

```bash
scripts/check-format.sh --fix
scripts/static-analysis.sh
```

Disable a clang-tidy check inline (`// NOLINT(check-name)`) only with a comment saying why.

## Adding a test

One line in the folder's `CMakeLists.txt`:

```cmake
notes_add_test(NAME tst_noterepository SOURCES tst_noterepository.cpp LABELS unit)
```

Labels: `unit`, `integration`, `ui`, `bench`, `slow`. UI tests add `LIBS notesapp_ui`.
Each test gets its own `QTemporaryDir` (`notes::test::TempDb`) and must never touch `~/.local/share` or the real keyring. Use `FakeClock` and `InMemoryKeyStore` from `tests/common/`.
Every `QAbstractItemModel` subclass is tested with `QAbstractItemModelTester`.

## Architecture decisions

Significant decisions go in `docs/adr/NNNN-short-title.md` (Context → Options → Decision → Consequences), merged before the code that depends on them.
