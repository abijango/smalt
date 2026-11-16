# 00: Scaffold

**Wave 0. Run alone; everything else depends on it.**

## Goal

Create the build system, the Kobo packaging, the shared interfaces, and empty modules, so wave 1 sessions can work in parallel without touching each other's files.

## Spec sections

Architecture, Crash safety, On-device data model, Sync engine (for interface shapes).

## Owns

Everything at the repo root, `include/smalt/`, `packaging/`, `cmake/`, `toolchains/`, `.github/`, `scripts/`, and empty stub folders under `src/`, `tests/` and `tools/`.

## Build

- CMake + Ninja, C++17. Option `SMALT_TARGET=desktop|kobo`.
- **Desktop target:** builds every module except `src/nickel/` as static libraries plus a test runner (Qt Test or Catch2), on Linux x86_64 with system Qt 5.
- **Kobo target:** cross-compiles with the Kobo toolchain and links NickelHook to produce `libsmalt.so`. Model the toolchain and NickelHook setup on Kobalt's `toolchains/` and `build-kobo.sh` (MIT-licensed parts; keep notices).
- `build-kobo.sh` produces `out/KoboRoot.tgz` containing the library and the `.adds/bookorbit/` assets: `uninstall` marker, default `config.ini.example`, CA bundle placeholder.
- Extend `scripts/cloud-setup.sh` to fetch and cache the cross toolchain.
- GitHub Actions workflow running the desktop build and tests on every PR.
- `.clang-format` based on Kobalt's.

## Interfaces to define in `include/smalt/`

Headers only, with doc comments that cite the spec section. Keep them small.

- `ledger.h`: open/close (for USB), sessions (append), progress, annotations, book state, book_map, pending decisions.
- `outbox.h`: enqueue, next batch (max 20), mark confirmed, mark failed with backoff.
- `bookorbit_client.h`: async calls with timeouts for library, series, search, download, progress, sessions, annotations, status, collections, change feed. Result types carry HTTP status and error kind.
- `safety.h`: startup marker, two-strike check, switch files (`disabled`, `disable-home`, `disable-sync`), readiness timeout.
- `nickel.h`: what the rest of the code needs from Nickel (signals for open, close, sleep, wake, USB, Wi-Fi; open book; import files; read and write rows). A fake implementation for desktop tests lives in `tests/fakes/`.
- `sync.h`: triggers in, results out.
- `ui_kit.h`: tokens, palette selection by device model, component factory.
- `log.h`: `smalt::log` writing to `.adds/bookorbit/logs/`.
- `paths.h`: every on-device path from the spec in one place.

## Acceptance

- `cmake -S . -B build -G Ninja -DSMALT_TARGET=desktop && cmake --build build && ctest --test-dir build` passes with placeholder tests.
- `./build-kobo.sh` produces `out/KoboRoot.tgz` (note in the PR if the toolchain download couldn't run in the cloud).
- CI green.
- `CLAUDE.md` build commands work as written; update them if anything changed.

## Device check (maintainer)

Install `KoboRoot.tgz` on the Kobo: the mod loads, logs a startup line, does nothing else, and removing `uninstall` cleanly uninstalls it.
