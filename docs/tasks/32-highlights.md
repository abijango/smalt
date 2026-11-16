# 32: Highlights (one way, Kobo to BookOrbit)

**Wave 3.**

## Goal

Send Kobalt's highlights to BookOrbit, including edits and deletions, without ever writing Kobalt's data.

## Spec sections

Feature design (Highlights and notes), Decisions after verification. Findings: `docs/VERIFY-FINDINGS.md` item 4. Kobalt's storage contract: its `doc/storage.md` (Profile connection, Backups and external database access).

## Owns

`src/features/highlights/`, `tests/features/highlights/`.

## Build

- After a book closes and Kobalt has closed `kobalt.sqlite` (returned to a rollback journal, no `-wal` file left), copy the database file set, then read the copy read-only. Never open the live file while Kobalt may hold it; skip and retry later if in doubt.
- Read that book's highlights (IDs, CREngine endpoints, text, note, colour, style, timestamps). Map Kobalt's book key to the ledger's book via `book_map`.
- Diff against the last sent state in the ledger; send creates, edits and deletions through `plugin/annotations/exchange` and `exchange-ack`.
- List BookOrbit-only highlights on the book's detail screen (read-only).
- Schema guard: check Kobalt's `PRAGMA user_version` (4 at the time of writing) and table layout; if it changed, stop and log rather than guess.

## Acceptance tests

Create, edit and delete round trips against the mock server; a changed Kobalt schema version stops cleanly; a locked or in-use database is skipped and retried.
