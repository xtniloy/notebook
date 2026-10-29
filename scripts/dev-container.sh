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

set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE="notesapp-dev:26.04"
WORKDIR="/workspaces/notebook"

# Cached after the first run; reruns only when setup-dev.sh or the Dockerfile change.
docker build --quiet -t "$IMAGE" -f .devcontainer/Dockerfile . >/dev/null

tty_flags=()
if [[ -t 0 && -t 1 ]]; then
    tty_flags=(-it)
fi

# SYS_PTRACE: LeakSanitizer inspects threads at exit.
exec docker run --rm "${tty_flags[@]}" \
    --cap-add SYS_PTRACE \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD:$WORKDIR" \
    -v notesapp-build:"$WORKDIR/build" \
    -w "$WORKDIR" \
    "$IMAGE" "${@:-bash}"
