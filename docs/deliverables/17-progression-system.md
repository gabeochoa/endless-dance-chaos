# Phase 17: Progression System

**Demo 4B** | Prerequisite: Phase 16 (build tools work)

## Goal

Track max-attendees-ever as the scaling metric. +1 of each facility type per 100 max attendees. New slots unlock automatically with a top-bar toast notification.

**Done when:** Playing longer naturally escalates difficulty through progression.

---

## Deliverables

### 1. Max Attendees Tracking

Each frame, compare current agent count to `max_attendees_ever`:

```cpp
// In GameState
int max_attendees_ever = 0;

// In an UpdateProgressionSystem:
int current_agents = count_all_agents();
if (current_agents > game_state.max_attendees_ever) {
    game_state.max_attendees_ever = current_agents;
}
```

### 2. Slot Calculation

```cpp
struct FacilitySlots : BaseComponent {
    int get_slots_per_type(int max_attendees_ever) const {
        return 1 + (max_attendees_ever / 100);
    }

    int stages_placed = 1;
    int bathrooms_placed = 1;
    int food_placed = 1;
    int gates_placed = 1;

    bool can_place(FacilityType type, int max_attendees_ever) const {
        int slots = get_slots_per_type(max_attendees_ever);
        switch (type) {
            case FacilityType::Bathroom: return bathrooms_placed < slots;
            case FacilityType::Food: return food_placed < slots;
            case FacilityType::Stage: return stages_placed < slots;
            default: return true; // paths, fences, gates unlimited for now
        }
    }
};
```

| Max Attendees | Slots Per Type |
|---------------|---------------|
| 0-99 | 1 |
| 100-199 | 2 |
| 200-299 | 3 |
| 300-399 | 4 |
| etc. | +1 per 100 |

### 3. Build Tool Integration

In placement validation (Phase 16), check slot availability:

```cpp
if (tool == BuildTool::Stage && !slots.can_place(FacilityType::Stage, max_attendees)) {
    // Show red preview — no slot available
    return false;
}
```

When placing: increment placed count.
When demolishing: decrement placed count.

### 4. Unlock Notification (Top Bar Toast)

When `max_attendees_ever` crosses a 100-milestone:

```cpp
int old_slots = 1 + (old_max / 100);
int new_slots = 1 + (new_max / 100);
if (new_slots > old_slots) {
    show_toast("New facility slots unlocked!");
}
```

Toast implementation:
- Text appears in top bar area
- Fades in, stays for 2-3 seconds, fades out
- Font: Fredoka, 14px
- Color: white text on semi-transparent green background

```cpp
struct ToastMessage : BaseComponent {
    std::string text;
    float lifetime = 3.0f;
    float elapsed = 0.f;
    float fade_duration = 0.5f;
};
```

### 5. Artist Crowd Size Scaling

Now that `max_attendees_ever` is tracked, wire it into artist schedule generation (Phase 12):

```cpp
// In generate_next_artist:
int crowd = calculate_crowd_size(game_state.max_attendees_ever);
```

More attendees → bigger artists → more crowd → more danger. This is the core positive feedback loop.

### 6. E2E Commands

| Command | Args | Description |
|---------|------|-------------|
| `set_max_attendees` | `VALUE` | Set max_attendees_ever directly |
| `assert_max_attendees` | `OP VALUE` | Assert max_attendees_ever |
| `assert_slots` | `TYPE OP VALUE` | Assert available slots for type |

**`tests/e2e/17_progression.e2e`**:
```
# Test slot unlocking
reset_game
wait 5

# Start with 1 slot each
assert_slots bathroom eq 1
assert_slots stage eq 1

# Set max attendees to 100+
set_max_attendees 150
wait 5

# Should have 2 slots now
assert_slots bathroom eq 2
assert_slots stage eq 2

# Set to 250
set_max_attendees 250
wait 5

assert_slots bathroom eq 3
screenshot 17_three_slots
```

**`tests/e2e/17_toast.e2e`**:
```
# Test unlock toast
reset_game
wait 5

set_max_attendees 99
wait 5
set_max_attendees 100
wait 5

# Should see toast
expect_text "unlocked"
screenshot 17_toast_visible
```

---

## Existing Code to Leverage

- `FestivalProgress` component (replace with `FacilitySlots`)
- Build tool validation from Phase 16
- Artist schedule from Phase 12

## What to Delete

- `FestivalProgress` component → replaced by `FacilitySlots`

---

## Acceptance Criteria

- [ ] `max_attendees_ever` tracked correctly (peak concurrent agents)
- [ ] +1 slot per facility type per 100 max attendees
- [ ] Build tools enforce slot limits (red preview when no slots)
- [ ] Demolishing returns slots
- [ ] Toast notification appears on unlock ("New facility slots unlocked!")
- [ ] Toast fades after 2-3 seconds
- [ ] Artist crowd sizes scale with max_attendees_ever
- [ ] Positive feedback loop: more people → bigger artists → more danger

## Out of Scope

- Cost system
- Per-facility-type slot differences
- Locked tool indicators
