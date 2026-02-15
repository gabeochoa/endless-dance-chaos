# Phase 12: Artist Schedule

**Demo 3B** | Prerequisite: Phase 11 (time clock works)

## Goal

Artists perform on a schedule. Crowd sizes scale with festival growth. Spawn rate ramps up before performances. Stage lights up during performances.

**Done when:** You can feel the waves — calm before the storm, then the rush hits.

---

## Deliverables

### 1. Artist Data Structure

```cpp
struct ScheduledArtist {
    std::string name;            // "Artist 472"
    float start_time_minutes;    // game minutes (e.g., 600.0 = 10:00am)
    float duration_minutes;      // 60 = 1 hour set
    int expected_crowd;          // how many agents this artist draws
    bool announced = false;
    bool performing = false;
    bool finished = false;
};
```

### 2. Schedule Component (Sliding Window)

Generate N artists ahead of current time. When an artist finishes, generate the next one.

```cpp
struct ArtistSchedule : BaseComponent {
    std::vector<ScheduledArtist> schedule;  // upcoming + current artists
    int look_ahead = 6;  // always keep 6 artists scheduled ahead

    ScheduledArtist* get_current();  // currently performing (or null)
    ScheduledArtist* get_next();     // next upcoming (or null)

    void generate_next(float after_time_minutes, int max_attendees_ever);
};
```

### 3. Schedule Generation

Fixed 2-hour slots during Day/Night phases (10am - midnight):
- Slots: 10:00, 12:00, 14:00, 16:00, 18:00, 20:00, 22:00
- 7 slots per day cycle
- Each artist gets a 1-hour set with 1-hour gap

Name generation: `"Artist XXX"` where XXX is random 3-digit number.

Crowd size scaling (linear, TODO for later tuning):

```cpp
int calculate_crowd_size(int max_attendees_ever) {
    // Base: 50-200, scales linearly with festival size
    int base = 50 + (max_attendees_ever / 10);
    // Add randomness: ±30%
    int variation = base * 0.3f;
    return base + random_int(-variation, variation);
    // TODO: revisit this formula during playtesting
}
```

Duration: 30-60 game minutes (random within range).

### 4. Spawn Rate Tied to Artists

Modify spawn system:
- 15 game-minutes before an artist starts: ramp up spawn rate
- Spawn rate = `base_rate * (artist.expected_crowd / 100.0f)`
- During performance: maintain elevated spawn rate
- After performance ends: return to base rate

```cpp
float get_spawn_rate(const ArtistSchedule& schedule, const GameClock& clock) {
    auto* next = schedule.get_next();
    if (next && clock.game_time_minutes > next->start_time_minutes - 15.0f) {
        return BASE_SPAWN_RATE * (next->expected_crowd / 100.0f);
    }
    auto* current = schedule.get_current();
    if (current) {
        return BASE_SPAWN_RATE * (current->expected_crowd / 100.0f);
    }
    return BASE_SPAWN_RATE;
}
```

### 5. Stage State Machine Integration

Connect schedule to existing `StageInfo` (or replace it):
- 15 min before start: `Announcing` → stage gets a subtle indicator
- At start time: `Performing` → stage lights up with color tint overlay
- At end time: `Clearing` → agents in watching zone start leaving
- After clearing: `Idle` → next artist's announcement begins

Stage glow during performance: simple color tint overlay:
```cpp
if (performing) {
    // Draw semi-transparent yellow rect over stage tiles
    DrawIsometricOverlay(stage_tiles, Color{255, 200, 0, 80});
}
```

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `get_current_artist` | (none) | Log current performing artist |
| `get_next_artist` | (none) | Log next scheduled artist |
| `assert_stage_state` | `STATE` | Assert stage state (idle/announcing/performing/clearing) |
| `force_artist` | `NAME CROWD DURATION` | Schedule a specific artist next |

**`tests/e2e/12_artist_schedule.e2e`**:
```
# Test artist schedule
reset_game
wait 5

# Jump to just before first artist
set_time 9 45
wait 30

assert_stage_state announcing
screenshot 12_announcing

# Jump to performance
set_time 10 5
wait 30

assert_stage_state performing
screenshot 12_performing

# Jump to end of set
set_time 11 5
wait 30

assert_stage_state clearing
screenshot 12_clearing
```

**`tests/e2e/12_spawn_ramp.e2e`**:
```
# Test spawn rate increases before performance
reset_game
wait 5

# Set time to ramp-up period
set_time 9 50
wait 120

# Should have spawned more agents than baseline
assert_agent_count gt 5
screenshot 12_spawn_ramp
```

---

## Existing Code to Leverage

- `StageInfo` component with state machine
- `Attraction` component with spawn rate
- `Artist` component (modify or replace)
- Random engine for name/crowd generation

## What to Delete

- `Artist` component (old version) → replaced by `ScheduledArtist`
- Fixed `StageInfo` timing constants → driven by schedule

---

## Acceptance Criteria

- [ ] Schedule generates artists in 2-hour slots (sliding window)
- [ ] Artists start at correct times relative to game clock
- [ ] Stage state machine transitions: Idle → Announcing → Performing → Clearing
- [ ] Spawn rate ramps up ~15 min before performance
- [ ] Stage lights up during performance (color tint)
- [ ] Crowd sizes scale with max_attendees_ever (linear + TODO)
- [ ] Artist names are "Artist XXX" format

## Out of Scope

- Timeline UI (Phase 13)
- Multiple stages
- Fancy artist name generation
