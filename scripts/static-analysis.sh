#!/usr/bin/env bash
# Runs clang-tidy and cppcheck over first-party sources. Any finding fails the run.
# Needs a configured build dir with compile_commands.json (default: build/debug).
# Usage: scripts/static-analysis.sh [--changed[=<git-ref>]] [build-dir]
#   --changed  only the .cpp files changed since <git-ref> (default HEAD: the
#              uncommitted and untracked ones), plus the .cpp files that include
#              a changed header. Much faster; CI and `check.sh full` check all.
# NOTES_JOBS limits parallel jobs (default: all cores). Each clang-tidy job can
# use ~1 GB, so lower it on machines with little RAM, e.g. NOTES_JOBS=4.

set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
changed_since=""
if [[ "${1:-}" == --changed* ]]; then
    changed_since="${1#--changed}"
    changed_since="${changed_since#=}"
    changed_since="${changed_since:-HEAD}"
    shift
fi
build_dir="${1:-build/debug}"
jobs="${NOTES_JOBS:-$(nproc)}"

# All first-party sources, or (with --changed) just the affected ones.
files=()
if [[ -n "$changed_since" ]]; then
    mapfile -t changed < <({
        git diff --name-only "$changed_since" -- src tests
        git ls-files --others --exclude-standard -- src tests
    } | sort -u)
    declare -A wanted=()
    for file in "${changed[@]}"; do
        [[ -f "$file" ]] || continue # deleted
        case "$file" in
        *.cpp) wanted["$file"]=1 ;;
        *.h)
            # Headers aren't in compile_commands.json: check their includers.
            while IFS= read -r includer; do
                wanted["$includer"]=1
            done < <(grep -rlE --include='*.cpp' "#include \"([^\"]*/)?$(basename "$file")\"" src tests || true)
            ;;
        esac
    done
    files=("${!wanted[@]}")
    if ((${#files[@]} == 0)); then
        echo "No changed sources since $changed_since; nothing to analyse."
        exit 0
    fi
    echo "Analysing ${#files[@]} changed file(s) since $changed_since."
fi

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "error: $build_dir/compile_commands.json not found. Run: cmake --preset debug" >&2
    exit 1
fi
# moc output must exist before analysing sources that include *.moc files.
cmake --build "$build_dir" >/dev/null

echo "== clang-tidy"
# Only our sources, not moc/rcc output under the build dir.
if ((${#files[@]} == 0)); then
    run-clang-tidy -quiet -j "$jobs" -p "$build_dir" "^$root/(src|tests)/"
else
    run-clang-tidy -quiet -j "$jobs" -p "$build_dir" "${files[@]/#/^$root/}"
fi

echo "== cppcheck"
filters=(--file-filter="$root/src/*" --file-filter="$root/tests/*")
if ((${#files[@]} > 0)); then
    filters=("${files[@]/#/--file-filter=$root/}")
fi
cppcheck --project="$build_dir/compile_commands.json" \
    "${filters[@]}" \
    --enable=warning,performance,portability --library=qt \
    --suppress='preprocessorErrorDirective:*_autogen/*' \
    --inline-suppr --error-exitcode=1 --quiet \
    --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
    -j "$jobs"

echo "Static analysis passed."
