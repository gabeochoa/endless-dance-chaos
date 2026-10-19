# Kenney sprite art (CC0)

Real sprite art for Endless Dance Chaos, copied from Kenney packs by
Kenney Vleugels (Kenney.nl), released under Creative Commons Zero
(CC0 1.0). Full license text: [KENNEY_LICENSE.txt](KENNEY_LICENSE.txt).
Source mirror (read-only, do not edit):
`~/p/wm_afterhours/resources/kenney/`. Paths below are relative to it.

## Reality check

These packs are ICONS / emotes / UI borders — not isometric building
tiles and not peep sprites. There are no peep sprites and no building
tiles in any of the packs, so **peeps stay geometric** (outlined body +
skin-tone head + hair cap + desire pip, as before) and buildings stay
geometric blocks + silhouettes. The sprite win is faces and icons:

- emote faces/symbols inside thought bubbles,
- real glyphs on two building sign boards,
- a music note on the stage canopy,
- icons on five toolbar buttons and the Guests-page need rows.

## Copied files

| File | Source pack / path | Used for |
| --- | --- | --- |
| `emote_face_sad.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_faceSad.png` | WC + FOOD thought bubbles; Guests-page WC / Food rows |
| `emote_face_angry.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_faceAngry.png` | HEAT thought bubble; Guests-page Heat row |
| `emote_alert.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_alert.png` | BAR thought bubble; Guests-page Bar row |
| `emote_music.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_music.png` | Stage canopy; Guests-page Watching row |
| `emote_cross.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_cross.png` | MedTent sign board; Med Tent toolbar button |
| `emote_drop.png` | `kenney_emotes-pack/PNG/Vector/Style 1/emote_drop.png` | Water Station sign board + toolbar button |
| `icon_door.png` | `kenney_game-icons/PNG/Black/1x/door.png` | Gate toolbar button |
| `icon_music_on.png` | `kenney_game-icons/PNG/Black/1x/musicOn.png` | Stage toolbar button |
| `icon_trashcan.png` | `kenney_game-icons/PNG/Black/1x/trashcan.png` | Demolish toolbar button |

Emotes are the Vector Style 1 set (32x38 canvas, coloured) rather than
the 16x16 Pixel set: the vector faces stay readable when scaled to
bubble size. Game-icons are the Black 1x set (50x50): black glyphs read
on the light-gray toolbar buttons. Only the sprites the renderers
reference were copied — not whole packs.

## Thought-bubble mapping

`urgent_need_glyph()` still picks the worst need; the bubble shell
(white ellipse + tail) is unchanged and the emote replaces the text
glyph inside it. The glyph text is still registered for E2E
(`assert_urgent_bubble_count` etc. keep working).

| Need | Bubble | Why |
| --- | --- | --- |
| HEAT | angry face | Heat is the killer need; distressed face |
| WC | sad face | No sick face exists in the pack |
| FOOD | sad face | Hungry guest is a sad guest |
| BAR | alert burst | Thirsty guest needs the bar now |

The VIP bubble keeps its geometric gold star: the pack's star emote
is a tiny symbol, and the drawn star reads better at bubble size.

## Deliberately NOT copied / NOT sprited

- `kenney_board-game-icons`: **nothing copied**. This checkout of the
  pack ships no license file, so its icons stay out even though an
  apple / shield would have fit the Food / Security signs. Those two
  signs keep their geometric glyphs (plate, shield), as do Bathroom
  (person pictogram — no WC icon in the verified packs) and Bar
  (no sign board at all).
- `kenney_fantasy-ui-borders`: **nothing copied**. Its panels are
  96x96 1-bit monochrome tiles that render near-blank; there is no
  9-slice frame to stretch over the 220x450 park window, and stretching
  one tile would distort it. Park window keeps its bevel panels.
  (License reproduced in [KENNEY_LICENSE.txt](KENNEY_LICENSE.txt) for
  provenance.)
- Park-window tab row: no icons. Tabs are fixed-width with E2E click
  geometry and text asserts; squeezing icons in would reflow them.
  Tab labels stay text.
- Toolbar tools without an unambiguous icon (Path, Fence, Bathroom,
  Food, Bar, Security, Tree, Bench, Dirt/Neon Path, Lamp) keep their
  letter + colour chip. No icon is better than a forced one.

## Toggle + headless fallback

- `set_sprites on|off` (E2E command; default ON, `reset_game`
  restores ON; `assert_sprites on|off` checks it). OFF draws only the
  geometric primitives — the pre-sprite look, everywhere.
- Every sprite draw goes through `draw_sprite_centered()` in
  [sprites.h](../../src/sprites.h): if sprites are off or the texture
  failed to load, it returns `false` and the caller draws its
  primitive instead. A failed load can never crash rendering.
- Textures load lazily, once, from `resources/sprites/` relative to
  the process working directory (repo root — same convention as
  `resources/fonts/`). Loaded-ness is `texture.width > 0`, which
  holds on the raylib, Metal, and none backends (the Metal texture
  has no `.id` field, so nothing branches on it).
- Headless: the suite runs `--headless`, where textures load through
  the HeadlessGL context. Verified on this machine: the probe
  `assert_sprites_loaded` reports 9/9 textures loaded headless, and
  the sprite-vs-primitive screenshot pairs (thought-bubble face vs
  `WC` text, toolbar icons, stage music note) differ in exactly the
  sprite regions — so headless screenshots DO show sprites. The
  primitive fallback still covers a run with no GPU context or a
  missing file. `assert_sprites_loaded` is a manual probe (not in
  the suite) that fails when no texture loaded at all. No windowed
  run was used to verify (headless-only lane).

## Where sprites render

- `src/render_world.cpp` — `draw_iso_bubble()` (thought bubbles),
  `draw_iso_silhouette()` sign boards (MedTent, Water) + stage canopy.
- `src/render_ui.cpp` — build-bar toolbar buttons, Guests-page rows.
- `src/sprites.h` — loader, toggle, fallback helper.
- `src/testing/e2e_commands.h` — `set_sprites`, `assert_sprites`,
  `assert_sprites_loaded`.
- Populated shot: `tests/e2e/40_populated_200.e2e` →
  `tests/e2e/screenshots/populated_200.png` (200 guests, stage area).
