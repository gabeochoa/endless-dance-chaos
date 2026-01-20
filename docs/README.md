# Endless Dance Chaos - Documentation

## Quick Links

- **[Roadmap](roadmap.md)** — Full game design specification
- **[Questions](questions.md)** — Answered clarification questions
- **[Phases](phases/00-overview.md)** — Engineering implementation phases

## For the Intern

Start here:

1. Read `roadmap.md` to understand the game
2. Read `phases/00-overview.md` to understand the development approach
3. Start with `phases/00-testing-framework.md` to set up E2E testing
4. Then proceed to `phases/01-density-system.md`
5. Complete each phase, get feedback, then move to next

## Phase Order

| # | Phase | Est. Time | Status |
|---|-------|-----------|--------|
| 00 | [Testing Framework](phases/00-testing-framework.md) | 3-4 hrs | ⬜ |
| 01 | [Density System](phases/01-density-system.md) | 2-3 hrs | ⬜ |
| 02 | [Crush Damage](phases/02-crush-damage.md) | 2-3 hrs | ⬜ |
| 03 | [Game Over](phases/03-game-over.md) | 2 hrs | ⬜ |
| 04 | [UI Skeleton](phases/04-ui-skeleton.md) | 3-4 hrs | ⬜ |
| 05 | [Time Clock](phases/05-time-clock.md) | 2-3 hrs | ⬜ |
| 06 | [Fence & Gate](phases/06-fence-gate.md) | 3-4 hrs | ⬜ |
| 07 | [Path Speed](phases/07-path-speed.md) | 1-2 hrs | ⬜ |
| 08 | [Build Tools](phases/08-build-tools.md) | 4-5 hrs | ⬜ |
| 09 | [Artist Schedule](phases/09-artist-schedule.md) | 3-4 hrs | ⬜ |
| 10 | [Top Bar UI](phases/10-top-bar-ui.md) | 2 hrs | ⬜ |
| 11 | [Build Bar UI](phases/11-build-bar-ui.md) | 2-3 hrs | ⬜ |
| 12 | [Timeline UI](phases/12-timeline-ui.md) | 3-4 hrs | ⬜ |
| 13 | [Minimap](phases/13-minimap.md) | 3-4 hrs | ⬜ |
| 14 | [Density Overlay](phases/14-density-overlay.md) | 2-3 hrs | ⬜ |
| 15 | [Polish Pass](phases/15-polish-pass.md) | 3-4 hrs | ⬜ |

**Total: ~45-55 hours**

## Key Principles

1. **Small commits** — Each phase is a commit
2. **Test before moving on** — Check acceptance criteria
3. **Ask for feedback** — After each phase
4. **Don't over-polish** — Get it working, then move on
5. **Leverage existing code** — Check what's already built before writing new

## What's Already Built

The codebase already has:
- Isometric camera with rotation
- Agent spawning and basic movement
- Facility components (Bathroom, Food, Stage)
- Path tiles with signpost-based pathfinding
- Builder state with pending tiles
- Stage state machine
- MCP integration for debugging

## Afterhours Library

The `vendor/afterhours` library provides:
- UI system with autolayout
- Input system
- Timer plugin
- Collision system
- Color utilities
- **E2E Testing** (sync from wordproc if missing)

See `vendor/afterhours/example/` for usage examples.

## E2E Testing

Run tests with:
```bash
./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_name.e2e"
```

Create `.e2e` script files in `tests/e2e_scripts/`. See [Testing Framework](phases/00-testing-framework.md) for details.

