# Phase 05: Time Clock

## Goal
Implement the game clock with 4 phases (Day/Night/Exodus/Dead Hours). Clock displays in 24-hour format.

## Prerequisites
- Phase 04 complete (UI skeleton exists for time display)

## Deliverables

### 1. Time Tracking in GameState
Add to `GameState` or create new component:
```cpp
struct GameClock : BaseComponent {
    float game_time_minutes = 600.0f;  // Start at 10:00 (10am = 600 minutes)
    
    // Real seconds per game minute (12 min real = 24 hours game)
    // 12 min = 720 sec, 24 hours = 1440 min
    // 720 sec / 1440 min = 0.5 sec per game minute
    static constexpr float SECONDS_PER_GAME_MINUTE = 0.5f;
    
    enum class Phase { Day, Night, Exodus, DeadHours };
    
    Phase get_phase() const {
        int hour = get_hour();
        if (hour >= 10 && hour < 18) return Phase::Day;      // 10am-6pm
        if (hour >= 18 || hour < 0) return Phase::Night;     // 6pm-midnight
        if (hour >= 0 && hour < 3) return Phase::Exodus;     // midnight-3am
        return Phase::DeadHours;                              // 3am-10am
    }
    
    int get_hour() const { return static_cast<int>(game_time_minutes / 60) % 24; }
    int get_minute() const { return static_cast<int>(game_time_minutes) % 60; }
    
    std::string format_time() const {
        return fmt::format("{:02d}:{:02d}", get_hour(), get_minute());
    }
};
```

### 2. Clock Update System
Each frame:
```cpp
clock.game_time_minutes += dt / GameClock::SECONDS_PER_GAME_MINUTE;
if (clock.game_time_minutes >= 1440.0f) {
    clock.game_time_minutes -= 1440.0f;  // Wrap at midnight
}
```

### 3. Phase Change Events
Log when phase changes (for debugging/future audio cues):
```cpp
if (new_phase != old_phase) {
    log_info("Phase changed to: {}", magic_enum::enum_name(new_phase));
}
```

## Existing Code to Use

- `GameState::game_time` — currently just elapsed time, can repurpose or keep separate
- fmt library for string formatting

## Afterhours to Use
- None for logic
- Timer plugin could be useful but simple dt accumulation works

## Implementation Steps

1. Create `GameClock` component
2. Add singleton `GameClock` entity at game start
3. Create `UpdateGameClockSystem` that advances time
4. Log phase changes
5. (Optional) Update top bar placeholder to show actual time

## Acceptance Criteria

- [ ] Time advances during gameplay
- [ ] 12 real minutes = 24 game hours
- [ ] Time displays in 24-hour format (14:34)
- [ ] Phase transitions detected and logged
- [ ] Pause (SPACE) stops clock
- [ ] Clock wraps correctly at midnight

## Testing

### E2E Test Script: `test_time.e2e`

```
# Test: Time system
reset_game
wait 5

# Set to morning
set_time 10 0
wait 5
screenshot time_10_00_day

# Set to evening
set_time 18 0
wait 5
screenshot time_18_00_night

# Set to exodus
set_time 0 30
wait 5
screenshot time_00_30_exodus

# Set to dead hours
set_time 4 0
wait 5
screenshot time_04_00_dead
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_time.e2e"`

### Manual Testing

1. Watch clock tick in top bar
2. Verify 24-hour format (e.g., "14:30")
3. Press SPACE to pause, verify time stops
4. Set different times and verify correct phase display

## Out of Scope
- Exodus behavior (agents leaving)
- Spawning changes based on phase
- UI styling of time display

