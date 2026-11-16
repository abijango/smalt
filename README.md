# Smalt

Smalt is a companion app for Kobo e-readers that syncs with a self-hosted [BookOrbit](https://github.com/bookorbit/bookorbit) server. It runs inside Kobo's own software (Nickel) as a NickelHook mod, next to an unmodified [Kobalt](https://github.com/nicoverbruggen/kobalt) reader.

It does three things:

- **Records exactly what you read.** Every reading session is stored on the device with its real start and end time, offline, and uploaded to BookOrbit when Wi-Fi is available. No estimates dated to sync time.
- **Replaces the home screen.** Browse and download your BookOrbit library (ebooks, PDFs, comics) and open books straight into Kobalt.
- **Syncs both ways.** Progress, highlights, notes, status, ratings and collections.

## Status

Design complete, build not started. The full design is in [`docs/SPEC.md`](docs/SPEC.md).

## Targets

| Device | Firmware |
| --- | --- |
| Kobo Clara Colour, Clara BW (6") | 4.46.23852 (Qt 5) first |
| Kobo Libra Colour (7") | 4.46.23852 (Qt 5) first |

Qt 6 firmware (5.x and 6.x) comes later.

## Repo guide

| Path | What's there |
| --- | --- |
| `CLAUDE.md` | Instructions every coding agent reads first |
| `docs/SPEC.md` | The design spec: source of truth |
| `docs/VERIFY.md` | On-device checks to run before wave 2 |
| `docs/tasks/` | One brief per build task, grouped in waves |
| `scripts/cloud-setup.sh` | Setup script for Claude Code cloud environments |

## Not affiliated

Not affiliated with Rakuten Kobo, BookOrbit or Kobalt.
