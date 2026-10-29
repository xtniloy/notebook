# Clean, refresh and rebuild

How to get back to a known-good build, and how to make sure the app you are
looking at is the code you just changed.

## Where builds live

| How you build | Output | Run the app |
|---|---|---|
| Native (Ubuntu 26.04, after `scripts/setup-dev.sh`) | `build/<preset>/` in the repo | `./build/debug/src/notesapp` |
| Container (`scripts/dev-container.sh`, VS Code/CLion dev container) | `notesapp-build` Docker volume, mounted over `build/` inside the container only | inside the container: `./build/debug/src/notesapp` (headless; see below to run it on your desktop) |

The two never share files: `CMakeCache.txt` stores absolute paths, so a build
directory only works where it was configured. Your host's `./build` folder is
**never** updated by container builds.

## "I don't see my changes"

1. **Check which binary you ran and when it was built.**

   ```bash
   ls -l --time-style=+%H:%M build/*/src/notesapp
   git log -3 --format='%cd %s' --date=format:%H:%M
   ```

   If the binary is older than your last change, it is stale: rebuild (below).

2. **Close the running app first.** Notes allows one instance at a time (task
   1.1). Launching a new build while an old window is open just brings the old
   window to the front and exits.

3. **Rebuild the preset you run** (`cmake --build --preset debug`). Building
   `debug-asan` or `release` does not update `build/debug/`.

## Native builds

Incremental rebuild (the normal case):

```bash
cmake --build --preset debug
```

Re-run configure from scratch, discarding `CMakeCache.txt` (after editing
`CMakePresets.json`, installing packages, or when configure picked up a wrong
setting). Expect most files to recompile:

```bash
cmake --preset debug --fresh
cmake --build --preset debug
```

Full clean of one preset:

```bash
rm -rf build/debug
cmake --preset debug && cmake --build --preset debug
```

Full clean of everything (all presets):

```bash
rm -rf build
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

Do a full clean when:

- configure fails with *"CMakeCache.txt directory ... is different"* or paths
  like `/work/...` or `/workspaces/...` (the folder was made inside a container);
- you upgraded Ubuntu, Qt, GCC or Clang;
- something fails to link and a fresh configure did not help.

A full rebuild compiles everything again. To make that fast, opt in to ccache
(installed by `setup-dev.sh`; CI already uses it) before configuring:

```bash
export CMAKE_CXX_COMPILER_LAUNCHER=ccache   # e.g. in ~/.bashrc
```

Its cache survives `rm -rf build`; clear it with `ccache -C`.

## Container builds

`scripts/dev-container.sh` rebuilds the image automatically when
`scripts/setup-dev.sh` or `.devcontainer/Dockerfile` change, then runs your
command:

```bash
scripts/dev-container.sh bash -c 'cmake --build --preset debug && ctest --preset debug'
```

Fresh configure inside the container:

```bash
scripts/dev-container.sh bash -c 'cmake --preset debug --fresh && cmake --build --preset debug'
```

Full clean of all container builds (deletes the volume; the next run recreates it):

```bash
docker volume rm notesapp-build
```

Rebuild the image from scratch (for example, to pick up Ubuntu package updates):

```bash
docker build --no-cache -t notesapp-dev:26.04 -f .devcontainer/Dockerfile .
```

VS Code: *Dev Containers: Rebuild Container* does the same.

### Running a container build on your desktop

The container has no display. On Ubuntu 26.04 the host's Qt runtime is the same
version, so a release binary built in the container runs on the host. Copy it
out of the volume (somewhere outside `build/`):

```bash
mkdir -p /tmp/notesapp
docker run --rm -u "$(id -u):$(id -g)" -v notesapp-build:/b:ro -v /tmp/notesapp:/out \
    notesapp-dev:26.04 cp /b/release/src/notesapp /out/
/tmp/notesapp/notesapp
```

Build `release` first if it is missing:
`scripts/dev-container.sh cmake --build --preset release`.

## After a refresh: check it worked

```bash
ctest --preset debug                 # all tests
scripts/check-format.sh              # formatting
./build/debug/src/notesapp           # look at it (close any running Notes first)
```

The full list of checks CI runs is in the [README](../README.md#checks-that-ci-runs).
