# Smalt: instructions for coding agents

Read this whole file before doing anything. Then read `docs/SPEC.md` and your task brief in `docs/tasks/`.

## What Smalt is

A NickelHook mod for Kobo e-readers that runs inside Nickel (Kobo's reading app) next to an unmodified Kobalt reader. It records reading sessions offline in its own SQLite ledger, syncs them and other reading data with a BookOrbit server through the protocol BookOrbit's own KOReader plugin uses (`/api/v1/koreader/…`, KOReader credentials), and replaces Nickel's home screen with a library browser.

`docs/SPEC.md` is the source of truth. If your brief and the spec disagree, follow the spec and say so in your PR description.

## Targets

- Devices: Kobo Clara Colour, Clara BW (6", 1448 × 1072), Libra Colour (7", 1680 × 1264). All 300 ppi; colour layers are 150 ppi.
- Firmware: 4.46.23852, which uses **Qt 5**. Qt 6 firmware is out of scope until the 4.x build is stable.
- Toolchain: an ARM cross-compiler for Kobo plus NickelHook. Model the toolchain setup on Kobalt's `toolchains/` and `build-kobo.sh` (MIT-licensed parts only; keep their licence notices).

## Hard rules

1. **Stay in your lane.** Only edit the folders your task brief says you own. If you need a change elsewhere (for example an interface in `include/smalt/`), stop and describe it in the PR instead of making it.
2. **Never break the device.** Code that runs inside Nickel must not block the UI thread, must not crash on missing files or network failure, and must respect the crash-safety rules in `docs/SPEC.md` ("Crash safety").
3. **Never lose reading data.** The ledger is append-only for sessions. Nothing in the outbox is deleted until the server confirms it. Never delete a ledger file; rename it.
4. **Never write to Nickel's database for a book that is open.**
5. **No secrets in the repo.** The KOReader username and key live in `.adds/bookorbit/config.ini` on the device. Tests use the mock server and fake credentials.
6. **Desktop-testable first.** Everything that doesn't need Nickel must build and pass tests on Linux x86_64 with the desktop target, because cloud sessions can't run a Kobo.
7. **Follow the visual design exactly** for anything in `src/ui/`: colours, rules and component behaviour are in `docs/SPEC.md` under "Visual design". Screenshots are in `docs/mockups/`.
8. **Never open Kobalt's database while Kobalt has it open.** Read `kobalt.sqlite` only after a book closes, from a copy, read-only. Never write it.

## How the build runs

Each wave runs in one cloud session that acts as orchestrator (see `docs/KICKOFF.md`): it starts one subagent per task brief, on the model `docs/KICKOFF.md` names, then integrates, tests and opens one pull request for the wave. Never merge, force-push, change CI, or edit `docs/SPEC.md` or this file without the maintainer's go-ahead. If something you need is missing (a toolchain download, a file, an endpoint detail), say exactly what is missing and stop; don't mock around it or guess.

## Repo layout

Created by task 00 (scaffold). Each later task owns one area.

| Path | Contents | Owner task |
| --- | --- | --- |
| `include/smalt/` | Shared interfaces (headers only) | 00 scaffold; changes need review |
| `src/core/ledger/` | SQLite ledger, schema, migrations | 10 |
| `src/core/outbox/` | Outbox queue and retry logic | 10 |
| `src/bookorbit/` | BookOrbit client (KOReader plugin protocol) | 11 |
| `src/safety/` | Startup marker, two-strike recovery, switches, uninstall | 12 |
| `src/ui/kit/` | Design tokens, palettes, components, mm-based layout | 13 |
| `src/nickel/` | NickelHook hooks and Nickel database access | 20, 30 |
| `src/recorder/` | Session capture | 20 |
| `src/sync/` | Sync engine, triggers, Wi-Fi control, conflict rules | 21 |
| `src/ui/screens/` | Home, Library, Series, Search, Book detail, More | 22 |
| `src/features/` | Jump prompt, highlights, collections, status, removals, migration | 31–34 |
| `tests/` | Unit and integration tests, mirroring `src/` | Same as the code under test |
| `tools/mock-bookorbit/` | Mock BookOrbit server for tests | 11 |
| `packaging/` | `.adds/bookorbit/` assets, `KoboRoot.tgz` build | 00 |

## Build and test

Task 00 creates these; until then they don't exist.

```bash
# Desktop build and tests (works in cloud sessions)
cmake -S . -B build -G Ninja -DSMALT_TARGET=desktop
cmake --build build
ctest --test-dir build --output-on-failure

# Kobo build (needs the cross toolchain)
./build-kobo.sh            # produces out/KoboRoot.tgz
```

Every PR must pass the desktop build and tests.

## Code style

- C++17, Qt 5 APIs only (no Qt 6-only APIs).
- `clang-format` with the repo's `.clang-format` (based on Kobalt's).
- No exceptions across the NickelHook boundary; return status values.
- Every network call has a timeout. Every file operation handles failure.
- Log through `smalt::log` (created in the scaffold) to `.adds/bookorbit/logs/`.

## Working on a task

1. Read `docs/SPEC.md`, then your brief in `docs/tasks/`.
2. Read the interfaces you implement or call in `include/smalt/`.
3. Write tests first where the brief lists acceptance tests.
4. Keep the PR to your task. If you find a problem elsewhere, note it in the PR description under "Found elsewhere".
5. PR description: what you built, how it maps to the spec sections, what you couldn't verify without a device, and any spec questions.

## Things that need a real device

Agents can't test these; mark them clearly in the PR so the maintainer checks them on the Kobo:

- NickelHook symbol resolution and hooks
- Nickel database reads and writes
- Wi-Fi control, sleep and wake signals
- Screen refresh behaviour and how colour looks on the Kaleido panel
- Anything in the "Verify first" list in `docs/SPEC.md`
