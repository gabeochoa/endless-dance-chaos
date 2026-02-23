# Refactor 04: Pool Lightweight Entities (Particles, Toasts, DeathMarkers)

## Problem

Every agent death spawns 6–12 `Particle` entities, each as a full ECS entity with component storage. Death bursts create and destroy dozens of entities per second. `ToastMessage` and `DeathMarker` are also full entities for what are essentially timed values.

### Current entity counts

| Type | Max alive | Lifetime | Creation rate |
|------|-----------|----------|---------------|
| `Particle` | ~60 (5 deaths × 12) | 0.3–0.5s | Bursty, up to 12/death |
| `DeathMarker` | 20 (hard cap) | 10s | 1 per death event tile |
| `ToastMessage` | ~3–5 | 2–5s | Sporadic |

### Cost per entity

Each entity involves:
- `EntityHelper::createEntity()` — vector push, ID generation
- `addComponent<T>()` — component map insertion (×2–3 components per entity)
- `merge_entity_arrays()` — required after batch creation
- `e.cleanup = true` + `EntityHelper::cleanup()` — deferred destruction

For particles especially, this is heavy machinery for objects that live < 0.5 seconds.

## Proposed Fix

### Replace with singleton pools

Create singleton components that hold fixed-capacity vectors:

```cpp
struct ParticlePool : afterhours::BaseComponent {
    struct Entry {
        vec2 position;
        vec2 velocity;
        float lifetime = 0.f;
        float max_lifetime = 0.f;
        float size = 3.f;
        Color color = {255, 80, 80, 255};
    };
    std::array<Entry, 256> particles{};
    int count = 0;

    void emit(vec2 pos, vec2 vel, float life, float sz, Color col);
    void update(float dt);  // tick all, compact dead
};

struct ToastPool : afterhours::BaseComponent {
    struct Entry {
        std::string text;
        float lifetime = 3.0f;
        float elapsed = 0.f;
        float fade_duration = 0.5f;
    };
    std::array<Entry, 8> toasts{};
    int count = 0;
};

struct DeathMarkerPool : afterhours::BaseComponent {
    struct Entry {
        vec2 position;
        float lifetime = 10.0f;
        float max_lifetime = 10.0f;
    };
    std::array<Entry, 24> markers{};
    int count = 0;
};
```

### System changes

- `spawn_death_particles()` → `pool->emit(...)` instead of `EntityHelper::createEntity()`
- `UpdateParticlesSystem` → `pool->update(dt)` iterates the flat array
- `RenderParticlesSystem` → iterates `pool->particles[0..count]`
- `spawn_toast()` → `toast_pool->add(text, lifetime)`
- `UpdateToastsSystem` / toast rendering → iterate `toast_pool`
- Death marker creation/rendering/capping → iterate `marker_pool`

### Removal from entity system

Delete these component types:
- `Particle` (component)
- `ToastMessage` (component)
- `DeathMarker` (component)

Remove associated entity cleanup logic from `reset_game_state()`.

## Files to change

- `src/components.h` — Add pool structs, remove `Particle`, `ToastMessage`, `DeathMarker`
- `src/entity_makers.cpp` — Add pools to Sophie, update `reset_game_state()`, update `spawn_toast()`
- `src/update_helpers.h` — Update `spawn_toast()` to use pool
- `src/crowd_systems.cpp` — Update `spawn_death_particles()`, `AgentDeathSystem`, `UpdateParticlesSystem`
- `src/polish_systems.cpp` — Update `UpdateDeathMarkersSystem`
- `src/update_systems.cpp` — Update `UpdateToastsSystem`
- `src/render_world.cpp` — Update `RenderDeathMarkersSystem`, `RenderParticlesSystem`
- `src/render_ui.cpp` — Update toast rendering, game over rendering

## Estimated impact

- **Performance:** Eliminates entity creation/destruction churn for ephemeral objects. No more `merge_entity_arrays()` calls for particles. Flat array iteration is cache-friendly.
- **Complexity:** Removes 3 component types and their associated create/query/cleanup patterns. Pool logic is simpler and self-contained.

## Risks

- Fixed-capacity arrays mean particles can be dropped if the pool is full. With 256 slots and < 0.5s lifetimes, this is unlikely to be hit. Can log a warning if it happens.
- Toasts and death markers are currently queried by `EntityQuery` in a few places (e.g., `reset_game_state`). These need to switch to `pool->clear()`.
