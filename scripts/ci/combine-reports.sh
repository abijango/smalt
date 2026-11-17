#!/usr/bin/env bash
# Combines the job reports into the PR comment.
#
#   scripts/ci/combine-reports.sh <reports-dir> [run-url] [commit-sha]
#
# Writes <reports-dir>/pr-comment.md. A section whose job didn't produce a
# report (cancelled, or crashed before writing it) says so. The first line is
# a hidden marker the workflow uses to find and update its own comment.
set -euo pipefail
dir=${1:?usage: combine-reports.sh <reports-dir> [run-url] [commit-sha]}
run_url=${2:-}
sha=${3:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}
out="$dir/pr-comment.md"

status="✅ All automated checks passed"
for section in desktop kobo; do
    if [[ ! -f "$dir/$section.md" ]] || head -1 "$dir/$section.md" | grep -q "❌"; then
        status="❌ Some automated checks failed"
    fi
done

{
    echo "<!-- smalt-ci-report -->"
    echo "## $status"
    echo
    echo -n "Commit \`${sha:0:8}\`"
    [[ -n "$run_url" ]] && echo -n " · [CI run and artifacts]($run_url)"
    echo " · updated $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo
    for section in desktop kobo; do
        if [[ -f "$dir/$section.md" ]]; then
            cat "$dir/$section.md"
        else
            echo "### ⚠️ ${section^} checks: no report"
            echo
            echo "The job didn't produce a report (cancelled, or it failed before writing one). See the CI run."
        fi
        echo
    done
    echo "---"
    echo "Device checks in the PR description still need a real Kobo. Download \`KoboRoot\` from the run's artifacts to install it."
} >"$out"

# GitHub comments are limited to 65,536 characters.
if [[ $(wc -c <"$out") -gt 60000 ]]; then
    head -c 60000 "$out" >"$out.tmp"
    printf '\n\n… report truncated; see the CI run for the full logs.\n' >>"$out.tmp"
    mv "$out.tmp" "$out"
fi
echo "$out"
