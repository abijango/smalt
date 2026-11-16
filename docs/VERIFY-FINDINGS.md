# Verify first: findings (November 16, 2026)

Sources: Kobalt source and docs (`doc/reader/progress.md`, `doc/sync-data.md`, `doc/storage.md`, `doc/reader/annotations.md`), the BookOrbit KOReader plugin from the maintainer's server (`bookorbit_api.lua`, `bookorbit_sweep.lua`, `bookorbit_stats_reader.lua`, `bookorbit_annotations.lua`, `bookorbit_catalog.lua`), and a `KoboReader.sqlite` from the Clara Colour on firmware 4.46.23852 (DbVersion 176).

## Kobo and Kobalt

### 1. Does Nickel record Kobalt's reading sessions?

**Partly, and not in a usable form on this device.**

- Kobalt reports sessions through Nickel's event engine exactly as Nickel's reader does: start and end of each reading interval, and a `pageTurned` event per page with the words read. Sleep ends an interval; wake starts a new one.
- Nickel turns these into `OpenContent` and `LeaveContent` analytics events (`SecondsRead`, `PagesTurned`, `IdleTime`, progress). **The database has none.** On 4.46, Nickel drops them in sideloaded mode, with stats tracking off, or when a server sets a privacy-centre URL without granting the analytics permission; Nickel's Kobo sync also uploads and clears them. Either way they can't be relied on.
- The `Event` table holds only per-book counters and the last timestamp for types 1020 and 1021 (for example count 6, last 17:41:13 and 17:41:20). It is not a session history.
- `content.TimeSpentReading` is reliable: Kobalt adds each interval's active seconds when the interval ends (13,553 s on the book read).
- **Kobalt keeps its own session history** in `reading_sessions` in `.adds/kobalt/…/kobalt.sqlite`: UUID, book key, start and last-observed UTC times (ms), active ms, start and end positions, status. But Kobalt holds an exclusive lock on that file while a book is open, and its storage contract says no other process may open it then. It closes the file when a book closes, at USB export and at power-off.

**Consequence:** Smalt records its own sessions from hooks (open, close, sleep, wake), takes active seconds from the change in `TimeSpentReading` across the interval, and the position from Nickel's `content` row. Optionally it reconciles with Kobalt's `reading_sessions` after a book closes, read-only on a copy, never while Kobalt's connection is open.

### 2. Does Kobalt save its position to Nickel on sleep?

**Yes.** Kobalt writes the full position to Nickel when a book opens (status only), before sleep, before USB export, at power-off and on close, through Nickel's own functions under Nickel's lock. Sleep writes before Nickel prepares the sleep screen, so Smalt's sleep hook must run after Kobalt's write.

On firmware 4, Nickel stores only a chapter path and element ID (for KEPUB, `OPF/file.html#kobo.158.5`). The exact offset is kept in Kobalt's own profile. Percentage stays at 99 while reading on firmware 4.

### 3. Does a position written to Nickel take effect in Kobalt?

**Yes, for a closed book:** "on later openings, a newer Nickel position wins over the local one." Write it through Nickel's functions with a newer `DateLastRead`.

**For an open book, Kobalt already has its own prompt:** after Nickel's sync, account or USB signals, Kobalt checks the committed position and asks "Keep reading here" or "Go to synced position". Kobalt's writer also stops if another writer changed the row. Smalt should not write to an open book; its wake prompt stays as designed.

### 4. Where does Kobalt keep highlights?

**In Kobalt's own profile, not Nickel's `Bookmark` table** (0 rows on this device). Since beta 4 they live in `kobalt.sqlite` (older JSON in `.adds/kobalt/annotations/<book>/`). Positions are CREngine positions, the same family as KOReader's XPointers.

**Consequence:** Kobo → BookOrbit highlight sync is possible by reading Kobalt's database when it's closed. BookOrbit → Kobo would mean writing a database that only Kobalt may write. Two-way highlights need an import interface in Kobalt (ask the maintainer), or stay one-way.

### 5. PDF and comic progress

