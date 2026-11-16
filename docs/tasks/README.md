# Build tasks

Each file here is a brief for one Claude Code cloud session. Sessions in the same wave run in parallel because they own separate folders. Merge every PR in a wave before starting the next.

## Order

| Wave | Tasks | Runs in parallel | Needs |
| --- | --- | --- | --- |
| 0 | `00-scaffold` | No (run alone) | Nothing |
| 1 | `10-ledger`, `11-bookorbit-client`, `12-crash-safety`, `13-ui-kit` | Yes | Wave 0 merged |
| 2 | `20-recorder`, `21-sync-engine`, `22-screens` | Yes | Wave 1 merged, `docs/VERIFY.md` items 1–9 answered |
| 3 | `30-home-takeover`, `31-jump-prompt`, `32-highlights`, `33-collections-status-removals` | Yes | Wave 2 merged and tested on the device |

## Prompt to give each session

```
Implement docs/tasks/<file>.md. Read CLAUDE.md, docs/SPEC.md and docs/VERIFY.md first.
Only edit the folders the brief says you own. Open a pull request when done.
```

## Between waves

1. Review and merge the wave's PRs.
2. Run `./build-kobo.sh`, copy `out/KoboRoot.tgz` to `.kobo/` on the Kobo, and test.
3. Write anything you find into the next wave's briefs before starting it.

## Starting sessions

**Claude Code desktop app:** open the Code tab, start a new session, pick your Smalt cloud environment under Cloud in the environment picker, choose the `smalt` repo, and paste the prompt above with the task file name. Start one session per task in the wave; they run in parallel in the sidebar.

**Terminal:** from the repo folder, one command per task:

```bash
claude --cloud "Implement docs/tasks/10-ledger.md. Read CLAUDE.md, docs/SPEC.md and docs/VERIFY.md first. Only edit the folders the brief says you own. Open a pull request when done."
```
