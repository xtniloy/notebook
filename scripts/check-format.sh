#!/usr/bin/env bash
# TC-BLD-001: fails if any C++ source is not clang-formatted.
# Usage: scripts/check-format.sh [--fix]

set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t files < <(find src tests tools \( -name '*.cpp' -o -name '*.h' \) -type f | sort)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "No C++ sources found."
    exit 0
fi

if [[ "${1:-}" == "--fix" ]]; then
    clang-format -i "${files[@]}"
    echo "Formatted ${#files[@]} file(s)."
else
    clang-format --dry-run --Werror "${files[@]}"
    echo "All ${#files[@]} file(s) are formatted."
fi
