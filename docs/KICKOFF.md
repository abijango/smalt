# Kicking off the build in Claude Code cloud sessions

One cloud session per wave. That session is the **orchestrator**: it runs the wave's tasks as parallel subagents, each on the model below, then integrates, tests and opens one pull request for the wave. You merge and test on the Kobo between waves.

## Models

| Role or task | Model | Why |
| --- | --- | --- |
| Orchestrator session (every wave) | Opus 5.5 | Splits work, integrates, reviews against the spec |
| 00 scaffold | Opus 5.5 (run by the orchestrator itself) | Toolchain, NickelHook and the shared interfaces |
| 10 ledger | opus | Data integrity on a FAT partition |
| 11 BookOrbit client | sonnet | Well-specified protocol, plenty of tests |
| 12 crash safety | opus | Boot-loop protection |
| 13 UI kit | sonnet | Defined tokens and components |
| 20 recorder | opus | Hooks, timing order at sleep, clocks |
| 21 sync engine | opus | Concurrency, Wi-Fi state, retries |
| 22 screens | sonnet | Built from the kit |
| 30 home takeover | opus | Hooking Nickel's home with fallbacks |
| 31 jump prompt | sonnet | Contained |
| 32 highlights | sonnet | Contained, strict read-only rule |
| 33 collections, status, removals | sonnet | Straightforward mapping |
| 34 migration | opus | One-shot, real user data |
| Wave review | opus | Cross-task consistency |

Lower-case names are the subagent model aliases the orchestrator passes when it starts each subagent.

## Starting a session (desktop app)

Code tab → new session → environment: your **Smalt** cloud environment → repository `abijango/smalt` → model **Opus 5.5** → paste the wave prompt below.

Terminal equivalent: `claude --cloud "<wave prompt>"` after selecting the environment with `/remote-env`.

## Wave 0 prompt

```
Implement docs/tasks/00-scaffold.md yourself. Read CLAUDE.md, docs/SPEC.md and docs/VERIFY-FINDINGS.md first.
Work on branch smalt/wave-0. Run the desktop build and tests and paste the summary.
Push and open a draft PR titled "Wave 0: scaffold" listing anything that needs a device check. Don't merge.
```

## Wave prompt (waves 1, 2, 3 and 3b)

Change the wave number and task list.

```
You are the orchestrator for wave 1 of Smalt: tasks 10, 11, 12 and 13.
Read CLAUDE.md, docs/SPEC.md, docs/VERIFY-FINDINGS.md, docs/tasks/README.md and docs/KICKOFF.md.

1. Start one subagent per task, all in parallel, each on the model docs/KICKOFF.md gives for it.
   Give each its brief (docs/tasks/<file>.md) and these rules: edit only the folders the brief owns;
   propose interface changes instead of making them; write the acceptance tests first; report what you built,
   what needs a device check, and any spec questions.
2. Don't edit the subagents' folders while they work.
3. When all have finished, run the full desktop build and tests. Send failures back to the owning subagent;
   fix only shared build files yourself.
4. Start one opus review subagent to check every task against its brief, docs/SPEC.md and CLAUDE.md
   (folder ownership, unchanged interfaces, tests present, crash-safety and data rules, nothing invented about
   BookOrbit or Kobalt). Fix what it finds through the owning subagent.
5. Commit each task separately as "<task number>: <task name>" on branch smalt/wave-1, push,
   and open one draft PR "Wave 1" with a section per task: summary, tests, device checks, questions.
6. Stop and report. Don't merge, and don't start the next wave.
Commit and push work in progress regularly.
```

## Between waves (you)

1. Review the wave PR; merge it.
2. `./build-kobo.sh`, copy `out/KoboRoot.tgz` to `.kobo/` on the Kobo, test the device checks.
3. Put anything you learned into the next wave's briefs or `CLAUDE.md`, commit, then start the next wave's session.
