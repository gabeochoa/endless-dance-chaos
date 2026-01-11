---
description: "Core coding standards, patterns, and conventions for the endless_dance_chaos project"
alwaysApply: true
---

# Project Standards

## Git Commit Format
Use short, descriptive commit messages with prefixes:
- no prefix for new features
- `bf -` for bug fixes  
- `be -` for backend/engine changes or refactoring
- `ui -` for UI changes

Rules:
- Use all lowercase
- Avoid special characters like ampersands (&) - use "and" instead
- Keep messages concise and descriptive

Examples:
- `implement boids separation force`
- `bf - fix stress not spreading correctly`
- `be - optimize neighbor queries with spatial hash`

## Code Style
- Keep functions focused and single-purpose
- Prefer early returns to reduce nesting
- Dont add any comments unless explicitly asked 
- use a function instead of a line with multiple ternary expressions
- Avoid using `auto` for non-template types - use explicit types instead
- Use `for (Entity &entity : EntityQuery().gen())` instead of `for (auto &ref : ...)` with `ref.get()`
- Prefer references over pointers when possible

## Project Structure
- `src/` contains main game code
- `src/engine/` contains reusable engine utilities
- `vendor/` contains third-party libraries (afterhours as git submodule)
- `resources/` contains assets (images, sounds, fonts)
- `output/` contains build artifacts

## Build System
- Use `make` for building
- Game executable is `output/dance.exe`
- Use `make run` to build and run
- Use `make clean` to remove build artifacts

## Debugging
- Use `log_info()`, `log_warn()`, `log_error()` for logging
- Add debug logs for complex systems like crowd simulation
- Remove verbose debug logs before committing

## TODO Comments
- Use `TODO` for incomplete features or known issues
- Use `TODO` for performance optimizations (e.g., "TODO this allocates...")
- Use `TODO` for future improvements

## Component Patterns
- All components inherit from `BaseComponent`
- Use `struct` for components, not `class`
- Components should be simple data containers
- Use `std::optional` for nullable fields
- Use `enum class` for component state enums

## System Patterns
- Systems inherit from `System<Components...>`
- Override `for_each_with(Entity&, Components&..., float)` for main logic
- Use `virtual void for_each_with(...) override` syntax
- Systems should be focused and single-purpose
- Use early returns to reduce nesting

## Naming Conventions
- Use `camelCase` or snake_case for variables and functions
- Use `PascalCase` for structs, classes, and enums
- Use `UPPER_CASE` for constants and macros
- Use descriptive names that indicate purpose
- Use `has_` prefix for boolean components (e.g., `HasStress`)
- Use `Can_` prefix for capability components (e.g., `CanMove`)
- Use `Is_` prefix for state components (e.g., `IsAttraction`)

## File Organization
- Use `#pragma once` at top of header files
- Group includes: standard library, third-party, project headers
- Use forward declarations when possible
- Keep header files focused and minimal

## Query and Filtering Patterns
- Prefer `EntityQuery` when possible over manual entity iteration
- Use `whereLambda` for complex filtering conditions
- Use `orderByLambda` for sorting entities instead of `std::sort`
- Use `gen_first()` for finding single entities instead of loops

## Game-Specific Patterns

### Crowd Agents (Boids)
- Use force-based movement (separation, path following, goal attraction)
- Keep agent logic simple - complexity emerges from interactions
- Use spatial hashing for neighbor queries when performance matters

### Stress System
- Stress is local, not global
- Stress spreads to neighbors over time
- High stress modifies movement forces

### Paths
- Player-drawn paths influence agent movement
- Paths have capacity limits
- Congestion is physical, not instant failure