Kobo's own reader writes the same `content` columns and session events as Kobalt (Kobalt mirrors it). Same caveats as item 1.

### Also found on this device

- All 452 books are KEPUBs with Kobo-style UUID IDs, delivered by **BookOrbit's Kobo sync**, and the device is signed in to it (bearer token). Kobo sync is therefore still active and sending reading state, which is the source of the timeline problem.
- Kobalt's own KOSync only runs for books that Nickel's sync doesn't carry, so it is inactive for all of these books.

## BookOrbit (KOReader plugin protocol)

All under `{server}/api/v1/koreader/`, authenticated with headers `x-auth-user` (username) and `x-auth-key` (MD5 of the password), the KOReader credentials from BookOrbit Settings > KOReader. Plugin endpoints use camelCase and add `deviceId`, `deviceModel`, `pluginVersion` and `deviceTime` to each body. API keys are not needed.

### 6. Endpoints, and can a device write sessions?

| Need | Endpoint |
| --- | --- |
| Check login | `GET users/auth` |
| Progress (kosync) | `GET syncs/progress/{digest}`, `PUT syncs/progress` with `document`, `percentage`, `progress` (XPointer), `device`, `device_id`, `timestamp` |
| Match books | `POST plugin/match-check` with partial-MD5 hashes plus title, authors, `bookFileId`; returns `bookId`, `bookFileId`, `positionCompatible`, `libraryVersion` |
| **Reading sessions** | `POST plugin/page-stats`: per book `{hash, bookFileId?, events: [{page, startTime, durationSeconds, totalPages}]}`; server acknowledges with a watermark |
| **"This device measures its sessions"** | `POST plugin/sweeps` with counts. This is what makes the server stop estimating sessions for the device |
| Highlights | `POST plugin/annotations/exchange`, `…/exchange-ack`, `…/pending`; fields include `pos0`, `pos1`, `text`, `note`, `color`, `drawer`, `chapter`, `datetime`, `deleted` |
| Bookmarks | `POST plugin/bookmarks/exchange`, `…/exchange-ack` |
| Status, progress in bulk | `POST plugin/book-states`, `POST plugin/progress` |
| Read status, rating | `PUT plugin/catalog/books/{id}/read-status`, `…/rating` |
| Catalog | `GET plugin/catalog/root`, `…/dashboard`, `…/dashboard/discover`, `…/dashboard/sections/{type}` (including up-next-in-series), `…/sections/{authors\|series\|collections\|smart-scopes\|libraries\|shelves}`, `…/books` with `q`, `page`, `size`, `sort`, `order`, `seriesId`, `collectionId`, `ids`, `…/books/{id}`, `…/manifest` (cursor) |
| Downloads | `GET plugin/catalog/files/{id}/download`, `…/books/{id}/thumbnail` |
| Server features | capability list with server version (e.g. `catalogDashboardSections`, `catalogListsV2`) |

**Yes, devices can upload measured sessions**, as page events. Smalt can synthesise page events from its session records (start time, duration, pages) or, better, record real page turns by observing the `pageTurned` events Kobalt sends through Nickel.

### 7. Position format

KOReader-style: a percentage plus an XPointer (`progress`). Kobalt uses CREngine positions, so they're compatible; `match-check` reports `positionCompatible` per book.

### 8. Change feed and deleted books

No explicit deleted-books feed found. `match-check` and sweeps return a `libraryVersion`; a book that no longer matches comes back `unmatched`. Removal detection = re-match and compare. Highlight deletions do sync.

### 9. Search, series, filters

Search (`q`), series list and series filter, collections, smart scopes, sort and paging all exist. "Up next in series" and "want to read" exist as dashboard sections. A format filter wasn't seen; filter locally by file format.

## Still open

- Exact payload of `plugin/sweeps` acknowledgement, and whether sessions measured under Smalt's device ID retire estimates created from **Kobo sync** under the Kobo device's ID (BookOrbit server code needed). Avoid the question by turning Kobo sync off.
- Home-screen takeover on 4.46.23852: still needs the scaffold build on the device.
