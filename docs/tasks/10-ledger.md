# 10: Ledger and outbox

**Wave 1.**

## Goal

The SQLite ledger and outbox that hold every reading record on the device until BookOrbit confirms it.

## Spec sections

On-device data model (all), Crash safety (Ledger protection), Sync engine (Order of a full sync, Conflict rules).

## Owns

`src/core/ledger/`, `src/core/outbox/`, `tests/core/`.

## Must not touch

`include/smalt/` (propose changes in the PR), everything else.

## Build

- Schema for every table in the spec, with versioned migrations.
- SQLite settings for a FAT partition: rollback journal, `synchronous=FULL`, no WAL, short transactions.
- Close and reopen cleanly around USB connect.
- Integrity check on open. On failure: rename the file with a timestamp, start a fresh ledger, log it. Never delete.
- Daily backup copy to `.adds/bookorbit/backup/`, keeping the last 7.
- UUIDs generated on device for every row that syncs.
- Sessions are append-only; reject updates to a session already sent.
- Annotation deletions are tombstones until confirmed.
- Per-field newest-wins for book state, with local and remote timestamps.
- Outbox: enqueue, oldest-first batches of 20 ordered sessions → positions → annotations → status, confirm, fail with exponential backoff capped at 6 hours.

## Acceptance tests

- Migrations from empty and from every earlier version.
- Corrupt file is renamed and a fresh ledger opens.
- Retried uploads never produce duplicate sessions (same UUID).
- Outbox survives a simulated crash mid-batch with nothing lost.
- Backup rotation keeps 7 files.
- Sessions under 10 seconds are rejected at insert.

## Addendum (added during wave 1)

Read-only queries the Stats task (23) needs, exposed through `include/smalt/ledger.h` (propose the additions in the PR):
- Sessions in a time range (start, end, active seconds, book, start and end page), ordered by start.
- Sessions for one book, newest first, with a limit.
- Whether each session has been confirmed by the server, so Stats can merge only unconfirmed minutes into the server heatmap.

## Addendum (after wave 1: session decisions)

Decided by the maintainer; see `docs/SPEC.md` ("How sessions are captured", "How BookOrbit builds sessions", "Sleeping inside a book", "Correcting a session", "Status and ratings"). Ledger changes, as schema version 2 with a migration:
- A `page_event` table: session UUID, book, page, total pages, start time (UTC), seconds on the page; append-only, never edited.
- `book_map` gets the page-stats watermark; the `server` row gets "sweep owed".
- A session's end position (and approximate mark) can be corrected after it's sent; its times can't.
- `book_state` stores BookOrbit's six statuses.
- The outbox never hands out a book's position while that book has unsent page events, unless those were rejected for good.
