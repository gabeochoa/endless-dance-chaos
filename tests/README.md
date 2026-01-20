# Testing

This project uses the afterhours E2E testing framework with a custom script DSL.

## Running Tests

```bash
# Run a single test
./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_density.e2e"

# Run all tests in directory
./output/dance.exe --test-mode --test-script-dir="tests/e2e_scripts"
```

## E2E Script Commands

### Built-in Commands (from afterhours)

| Command | Description | Example |
|---------|-------------|---------|
| `type "text"` | Type characters | `type "Hello"` |
| `key MODIFIER+KEY` | Press key combo | `key CTRL+S` |
| `click x y` | Mouse click | `click 400 300` |
| `double_click x y` | Double click | `double_click 200 150` |
| `drag x1 y1 x2 y2` | Mouse drag | `drag 100 100 300 100` |
| `mouse_move x y` | Move cursor | `mouse_move 250 200` |
| `wait N` | Wait N frames | `wait 5` |
| `screenshot name` | Take screenshot | `screenshot 01_test` |

### Game-Specific Commands

| Command | Description | Example |
|---------|-------------|---------|
| `spawn_agent X Y TYPE` | Spawn one agent | `spawn_agent 0 0 stage` |
| `spawn_agents X Y N TYPE` | Spawn N agents | `spawn_agents 0 0 50 bathroom` |
| `clear_agents` | Remove all agents | `clear_agents` |
| `clear_map` | Remove agents + facilities | `clear_map` |
| `set_time HOUR MIN` | Set game clock | `set_time 14 30` |
| `get_agent_count` | Log agent count | `get_agent_count` |
| `get_density X Y` | Log tile density | `get_density 0 0` |
| `trigger_game_over` | Force game over | `trigger_game_over` |
| `reset_game` | Full reset | `reset_game` |
| `place_facility TYPE X Y` | Place facility | `place_facility bathroom 5 3` |

## Writing Tests

1. Create a `.e2e` file in `tests/e2e_scripts/`
2. Add commands, one per line
3. Use `#` for comments
4. Use `wait N` between actions for timing

### Example Test

```
# Test: Basic density tracking
# Verifies agents are counted correctly

reset_game
wait 5

# Spawn agents at origin
spawn_agents 0 0 10 stage
wait 10

# Toggle density overlay
key TAB
wait 5

screenshot density_test
```

## Prerequisites

The E2E testing plugin needs to be synced from wordproc's afterhours:

```bash
cp -r ~/p/wordproc/vendor/afterhours/src/plugins/e2e_testing \
      ~/p/endless_dance_chaos/vendor/afterhours/src/plugins/
```

See `docs/phases/00-testing-framework.md` for full setup instructions.

