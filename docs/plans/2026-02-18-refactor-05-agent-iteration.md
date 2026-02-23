# Refactor 05: Deduplicate Agent Iteration Across Systems

## Problem

Multiple systems independently scan all agent entities every frame:

| System | File | Query | Purpose |
|--------|------|-------|---------|
| `UpdateTileDensitySystem` | crowd_systems.cpp | `Agent + Transform` | Count per-tile density |
| `RenderMinimapSystem` | render_ui.cpp | `Agent + Transform` | Draw minimap dots |
| `TrackStatsSystem` | crowd_systems.cpp | `Agent` | Count total agents |
| `ExodusSystem` | crowd_systems.cpp | `Agent` | Switch all agents to exit |
| `AgentDeathSystem` | crowd_systems.cpp | `Agent + AgentHealth + Transform` | Find dead agents |
| `RenderUISystem` (hover) | render_ui.cpp | `Agent + Transform` | Per-tile agent breakdown |

Each `EntityQuery().whereHasComponent<Agent>().gen()` is a linear scan of all entities. With 6+ systems, agents are iterated 6+ times per frame.

## Proposed Fix

### Create a per-frame agent cache singleton

```cpp
struct AgentFrameCache : afterhours::BaseComponent {
    int total_agent_count = 0;

    // Per-tile agent lists for hover queries
    // Key: z * MAP_SIZE + x, Value: count of agents on that tile
    // (Already exists as tile.agent_count, but this avoids re-scanning)

    // Dead agent IDs for deferred processing
    std::vector<int> dead_agent_ids;

    // Agent positions for minimap (pre-extracted)
    struct MinimapDot {
        float gx, gz;
        int want_idx;
    };
    std::vector<MinimapDot> minimap_dots;

    bool dirty = true;
};
```

### Consolidate into a single scan

Create `UpdateAgentCacheSystem` that runs once per frame and:

1. Iterates all agents exactly once.
2. Counts per-tile density (currently `UpdateTileDensitySystem`).
3. Records total agent count (currently `TrackStatsSystem::gen_count()`).
4. Collects minimap dot positions (currently in `RenderMinimapSystem`).
5. Identifies dead agents (currently `AgentDeathSystem` scan).

### Consumer systems read the cache

- `RenderMinimapSystem` reads `cache->minimap_dots` instead of querying.
- `TrackStatsSystem` reads `cache->total_agent_count`.
- `AgentDeathSystem` reads `cache->dead_agent_ids` and processes only those.
- Hover info uses `tile.agent_count` (already set by the cache system).

### Systems that must still iterate

- `ExodusSystem` needs to modify agent state — this must still iterate, but only runs during the Exodus phase (a few seconds per day cycle, not every frame).
- Per-agent update systems (`AgentMovementSystem`, `NeedTickSystem`, etc.) must still iterate via `for_each_with` — these are inherent per-agent work.

## Files to change

- `src/components.h` — Add `AgentFrameCache` struct
- `src/entity_makers.cpp` — Register singleton on Sophie
- `src/crowd_systems.cpp` — Merge density counting, death detection, stats into one system
- `src/render_ui.cpp` — Read minimap data from cache, simplify hover

## Estimated impact

- **Performance:** Reduces agent iteration from 6×/frame to 1×/frame for cache-building, plus 1× for per-agent update systems. With 200 agents, saves ~1,000 entity lookups per frame.
- **Complexity:** Centralizes agent statistics. Removes redundant `EntityQuery` calls scattered across files.

## Risks

- Cache is stale for systems that run before the cache system. Must ensure `UpdateAgentCacheSystem` runs early in the update phase.
- Adding/removing agents mid-frame (e.g., spawning, death cleanup) could invalidate the cache. Solution: cache is rebuilt every frame unconditionally at the start of the update phase.
- Minimap dots are populated during update but consumed during render. The data is frame-coherent since render follows update.
