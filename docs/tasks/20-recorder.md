# 20: Recorder

**Wave 2. Needs `docs/VERIFY.md` items 1, 2 and 5 answered.**

## Goal

Capture every reading session with real start and end times, on book close and on sleep with a book open, into the ledger.

## Spec sections

On-device data model (How sessions are captured, Sleeping inside a book), Crash safety.

## Owns

`src/recorder/`, the recorder-related parts of `src/nickel/` (signal hooks for open, close, sleep, wake, USB; read-only Nickel database access), `tests/recorder/`.

## Must not touch

Ledger internals (call through `include/smalt/ledger.h`), sync, UI.

## Build

- Hooks for book open, book close, sleep, wake and USB connect, using NickelHook on 4.46.23852.
- Each boundary stamped with the device clock and a monotonic clock.
- On close and on sleep: read the book's events and `content` row from Nickel's database, merge with the mod's own boundaries (mod boundaries win), write a session and the latest position to the ledger, enqueue in the outbox.
- Sleep with a book open ends the session; wake starts a new one. Never stretch a session across sleep.
- If the position isn't fresh at sleep, record the last saved position flagged approximate; correct it at the next close or sleep.
- Drop sessions under 10 seconds.
- Close the ledger on USB connect, reopen after.
- Works for Kobalt (EPUB) and Kobo's reader (PDF, CBZ).
- All Nickel access behind `include/smalt/nickel.h` so desktop tests use the fake.

## Acceptance tests (desktop, with the Nickel fake)

- Open → read → close produces one session with correct times.
- Open → sleep → wake → close produces two sessions and no time counted during sleep.
- Clock jump while asleep (NTP correction) doesn't distort durations.
- USB connect mid-session closes the ledger safely.

## Device checks (maintainer)

The scenarios above on the Kobo, then compare with Nickel's own records.
