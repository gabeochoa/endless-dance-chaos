# Phase 15: Polish Pass

## Goal
Add final MVP polish: death particles, stage glow, compass indicator, game over transition.

## Prerequisites
- All previous phases complete

## Deliverables

### 1. Death Particle Effect
When agent dies:
- Spawn 5-8 small square particles
- Size: 2-4px
- Burst radius: 8-12px
- Duration: 0.3-0.5 sec
- Color: matches agent color (or use red/white)

```cpp
void spawn_death_particles(vec2 position) {
    for (int i = 0; i < random_int(5, 8); i++) {
        float angle = random_float(0, 2 * PI);
        float speed = random_float(20, 40);
        // Create particle entity with velocity and lifetime
    }
}
```

### 2. Mass Death Merge
If 5+ agents die in same frame, same tile:
- Merge into one bigger burst
- More particles (10-15)
- Larger radius (20-30px)

### 3. Stage Glow Effect
When artist is performing:
- Color tint overlay on stage sprite
- Day: Warm yellow tint
- Night: Bright magenta/cyan pulse (optional)

Simple implementation:
```cpp
if (stage_info.state == StageState::Performing) {
    DrawRectangle(stage_pos, stage_size, Color{255, 200, 0, 80});
}
```

### 4. Compass Indicator
Show "N" indicator on screen when camera rotates:
- Small "N" text in corner
- Points in consistent direction
- Updates with camera rotation

### 5. Game Over Transition
When 10th death occurs:
- Brief pause (2-3 seconds)
- Death particle plays
- Fade to game over screen

### 6. Day/Night Color Transition
- Palette shifts over 1 hour game time
- Day colors (10am-6pm): Miami pastel
- Night colors (6pm-3am): EDC neon

## Existing Code to Use

- Particle systems (if any exist)
- `StageInfo::state` for stage glow
- `IsometricCamera::rotation_index` for compass
- Color palettes from roadmap

## Afterhours to Use

- Animation plugin for particles
- Timer plugin for delays
- Color interpolation utilities

## Implementation Steps

1. Create simple particle system (or use existing)
2. Spawn particles on agent death
3. Add stage glow render
4. Add compass indicator
5. Improve game over transition timing
6. (Optional) Day/night color transition

## Particle System (Simple)
```cpp
struct Particle : BaseComponent {
    vec2 position;
    vec2 velocity;
    float lifetime;
    float max_lifetime;
    Color color;
};

// Update: move by velocity, decrease lifetime, fade alpha
// Delete when lifetime <= 0
```

## Acceptance Criteria

- [ ] Death shows particle burst
- [ ] Multiple deaths in same spot merge into bigger burst
- [ ] Stage visibly "lit up" during performance
- [ ] Compass shows current orientation
- [ ] Game over has brief pause before screen appears
- [ ] (Stretch) Day/night colors shift

## Out of Scope
- Sound effects
- Screen shake
- Complex animations
- Extensive juice

## MVP COMPLETE 🎉
After this phase, MVP is complete. The game should be:
- Playable end-to-end
- Visually clear
- Core loop functional (survive → build → survive more)

