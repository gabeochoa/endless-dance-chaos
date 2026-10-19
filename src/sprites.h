#pragma once

// Kenney CC0 sprite assets (see resources/sprites/README.md +
// KENNEY_LICENSE.txt).
//
// Reality check: the Kenney packs are ICONS / emotes / UI borders, not
// isometric building tiles and not peep sprites, so there are no sprite
// peeps or sprite buildings here. Sprites cover only:
//   - emote faces/symbols inside urgent-need thought bubbles,
//   - a red-cross / water-drop glyph on two building sign boards,
//   - a music note on the stage canopy,
//   - a few toolbar buttons, and the Guests-page need rows.
//
// Loading mirrors the existing font pattern (get_font() in
// render_helpers.h): lazy, once, from a relative path off the process
// working directory (repo root, same as resources/fonts/). A texture
// that fails to load — no GL context, headless without GPU, missing
// file — stays width 0, and every draw call falls back to the
// geometric primitive it replaced. Nothing here may crash on a
// failed load: always draw through draw_sprite_centered() and keep
// the primitive path for its `false` return.
//
// Toggle: sprites_enabled(), default ON. E2E `set_sprites on|off`
// flips it, `assert_sprites` checks it, and reset_game_state()
// restores ON so scripts start hermetic. `assert_sprites_loaded`
// (probe only, not in the suite) fails when no texture loaded at all.

#include <array>

#include "afterhours/src/drawing_helpers.h"

enum class SpriteId : int {
    FaceSad = 0,  // emote_face_sad.png
    FaceAngry,    // emote_face_angry.png
    Alert,        // emote_alert.png
    Music,        // emote_music.png
    Cross,        // emote_cross.png (red cross)
    Drop,         // emote_drop.png (water drop)
    Door,         // icon_door.png (game-icons, black)
    MusicOn,      // icon_music_on.png (game-icons, black)
    Trashcan,     // icon_trashcan.png (game-icons, black)
    Count,
};

struct SpriteDef {
    const char* path;
    float width;  // native PNG size, for the source rect
    float height;
};

inline constexpr SpriteDef SPRITE_DEFS[] = {
    {"resources/sprites/emote_face_sad.png", 32.f, 38.f},
    {"resources/sprites/emote_face_angry.png", 32.f, 38.f},
    {"resources/sprites/emote_alert.png", 32.f, 38.f},
    {"resources/sprites/emote_music.png", 32.f, 38.f},
    {"resources/sprites/emote_cross.png", 32.f, 38.f},
    {"resources/sprites/emote_drop.png", 32.f, 38.f},
    {"resources/sprites/icon_door.png", 50.f, 50.f},
    {"resources/sprites/icon_music_on.png", 50.f, 50.f},
    {"resources/sprites/icon_trashcan.png", 50.f, 50.f},
};

inline constexpr int SPRITE_COUNT = static_cast<int>(SpriteId::Count);

// Master toggle: sprites ON by default; OFF draws only primitives.
inline bool& sprites_enabled() {
    static bool enabled = true;
    return enabled;
}

// Loaded textures, filled once on first use. A slot stays width 0 when
// its file failed to load (headless / no-GPU / missing file).
inline std::array<afterhours::TextureType, static_cast<size_t>(SPRITE_COUNT)>&
sprite_bank() {
    static std::array<afterhours::TextureType,
                      static_cast<size_t>(SPRITE_COUNT)>
        bank{};
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (int i = 0; i < SPRITE_COUNT; i++)
            bank[static_cast<size_t>(i)] =
                afterhours::load_texture(SPRITE_DEFS[i].path);
    }
    return bank;
}

// A texture counts as loaded when it has dimensions. This is backend
// portable: raylib Texture2D has width/height, the Metal MyTexture
// has width/height, and the none backend returns a 0x0 texture. Never
// branch on `.id` — the Metal texture has no `id` field.
inline bool sprite_loaded(SpriteId id) {
    if (id == SpriteId::Count) return false;
    const auto& tex = sprite_bank()[static_cast<size_t>(id)];
    return tex.width > 0 && tex.height > 0;
}

inline int sprites_loaded_count() {
    int n = 0;
    for (int i = 0; i < SPRITE_COUNT; i++)
        if (sprite_loaded(static_cast<SpriteId>(i))) n++;
    return n;
}

// Draw a sprite centred on (cx, cy), `size` pixels wide, aspect
// preserved (emotes are 32x38, game-icons 50x50), white tint.
// Returns false when sprites are toggled off or the texture failed
// to load — the caller draws its geometric primitive instead.
inline bool draw_sprite_centered(SpriteId id, float cx, float cy, float size,
                                 afterhours::Color tint = {255, 255, 255,
                                                           255}) {
    if (!sprites_enabled() || !sprite_loaded(id)) return false;
    const auto& tex = sprite_bank()[static_cast<size_t>(id)];
    const SpriteDef& def = SPRITE_DEFS[static_cast<int>(id)];
    float w = size;
    float h = size * (def.height / def.width);
    afterhours::draw_texture_pro(tex, {0.f, 0.f, def.width, def.height},
                                 {cx, cy, w, h}, {w * 0.5f, h * 0.5f}, 0.f,
                                 tint);
    return true;
}

// Thought-bubble face for an urgent-need glyph (see urgent_need_glyph
// in render_helpers.h). HEAT is the killer need so it gets the angry
// face; WC / FOOD both get the sad face (there is no sick face in the
// pack, and a hungry guest is a sad guest); BAR gets the alert burst.
inline SpriteId sprite_for_need_glyph(const char* glyph) {
    if (!glyph) return SpriteId::Count;
    if (glyph[0] == 'H') return SpriteId::FaceAngry;  // HEAT
    if (glyph[0] == 'W') return SpriteId::FaceSad;    // WC
    if (glyph[0] == 'F') return SpriteId::FaceSad;    // FOOD
    if (glyph[0] == 'B') return SpriteId::Alert;      // BAR
    return SpriteId::Count;
}
