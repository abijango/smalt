#!/usr/bin/env bash
# Checks (or with --fix, applies) clang-format on first-party C and C++ files.
# Vendored code listed in .clang-format-ignore is skipped.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mode=check
[[ "${1:-}" == --fix ]] && mode=fix

# Tracked and new files that still exist (a deleted, not yet committed file is skipped).
files=()
while IFS= read -r f; do
    if [[ -f "$f" ]]; then files+=("$f"); fi
done < <(git ls-files -co --exclude-standard -- '*.h' '*.cpp' '*.c' \
    | grep -v -E '^(toolchains/nickelhook/|build/|out/|\.claude/)' || true)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "No files to check"
    exit 0
fi
if [[ $mode == fix ]]; then
    clang-format -i "${files[@]}"
    echo "Formatted ${#files[@]} files"
else
    clang-format --dry-run --Werror "${files[@]}"
    echo "clang-format: ${#files[@]} files OK"
fi
