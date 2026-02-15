# Phase 09: Game Over

**Demo 2D** | Prerequisite: Phase 07 (death counting works)

## Goal

10 deaths = game over. Show "FESTIVAL SHUT DOWN" screen with stats using afterhours UI system. SPACE restarts.

**Done when:** Full fail state loop — play, die, see stats, restart.

---

## Deliverables

### 1. Game Over Detection

In `CrushDamageSystem` or a new `CheckGameOverSystem`:

```cpp
if (game_state.death_count >= MAX_DEATHS) {
    game_state.status = GameStatus::GameOver;
}
```

### 2. Game Pause on Game Over

When `status == GameOver`:
- All game logic systems skip (agents stop moving, timers stop)
- Camera and input still work (player can look around)
- Game over UI renders on top

### 3. Game Over Screen (Afterhours UI)

Use afterhours UI system with autolayout:

```
┌──────────────────────────────┐
│                              │
│    FESTIVAL SHUT DOWN        │
│                              │
│    Deaths: 10/10             │
│    Agents Served: XXX        │
│    Time Survived: XX:XX      │
│                              │
│    [ Press SPACE to restart ]│
│                              │
└──────────────────────────────┘
```

- Semi-transparent dark overlay (`#000000CC`) covering entire screen
- Text centered using afterhours flex layout
- Font: Fredoka, "FESTIVAL SHUT DOWN" at 32px, stats at 16px
- Stats pulled from `GameState`

### 4. Restart Functionality

When SPACE is pressed during game over:
- Clear all agent entities
- Reset all grid tiles to initial state (grass + perimeter fence + gate)
- Reset `death_count = 0`
- Reset `status = GameStatus::Running`
- Re-place initial facilities (stage, bathroom, food)
- Reset `total_agents_served = 0`

### 5. Game State Additions

```cpp
struct GameState : BaseComponent {
    // Existing...
    GameStatus status = GameStatus::Running;
    int death_count = 0;
    int total_agents_served = 0;

    // Add for game over screen
    float time_survived = 0.f; // total seconds played
    int max_attendees = 0;     // peak simultaneous agents
};
```

Track `max_attendees` by comparing current agent count each frame.

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `trigger_game_over` | (none) | Force game over state |
| `assert_game_status` | `STATUS` | Assert status (running/gameover) |

**`tests/e2e/09_game_over.e2e`**:
```
# Test game over screen
reset_game
wait 5

# Force game over
trigger_game_over
wait 10

assert_game_status gameover
expect_text "FESTIVAL SHUT DOWN"
screenshot 09_game_over_screen

# Restart
key SPACE
wait 10

assert_game_status running
assert_death_count eq 0
screenshot 09_after_restart
```

**`tests/e2e/09_natural_game_over.e2e`**:
```
# Test game over from actual deaths
reset_game
wait 5

# Spawn massive crush
spawn_agents 25 25 45 stage
wait 600

# Should have 10 deaths by now
assert_game_status gameover
screenshot 09_natural_game_over
```

---

## Existing Code to Leverage

- `GameState::status` and `GameStatus` enum
- `GameState::total_agents_served`
- Afterhours UI system (`vendor/afterhours/src/plugins/ui.h`)
- Fredoka font from Phase 01

---

## Acceptance Criteria

- [ ] Game stops when 10 deaths reached
- [ ] "FESTIVAL SHUT DOWN" text visible on screen
- [ ] Stats displayed: deaths, agents served, time survived
- [ ] SPACE restarts the game
- [ ] New game starts fresh (0 deaths, 0 agents, initial map state)
- [ ] E2E: `trigger_game_over` and `assert_game_status` work
- [ ] E2E: `expect_text` finds the game over text

## Out of Scope

- Main menu
- Pause menu (post-MVP)
- High score tracking
- Death heatmap
