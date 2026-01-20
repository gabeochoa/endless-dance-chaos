# Endless Dance Chaos

A **Mini Metro-style minimalist management game** about running a music festival.

**One-sentence pitch**: "Tower defense, but the towers are bathrooms and the creeps are festival-goers."

## Getting Started

```bash
make
./output/dance.exe
```

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
