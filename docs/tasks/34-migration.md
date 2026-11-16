# 34: Migration off Kobo sync

**Wave 3, after 30–33 work on the device.**

## Goal

A one-time "Migrate from Kobo sync" action that moves the reader from BookOrbit's Kobo sync to Smalt without losing positions or highlights.

## Spec sections

Migration off Kobo sync, Highlights and notes, Reading progress.

## Owns

`src/features/migration/`, `tests/features/migration/`.

## Build

- Settings action, available only while no book is open. While migration is pending, Smalt records but uploads no sessions.
- Inventory the Kobo-sync books in Nickel's database (UUID `ContentID`s, KEPUB) with title, author, `ReadStatus`, `___PercentRead`, `DateLastRead`.
- Match them to BookOrbit books (BookOrbit ID if the Kobo ID maps to it, otherwise title and author through the catalog), and store the result.
- Upload Kobalt's highlights for those books using task 32's reader.
- Save a "reading now" list so the home screen offers those books first after re-signing in.
- After the user signs in to Kobo's own servers: offer to download the reading-now books, write each one's BookOrbit position into Nickel before first open, and mark migration done.
- Show a short checklist screen with the spec's six steps and the current one highlighted.

## Acceptance tests

Inventory and matching on a copy of a real `KoboReader.sqlite` (the maintainer can supply one); no session uploads while pending; highlights uploaded once.
