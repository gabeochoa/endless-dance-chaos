# Phase 09: Artist Schedule

## Goal
Artists perform on a fixed schedule (every 2 hours, 1-hour sets). Spawn rate tied to upcoming artist's crowd size.

## Prerequisites
- Phase 05 complete (time clock works)
- Phase 06 complete (agents can enter through gates)

## Deliverables

### 1. Artist Data Structure
```cpp
struct ScheduledArtist {
    std::string name;           // "Artist 472"
    int start_time_minutes;     // e.g., 600 for 10:00
    int duration_minutes = 60;  // 1 hour set
    int expected_crowd = 100;   // How many will come
    bool announced = false;
    bool performing = false;
    bool finished = false;
};
```

### 2. Schedule Generator
At game start, generate day's schedule:
```cpp
std::vector<ScheduledArtist> generate_daily_schedule() {
    std::vector<ScheduledArtist> schedule;
    // Day phase: 10am, 12pm, 2pm, 4pm
    for (int hour : {10, 12, 14, 16}) {
        schedule.push_back({
            .name = fmt::format("Artist {:03d}", random_int(100, 999)),
            .start_time_minutes = hour * 60,
            .expected_crowd = random_int(50, 200)  // Scale with festival size later
        });
    }
    return schedule;
}
```

### 3. Schedule Component
```cpp
struct ArtistSchedule : BaseComponent {
    std::vector<ScheduledArtist> today;
    int current_artist_index = -1;  // -1 = none performing
    
    ScheduledArtist* get_current() {
        if (current_artist_index < 0) return nullptr;
        return &today[current_artist_index];
    }
    
    ScheduledArtist* get_next() {
        for (auto& a : today) {
            if (!a.finished && !a.performing) return &a;
        }
        return nullptr;
    }
};
```

### 4. Spawn Rate System
Modify attraction spawning:
```cpp
// Ramp up spawning 15 game-minutes before performance
auto* next = schedule.get_next();
if (next && clock.game_time_minutes > next->start_time_minutes - 15) {
    spawn_rate = base_rate * (next->expected_crowd / 100.0f);
}
```

### 5. Stage Integration
Link schedule to existing `StageInfo` state machine:
- Announcing starts 15 min before
- Performing when artist starts
- Clearing after set ends

## Existing Code to Use

- `StageInfo` component with state machine
- `Attraction` component with spawn rate/timer
- Random engine in `engine/random_engine.h`

## Afterhours to Use
- None needed

## Implementation Steps

1. Create `ScheduledArtist` and `ArtistSchedule` components
2. Generate schedule at game start
3. Create `UpdateArtistScheduleSystem`:
   - Check clock vs schedule
   - Trigger state transitions
4. Modify spawn rate based on upcoming artist
5. Connect to existing StageInfo

## Acceptance Criteria

- [ ] Schedule generated at game start
- [ ] Artists start at correct times
- [ ] Stage state machine triggers correctly
- [ ] Spawn rate increases before performances
- [ ] Can see artist names (in console for now)
- [ ] Schedule wraps to next day

## Testing

### New E2E Commands

Add to `e2e_commands.h`:
```cpp
// Handle 'make_schedule' - generate a test schedule
struct HandleMakeScheduleCommand : System<testing::PendingE2ECommand> {
    // Creates schedule with known times for testing
};

// Handle 'get_current_artist' - logs current artist
struct HandleGetCurrentArtistCommand : System<testing::PendingE2ECommand> {
    // Logs artist name and state
};
```

### E2E Test Script: `test_artist_schedule.e2e`

```
# Test: Artist schedule
reset_game
wait 5

# Jump to 10am (first artist)
set_time 9 45
wait 60

screenshot before_first_artist

# Jump to performance time
set_time 10 0
wait 60

screenshot during_performance

# Jump to end of set
set_time 11 0
wait 60

screenshot after_performance
```

Run: `./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_artist_schedule.e2e"`

### Manual Testing

1. Run game, let clock advance
2. Verify stage announces before performance
3. Verify stage performs at scheduled time
4. Verify spawn rate increases before performance

## Out of Scope
- Timeline UI display (Phase 12)
- Multiple stages
- Crowd size scaling with festival progression

