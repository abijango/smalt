# Project instructions (paste into Project settings > Memory > Project instructions)

This project builds Smalt, a NickelHook mod for Kobo e-readers that records reading sessions offline and syncs them, progress, highlights, status and downloads with a BookOrbit server, next to an unmodified Kobalt reader. The repository is abijango/smalt.

Sources of truth, in order: docs/SPEC.md, CLAUDE.md, docs/VERIFY-FINDINGS.md, then the task brief in docs/tasks/. If they disagree, follow the spec and say so in the pull request.

How work is organised
- Work runs in waves (docs/tasks/README.md). Only start threads for the current wave. Never start a wave until I say "wave N approved" in the project conversation.
- One thread per task brief. A thread edits only the folders its brief says it owns. Changes to include/smalt/ are proposed in the PR description, never made, except in task 00.
- Start each thread with the model named for its task in docs/KICKOFF.md. If none is named, use the project's thread model.

How each thread works
- Branch from main, named smalt/<task-number>-<short-name>. Open one draft pull request per thread, titled "<task-number>: <task name>".
- Read CLAUDE.md, docs/SPEC.md, docs/VERIFY-FINDINGS.md and the brief before writing code. Write the brief's acceptance tests first.
- Before calling work done: run the desktop build and tests (cmake -S . -B build -G Ninja -DSMALT_TARGET=desktop && cmake --build build && ctest --test-dir build --output-on-failure) and paste the summary lines in the final message.
- The PR description lists: what was built, the spec sections it implements, anything that needs a real Kobo to verify (marked "Device check"), and spec questions.
- Commit and push work in progress regularly; a resumed sandbox can lose uncommitted changes.

When something is missing
- If a tool, toolchain download, secret or file you need is unavailable, say exactly what is missing in your first message and stop. Don't mock around it, stub it silently or guess an interface.
- Never invent BookOrbit endpoints or Kobalt behaviour. Use docs/VERIFY-FINDINGS.md; if it doesn't cover something, ask.

Needs my go-ahead
- Merging any pull request, changing CI, force-pushing, editing docs/SPEC.md or CLAUDE.md, or changing an interface in include/smalt/ after wave 0.

Coordinator behaviour
- Propose a wave's threads with their models and wait for my go-ahead the first time; after wave 1 you may start a wave's threads together once I approve the wave.
- Post only when a thread finishes, is blocked, or needs me. When a whole wave's PRs are ready, start one review thread (see docs/KICKOFF.md) and then tell me the wave is ready for device testing.
