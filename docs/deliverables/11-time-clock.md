# Phase 11: Time Clock

**Demo 3A** | Prerequisite: Phase 10 (Demo 2 complete)

## Goal

Implement 24-hour game clock with 4 phases (Day/Night/Exodus/Dead Hours). SPACE pauses with a game speed enum.

**Done when:** Clock ticks, phases transition, you can feel the rhythm of the cycle.

---

## Deliverables

### 1. Game Speed Enum

```cpp
enum class GameSpeed { Paused, OneX, TwoX, FourX };

struct GameClock : BaseComponent {
    float game_time_minutes = 600.0f;  // Start at 10:00am

    GameSpeed speed = GameSpeed::OneX;

    // 12 real minutes = 24 game hours = 1440 game minutes
    // 720 real sec / 1440 game min = 0.5 real sec per game min
    static constexpr float SECONDS_PER_GAME_MINUTE = 0.5f;

    float speed_multiplier() const {
        switch (speed) {
            case GameSpeed::Paused: return 0.0f;
            case GameSpeed::OneX:   return 1.0f;
            case GameSpeed::TwoX:   return 2.0f;
            case GameSpeed::FourX:  return 4.0f;
        }
        return 1.0f;
    }

    enum class Phase { Day, Night, Exodus, DeadHours };

    Phase get_phase() const {
        int hour = get_hour();
        if (hour >= 10 && hour < 18) return Phase::Day;
        if (hour >= 18 && hour < 24) return Phase::Night;
        if (hour >= 0 && hour < 3) return Phase::Exodus;
        return Phase::DeadHours; // 3am - 10am
    }

    int get_hour() const {
        return static_cast<int>(game_time_minutes / 60.0f) % 24;
    }
    int get_minute() const {
        return static_cast<int>(game_time_minutes) % 60;
    }

    std::string format_time() const;  // returns "14:34"

    static const char* phase_name(Phase p);  // "Day", "Night", etc.
};
```

### 2. Clock Update System

Create `UpdateGameClockSystem`:

```cpp
// Each frame:
float game_dt = (dt / GameClock::SECONDS_PER_GAME_MINUTE) * clock.speed_multiplier();
clock.game_time_minutes += game_dt;

// Wrap at 24 hours
if (clock.game_time_minutes >= 1440.0f) {
    clock.game_time_minutes -= 1440.0f;
}
```

### 3. Phase Change Detection

Track previous phase, log on change:

```cpp
Phase old_phase = clock.get_phase();
// ... advance time ...
Phase new_phase = clock.get_phase();
if (new_phase != old_phase) {
    log_info("Phase: {} → {}", phase_name(old_phase), phase_name(new_phase));
}
```

### 4. Pause Control

SPACE key toggles between `Paused` and `OneX`:

```cpp
if (space_pressed) {
    if (clock.speed == GameSpeed::Paused)
        clock.speed = GameSpeed::OneX;
    else
        clock.speed = GameSpeed::Paused;
}
```

When paused:
- Game logic systems check `clock.speed != Paused` before running
- Camera, input, UI, and building still work
- Clock display shows paused indicator (e.g., time text blinks or shows "⏸")

### 5. Time Display

Update debug HUD to show time:
- Format: "14:34" (24-hour)
- Show current phase name: "Day", "Night", "Exodus", "Dead Hours"

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_time` | `HOUR MINUTE` | Set game clock |
| `set_speed` | `SPEED` | Set game speed (paused/1x/2x/4x) |
| `assert_phase` | `PHASE` | Assert current phase (day/night/exodus/dead) |
| `assert_time_between` | `H1 M1 H2 M2` | Assert time is in range |

**`tests/e2e/11_time_clock.e2e`**:
```
# Test time phases
reset_game
wait 5

# Day phase
set_time 10 0
wait 5
assert_phase day
expect_text "10:00"
screenshot 11_day

# Night phase
set_time 18 0
wait 5
assert_phase night
screenshot 11_night

# Exodus phase
set_time 0 30
wait 5
assert_phase exodus
screenshot 11_exodus

# Dead hours
set_time 4 0
wait 5
assert_phase dead
screenshot 11_dead_hours
```

**`tests/e2e/11_pause.e2e`**:
```
# Test pause
reset_game
wait 5

set_time 12 0
wait 5

# Pause
key SPACE
wait 30

# Time should not have advanced
assert_time_between 12 0 12 1
screenshot 11_paused

# Unpause
key SPACE
wait 60

# Time should have advanced
screenshot 11_unpaused
```

---

## Existing Code to Leverage

- `GameState::game_time` — repurpose or keep separate from GameClock
- fmt library for time formatting
- Input system for SPACE key

## What to Delete

- `GameState::game_time` (simple elapsed float) → replaced by `GameClock`

---

## Acceptance Criteria

- [ ] Time advances during gameplay (12 real min = 24 game hours)
- [ ] Time displays in 24-hour format ("14:34")
- [ ] 4 phases transition correctly (Day → Night → Exodus → Dead Hours)
- [ ] Phase transitions logged to console
- [ ] SPACE toggles pause (time stops, camera/building still works)
- [ ] Clock wraps correctly at midnight → next day
- [ ] GameSpeed enum supports Paused/1x/2x/4x (only 1x and Paused active for MVP)

## Out of Scope

- Spawning changes based on phase (Phase 13)
- Exodus agent behavior (Phase 15)
- Day/night color palette transition (Phase 20)
- 2x/4x speed buttons in UI (post-MVP)
