# 32: Highlights and notes

**Wave 3. Needs `docs/VERIFY.md` item 4.**

## Goal

Two-way highlight and note sync with safe placement.

## Spec sections

Feature design (Highlights and notes), On-device data model (`annotation`), Sync engine (Conflict rules), Copy.

## Owns

`src/features/highlights/`, `tests/features/highlights/`.

## Build

- Read Kobalt's highlights from wherever VERIFY item 4 found them; store with position, text and about 40 characters either side.
- Place remote highlights by position, then by text search; list unplaced ones on the book detail screen with the spec's copy.
- Write into Nickel's database only for closed books.
- Tombstones for deletions; newest edit wins.

## Acceptance tests

Round trips: create, edit and delete on each side; a highlight from a different edition that can only be placed by text; one that can't be placed.
