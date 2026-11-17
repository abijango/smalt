#!/usr/bin/env python3
"""Turns ctest's JUnit file into a Markdown section for the PR report.

Usage: junit-report.py <ctest-junit.xml>

Prints one table row per test program (area, result, test functions passed,
time) and, for every failing program, its output in a collapsed block. Only
the standard library is used, so it runs in a bare container.
"""
import re
import sys
import xml.etree.ElementTree as ET

MAX_OUTPUT_CHARS = 6000  # Per failing test; keeps the PR comment under GitHub's limit.
TOTALS = re.compile(r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped")
FAIL_PREFIXES = ("FAIL!", "XPASS", "QFATAL", "QWARN")


def failure_lines(output):
    """Qt Test failure lines plus their indented details (Actual, Expected, Loc)."""
    picked = []
    keep = False
    for line in output.splitlines():
        if line.startswith(FAIL_PREFIXES):
            picked.append(line)
            keep = True
        elif keep and line.startswith((" ", "\t")):
            picked.append(line)
        else:
            keep = False
    return "\n".join(picked)


def tail(text, limit):
    if len(text) <= limit:
        return text
    return "… (truncated)\n" + text[-limit:]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: junit-report.py <ctest-junit.xml>")
    try:
        root = ET.parse(sys.argv[1]).getroot()
    except (OSError, ET.ParseError) as error:
        print(f"Could not read the test results: `{error}`")
        return 1

    cases = root.iter("testcase")
    rows = []
    failures = []
    functions_passed = functions_failed = 0
    for case in cases:
        name = case.get("name", "?")
        status = case.get("status", "")
        failed = case.find("failure") is not None or status == "fail"
        not_run = status in ("notrun", "disabled") or case.find("skipped") is not None
        output = case.findtext("system-out") or ""
        seconds = float(case.get("time", "0") or 0)

        counts = "–"
        match = TOTALS.search(output)
        if match:
            passed, failed_fns, skipped = (int(g) for g in match.groups())
            functions_passed += passed
            functions_failed += failed_fns
            counts = f"{passed}/{passed + failed_fns}" + (f" ({skipped} skipped)" if skipped else "")

        result = "❌ failed" if failed else ("⏭️ not run" if not_run else "✅ passed")
        rows.append(f"| `{name}` | {result} | {counts} | {seconds:.2f} s |")
        if failed:
            message = ""
            failure = case.find("failure")
            if failure is not None:
                message = failure.get("message", "")
            highlights = failure_lines(output) or message or "(no failure lines)"
            failures.append((name, highlights, output))

    total = len(rows)
    failed_count = len(failures)
    print(f"**{total - failed_count} of {total} test programs passed**, "
          f"{functions_passed} of {functions_passed + functions_failed} Qt Test functions passed.\n")
    print("| Test | Result | Functions | Time |")
    print("| --- | --- | --- | --- |")
    print("\n".join(rows) if rows else "| (no tests found) | | | |")
    for name, highlights, output in failures:
        print(f"\n<details><summary>❌ <code>{name}</code> output</summary>\n")
        print("Failure lines:\n\n```\n" + highlights.strip() + "\n```\n")
        print("Full output:\n\n```\n" + tail(output.strip(), MAX_OUTPUT_CHARS) + "\n```\n</details>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
