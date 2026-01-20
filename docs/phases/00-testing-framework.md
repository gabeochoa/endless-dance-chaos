# Phase 00: E2E Testing Framework

## Goal
Set up E2E testing using the afterhours `e2e_testing` plugin, with custom game commands for spawning agents, setting time, etc.

## Prerequisites
- Game compiles and runs
- Sync `e2e_testing` plugin from wordproc's afterhours (or update vendor/afterhours)

## The Afterhours E2E Framework

The afterhours library includes a full E2E testing framework:

- **Script DSL**: `.e2e` files with simple commands like `type "hello"`, `key CTRL+S`, `click 100 200`
- **E2ERunner**: Parses scripts and creates `PendingE2ECommand` entities
- **Built-in handlers**: `type`, `click`, `key`, `wait`, `screenshot`, etc.
- **Custom handlers**: You create Systems that handle `PendingE2ECommand` for app-specific commands

### How Wordproc Does It

See `/Users/gabeochoa/p/wordproc/src/testing/` for reference:
- `e2e_commands.h` — Custom command handlers as ECS Systems
- `e2e_runner.h` — Wrapper to initialize the runner
- `e2e_integration.h` — Integration with app components

---

## Deliverables

### 1. Sync E2E Testing Plugin

The `e2e_testing` folder exists in wordproc's afterhours but not in endless_dance_chaos. Either:
- Copy the folder from wordproc's afterhours
- Or sync the entire afterhours vendor folder

```bash
# Option: Copy just the e2e_testing folder
cp -r ~/p/wordproc/vendor/afterhours/src/plugins/e2e_testing \
      ~/p/endless_dance_chaos/vendor/afterhours/src/plugins/
```

### 2. Create Custom Command Handlers

Create `src/testing/e2e_commands.h`:

```cpp
#pragma once

#include "../components.h"
#include "../entity_makers.h"
#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>

namespace e2e_commands {

using namespace afterhours;

// Handle 'spawn_agent X Y TYPE'
struct HandleSpawnAgentCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("spawn_agent")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("spawn_agent requires: x y type");
            return;
        }
        
        float x = cmd.arg_as<float>(0);
        float y = cmd.arg_as<float>(1);
        std::string type = cmd.arg(2);
        
        FacilityType want = FacilityType::Stage;
        if (type == "bathroom") want = FacilityType::Bathroom;
        else if (type == "food") want = FacilityType::Food;
        
        make_agent(x, y, want);
        cmd.consume();
    }
};

// Handle 'spawn_agents X Y COUNT TYPE'
struct HandleSpawnAgentsCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("spawn_agents")) return;
        if (!cmd.has_args(4)) {
            cmd.fail("spawn_agents requires: x y count type");
            return;
        }
        
        float x = cmd.arg_as<float>(0);
        float y = cmd.arg_as<float>(1);
        int count = cmd.arg_as<int>(2);
        std::string type = cmd.arg(3);
        
        FacilityType want = FacilityType::Stage;
        if (type == "bathroom") want = FacilityType::Bathroom;
        else if (type == "food") want = FacilityType::Food;
        
        for (int i = 0; i < count; i++) {
            make_agent(x, y, want);
        }
        cmd.consume();
    }
};

// Handle 'clear_agents'
struct HandleClearAgentsCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("clear_agents")) return;
        
        EntityQuery()
            .whereHasComponent<Agent>()
            .gen([](Entity& e) {
                e.cleanup = true;
            });
        
        cmd.consume();
    }
};

// Handle 'set_time HOUR MINUTE'
struct HandleSetTimeCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("set_time")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("set_time requires: hour minute");
            return;
        }
        
        int hour = cmd.arg_as<int>(0);
        int minute = cmd.arg_as<int>(1);
        
        auto* game_state = EntityHelper::get_singleton_cmp<GameState>();
        if (game_state) {
            // Convert to game_time (assuming 12 real min = 24 game hours)
            float total_minutes = hour * 60.0f + minute;
            game_state->game_time = (total_minutes / 1440.0f) * 720.0f; // 720s = 12min
        }
        
        cmd.consume();
    }
};

// Handle 'get_agent_count' - logs the count
struct HandleGetAgentCountCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("get_agent_count")) return;
        
        int count = 0;
        EntityQuery()
            .whereHasComponent<Agent>()
            .gen([&count](Entity&) { count++; });
        
        log_info("Agent count: {}", count);
        cmd.consume();
    }
};

// Handle 'get_density X Y' - logs density at tile
struct HandleGetDensityCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("get_density")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("get_density requires: x y");
            return;
        }
        
        int x = cmd.arg_as<int>(0);
        int y = cmd.arg_as<int>(1);
        
        // Query density from DemandHeatmap or count agents in tile
        int density = 0;
        // ... implementation depends on density system
        
        log_info("Density at ({}, {}): {}", x, y, density);
        cmd.consume();
    }
};

// Handle 'trigger_game_over'
struct HandleTriggerGameOverCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("trigger_game_over")) return;
        
        auto* game_state = EntityHelper::get_singleton_cmp<GameState>();
        if (game_state) {
            game_state->status = GameStatus::GameOver;
        }
        
        cmd.consume();
    }
};

// Handle 'reset_game'
struct HandleResetGameCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("reset_game")) return;
        
        // Clear all agents
        EntityQuery()
            .whereHasComponent<Agent>()
            .gen([](Entity& e) { e.cleanup = true; });
        
        // Reset game state
        auto* game_state = EntityHelper::get_singleton_cmp<GameState>();
        if (game_state) {
            game_state->status = GameStatus::Running;
            game_state->game_time = 0.f;
            game_state->death_count = 0;
            game_state->agents_served = 0;
        }
        
        cmd.consume();
    }
};

// Handle 'place_facility TYPE X Y'
struct HandlePlaceFacilityCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("place_facility")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("place_facility requires: type x y");
            return;
        }
        
        std::string type = cmd.arg(0);
        float x = cmd.arg_as<float>(1);
        float y = cmd.arg_as<float>(2);
        
        if (type == "bathroom") make_bathroom(x, y);
        else if (type == "food") make_food(x, y);
        else if (type == "stage") make_stage(x, y);
        else {
            cmd.fail("Unknown facility type: " + type);
            return;
        }
        
        cmd.consume();
    }
};

// Handle 'clear_map' - remove all dynamic entities
struct HandleClearMapCommand : System<testing::PendingE2ECommand> {
    virtual void for_each_with(Entity&, testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("clear_map")) return;
        
        // Clear agents
        EntityQuery()
            .whereHasComponent<Agent>()
            .gen([](Entity& e) { e.cleanup = true; });
        
        // Clear facilities (optional - keep if you want)
        EntityQuery()
            .whereHasComponent<Facility>()
            .gen([](Entity& e) { e.cleanup = true; });
        
        cmd.consume();
    }
};

// Register all game-specific commands
inline void register_game_commands(SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleSpawnAgentCommand>());
    sm.register_update_system(std::make_unique<HandleSpawnAgentsCommand>());
    sm.register_update_system(std::make_unique<HandleClearAgentsCommand>());
    sm.register_update_system(std::make_unique<HandleSetTimeCommand>());
    sm.register_update_system(std::make_unique<HandleGetAgentCountCommand>());
    sm.register_update_system(std::make_unique<HandleGetDensityCommand>());
    sm.register_update_system(std::make_unique<HandleTriggerGameOverCommand>());
    sm.register_update_system(std::make_unique<HandleResetGameCommand>());
    sm.register_update_system(std::make_unique<HandlePlaceFacilityCommand>());
    sm.register_update_system(std::make_unique<HandleClearMapCommand>());
}

} // namespace e2e_commands
```

