# 33: Collections, status, ratings and removals

**Wave 3.**

## Goal

Mirror BookOrbit collections, sync status and ratings both ways, and handle books removed upstream.

## Spec sections

Feature design (Collections, Status and ratings, Books removed upstream), Copy.

## Owns

`src/features/collections/`, `src/features/status/`, `src/features/removals/`, matching `tests/features/`.

## Build

- Collections: one Nickel collection per BookOrbit collection, only on-device books, track the ones the mod created, never touch others, overwrite Kobo-side edits on next sync. No smart scopes.
- Status and ratings mapping per the spec's table; "Abandoned" kept in the ledger, optional "Abandoned" collection.
- Removals: queue in `pending_decision`, home banner, review screen with Remove from Kobo, Keep (unlink), Decide later; push unsent data before removing.

## Acceptance tests

Collection changes upstream propagate; user collections untouched; each removal choice behaves as specified.
