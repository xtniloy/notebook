#!/usr/bin/env bash
# Runs a command (default: an interactive shell) in the Ubuntu 26.04 dev image,
# building the image first if needed. For contributors not on Ubuntu 26.04, and
# for checking a change in a clean environment.
#
# Usage: scripts/dev-container.sh [command...]
#   scripts/dev-container.sh bash -c 'cmake --preset debug && ctest --preset debug'
#
# Build output lives on the `notesapp-build` Docker volume, not in ./build, so it
# never mixes with a native build. Remove it with: docker volume rm notesapp-build
#
# Resource limits, so a full build can't freeze the machine (each compiler or
# clang-tidy job can use about 1 GB):
#   NOTES_CPUS    CPUs for the container and parallel jobs (default: all but 2)
#   NOTES_MEMORY  memory cap, no swap inside (default: 85% of RAM, e.g. 12g)
# Example: NOTES_CPUS=4 NOTES_MEMORY=6g scripts/dev-container.sh scripts/check.sh full

set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE="notesapp-dev:26.04"
WORKDIR="/workspaces/notebook"

# Cached after the first run; reruns only when setup-dev.sh or the Dockerfile change.
docker build --quiet -t "$IMAGE" -f .devcontainer/Dockerfile . >/dev/null

cores="$(nproc)"
cpus="${NOTES_CPUS:-$((cores > 2 ? cores - 2 : 1))}"
ram_kb="$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)"
memory="${NOTES_MEMORY:-$((ram_kb * 85 / 100 / 1024))m}"

tty_flags=()
if [[ -t 0 && -t 1 ]]; then
    tty_flags=(-it)
fi

# SYS_PTRACE: LeakSanitizer inspects threads at exit.
exec docker run --rm "${tty_flags[@]}" \
    --cap-add SYS_PTRACE \
    --cpus "$cpus" --memory "$memory" --memory-swap "$memory" \
    -e CMAKE_BUILD_PARALLEL_LEVEL="$cpus" -e NOTES_JOBS="$cpus" \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD:$WORKDIR" \
    -v notesapp-build:"$WORKDIR/build" \
    -w "$WORKDIR" \
    "$IMAGE" "${@:-bash}"
