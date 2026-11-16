# 31: Jump prompt and two-way progress

**Wave 3. Needs `docs/VERIFY.md` items 2, 3 and 7.**

## Goal

Offer a newer position from another device before a book opens, and on wake inside a book.

## Spec sections

Feature design (Reading progress), Sync engine (Conflict rules), UX details and copy.

## Owns

`src/features/progress/`, the open-book hook in `src/nickel/`, `tests/features/progress/`.

## Build

- Before Nickel opens a book: if the cached remote position is newer and more than about one page ahead, show the dialog from the spec. Jump writes the position to Nickel's database (book closed), then lets the open continue.
- On wake inside a book: same check; "Reopen at 54%" closes, writes, reopens.
- Never offer to move backwards; never prompt about this device's own position.
- Position conversion between BookOrbit's format and Nickel's, with chapter-plus-percentage fallback.

## Device checks (maintainer)

Read ahead on the web or phone, then open the book on the Kobo; same with the book left open across sleep.
