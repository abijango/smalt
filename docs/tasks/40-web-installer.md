# 40: Web installer (proposed)

**Wave 4, proposed. Needs wave 3 working on the device, and the maintainer's approval of the spec section below.** Until it is approved, this brief is a draft: no session should start it, and `docs/SPEC.md` is unchanged.

## Goal

A web page that installs, updates and removes Smalt on a Kobo connected by USB, checks that the device and firmware are supported, and writes the BookOrbit login into `config.ini`, so nobody has to find hidden folders or edit files by hand.

## Spec sections

The proposed "Installation" section below; Crash safety ("Uninstall"); Sync engine ("Networking": credentials); CLAUDE.md hard rules 3 (never lose reading data) and 5 (no secrets in the repo).

## Owns

`installer/` (the site, its tests and its build), and the Pages deployment workflow `.github/workflows/installer.yml` (CI changes need the maintainer's go-ahead; approving this brief gives it for that one file).

## Must not touch

Everything else. In particular `src/`, `include/smalt/`, `packaging/` and `toolchains/`. The site consumes `out/Smalt-qt5.zip` as built by `./build-kobo.sh`; if it needs something from the mod (for example the installed version on user storage), propose it in the PR.

## Prior art

Kobalt's website (kobalt.nicoverbruggen.be, "Install or remove on Kobo") does this for Kobalt. Read on November 17, 2026 from its published script (`assets/installer-page-*.js`). Its source isn't in the Kobalt repository and no licence was found for it, so **write Smalt's installer from scratch; do not copy its code.** What it does, as a model:

- Chrome and Edge on a desktop only: `window.showDirectoryPicker({ mode: "readwrite" })` on the Kobo drive (`KOBOeReader`). Other browsers get manual instructions.
- Reads `.kobo/version` (six comma-separated fields: serial, …, firmware version as the third field, …, hardware ID as the sixth) and refuses a folder that isn't the drive's root.
- Maps the hardware ID to a model and checks the serial prefix. Its table has Kobo Clara Colour `00000000-0000-0000-0000-000000000393` (`N367`), Kobo Clara BW `…391` (`N365`) and `…395` (`P365`), Kobo Libra Colour `…390` (`N428`). **Confirm these on the devices** before relying on them.
- Writes a probe file, reads it back and deletes it before changing anything.
- Verifies the package (SHA-256 per file against a manifest) before writing, and writes `.kobo/KoboRoot.tgz` last.
- Restores Kobalt's `uninstall` marker if its first-load receipt exists but the marker is missing.
- Edits `ExcludeSyncFolders` in `.kobo/Kobo/Kobo eReader.conf` so Nickel doesn't import files from hidden folders.

## Build

- **Static site** in `installer/`: plain HTML, CSS and ES modules, no framework, no third-party scripts, no analytics. Everything runs in the browser; the page makes no network requests except loading its own files and the package.
- **Hosting:** GitHub Pages (or another static host the maintainer picks), with `Smalt-qt5.zip` and a manifest (version, SHA-256 of `KoboRoot.tgz` and of each `.adds` file) published next to the page, built from a tagged release. Serving the package from the same site avoids cross-origin download limits.
- **Connect:** pick the Kobo drive; read `.kobo/version`; show model, firmware and serial check. Supported: Clara Colour, Clara BW and Libra Colour on firmware 4.46.23852 (`docs/SPEC.md`, "Decisions so far"). Anything else: explain why and stop. Write probe as above.
- **State:** show one of: not installed; installed (receipt `.adds/.bookorbit.initialized` and marker `.adds/bookorbit/uninstall` present); removal pending (receipt present, marker missing); and whether `config.ini` exists. The library itself lives on the system partition and isn't visible over USB, so the installed version comes from a file on user storage (propose one in the PR, for example the installer writing `.adds/bookorbit/package.json`, or the mod writing its version at startup).
- **Install or update:** verify the package against the manifest; write `.adds/bookorbit/certs/ca-bundle.pem` and the other `.adds/bookorbit/` files from the ZIP; restore the `uninstall` marker if the receipt exists but the marker doesn't; write `.kobo/KoboRoot.tgz` last; tell the user to eject and wait for the Kobo to restart.
- **Login form:** server URL (HTTPS only), KOReader username and password, with the same wording as Settings ("from BookOrbit's Settings > KOReader"). Writes `.adds/bookorbit/config.ini` in the format of `config.ini.example`, only when the user submits the form; an existing `config.ini` is shown as present and only replaced after confirmation. The password goes to the Kobo's drive and nowhere else; it is never stored in the browser. No "Test connection" in the page: the BookOrbit server's cross-origin rules may block it, and the device's Settings screen has one.
- **Remove:** delete `.adds/bookorbit/uninstall` and explain that the next restart removes Smalt. Optional checkbox: also delete `config.ini` (the login). **Never** delete or offer to delete `ledger.sqlite`, `backup/` or `logs/` (CLAUDE.md hard rule 3); say that they stay.
- **Kobo settings:** if Smalt needs `ExcludeSyncFolders` (check on the device whether Nickel imports anything from `.adds/bookorbit/`), merge Smalt's entries into the existing value instead of replacing it, because Kobalt's installer edits the same setting. `BookOrbit/` (downloads) must never be excluded.
- **Kobalt checks (read-only):** warn if Kobalt isn't installed (Smalt opens books in Kobalt). Don't open `kobalt.sqlite` (hard rule 8). If checking that Kobalt's KOSync is off is wanted (`docs/SPEC.md`, "Wi-Fi control"), propose how first.
- **Copy:** plain, sentence case, matching `docs/SPEC.md` "UX details and copy". Warn that a Kobo software update can remove mods.

## Acceptance tests

- Unit tests (Node's built-in test runner, no dependencies) for: parsing `.kobo/version` (good, short, malformed); model and firmware decisions for each supported and a few unsupported devices; the plan of file writes and deletions for install, update, removal and login, run against an in-memory fake of the directory API; `config.ini` generation (escaping, HTTPS-only URL); `ExcludeSyncFolders` merging that keeps Kobalt's entries.
- Removal never touches `ledger.sqlite`, `backup/` or `logs/` (test with a fake drive that has them).
- A package with a wrong SHA-256 is refused before anything is written.
- The page works offline after loading, except for downloading the package.
- CI: the Pages workflow runs the unit tests and deploys only from a release tag.

## Device checks (maintainer)

On a Clara Colour (and a Clara BW or Libra Colour if available): install from the page, restart, Smalt runs; enter the login, the device syncs; update to a newer package; remove from the page, restart, Smalt is gone and the ledger is still there; reinstall works; an unsupported firmware is refused.

## Proposed addition to `docs/SPEC.md`

For the maintainer to approve, edit or reject. Suggested place: a new section "Installation" after "Migration off Kobo sync".

> ## Installation
>
> Smalt installs like Kobalt: one system package, `KoboRoot.tgz`, containing only the mod library. On its first start the mod writes its own support files to `.adds/bookorbit/` (the `uninstall` marker and `config.ini.example`) and records a receipt, so deleting `uninstall` later removes it, and a reinstall writes the marker again. A ZIP, `Smalt-qt5.zip`, holds `.kobo/KoboRoot.tgz` and the other `.adds/bookorbit/` files for installing by hand.
>
> A web installer does the same from Chrome or Edge on a computer, with the Kobo connected by USB:
>
> - It checks the model and firmware from `.kobo/version` and refuses anything Smalt doesn't support.
> - It installs or updates Smalt, and asks for the BookOrbit server and KOReader login, which it writes to `.adds/bookorbit/config.ini` on the Kobo and nowhere else.
> - It removes Smalt by deleting the `uninstall` marker. The ledger, its backups and the logs always stay.
>
> The installer is a static page with no tracking. It never sends the login anywhere.
