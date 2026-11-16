# 12: Crash safety

**Wave 1.**

## Goal

The safeguards that guarantee the Kobo always reaches a working home screen and reading data survives.

## Spec sections

Crash safety (all).

## Owns

`src/safety/`, `tests/safety/`.

## Must not touch

`include/smalt/` (propose changes), everything else.

## Build

- Startup marker `.adds/bookorbit/startup-session`: written before home takeover, removed after the home screen draws, on clean shutdown, power-off, and fallback to Nickel's home.
- Two-strike recovery: marker found on two consecutive boots → write `disabled` and `RECOVERY.txt` (plain English: what happened, that reading data is safe, how to retry), then stay inactive. A single leftover marker only logs.
- Switch files: `disabled` (the whole mod stays inert), `disable-home` (no takeover, recording and sync run), `disable-sync` (no network, recording runs).
- Readiness timeout: 30 s after boot without readiness → no takeover this boot; recording still runs.
- Wait-for-Nickel: an API that reports Nickel ready, with a fallback check if the signal was missed (the Nickel side is a stub here; task 30 wires it).
- Uninstall: if `.adds/bookorbit/uninstall` is missing at boot, remove the mod's installed files, keep the ledger and backups, and log it.
- Sleep watchdog helper: a hard timer that always releases the sleep hold.

## Acceptance tests

- Boot sequences with a fake clock and filesystem: clean, one crash, two crashes, crash then clean.
- `RECOVERY.txt` written once with readable content.
- Each switch file disables exactly what the spec says.
- Watchdog always fires within 15 s.
