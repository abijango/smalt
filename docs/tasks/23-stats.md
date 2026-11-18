# 23: Stats

**Wave 2, in parallel with 20, 21 and 22.**

## Goal

The Stats tab (Overview, Patterns, Year) and the "Your reading" section on book detail, combining BookOrbit's account-wide dashboard with Smalt's own ledger.

## Spec sections

Stats (all), Visual design (Devices and scaling, Monochrome palette), UX details and copy (E Ink rules). Plugin reference: `bookorbit_board_data.lua` in the KOReader plugin shows how the dashboard fields are read and how local unsynced minutes are merged.

## Owns

`src/stats/`, `src/ui/screens/stats/`, `src/bookorbit/dashboard.*` (new files only), `tests/stats/`.

## Must not touch

Other files in `src/bookorbit/`, `src/ui/kit/` (propose additions), `src/ui/screens/` outside `stats/`, ledger internals.

## Build

- **Dashboard parsing** (`src/bookorbit/dashboard.*`): typed model for `readingYear` (`startDate`, `minutes[]`), `readingStreak` (`currentStreak`, `longestStreak`, `lastSevenDays`), `readingGoal` (`goalBooks`, `completedBooks`, `year`), `finishedThisYear`, `yearAgo` (`book`, `minutes`, `days`, `endProgress`, `firstDay`), `achievements` (`next`), `highlightOfTheDay`. Every field optional; missing or null hides its widget. Fetch through the existing client's dashboard call; cache in `remote_cache`.
- **Aggregation** (`src/stats/`): from ledger sessions, compute minutes per day, today's session blocks, minutes by hour of day and by weekday (last 90 days), records, per-book totals, pages per hour and the forecast exactly as the spec's Rules say. Merge confirmed-pending local minutes into the server heatmap, dropping them once the server's day includes them. Pure functions, no I/O, fully unit-tested.
- **Screens** (`src/ui/screens/stats/`): Overview, Patterns and Year with the chip switcher, and the "Your reading" section for book detail, replacing task 22's placeholders. Heatmap 26 weeks on the Claras, 52 on the Libra; colour and monochrome steps from the spec. "All devices" and "This Kobo" labels.
- **Chart components** (heatmap grid, legend, streak dots, hour histogram, weekday bars, 24-hour session strip, day strip): build them here from kit tokens, in `src/ui/screens/stats/widgets/`; propose moving them into the kit later if other screens need them.

## Acceptance tests

- Aggregation against fixture ledgers: sessions crossing midnight split across days; sleep gaps never counted; time zone changes; empty ledger.
- Merge: unsynced local minutes appear today, then disappear when the server's day catches up, never double-counting.
- Missing dashboard fields hide their widgets; offline uses the cache.
- Forecast: fewer than three sessions shows the fallback text; otherwise matches the spec's formula.
- Screenshot tests of the three screens and the book section on all three device profiles and both palettes.

## Device check (maintainer)

Numbers match BookOrbit's web stats for the same days; heatmap readable on the Kaleido panel and on the Clara BW.
