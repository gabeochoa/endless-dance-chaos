# Phase 20: Polish Pass (Demo 4 / MVP Complete)

**Demo 4E** | Prerequisite: All previous phases complete

## Goal

Final MVP polish: day/night color palette transition, compass indicator, movement slowdown in dangerous zones, controller validation, final tuning, bug fixing.

**Done when:** The game looks and feels complete. Ready to ship MVP.

---

## Deliverables

### 1. Day/Night Color Palette Transition

Smoothstep interpolation between day and night palettes over 1 game-hour:

**Day (10am-6pm) — Miami Pastel:**
| Element | Hex |
|---------|-----|
| Ground | `#F5E6D3` |
| Grass | `#98D4A8` |
| Path | `#E8DDD4` |
| Stage | `#FFD93D` |

**Night (6pm-3am) — EDC Neon:**
| Element | Hex |
|---------|-----|
| Ground | `#1A1A2E` |
| Grass | `#2D4A3E` |
| Path | `#2A2A3A` |
| Stage | `#FFE600` |

Transition logic:

```cpp
float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Color lerp_color(Color a, Color b, float t) {
    return Color{
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
        (uint8_t)(a.a + (b.a - a.a) * t)
    };
}

// Transition happens over 1 game-hour (60 game minutes)
// Day→Night: 5pm-6pm (minutes 1020-1080)
// Night→Day: 9am-10am (minutes 540-600)
float get_day_night_t(float game_time_minutes) {
    int hour = (int)(game_time_minutes / 60) % 24;
    float minute_in_hour = fmod(game_time_minutes, 60);

    if (hour == 17) { // 5pm → transitioning to night
        return smoothstep(0, 60, minute_in_hour);
    } else if (hour == 9) { // 9am → transitioning to day
        return 1.0f - smoothstep(0, 60, minute_in_hour);
    } else if (hour >= 18 || hour < 3) { // night
        return 1.0f;
    } else if (hour >= 10 && hour < 17) { // day
        return 0.0f;
    }
    // Dead hours (3am-9am): night palette
    return 1.0f;
}
```

Apply `t` to all tile colors, UI backgrounds, etc.

### 2. Compass Indicator

Show "N" in a corner of the screen that rotates with the camera:

```cpp
// camera.rotation_index is 0-3 (0=north up, 1=east up, etc.)
// Position the "N" at the correct edge based on rotation
```

- Small "N" text, Fredoka 14px
- Positioned in a corner or along the edge
- Rotates to always indicate true north
- Semi-transparent background circle

### 3. Stage Color Tint (Phase 12 Enhancement)

Already partially implemented. Ensure:
- Stage tiles get a warm yellow overlay during performance
- Night: brighter, more vivid stage glow
- Overlay animates subtly (gentle pulse, optional)

### 4. Controller Input Validation

Test all actions with the controller mapping:

| Button | Action | Works? |
|--------|--------|--------|
| D-pad / Left Stick | Move cursor | |
| A / Left-click | Place/Confirm | |
| B / Right-click | Cancel/Delete | |
| L Bumper / `[` | Prev tool | |
| R Bumper / `]` | Next tool | |
| Y / TAB | Toggle overlay | |
| Start / SPACE | Pause | |
| L/R Triggers / Q/E | Rotate camera | |
| Right Stick / WASD | Pan camera | |

### 5. Final Tuning

Review and adjust all tuning parameters:

| Parameter | Value | Feels Right? |
|-----------|-------|-------------|
| Agent speed (path) | 0.5 t/s | |
| Agent speed (grass) | 0.25 t/s | |
| Crush damage rate | 0.2/sec | |
| Max deaths | 10 | |
| Service time | 1 sec | |
| Spawn rate base | 0.5/sec | |
| Pheromone decay | ~60 sec | |
| Watch duration | 30-120 sec | |
| Need timers | 30-90 / 45-120 sec | |
| Phase length | 3 min real | |

### 6. Bug Fixing

Common final issues:
- UI elements overlapping game view
- Minimap not updating correctly on rotation
- Pheromone trails creating unwanted loops
- Gate chokepoints too severe or too easy
- Death particles rendering behind tiles
- Camera bounds allowing too much panning
- Font rendering at different zoom levels

### 7. Performance Optimization

Target: 500+ agents at 60 FPS (stretch: 1000+)

Optimization areas:
- Batch rendering for agents
- Skip rendering off-screen agents
- Pheromone decay batching
- Density calculation optimization

### 8. E2E Tests

**`tests/e2e/20_day_night.e2e`**:
```
# Test day/night transition
reset_game
wait 5

set_time 12 0
wait 5
screenshot 20_daytime

set_time 17 30
wait 5
screenshot 20_sunset_transition

set_time 20 0
wait 5
screenshot 20_nighttime

set_time 9 30
wait 5
screenshot 20_sunrise_transition
```

**`tests/e2e/20_compass.e2e`**:
```
# Test compass rotation
reset_game
wait 5
screenshot 20_compass_north

key E
wait 5
screenshot 20_compass_east

key E
wait 5
screenshot 20_compass_south
```

**`tests/e2e/20_full_game.e2e`**:
```
# Full game loop test
reset_game
wait 5

# Build layout
draw_path_rect 1 24 50 28
draw_path_rect 19 14 21 38
place_building bathroom 15 15
place_building food 15 35
wait 10

# Let game run through a full cycle
set_time 10 0
wait 720

# Should have survived (or game over)
screenshot 20_full_cycle
```

---

## Acceptance Criteria

- [ ] Day/night colors transition smoothly (smoothstep over 1 hour)
- [ ] Compass "N" visible and updates with camera rotation
- [ ] Stage glow visible during performances
- [ ] All controller inputs work per mapping table
- [ ] All tuning parameters feel right (playtest manually)
- [ ] No visible bugs in normal gameplay
- [ ] 500+ agents at 60 FPS
- [ ] Full game loop works: spawn → build → survive → escalate → die → restart

## Out of Scope

- Sound effects / music
- Screen shake / juice effects
- Multiple stages
- Save/load
- Speed controls UI (2x/4x)
- Tutorial

---

## MVP Complete Checklist

At this point, the full MVP is complete:

### Demo 1: "Crowd on a Grid"
- [x] Phase 01: Grid, rendering, E2E framework
- [x] Phase 02: Path building
- [x] Phase 03: Agent spawning & movement
- [x] Phase 04: Facilities & needs
- [x] Phase 05: Integration & playtest

### Demo 2: "The Crush"
- [x] Phase 06: Fence & gate
- [x] Phase 07: Density & crush damage
- [x] Phase 08: Density visualization
- [x] Phase 09: Game over
- [x] Phase 10: Facility polish

### Demo 3: "The Schedule"
- [x] Phase 11: Time clock
- [x] Phase 12: Artist schedule
- [x] Phase 13: Timeline UI
- [x] Phase 14: Pheromone trails
- [x] Phase 15: Exodus & carryover

### Demo 4: "MVP"
- [x] Phase 16: Full build tools
- [x] Phase 17: Progression system
- [x] Phase 18: Top bar & build bar UI
- [x] Phase 19: Minimap
- [x] Phase 20: Polish pass

**Total: 20 phases across 4 demos. The game is ready to ship.**
