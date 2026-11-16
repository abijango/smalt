# Smalt (BookOrbit Companion for Kobo) – Design Spec

Source: the design spec doc, exported November 16, 2026. Mockup images are in `docs/mockups/`.

## Overview

The companion is a Kobo mod that runs inside Nickel beside an unmodified Kobalt. It lets you browse and download books from BookOrbit, records exactly what you read while offline, and syncs it to BookOrbit's API in the background when Wi-Fi is available.

It replaces BookOrbit's built-in Kobo sync. That sync builds reading sessions partly from Kobo's cumulative reading-time counter, which only says how many minutes were added since the last sync. When you read offline and sync later, those minutes get dated to the sync time, not to when you read ([BookOrbit issue #1667](https://github.com/bookorbit/bookorbit/issues/1667)). The companion records each session with its real start and end times on the device, so nothing has to be estimated.

### Decisions so far

| Area | Decision |
| --- | --- |
| Target device | Kobo Clara Colour and Clara BW (6") first, then Libra Colour (7"); firmware 4.46.23852 (Qt 5) first, Qt 6 firmware (5.x and 6.x) later |
| Relation to Kobalt | Separate companion mod; Kobalt is not modified |
| Home screen | The companion is the home screen; books open straight into Kobalt, and Kobo's own home is at the top of the More tab |
| Wi-Fi triggers | Just before sleep, on wake, and on book close |
| Reading progress | Two-way; ask before jumping to a newer position from another device |
| Highlights and notes | Two-way |
| Collections | BookOrbit is the source; mirrored one-way into Nickel collections, Kobo edits overwritten |
| Books removed upstream | Ask before removing anything from the device |
| Status and ratings | Two-way: finished, abandoned and ratings |
| Download format | EPUBs open in Kobalt; PDFs and comics (CBZ) open in Kobo's built-in reader |

Out of scope for now: audiobooks, DRM-protected books, and KOReader.

## Architecture

The companion is one NickelHook mod (Qt 5 on 4.x) loaded into Nickel's process, next to Kobalt. Kobalt keeps doing what it does today, and the mod never calls into it: everything flows through Nickel.

![Architecture](mockups/architecture.png)

Inside the Nickel process: Kobalt (unmodified) renders EPUBs and reports progress to Nickel; the Nickel database holds progress, sessions, highlights and collections; Nickel signals report book open, close, sleep, wake and Wi-Fi changes. The companion contains four parts: the home screen and library, the recorder, the sync engine and the ledger. The sync engine talks to the BookOrbit server over HTTPS with an API key.

Kobalt hands progress and sessions to Nickel; Nickel's signals tell the recorder when a book is closed or the device sleeps, and it copies them into the ledger at that moment, and the sync engine moves the ledger to BookOrbit and writes pulled changes back into Nickel while the book is closed.

### What to reuse

| Source | What to take |
| --- | --- |
| Kobalt | Its Qt 5 NickelHook build toolchain, install packaging (`.adds` plus `KoboRoot.tgz`), and its `AGENTS.md` conventions for agent-driven builds |
| NickelMenu | A fallback entry that switches between the companion and Kobo's own home, which works on 4.x |
| NickelDBus | Wi-Fi control, network and sleep signals, quiet library import, and opening a book |
| BookOrbit API (3.8.0+) | Library, downloads and sync, using a scoped API key |
| BookOrbit KOReader plugin | Reference for sync behaviour: session rules, progress and highlight handling |

## Crash safety

The companion runs inside Nickel's process, so a hard crash takes Nickel down with it. The safeguards below copy Kobalt's approach, with two goals: the Kobo always reaches a working home screen, and your reading data survives anything the mod does.

### What Kobalt does

- **Startup recovery:** after two startups in a row that didn't finish, Kobalt disables itself, keeps books, settings and reading data, and leaves a plain-text explanation. Reinstalling allows a retry. A single crash, or a crash in Kobo's own reader after Kobalt started, doesn't trigger it.
- **Startup marker:** a marker file exists only while Kobalt is starting. A crash during startup leaves it behind; a clean exit, a fallback to Nickel, power-off and reboot all remove it.
- **Waiting for Nickel:** Kobalt waits for Nickel to finish starting before creating its reader, and recovers by checking Nickel's state directly if it misses that signal.
- **Readiness timeout:** if Kobalt isn't confirmed ready after 30 seconds, it records the failure and leaves Kobo's built-in reader in charge for that boot.
- **Failure per book:** if a book won't open, Kobalt offers "Try built-in reader" or "Go back", and remembers that choice until Nickel restarts.

### The companion's safeguards

| Safeguard | How it works |
| --- | --- |
| Startup marker | Before taking over the home screen, the mod writes `.adds/bookorbit/startup-session`. It's removed once the home screen has drawn, and on clean shutdown, power-off or a fallback to Nickel's home. A crash during startup leaves it behind. |
| Two-strike recovery | If startup finds the marker from two boots in a row, the mod disables itself: it writes `.adds/bookorbit/disabled` and a `RECOVERY.txt` explaining what happened, and Nickel boots as normal. The ledger, outbox and settings are untouched. Deleting `disabled` or reinstalling tries again. A single crash doesn't trigger it. |
| Wait for Nickel | The home screen is only replaced after Nickel reports it has finished starting. If that signal is missed, the mod checks Nickel's state directly instead of waiting forever. |
| Readiness timeout | If the companion isn't ready 30 seconds after boot, it stays out of the way for that boot and Nickel's home is used. Recording still runs. |
| Separate switches | Home takeover and sync can each be switched off with their own file (`disable-home`, `disable-sync`), so a problem in the home screen or the network code never stops reading data being recorded. |
| Book handoff | The companion never opens books itself; it hands them to Nickel, so Kobalt's own per-book fallbacks still apply. |
| Ledger protection | If the ledger fails its integrity check at startup, it's renamed with a timestamp and kept, and a fresh ledger is started; nothing is deleted. A copy is also kept daily in `.adds/bookorbit/backup/`. |
| Sleep watchdog | The pre-sleep sync runs under a hard 15-second timer. When it fires, the device sleeps whatever the network is doing. |
| No network at startup | Startup never waits on Wi-Fi or the server; the first sync always happens later. |
| Uninstall | Deleting `.adds/bookorbit/uninstall` and rebooting removes the mod, the same pattern Nico's other mods use. The ledger and its backups stay on the device. |

## On-device data model

The mod keeps its own SQLite ledger, separate from Nickel's database. Nickel's database is only read from, plus written to in a few narrow, guarded cases (progress jumps, highlights, collections). The ledger is the record of truth for what was read on this device, and the outbox is what still has to reach BookOrbit.

### Where it lives

The ledger sits at `.adds/bookorbit/ledger.sqlite` on the user partition, the only space that survives firmware updates. Two consequences:

- Nickel unmounts that partition when you connect USB, so the mod must close the database on the USB-connect signal and reopen it after.
- The partition is FAT, so use SQLite's standard rollback journal with full syncing, not WAL mode, and keep transactions short.

### Tables

| Table | Holds | Notes |
| --- | --- | --- |
| `server` | Server URL, device ID, sync cursors | One row; the API key itself stays in a separate config file |
| `book_map` | Nickel ContentID ↔ BookOrbit book and file IDs, file hash, how it got here | Written when the mod downloads a book; matched once for existing books |
| `session` | One row per reading session: UUID, book, start and end time (UTC), active seconds, start and end position and percentage, pages turned | Append-only; never edited after it's sent |
| `progress` | Latest local position per book, with timestamp | Also holds the latest known remote position, for the offline jump prompt |
| `annotation` | Local UUID, remote ID, anchor, highlighted text plus surrounding text, note, colour, created, updated and deleted times | Deletions kept as tombstones until the server confirms them |
| `book_state` | Status (reading, finished, abandoned) and rating, each with local and remote update times | Newest change wins per field |
| `outbox` | Pending uploads: kind, entity UUID, payload, attempts, next retry time | Removed only after the server confirms |
| `remote_cache` | Collections, catalogue pages and cover thumbnails for the browser | Disposable; safe to wipe |
| `pending_decision` | Books removed upstream, waiting for you to choose | Drives the "Remove from device?" prompt |

### How sessions are captured

Two sources, cross-checked:

1. **The mod's own hooks.** It records book open and close, sleep and wake, and USB connect with the device clock and a monotonic clock. These mark hard session boundaries even if nothing else does.
2. **Nickel's records.** Kobalt hands progress and reading sessions to Nickel, and its beta 5 started logging Nickel's detailed session events. On book close, and when the device goes to sleep with a book still open, the mod copies those events and the book's current position into its own ledger, before anything else can purge them.

The mod's own boundaries win when the two disagree. Sessions under 10 seconds are dropped, matching BookOrbit's own rule for KOReader.

Every ledger row has a UUID made on the device. The server de-duplicates on it, so a retried or partial upload can never count the same reading twice.

### Sleeping inside a book

Putting the Kobo to sleep without closing the book is treated as the end of a session, the same as closing it.

- **Sleep ends the session.** Its end time is the moment of sleep, and its end position is the page on screen. The session is written to the ledger and queued before the pre-sleep sync decides whether to connect.
- **Wake starts a new session** for the same book. A session is never stretched across sleep, so the hours the device slept are never counted as reading.
- **If the position isn't fresh at sleep** (Kobalt hasn't saved it to Nickel yet), the mod records the last saved position, marks it approximate, and corrects it at the next sleep or close.
- **Pulled changes for that book wait**, because the book never closes. Positions are handled with a prompt on wake instead (see Reading progress); highlights and status wait for the next close.

## Sync engine

Sync is opportunistic and bounded: it runs at a few natural moments, brings Wi-Fi up only when there is work to do, gives up quickly, and always puts Wi-Fi back the way it found it. Nothing is lost when a sync is cut short, because the outbox only shrinks after the server confirms.

### Triggers

| Trigger | What runs | Limits |
| --- | --- | --- |
| Book closed | Push that book's session, position, highlights and status | Brings Wi-Fi up only if the outbox has items |
| Just before sleep | End the open book's session in the ledger, then push the whole outbox; quick pull of remote positions | Holds sleep for at most 15 s; skipped below 20% battery or with an empty outbox and a pull under 6 h old |
| Wake | Pull first (positions, highlights, status, collections), then push | Runs in the background; never blocks the screen |
| Wi-Fi already on | Full sync | Free, because no connection is needed |
| "Sync now" | Full sync, with progress shown | Manual, no limits except a 60 s timeout |

A sync that finished successfully in the last 5 minutes, with an empty outbox, skips any trigger except "Sync now". Book close is often followed by sleep, so the two collapse into one sync.

### Order of a full sync

1. **Push** the outbox, oldest first: sessions, then positions, highlights and notes, then status and ratings. Batches of 20; each batch is one request.
2. **Pull** changes since the last cursor: positions, highlights, status and ratings, collections, and books deleted upstream.
3. **Apply** what was pulled, but only where safe: nothing is written to Nickel's database for a book that is open, and those writes wait for book close. A book left open across sleep gets a position prompt on wake instead.
4. **Refresh** the browser cache and cover thumbnails if time remains.

Pushing first means the server already has this device's reading before it answers the pull.

### Conflict rules

| Data | Rule |
| --- | --- |
| Sessions | Append-only; no conflicts possible |
| Position | Newest timestamp wins, but a newer remote position is offered as a prompt, never applied silently |
| Highlights and notes | Per highlight: newest edit wins; deletions win over edits made before them |
| Status and rating | Per field: newest change wins |
| Collections | BookOrbit always wins |
| Book deletions upstream | Never applied automatically; queued for you to decide |

### Wi-Fi control

On 4.x the mod drives Wi-Fi through the same Nickel functions NickelDBus uses, not by talking to the Wi-Fi driver directly, so Nickel's status bar and settings stay consistent. Before connecting, it notes whether Wi-Fi was off; afterwards it turns it off again only if it was the one that turned it on.

One side effect to block: when Wi-Fi comes up, Nickel may start its own Kobo sync. If the device was ever pointed at BookOrbit's Kobo sync endpoint, that endpoint must be removed from Kobo's config, or BookOrbit will record the same reading twice from two sources.

### Networking

- HTTPS with a CA bundle shipped in the mod, since the device's own certificates may be too old for current Let's Encrypt chains.
- The API key is stored in `.adds/bookorbit/config.ini`, scoped to your user with only the permissions the mod needs.
- Timeouts: 5 s to connect, 15 s per request; failed items retry with exponential backoff, capped at 6 hours.

## Feature design

### Home screen and library

The companion replaces Nickel's home screen. It's what you see when the Kobo starts, when you close a book in Kobalt, and when you tap Home.

- **Opening books:** Continue and Open hand the book to Nickel's open-book step, which shows the jump prompt if needed and then opens it in Kobalt as your default reader, with no Nickel screens in between.
- **Kobo's home stays close:** Kobo home, at the top of the More tab, opens Nickel's own home and library, for books outside BookOrbit, the Kobo store and system settings.
- **Nickel's status bar stays:** the companion draws below it, so the clock, Wi-Fi, battery and frontlight controls work as normal.
- **Escape hatch:** Nickel's own home takes over whenever the companion isn't ready, has failed at startup, or has been switched off (see Crash safety). A NickelMenu entry also switches between the two.

Screens:

- **Home:** Reading now (everything in progress on this Kobo, newest first, with a format label on comics and PDFs), Up next in your series (the next unread book in each series you're reading), Continue from other devices, and Recently added with format chips.
- **Library:** everything in BookOrbit except audiobooks, with chips for format (Books, Comics, PDFs), status (Unread, Reading, Finished) and On Kobo, plus sort.
- **Series:** every series with a cover stack, read count and a segmented progress bar, with chips for Reading, Unread, Comics and sort.
- **Series detail:** books in series order with their state, a Continue button for the book in progress, and Download next 3.
- **Book detail:** cover, metadata, series position, reading status, and one main button that changes with state: Download, Downloading 42%, Open, or Update available.
- **Search:** results grouped into series, authors and books.
- **More:** Kobo home, Shelves (on the Claras), Settings and sync status.

### Large-library UX

- **Pages, not scrolling.** Long lists are split into pages with a counter ("Page 1 of 54"), because scrolling smears on E Ink. Tapping the counter jumps to a letter. On the Libra Colour, the page-turn buttons flip list pages.
- **Search is one tap away** in the header of every tab. It runs against BookOrbit's API; offline, it searches only books on the Kobo and says so.
- **Filters stay put.** Each tab remembers its chips, and one tap clears them all. On Kobo shows only downloaded items, the useful view when offline.
- **Series come first.** Up next on Home surfaces the next unread book in every series you're reading, and Download next 3 queues a run of books in one tap.
- **Pages are cached** with their covers, so going back is instant and works offline.

Downloads:

1. Fetch the file in its own format: EPUB, PDF or CBZ. Queue several at once; downloads resume after a dropped connection.
2. Save to `/mnt/onboard/BookOrbit/<Author>/<Title>.<ext>` and record the BookOrbit IDs and file hash in `book_map`.
3. Ask Nickel to import the new files quietly, without the full "Importing content" screen where possible.
4. Offer **Open**, which opens the book through Nickel so EPUBs go to Kobalt and PDFs and comics to Kobo's built-in reader.

The home screen and library work offline from their cache, with anything not downloaded shown as unavailable until Wi-Fi is back.

### Reading progress

The mod sits in front of Nickel's "open book" step. If BookOrbit has a position for this book that is newer and more than about one page ahead, it asks before Kobalt opens:

> Your phone is at 54% (Chapter 12), 2 hours ago. You're at 41% here. **Jump to 54%** · Stay here

- The remote position comes from the cache filled on wake and book close, so the prompt also works offline.
- If you jump, the mod writes the new position into Nickel's database before Kobalt reads it.
- It never offers to move backwards, and never prompts about this Kobo's own position.
- Exact positions are converted where possible; otherwise it falls back to chapter plus percentage within the chapter.

On close or sleep, the book's position is pushed with the session. If you wake the device inside a book and another device is now ahead, the prompt appears on wake: "Reopen at 54%" or "Stay here". Reopening closes the book, writes the new position into Nickel's database, and opens it again in Kobalt.

### Highlights and notes

Each highlight is stored with its position, the highlighted text, about 40 characters either side, the note and the colour. Highlights from BookOrbit are placed by position first and by text search if that fails; any that can't be placed are listed on the book's detail page, not silently dropped. Highlights are only written into Nickel's database while that book is closed.

### Collections

BookOrbit is the source. The mod creates and updates a Nickel collection for each BookOrbit collection, containing only books that are on the device. It tracks which Nickel collections it created and never touches any others. If you edit a mirrored collection on the Kobo, the next sync puts it back. Dynamic smart scopes are only available in the browser, not mirrored.

### Status and ratings

| BookOrbit | On the Kobo |
| --- | --- |
| Reading | Nickel's "reading" status |
| Finished | Nickel's "finished" status |
| Abandoned | No Nickel equivalent: kept in the ledger, shown in the browser, and optionally mirrored as an "Abandoned" collection |
| Rating (1 to 5 stars) | Nickel's book rating |

Both directions, newest change per field wins.

### Books removed upstream

When a downloaded book is deleted from BookOrbit or drops out of a mirrored collection, nothing happens on the Kobo until you choose. A banner on the home screen says how many books are waiting. For each one you can pick Remove from Kobo, Keep (unlink it from BookOrbit), or Decide later. Before removing a book, the mod pushes any of its unsent reading data.

## UX details and copy

The companion should be invisible while you read and quiet the rest of the time: sync is silent when it works, and speaks up only when you need to decide something or something keeps failing.

### E Ink rules

- No animations or spinners. Progress is static text that updates a few times, such as "Downloading 3 of 5".
- Partial refreshes for list scrolling and button presses; a full refresh only when changing screens.
- Touch targets at least 9 mm tall, with generous spacing in lists.
- Design in greyscale first. The Clara Colour's colour layer is lower resolution, so colour is only for accents such as highlight colours.
- Covers as small, pre-dithered thumbnails, cached on the device.

### Where sync status shows

- **Home screen header:** "Synced 10:42 · 3 waiting", or "Offline · 3 waiting".
- **Sleep screen:** a small "Syncing…" line while the pre-sleep sync holds the device awake.
- **Nickel toasts:** only after a manual "Sync now", or when the same problem has failed three syncs in a row.

### Copy

| Moment | Text |
| --- | --- |
| Jump prompt | Your phone is at 54% (Chapter 12), 2 hours ago. You're at 41% here. **Jump to 54%** · Stay here |
| Manual sync done | Synced: 2 sessions, 1 highlight, 1 position |
| Repeated failure | Couldn't reach BookOrbit. Your reading is saved and will sync later. |
| Key rejected | BookOrbit rejected the API key. Check it in BookOrbit settings. |
| Removal banner | 3 books were removed from BookOrbit. **Review** |
| Removal choice | Remove from Kobo · Keep, unlinked · Decide later |
| Highlight not placed | 2 highlights from BookOrbit couldn't be placed in this edition. **Show them** |

### Settings screen

- Server URL and API key, with a **Test connection** button.
- A switch for each sync trigger, plus the battery threshold for pre-sleep sync.
- Collection mirroring on or off, and whether to keep an "Abandoned" collection.
- Download folder.
- **Export log** to the user partition, for debugging over USB.

## Visual design

This design is locked. It follows Nickel and Kobalt: black text on white, thin rules, one main button per screen, and no decoration. Colour is used sparingly, through the Clara Colour's colour layer, for book covers and one muted teal accent.

![Home, book detail and jump prompt](mockups/home-book-jump.png)

### Colours

| Token | Value | Used for |
| --- | --- | --- |
| Ink | `#111111` | All text and icons, header rules, decision buttons |
| Secondary | `#555555` | Supporting text, inactive tabs |
| Rule | `#DDDDDD` | Dividers between list rows |
| Paper | `#FFFFFF` | Backgrounds |
| Teal | `#3E6F6A` | Download button, progress bars, active tab underline |
| Teal tint | `#DCE8E5` | Background of small download badges |
| Teal dark | `#2F5853` | Icon on the teal tint |

### Rules

- **Teal means getting and progress:** download buttons and badges, progress bars and the active tab. Nothing else.
- **Black means decisions:** Continue, Jump, Open and other main actions are solid black buttons; secondary actions are outlined in black.
- **Covers are always in colour**, as thumbnails in lists and larger on detail screens.
- **Text is never coloured.** Black and grey render at the full 300 ppi, colour at 150 ppi, so colour stays on solid areas.
- **Colour goes on things that rarely change**, because colour areas refresh more slowly. Live values such as a download percentage stay in black text.
- **Dialogs** get a 2 px black border over the dimmed page, with no shadows.
- **Lists** are rows separated by thin rules, never cards.
- **Type** follows Nickel's interface font, with titles in medium weight and everything in sentence case.

### Devices and scaling

| Device | Screen | Colour | Layout |
| --- | --- | --- | --- |
| Kobo Clara Colour | 6", 1448 × 1072, 300 ppi (colour at 150 ppi) | Yes | Base layout |
| Kobo Clara BW | 6", 1448 × 1072, 300 ppi | No | Base layout with the monochrome palette |
| Kobo Libra Colour | 7", 1680 × 1264, 300 ppi (colour at 150 ppi) | Yes | Wider layout; page-turn buttons |

Layout is defined in physical millimetres, not pixels, so text, covers and touch targets are the same physical size on every device. The 7" Libra shows more items, not bigger ones.

![Home screen on three devices](mockups/home-three-devices.png)

- **Reading now:** 3 covers on the Claras, 4 on the Libra.
- **Up next in your series:** 1 row on the Claras, 2 on the Libra.
- **Tabs:** Home, Library, Series and More on the Claras, with Shelves and Kobo home under More. The Libra adds Shelves as a fifth tab.
- **Orientation:** portrait only in version 1. A two-pane landscape layout for the Libra is a later addition.

### Monochrome palette (Clara BW)

The device model is read at startup and picks the palette; every layout is shared.

| On colour devices | On Clara BW |
| --- | --- |
| Teal: progress bars, active tab, Download button | Ink `#111111` |
| Teal tint: download badges | Light grey `#E6E6E6` |
| Teal dark: icon on the tint | Ink `#111111` |
| Colour covers | Greyscale covers |

### Series and search

![Series list, series detail and search](mockups/series-detail-search.png)

## Risks, open questions and build phases

The biggest unknowns are about Kobalt's behaviour and BookOrbit's API, not about the mod itself, so the first phase is a short spike that answers them before any real building.

### Verify first

- [ ] Kobalt's Qt 5 package runs on 4.46.23852 and Nickel records its sessions (with the session start and end events) in the event table.
- [ ] Kobalt reads its starting position from Nickel's database, so a jump written there takes effect, and saves its position to Nickel when the device sleeps with a book open.
- [ ] Where Kobalt stores highlights: Nickel's bookmark table or its own files.
- [ ] Which BookOrbit endpoints accept 3.8.0 API keys, and whether any of them write reading sessions. If not, use the KOReader sync endpoints with a KOReader credential instead.
- [ ] The position format BookOrbit returns for EPUBs read on the web or phone.
- [ ] Whether BookOrbit exposes a feed of changes since a cursor, including deleted books, or the mod has to compare lists itself.
- [ ] The mod can take over Nickel's home screen on 4.46.23852 and hand back to it cleanly, including after a book is closed in Kobalt.
- [ ] BookOrbit's API supports server-side search, a paged series list, and filters by format and status.
- [ ] Progress for PDFs and comics read in Kobo's built-in reader lands in Nickel's database the same way as Kobalt's.

### Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| A firmware update removes the mod or moves you off 4.x | Mod stops working | Keep Kobo's automatic updates blocked; the ledger survives on the user partition |
| A Kobalt update changes how it uses Nickel's data | Missed sessions or highlights | Pin the tested Kobalt version; the mod's own open and close hooks still mark sessions |
| Writing to Nickel's database corrupts it | Lost library state | Write only to closed books, in short transactions, after a backup copy of the database |
| Pre-sleep sync drains the battery | Shorter battery life | 15 s cap, battery threshold, skip when nothing is queued |
| Positions don't convert exactly | Jump lands near, not on, the right spot | Chapter plus percentage fallback; the prompt shows both positions |
| API keys can't write sessions | No session upload | Fall back to the KOReader sync protocol |

### Build phases

1. **Spike:** answer every "Verify first" item on the device and against your server.
2. **Recorder:** the ledger, session capture and outbox, with a manual "Sync now" that pushes sessions. This alone fixes your reading timelines.
3. **Background sync:** book close, wake and pre-sleep triggers, with Wi-Fi control.
4. **Browser:** library, search, book detail, download, import and open in Kobalt.
5. **Progress and status:** two-way positions with the jump prompt, plus status and ratings.
6. **Highlights and notes:** two-way, with text-search fallback.
7. **Collections and removals:** the one-way mirror and the removal review screen.
8. **Qt 6 port:** for 5.x and 6.x firmware, once 4.x is stable.

### Sources

- [Kobalt README](https://github.com/nicoverbruggen/kobalt)
- [BookOrbit repository](https://github.com/bookorbit/bookorbit)
- [BookOrbit 3.8.0 release (API keys)](https://github.com/bookorbit/bookorbit/releases)
- [BookOrbit issue #1667: Kobo reading time counted twice](https://github.com/bookorbit/bookorbit/issues/1667)
- [BookOrbit KOReader sync docs](https://bookorbit.app/koreader/)
- [BookOrbit Kobo sync docs](https://bookorbit.app/kobo/)
- [Kobalt v1.0-beta.5 release (startup recovery)](https://github.com/nicoverbruggen/kobalt/releases/tag/v1.0-beta.5)
- [Kobalt website](https://kobalt.nicoverbruggen.be/)
- [Kobalt reader behaviour docs (fork copy)](https://github.com/frederico-apolonia/kobalt-kindle-scribe/blob/main/doc/reader.md)
