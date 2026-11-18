# 22: Screens

**Wave 2. Needs task 13 merged; `docs/VERIFY.md` item 9 answered.**

## Goal

Every companion screen built from the UI kit, working against the API client and ledger, offline-capable.

## Spec sections

Feature design (Home screen and library, Large-library UX, Downloads, Books removed upstream banner only), UX details and copy, Visual design (Devices and scaling). Mockups in `docs/mockups/`.

## Owns

`src/ui/screens/`, `tests/ui/screens/`.

## Must not touch

`src/ui/kit/` (propose changes), sync, recorder, Nickel hooks (home takeover is task 30).

## Build

- Screens: Home, Library, Series, Series detail, Book detail, Search, More (with Kobo home, Shelves, Settings, sync status), Settings. The Stats tab and the book detail "Your reading" section are task 23: leave a slot for each (a placeholder widget task 23 replaces).
- Tab bar on every device: Home, Library, Series, Stats, More. Device layouts: Clara (3 reading covers, 1 up-next row) and Libra (4 covers, 2 rows), portrait only.
- Paged lists with page counter and jump-to-letter; Libra page-turn buttons flip pages.
- Search in every header; grouped results; offline searches only on-device books and says so.
- Chips remembered per tab; one tap clears.
- Download flow: queue, resume, save path from the spec, record in `book_map`, request quiet import, then Open via Nickel (EPUB → Kobalt, PDF and CBZ → Kobo's reader). Static progress text, no spinners.
- Copy exactly as the spec's Copy table.
- Page and cover caching for instant back navigation and offline use.

## Acceptance tests

- Screenshot tests of each screen on the three device profiles, compared against `docs/mockups/`.
- Offline mode shows cached pages and marks undownloaded items unavailable.
- Download resumes and lands at the right path.

## Addendum (after wave 1: session decisions)

Settings gets **Push while reading** (Off, Only when connected, Connect briefly, with the interval) and **New session after sleep** (15 minutes to Never), as in `docs/SPEC.md` ("Settings screen"). The status chips and labels use BookOrbit's six statuses (`docs/SPEC.md`, "Status and ratings").
