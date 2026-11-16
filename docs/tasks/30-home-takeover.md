# 30: Home screen takeover

**Wave 3. Needs wave 2 tested on the device.**

## Goal

Make the companion Nickel's home screen safely, with every fallback in the spec.

## Spec sections

Feature design (Home screen and library: opening books, Kobo's home, status bar, escape hatch), Crash safety (all).

## Owns

Home-takeover parts of `src/nickel/`, `packaging/` NickelMenu entry, `tests/nickel/`.

## Build

- Replace Nickel's home view after Nickel reports ready (with the missed-signal fallback), drawing below Nickel's status bar.
- Show the companion at startup, after closing a book, and on Home.
- Wire the startup marker, two-strike recovery, readiness timeout and `disable-home` from task 12.
- Kobo home in the More tab opens Nickel's own home; a NickelMenu entry switches between the two.
- Opening books goes through Nickel's open-book step (task 31 adds the prompt there).

## Device checks (maintainer)

Boot, close a book, tap Home, switch to Kobo home and back, force two failed startups and confirm recovery, `disable-home` works.
