# Phase 03: Game Over

## Goal
When death count reaches 10, trigger game over. Show a basic game over screen with stats.

## Prerequisites
- Phase 02 complete (death counting works)

## Deliverables

### 1. Game Over Detection
In the crush damage system or a new system:
```cpp
if (game_state.death_count >= GameState::MAX_DEATHS) {
    game_state.status = GameStatus::GameOver;
}
```

### 2. Game Over Screen (Simple)
When `status == GameOver`:
- Pause game logic (agents stop moving)
- Draw overlay with:
  - "FESTIVAL SHUT DOWN" text (centered)
  - Deaths: X/10
  - Agents Served: Y
  - Press SPACE to restart

### 3. Restart Functionality
- SPACE key resets game state
- Clear all agents
- Reset death count
- Reset facilities to initial state

## Existing Code to Use

- `GameState::status` and `GameStatus` enum — already exists
- `GameState::total_agents_served` — already tracked
- `running` global in `main.cpp` — for quit

## Afterhours to Use
- Basic raylib drawing for now (UI system comes in Phase 04)

## Implementation Steps

1. Add game over check after death processing
2. Create `RenderGameOverSystem` that:
   - Only runs when `status == GameOver`
   - Draws semi-transparent black overlay
   - Draws text centered on screen
3. Create `HandleGameOverInputSystem`:
   - SPACE = restart
   - ESC = quit to menu (or just quit for now)

## Acceptance Criteria

- [ ] Game stops when 10 deaths reached
- [ ] "FESTIVAL SHUT DOWN" appears on screen
- [ ] Stats displayed (deaths, agents served)
- [ ] SPACE restarts the game
- [ ] New game starts fresh (0 deaths, 0 agents)

## Out of Scope
- Fancy transition (brief pause before showing screen)
- Main menu
- Proper UI styling

