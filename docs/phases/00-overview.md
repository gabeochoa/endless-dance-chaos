# Development Phases Overview

Each phase is a small, commit-worthy deliverable that moves us toward MVP.

## Current State Analysis

**Already Implemented:**
- ✅ Isometric camera with rotation (Q/E keys)
- ✅ Agent spawning and basic movement
- ✅ Facility components (Bathroom, Food, Stage)
- ✅ Path tiles with signpost-based pathfinding
- ✅ Basic builder state (pending tiles)
- ✅ Game state tracking (stress, served agents)
- ✅ Stage state machine (Idle → Announcing → Performing → Clearing)
- ✅ MCP integration for debugging

**Afterhours Features Available:**
- UI system with autolayout and theming
- Input system with action mapping
- Timer plugin
- Collision system
- Color utilities
- Window manager

---

## Phase Sequence

| Phase | Name | Deliverable | Est. Time |
|-------|------|-------------|-----------|
| 01 | Density System | Track agents per tile, display count | 2-3 hrs |
| 02 | Crush Damage | Damage in critical zones, death counter | 2-3 hrs |
| 03 | Game Over | 10 deaths = game over screen | 2 hrs |
| 04 | UI Skeleton | Placeholder UI layout (top bar, sidebar, build bar) | 3-4 hrs |
| 05 | Time Clock | 24-hour clock, phases (Day/Night/Exodus/Dead) | 2-3 hrs |
| 06 | Fence & Gate | Fence blocks movement, gates allow entry | 3-4 hrs |
| 07 | Path Speed | Paths = full speed, grass = half speed | 1-2 hrs |
| 08 | Build Tools | Path/Fence/Facility placement with controller | 4-5 hrs |
| 09 | Artist Schedule | Fixed 2-hour slots, spawn rate tied to artist | 3-4 hrs |
| 10 | Top Bar UI | Wire up time, deaths, attendees | 2 hrs |
| 11 | Build Bar UI | Tool icons, selection indicator | 2-3 hrs |
| 12 | Timeline UI | Artist schedule display | 3-4 hrs |
| 13 | Minimap | Map overview with camera viewport | 3-4 hrs |
| 14 | Density Overlay | TAB toggle heat map | 2-3 hrs |
| 15 | Polish Pass | Death particles, stage glow, N indicator | 3-4 hrs |

**Total Estimated: ~40-50 hours (≈1 week)**

---

## How to Use These Phases

1. Read the phase file completely before starting
2. Implement the deliverables in order
3. Test the acceptance criteria
4. Commit when phase passes
5. Get feedback before moving to next phase

Each phase file contains:
- **Goal**: What this phase accomplishes
- **Prerequisites**: What must be done first
- **Deliverables**: Specific things to implement
- **Existing Code to Use**: Files/functions to leverage
- **Afterhours to Use**: Library features to leverage
- **Acceptance Criteria**: How to know you're done
- **Out of Scope**: What NOT to do in this phase

