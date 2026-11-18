# 20: Recorder

**Wave 2.**

## Goal

Capture every reading session with real start and end times, on book close and on sleep with a book open, into the ledger.

## Spec sections

On-device data model (How sessions are captured, Sleeping inside a book), Decisions after verification, Crash safety. Findings: `docs/VERIFY-FINDINGS.md` items 1, 2 and 5.

## Owns

`src/recorder/`, the recorder-related parts of `src/nickel/` (signal hooks for open, close, sleep, wake, USB; read-only Nickel database access), `tests/recorder/`.

## Must not touch

Ledger internals (call through `include/smalt/ledger.h`), sync, UI.

## Build

- Hooks for book open, book close, sleep, wake and USB connect with NickelHook on 4.46.23852, stamped with the device clock and a monotonic clock.
- **Active time:** read `content.TimeSpentReading` at session start and after Kobalt's (or Nickel's reader's) end-of-interval write; the difference is the active seconds. If it didn't change, fall back to the monotonic interval and mark the session estimated.
- **Ordering at sleep:** Kobalt writes its position and reading time before Nickel prepares the sleep screen. Read after that write; if it hasn't landed, record the last saved values and mark approximate.
- **Positions:** read `ChapterIDBookmarked` and `___PercentRead` from the `content` row (firmware 4 stores chapter path plus element ID only).
- **Page events for upload:** preferably observe the `pageTurned` events Kobalt sends through Nickel's event engine (in-process, without changing them) to record real page timings. If that hook isn't available, synthesise events from the session (start time, active seconds, start and end page).
- Sleep with a book open ends the session; wake starts a new one. Drop sessions under 10 seconds.
- Ignore books not in `book_map` (Kobo store books).
- Close the ledger on USB connect, reopen after.
- Never open Kobalt's database while a book is open.
- All Nickel access behind `include/smalt/nickel.h` so desktop tests use the fake.

## Acceptance tests (desktop, with the Nickel fake)

- Open → read → close produces one session with correct times and active seconds from the counter delta.
- Open → sleep → wake → close produces two sessions and no time counted during sleep.
- Clock jump while asleep doesn't distort durations.
- USB connect mid-session closes the ledger safely.

## Device checks (maintainer)

The scenarios above on the Kobo, compared with Kobalt's own reading history.

## Addendum (after wave 1: session decisions)

See `docs/SPEC.md` ("How sessions are captured", "Sleeping inside a book", "Correcting a session"):
- Page events are the primary record: one per page you leave, from Kobalt's `pageTurned` events; the page on screen ends at close or sleep. `TimeSpentReading` is the cross-check; synthesised page events only when page turns can't be observed.
- Sleep pauses the session. "New session after sleep" (15 minutes by default) decides whether wake continues the same session or starts a new one; sleep time is never counted.
- An approximate end position is corrected at the next wake, sleep or close, even after the session was sent; times never change.
