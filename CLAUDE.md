# Agent rules — see CORRECT.md for evidence
- e2e: use `set_move_speed`, never `set_agent_speed` (it also speeds the clock) unless header says `# coupled-speed-ok: why`.
- e2e: every test needs `assert_*`/`expect_*` or `# visual-only: why`; never an always-true threshold.
- No `raylib::` outside `src/rl.h` (use afterhours gfx/vec types). Run `make check` before claiming tests pass.
