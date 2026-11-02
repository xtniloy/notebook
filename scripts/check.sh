#!/usr/bin/env bash
# The checks to run before a change goes in, in two sizes.
#
# Usage: scripts/check.sh quick|full
#   quick  debug build, all tests, format check, and static analysis of the
#          changed files only. For small iterations (a size, a color, a fix):
#          a few minutes.
#   full   what CI runs: all five presets (debug, debug-asan, release, bench,
#          fuzz) with warnings as errors, the tests in debug and under
#          AddressSanitizer, the benchmark, static analysis of every file on
#          build/debug and build/bench, and the format check. Before
#          committing a finished task or pushing.
#
# Needs the dev tools (Qt, clang-tidy, ...); without them, run it in the dev
# container, which also limits CPU and memory:
#   scripts/dev-container.sh scripts/check.sh quick
#
# Exits non-zero if anything fails; every step still runs, so one report shows
# all problems.

set -uo pipefail
cd "$(dirname "$0")/.."

mode="${1:-}"
if [[ "$mode" != quick && "$mode" != full ]]; then
    echo "usage: scripts/check.sh quick|full" >&2
    exit 2
fi

failed=()
step() {
    local name="$1"
    shift
    echo
    echo "==== $name"
    if ! "$@"; then
        failed+=("$name")
    fi
}

build() {
    cmake --preset "$1" -DCMAKE_COMPILE_WARNING_AS_ERROR=ON >/dev/null &&
        cmake --build --preset "$1"
}

start=$SECONDS
if [[ "$mode" == quick ]]; then
    step "build debug" build debug
    step "tests (debug)" ctest --preset debug -L "unit|integration|ui" --output-on-failure
    step "static analysis (changed files)" scripts/static-analysis.sh --changed build/debug
else
    for preset in debug debug-asan release bench fuzz; do
        step "build $preset" build "$preset"
    done
    step "tests (debug)" ctest --preset debug -L "unit|integration|ui" --output-on-failure
    step "tests (AddressSanitizer)" ctest --preset debug-asan --output-on-failure
    step "benchmark" ctest --preset bench -V
    step "static analysis (build/debug)" scripts/static-analysis.sh build/debug
    step "static analysis (build/bench)" scripts/static-analysis.sh build/bench
fi
step "format" scripts/check-format.sh

echo
elapsed=$((SECONDS - start))
if ((${#failed[@]} > 0)); then
    echo "FAILED ($mode, $((elapsed / 60))m $((elapsed % 60))s): ${failed[*]}"
    exit 1
fi
echo "All $mode checks passed ($((elapsed / 60))m $((elapsed % 60))s)."
