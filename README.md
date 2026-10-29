# Notes (working name)

A native Ubuntu notes app with the layout and workflow of macOS Notes, encrypted by default.
C++20 · Qt 6 Widgets · SQLCipher · libsodium.

- [Project plan](notebook-project-plan.md): features, architecture, security design
- [Task list](notebook-task-list.md): phases, tasks, and test cases
- [Architecture decisions](docs/adr/)
- [Contributing](docs/contributing.md)

> Status: **Phase 0** (project setup). The app opens an empty window.

## Naming

The name users see is `NOTES_DISPLAY_NAME` in the top-level `CMakeLists.txt` (currently "Notes", a placeholder that can't ship: GNOME and Apple both have apps called Notes). Change it any time.

Never change `NOTES_APP_ID`, `NOTES_STORAGE_NAME` or `NOTES_ORG_*` once people have data: they decide where settings, notes and keyring entries live. `tst_appcontext` fails if they change.

## Building

Requires **Ubuntu 26.04 or newer** (Qt ≥ 6.8, see [ADR 0001](docs/adr/0001-minimum-platform.md)).

```bash
# 1. Install toolchain and libraries (safe to run again; only installs what's missing)
scripts/setup-dev.sh

# 2. Configure, build, test
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# 3. Run
./build/debug/src/notesapp
```

### Other systems: the dev container

On anything other than Ubuntu 26.04+ (another distro, an older Ubuntu, Windows with WSL 2, macOS), build in the Ubuntu 26.04 container instead. It has the same packages as `setup-dev.sh` and CI. You need Docker.

- **VS Code:** install *Dev Containers*, then *Reopen in Container*. **CLion:** open `.devcontainer/devcontainer.json` and choose *Create Dev Container*. Both use the presets below unchanged.
- **Any terminal:**

  ```bash
  scripts/dev-container.sh                     # shell inside the container
  scripts/dev-container.sh bash -c 'cmake --preset debug && cmake --build --preset debug && ctest --preset debug'
  ```

The first run builds the image (a few minutes); later runs start in seconds. Container builds go to the `notesapp-build` Docker volume, not to `./build`, so they never collide with a native build (`docker volume rm notesapp-build` clears them).

Tests run headless in the container. Showing the app's window from the container needs X11/Wayland forwarding, which is not set up; for GUI work and debugging, a native Ubuntu 26.04 install is the smoothest option. Ubuntu developers can also use `scripts/dev-container.sh` to check a change in a clean environment before opening a PR.

### Presets

| Preset | Use |
|---|---|
| `debug` | Day-to-day development. Warnings are errors. |
| `debug-asan` | Same, with AddressSanitizer + UBSan. Run before every PR: `cmake --preset debug-asan && cmake --build --preset debug-asan && ctest --preset debug-asan` |
| `release` | `-O2`, LTO, stripped binary. No tests. |
| `bench` | Release flags plus the `tests/bench/` performance budgets (Plan §10): `cmake --preset bench && cmake --build --preset bench && ctest --preset bench`. CI runs it nightly. |
| `fuzz` | clang + libFuzzer instrumentation for `tests/fuzz/` targets. |

Build output goes to `build/<preset>/`.

### Tests

```bash
ctest --preset debug -L unit          # one label: unit, integration, ui, bench, slow
ctest --preset debug -R tst_smoke     # one test
```

UI tests run headless (`QT_QPA_PLATFORM=offscreen` is set automatically).

### Checks that CI runs

```bash
scripts/check-format.sh               # clang-format (add --fix to apply)
scripts/static-analysis.sh            # clang-tidy + cppcheck on build/debug
```

### IDE setup

Every preset writes `build/<preset>/compile_commands.json`.

- **CLion:** open the folder; enable the `debug` preset under *Settings → Build → CMake*.
- **VS Code:** install the *clangd* and *CMake Tools* extensions; select the `debug` preset.
- **Qt Creator:** open `CMakeLists.txt`; presets are picked up automatically.

## Layout

```
src/        app · ui · editor · models · services · storage · crypto · sync
tests/      unit · integration · ui · bench · fuzz · common (helpers & fakes)
resources/  icons · fonts · themes · licenses
docs/       adr/ and project docs
scripts/    setup and CI helper scripts
```

`notesapp_core` (storage, crypto, services, sync) has no widget dependency, so most tests link without a GUI. `notesapp_ui` holds the widgets; `notesapp` is just `main.cpp`.
