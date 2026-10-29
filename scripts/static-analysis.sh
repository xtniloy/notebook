#!/usr/bin/env bash
# Runs clang-tidy and cppcheck over first-party sources. Any finding fails the run.
# Needs a configured build dir with compile_commands.json (default: build/debug).
# Usage: scripts/static-analysis.sh [build-dir]

set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
build_dir="${1:-build/debug}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "error: $build_dir/compile_commands.json not found. Run: cmake --preset debug" >&2
    exit 1
fi
# moc output must exist before analysing sources that include *.moc files.
cmake --build "$build_dir" >/dev/null

echo "== clang-tidy"
# Only our sources, not moc/rcc output under the build dir.
run-clang-tidy -quiet -p "$build_dir" "^$root/(src|tests)/"

echo "== cppcheck"
cppcheck --project="$build_dir/compile_commands.json" \
    --file-filter="$root/src/*" --file-filter="$root/tests/*" \
    --enable=warning,performance,portability --library=qt \
    --suppress='preprocessorErrorDirective:*_autogen/*' \
    --inline-suppr --error-exitcode=1 --quiet \
    --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
    -j "$(nproc)"

echo "Static analysis passed."