### 3. Create E2E Runner Integration

Create `src/testing/e2e_runner.h`:

```cpp
#pragma once

#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>
#include "e2e_commands.h"

namespace e2e {

using ScriptRunner = afterhours::testing::E2ERunner;

inline void initialize_e2e_testing(
    SystemManager& sm,
    const std::string& script_path,
    const std::string& screenshot_dir
) {
    using namespace afterhours::testing;
    
    // 1. Register built-in handlers
    register_builtin_handlers(sm);
    
    // 2. Register game-specific handlers
    e2e_commands::register_game_commands(sm);
    
    // 3. Register unknown handler (warns about unrecognized commands)
    register_unknown_handler(sm);
    
    // 4. Register cleanup (must be last)
    register_cleanup(sm);
}

} // namespace e2e
```

### 4. Update main.cpp

Add test mode support:

```cpp
int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    bool test_mode = cmdl[{"--test-mode"}];
    std::string test_script;
    cmdl({"--test-script"}) >> test_script;
    std::string screenshot_dir = "output/e2e";
    cmdl({"--screenshot-dir"}) >> screenshot_dir;

    // ... window init ...

    if (test_mode && !test_script.empty()) {
        e2e::initialize_e2e_testing(systems, test_script, screenshot_dir);
    }

    game();
    // ...
}
```

### 5. Create Test Scripts

Create `tests/e2e_scripts/` folder with test scripts:

**`tests/e2e_scripts/test_density.e2e`**:
```
# Test: Density tracking
# Verifies agents are counted correctly per tile

clear_agents
wait 2

# Spawn 10 agents at origin
spawn_agents 0 0 10 stage
wait 5

# Verify count
get_agent_count

screenshot density_test

# Spawn more
spawn_agents 0 0 30 stage
wait 5

screenshot density_critical
```

**`tests/e2e_scripts/test_game_over.e2e`**:
```
# Test: Game over trigger
# Verifies game over can be triggered

reset_game
wait 2

trigger_game_over
wait 2

screenshot game_over
```

**`tests/e2e_scripts/test_time.e2e`**:
```
# Test: Time system
# Verifies time can be set

reset_game
wait 2

set_time 10 0
wait 2
screenshot time_morning

set_time 18 0
wait 2
screenshot time_evening

set_time 0 30
wait 2
screenshot time_exodus
```

---

## Command Reference

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

### Game-Specific Commands (custom)

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

---

## Running Tests

```bash
# Run a single test
./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_density.e2e"

# Run all tests in directory (batch mode)
./output/dance.exe --test-mode --test-script-dir="tests/e2e_scripts"

# With custom screenshot directory
./output/dance.exe --test-mode --test-script="tests/e2e_scripts/test_density.e2e" --screenshot-dir="output/test_screenshots"
```

---

## Acceptance Criteria

- [ ] E2E testing plugin synced from wordproc afterhours
- [ ] `src/testing/e2e_commands.h` created with game commands
- [ ] `--test-mode` and `--test-script` flags work
- [ ] `spawn_agent` command spawns agents
- [ ] `clear_agents` command removes agents
- [ ] `set_time` command changes game clock
- [ ] `screenshot` command captures image
- [ ] At least 3 test scripts created and pass

## Out of Scope
- CI integration
- Visual regression testing
- Full test coverage
- Cheat keys / debug console (optional, can add later)
