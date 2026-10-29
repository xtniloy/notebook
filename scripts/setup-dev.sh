#!/usr/bin/env bash
# Installs everything needed to build, test, and profile NotesApp on Ubuntu 26.04+.
# Idempotent: only missing packages are installed; a second run changes nothing.
#
# Usage: scripts/setup-dev.sh [--ci]
#   --ci   skip debugging/profiling tools (gdb, valgrind, heaptrack, perf)

set -euo pipefail

CI_MODE=0
for arg in "$@"; do
    case "$arg" in
        --ci) CI_MODE=1 ;;
        -h | --help)
            sed -n '2,7p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

# See docs/adr/0001-minimum-platform.md
MIN_UBUNTU="26.04"

PACKAGES=(
    # Build
    build-essential cmake ninja-build pkgconf ccache git
    # Compilers and static analysis
    clang libclang-rt-dev clang-tidy clang-format cppcheck
    # Qt 6
    qt6-base-dev qt6-tools-dev qt6-svg-dev qt6-svg-plugins qt6-networkauth-dev qt6-httpserver-dev
    # Crypto and storage
    libsqlcipher-dev libsodium-dev qtkeychain-qt6-dev
)
if [[ $CI_MODE -eq 0 ]]; then
    PACKAGES+=(gdb valgrind heaptrack linux-tools-generic)
fi

if [[ -r /etc/os-release ]]; then
    # shellcheck source=/dev/null
    . /etc/os-release
    if [[ "${ID:-}" != "ubuntu" ]]; then
        echo "warning: this script targets Ubuntu; detected '${ID:-unknown}'." >&2
    elif dpkg --compare-versions "${VERSION_ID:-0}" lt "$MIN_UBUNTU"; then
        echo "error: Ubuntu $MIN_UBUNTU or newer is required (found $VERSION_ID)." >&2
        echo "       Older releases ship Qt < 6.8; use the Flatpak build instead (ADR 0001)." >&2
        exit 1
    fi
fi

missing=()
for pkg in "${PACKAGES[@]}"; do
    if [[ "$(dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null)" != "install ok installed" ]]; then
        missing+=("$pkg")
    fi
done

if [[ ${#missing[@]} -eq 0 ]]; then
    echo "All ${#PACKAGES[@]} packages already installed. Nothing to do."
    exit 0
fi

SUDO=""
if [[ $EUID -ne 0 ]]; then
    SUDO="sudo"
fi

echo "Installing ${#missing[@]} package(s): ${missing[*]}"
$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends "${missing[@]}"
echo "Done."
