# 21: Sync engine

**Wave 2. Needs `docs/VERIFY.md` items 6–8 answered.**

## Goal

Move the outbox to BookOrbit and pull changes back, at the right moments, within strict time and battery limits.

## Spec sections

Sync engine (all), Crash safety (Separate switches, Sleep watchdog, No network at startup).

## Owns

`src/sync/`, the Wi-Fi and sleep-hold parts of `src/nickel/`, `tests/sync/`.

## Must not touch

The API client internals (call through `include/smalt/bookorbit_client.h`), ledger internals, UI.

## Build

- Triggers and limits exactly as the spec's Triggers table, including the 5-minute skip and close-then-sleep collapse.
- Pre-sleep hold with the 15 s watchdog from task 12; skip below 20% battery.
- Wi-Fi: remember prior state, bring it up only when there's work, restore it afterwards. Use the same Nickel functions NickelDBus uses.
- Full-sync order: push (batches of 20), pull since cursor, apply only to closed books (queue the rest), refresh caches if time remains.
- Conflict rules exactly as the spec's table. Remote positions are stored for the jump prompt, never applied silently.
- Respect `disable-sync`. Never sync during startup.
- Expose status for the home screen header ("Synced 10:42 · 3 waiting", "Offline · 3 waiting") and the failure-count rule for toasts.

## Acceptance tests (desktop, with the mock server and Nickel fake)

- Each trigger runs the right work and respects its limits.
- A sync cut off at any point loses nothing and duplicates nothing on retry.
- Wi-Fi is restored to its previous state in every path, including timeouts.
- Writes for an open book wait until close.
