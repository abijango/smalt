# Build tasks

Each file here is a brief for one Claude Code cloud session. Sessions in the same wave run in parallel because they own separate folders. Merge every PR in a wave before starting the next.

## Order

| Wave | Tasks | Runs in parallel | Needs |
| --- | --- | --- | --- |
| 0 | `00-scaffold` | No (run alone) | Nothing |
| 1 | `10-ledger`, `11-bookorbit-client`, `12-crash-safety`, `13-ui-kit` | Yes | Wave 0 merged |
| 2 | `20-recorder`, `21-sync-engine`, `22-screens` | Yes | Wave 1 merged (verification answers are in `docs/VERIFY-FINDINGS.md`) |
| 3 | `30-home-takeover`, `31-jump-prompt`, `32-highlights`, `33-collections-status-removals` | Yes | Wave 2 merged and tested on the device |
| 3b | `34-migration` | No | Wave 3 working on the device |

## Running a wave

See `docs/KICKOFF.md`: one orchestrator cloud session per wave, which runs the wave's tasks as parallel subagents on the right models.
