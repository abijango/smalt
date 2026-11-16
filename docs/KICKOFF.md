# Kicking off the build with a Claude Code project

## Models per task

Set **Project settings > General**: Thread model **Sonnet 5.5**, high effort. Coordinator model **Opus 5.5**, low effort. Tasks marked Opus below ask for it explicitly.

| Task | Model | Why |
| --- | --- | --- |
| 00 scaffold | Opus 5.5, high | Cross toolchain, NickelHook setup and the shared interfaces everything else depends on |
| 10 ledger | Opus 5.5, high | Data integrity on a FAT partition; mistakes lose reading data |
| 11 BookOrbit client | Sonnet 5.5, high | Well-specified protocol, plenty of tests |
| 12 crash safety | Opus 5.5, high | Boot-loop protection; must be right first time |
| 13 UI kit | Sonnet 5.5, high | Defined design tokens and components |
| 20 recorder | Opus 5.5, high | Hooks, timing order at sleep, clock handling |
| 21 sync engine | Opus 5.5, high | Concurrency, Wi-Fi state, retry and conflict rules |
| 22 screens | Sonnet 5.5, high | Screens built from the kit |
| 30 home takeover | Opus 5.5, high | Hooking Nickel's home with fallbacks |
| 31 jump prompt | Sonnet 5.5, high | Contained feature |
| 32 highlights | Sonnet 5.5, high | Contained, with a strict read-only rule |
| 33 collections, status, removals | Sonnet 5.5, medium | Straightforward mapping |
| 34 migration | Opus 5.5, high | One-shot, touches real user data |
| Wave review | Opus 5.5, high | Cross-PR consistency against the spec |

## First message (wave 0)

```
Read docs/tasks/README.md, docs/KICKOFF.md and docs/PROJECT-INSTRUCTIONS.md.
Remember: work runs in waves and a wave starts only when I say "wave N approved".
Start one thread for docs/tasks/00-scaffold.md on Opus 5.5 at high effort.
Tell me when its PR is ready.
```

## Starting later waves

After you've merged the previous wave and tested on the Kobo:

```
Wave 1 approved. Start one thread per brief in wave 1, with the models in docs/KICKOFF.md, all at once.
```

Use the same message for waves 2, 3 and 3b, changing the number.

## Wave review thread

The coordinator starts this when every PR in a wave is ready:

```
On Opus 5.5 at high effort, review every open PR from wave N against docs/SPEC.md, CLAUDE.md and the briefs.
Check: folder ownership respected, interfaces unchanged, acceptance tests present and passing, crash-safety and data rules followed,
nothing invented about BookOrbit or Kobalt. Leave review comments on the PRs. Don't push changes. Summarise blockers for me.
```

## Between waves (you)

1. Read the review summary; merge the PRs.
2. Build with ./build-kobo.sh, copy out/KoboRoot.tgz to .kobo/ on the Kobo, test the "Device check" items.
3. Tell the coordinator anything that failed and ask it to remember fixes that apply to later waves.
4. Send "wave N approved".
