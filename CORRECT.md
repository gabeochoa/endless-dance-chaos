# CORRECT — endless-dance-chaos (2026-10-04)
Dirty fun-loop WIP in the working tree (main.cpp, systems.h, e2e_commands.h, random_engine.cpp, RaylibOpOverloads.h, .gitignore, afterhours_gaps.md) was left untouched; all commits below are disjoint from it.

## Classes (each happened >=2x)
| # | Class | Evidence |
|---|---|---|
| 1 | Coupled speed: `set_agent_speed` also sets clock `debug_time_mult`, racing tests to Exodus | a3c62dc flaky 22_stage_fill; 7e81102 adds `set_move_speed` to decouple, yet 24 tests still used the coupled form |
| 2 | Vacuous e2e: always-true `gte 0`, screenshot-only tests that can only pass | 29ad85b fixes 7 (15_exodus, 20_full_game…); 7e81102 strengthens more; one `gte 0` (15_carryover) still present at HEAD |
| 3 | Direct `raylib::` in game code, breaking the Metal/Sokol backend | 7 decouple commits: 3f2a1d1, d27f4ab, 25c6189, d7b8b1e, dab8306, aa1caf2, 1f26f7d |
Not counted (once): `make all` running `clang-format -i` (mutates tree on build) — noted, not gated.

## Fix level + why
1. Behaviour tests + lint (`scripts/check_e2e_speed.py`, in `make check`/`make test`): the decoupled command already existed; only enforcement stops copy-the-nearest-example. Migrated 24 tests, allowlisted 3 clock-driven with `# coupled-speed-ok:`.
2. Lint (`scripts/check_e2e_assertions.py`): bans vacuous thresholds, requires assert or explicit `# visual-only:` — visual tests stay, but labelled.
3. Lint naming the seam (`scripts/check_raylib_boundary.py`): architecture (rl.h seam) already existed; a check is the highest level that prevents regression without touching vendor. Allowlist: e2e pixel readback in e2e_commands.h only.

## Commits (local, branch expo, not pushed)
- cb2cf8d e2e: ban coupled set_agent_speed, migrate to set_move_speed
- af0e13a e2e: require assertion or explicit visual-only
- d2f1db8 lint: forbid direct raylib outside src/rl.h
- (this file) docs: CORRECT.md + CLAUDE.md

## Proof (fails on real past mistakes, passes now)
- check_e2e_speed: FAIL on `a3c62dc^:22_stage_fill.e2e` (exit 1), ok 81 files now.
- check_e2e_assertions: FAIL on `29ad85b^:15_exodus/20_full_game` (vacuous), ok now; it also caught live 15_carryover.
- check_raylib_boundary: FAIL on `aa1caf2^:src/audio.h` (51 raylib:: uses), ok now. `make check` green. Game/e2e suite not run (needs a built windowed binary; migrations are 1:1 to the existing set_move_speed command).

## Rule table
| Rule | Gate |
|---|---|
| set_move_speed, not set_agent_speed | check_e2e_speed.py |
| assert or `# visual-only:`; no always-true thresholds | check_e2e_assertions.py |
| no raylib:: outside src/rl.h | check_raylib_boundary.py |
