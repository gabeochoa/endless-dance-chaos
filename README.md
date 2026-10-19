# Endless Dance Chaos

A **Mini Metro-style minimalist management game** about running a music festival.

**One-sentence pitch**: "Tower defense, but the towers are bathrooms and the creeps are festival-goers."

## Getting Started

```bash
make
./output/dance.exe
```

## Building with Zig

Requires zig 0.16.0. The Makefile remains the primary build; this is an
alternative that produces the same game from the same sources.

```sh
zig build              # -> zig-out/bin/dance
zig build run          # build + run; extra args after `--` are forwarded
zig build run -- --test-mode --test-script tests/e2e/01_grid_exists.e2e
zig build test         # build + run ./dance --test-mode --headless --test-script-dir tests/e2e
```

`zig build test` runs the full e2e suite (101 scripts, ~900–1500s of
real-second waits). The `test` step applies no timeout: if you want one,
wrap it externally, e.g. `perl -e 'alarm 1500; exec @ARGV' -- zig build test`.

raylib assumption: `build.zig` finds raylib through zig 0.16's built-in
pkg-config support (`linkSystemLibrary("raylib", .{})`, which defaults to
`use_pkg_config = .yes`). That only works when the `pkg-config` on PATH can
see `raylib.pc`. On this machine the Homebrew raylib lives at
`/Users/gabeochoa/homebrew/Cellar/raylib/5.5`, and `build.zig` adds that
Cellar's `include/` + `lib/` as a fallback when they exist on disk, so the
build works even when PATH puts a non-Homebrew `pkg-config` first (whose
search path has no raylib). The fallback is inert on machines without that
Cellar path. No `build.zig.zon` is needed (verified: zig 0.16 builds fine
without one).

Notes: no PCH — the dance Makefile has none, and sources include headers
normally. Optimization/debug flags come from `-Doptimize` (default `Debug`,
i.e. `-O0` + debug info, matching the Makefile's `-g` with no `-O` flag);
the C++ warning flags are the Makefile's `FLAGS`/`NOFLAGS` verbatim.
The Metal/Sokol build (`make metal`, `-DAFTER_HOURS_USE_METAL`) is NOT
covered by `build.zig`: zig covers the default raylib build only, so keep
using `make metal` for that backend. MCP is off by default, as in the
Makefile (only `make MCP=1` defines `AFTER_HOURS_ENABLE_MCP`).
The macOS trackpad-pinch monitor is on: `build.zig` passes `-fblocks` +
`-DAFTER_HOURS_ENABLE_MACOS_GESTURES` (Makefile `GESTURE_FLAGS`), and zig's
bundled clang accepts `-fblocks`. `zig build test` passes the fuller
explicit form `--test-mode --headless --test-script-dir tests/e2e`, while
the Makefile's `test` target passes only `--test-script-dir tests/e2e`;
both run the same way today because `src/main.cpp` never sets
`cfg.display` from the parsed `--headless` flag (see
[afterhours_gaps.md](afterhours_gaps.md)).

## Documentation

See `docs/` for full documentation:

- **[docs/README.md](docs/README.md)** — Documentation index
- **[docs/roadmap.md](docs/roadmap.md)** — Full game design spec
- **[docs/questions.md](docs/questions.md)** — Clarification Q&A
- **[docs/phases/](docs/phases/)** — Engineering implementation phases

## Controls

| Input | Action |
|-------|--------|
| WASD / Arrow Keys | Pan camera |
| Q / E | Rotate camera |
| Mouse Wheel | Zoom |
| TAB | Toggle density overlay |
| SPACE | Pause |
| ESC | Quit |

## Project Structure

```
src/
├── main.cpp           # Entry point
├── components.h       # ECS components
├── game.h            # Game constants
├── camera.h          # Isometric camera
├── entity_makers.*   # Entity factory functions
├── systems.h         # System registration
├── update_systems.cpp # Game logic systems
├── render_systems.cpp # Rendering systems
└── engine/           # Utilities

vendor/
├── afterhours/       # ECS framework + UI
├── raylib/           # Graphics
└── ...               # Other deps

docs/
├── roadmap.md        # Full design spec
├── questions.md      # Design Q&A
└── phases/           # Implementation phases
```
