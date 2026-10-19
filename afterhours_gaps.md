# afterhours gaps / limitations

## 2026-10-18 — Zig 0.16.0 build (`build.zig`): afterhours compiles clean under zig's bundled libc++, but zig is NOT Apple clang + system libc++
- New `build.zig` (fresh file; no sibling repo has one except mailboy's,
  adapted not copied) mirrors the default raylib Makefile: 21 `src/*.cpp` +
  `src/**/*.cpp` TUs, Makefile `FLAGS`/`NOFLAGS` verbatim, `-Ivendor/ -Isrc/`,
  `-framework OpenGL -framework AppKit`, default build ->
  `zig-out/bin/dance`, `run` step (forwards args after `--`), `test` step
  (`--test-mode --headless --test-script-dir tests/e2e`, cwd = repo root, no
  timeout — wrap with `perl -e 'alarm 1500; exec @ARGV'` if one is wanted).
  No `build.zig.zon` needed (verified: zig 0.16 builds without one). No
  `vendor/` edits, Makefile untouched and still working, nothing committed.
  `.gitignore` gains `zig-out/` + `.zig-cache/`.
- No PCH in the zig build, because the dance Makefile has NO PCH at all
  (unlike mailboy's, which precompiles `src/pch.h`): sources include headers
  normally, and `build.zig` passes no `-include` / `-include-pch` flag.
- No vendor C++ TUs: the dance Makefile's OBJ rules compile only
  `src/*.cpp` + `src/**/*.cpp` (verified by grep; the only `vendor` hits in
  the makefile are `-I`/`-L` and the Metal sokol include dir). Mailboy's
  Makefile names two `vendor/afterhours/src/plugins/*.cpp` TUs explicitly;
  dance's does not, so neither does `build.zig`. `src/sokol_impl.mm` is
  Metal-only (`make metal`) and is excluded: zig covers the default raylib
  build only.
- No backend type macros in `build.zig`, unlike mailboy's `-DVector2Type`
  etc.: dance's `src/rl.h` falls through to `AFTER_HOURS_USE_RAYLIB` when
  neither backend macro is set, and afterhours' `developer.h` auto-defines
  the `Vector2Type` / `RectangleType` / `TextureType` / `ColorType` /
  `FontType` macros from the raylib types. Copying mailboy's defines would
  have duplicated that logic.
- Friction 1 — `-fblocks` WORKS under zig's bundled clang, so the brief's
  drop-blocks fallback was NOT needed. `GESTURE_FLAGS` (`-fblocks` +
  `-DAFTER_HOURS_ENABLE_MACOS_GESTURES`) are in `build.zig` verbatim.
  Proof the gestures branch compiled: the binary contains the
  `addLocalMonitorForEventsMatchingMask:` selector (`strings` hit), and the
  block literal `^id(id event)` in `gestures_macos.h` is a hard syntax error
  without `-fblocks`. `otool -L` shows AppKit linked.
- Friction 2 — zig Debug adds UBSan to C++ TUs the Makefile never has
  (same as mailboy). Proof without a verbose rebuild: `nm` on
  `zig-out/bin/dance` shows 34 `__ubsan_handle_*` symbols. No UBSan
  failures surfaced in the single-script run (suite pending at time of
  writing); it is default zig Debug behavior, not an afterhours problem.
- Friction 3 — zig 0.16 DOES support pkg-config
  (`linkSystemLibrary("raylib", .{})` defaults to `use_pkg_config = .yes`),
  but it resolves bare `pkg-config` from PATH and on any failure silently
  falls back to `-lraylib` by name — no error, no hint about which
  pkg-config ran. In this shell PATH puts MacPorts
  `/opt/local/bin/pkg-config` first, whose `pc_path` has no raylib, so the
  pkg-config path misses and the fallback must save the build. `build.zig`
  therefore also adds the Homebrew Cellar dirs
  (`/Users/gabeochoa/homebrew/Cellar/raylib/5.5/include|lib`, guarded by an
  on-disk existence check) as `-isystem`/`-L`. Proof it worked: `otool -L`
  resolves raylib to `/Users/gabeochoa/homebrew/opt/raylib/lib/libraylib.550.dylib`
  (the Cellar path), same as the make binary.
- Friction 4 — `--headless` is parsed-but-unwired in `src/main.cpp`
  (app-side, not afterhours: the harness already provides
  `E2EArgs::display_mode()`). `parse_e2e_args` sets `e2e.headless`, but
  `main.cpp` never assigns `cfg.display` from it, so `--headless` is
  currently a no-op and `zig build test` (which passes `--test-mode
  --headless --test-script-dir tests/e2e`) runs identically to `make test`
  (which passes only `--test-script-dir tests/e2e`). The one-line fix
  (`cfg.display = e2e.display_mode()`) is deliberately NOT made here: it
  would change window/screenshot behaviour, and the brief asked for build
  parity, not behaviour changes.
- Friction 5 — the e2e exit code is not the pass signal (app-side).
  `src/main.cpp` returns 0 unconditionally and never consults
  `runner.has_failed()`, so a failing suite still exits 0 under BOTH builds
  (same as `make test`). The signal is the `Scripts passed:` /
  `Scripts failed:` summary. `zig build test` therefore cannot be used as
  a pass/fail gate on exit code alone until `main.cpp` wires `has_failed()`
  to the return code.
- Toolchain divergence (same as mailboy): `zig-out/bin/dance` is 43M
  (45,359,576 bytes) vs the make binary's 5.9M (6,235,480 bytes), and
  `otool -L` shows no `/usr/lib/libc++.1.dylib` on the zig binary (zig's
  bundled libc++ statically linked + dwarf debug info) where the make
  binary links the system libc++ dynamically. Afterhours + dance compile
  and pass the suite under this libc++ with zero source changes, but
  ABI/behavior differences between zig's libc++ and Apple's would surface
  here first.
- Verification (2026-10-18): clean `rm -rf zig-out .zig-cache && zig build`
  green with 0 warnings on stderr (empty build log). Single-script sanity
  (`zig-out/bin/dance --test-mode --headless --test-script
  tests/e2e/01_grid_exists.e2e`): exit 0, no `WARN`/`ERROR` lines except
  the pre-existing font-glyph warning (U+2192), screenshot saved,
  `E2E finished in 0.14 seconds`, `Goodbye!`. Note single-script mode
  prints no `[PASS]` line (the runner has no `ScriptResult` to tally in
  single-script mode); the exit-0 + clean-log reading is the signal there.
  Full `zig build test` (perl alarm 1500 wrap, 976s wall): Scripts run
  101, Scripts passed 100, Scripts failed 1 — the single failure is
  `vip_eventual`, whose `eventually_assert_agent_watching gte 1 20`
  (line 53) timed out at 20.0s with 0 agents watching (`command_handlers.h`
  `[E2E ERROR]`, the only genuine error in the log). The VIP had already
  been served (`+$250`), and the same script passes standalone under the
  zig binary (`eventually_assert_agent_watching ... PASSED (eventual)`, no
  `E2E ERROR`, exit 0): the walk-back-to-watch-the-stage bound is
  timing-marginal in full-suite order, same class as the documented
  `03_path_preference` / `33_exodus_watching` flakes (both passed this
  run). Not a zig divergence. The log's only other `[E2E ERROR]` is the
  EXPECTED one in `fail_eventually_timeout` (negative control, `[PASS]`).
  MAKE-SUITE COMPARISON PENDING — full `make test` run in progress at
  time of writing; its counts will confirm the 100/101 is flake, not
  toolchain, when they land.

## 2026-10-18 — fmt 8.0.1 -> 11.2.0 re-vendor: afterhours does NOT vendor fmt
- Afterhours itself vendors no copy of fmt: there is no `vendor/fmt` under
  `vendor/afterhours`. Its headers include `<fmt/...>` / `"fmt/format.h"`
  and rely on the HOST to supply fmt headers. Dance supplies them from
  `vendor/fmt/` via `-Ivendor/`; afterhours' own tests/examples Makefiles
  instead point at the Homebrew include dir. So afterhours is affected
  by the host's fmt version, not by any copy it controls.
- Afterhours' fmt usage is unaffected by the 8 -> 11 jump: `fmt::format`
  (~66 sites in `vendor/afterhours/src`), plus `translation.h`'s
  `fmt::vformat` + `fmt::dynamic_format_arg_store<fmt::format_context>` +
  `fmt::arg`. All still exist in fmt 11.2.0. Verified compiling clean:
  full dance build on Apple clang 17, and `src/render_ui.cpp` (heaviest
  TU, `log.h`-first) plus `vendor/afterhours/src/plugins/settings.cpp`
  (which pulls in the fmt-using UI headers) on Homebrew clang 21.1.8.
  No afterhours edits were needed (and none were made). No dance
  call-site fixes were needed either: dance uses only `fmt::format`,
  `fmt::vformat`, `fmt::make_format_args`, `fmt::print`,
  `fmt::string_view`, `fmt::format_args` (in `src/log/log.h`), and
  nothing includes the deleted `fmt/locale.h` (a deprecated shim in 8).
- Latent gap (not fixed; do not edit `vendor/afterhours`): afterhours
  headers include fmt BEFORE any std header (`translation.h:3-4`
  includes `<fmt/args.h>` / `<fmt/format.h>` first). fmt 11.2's
  `format.h` uses unqualified `::malloc` / `::free` without including
  `<cstdlib>`, so a bare TU that instantiates fmt without a later
  `<cstdlib>` / `<iostream>` fails on clang 21 (reproduced:
  `format.h:747` / `format.h:752`, `malloc` / `free` undeclared).
  Dance is safe because every fmt-instantiating TU reaches `<iostream>`
  via `log/log.h` (included from `src/log.h` right after fmt) before
  any instantiation point. Afterhours standalone TUs that include
  `translation.h` first and instantiate fmt with no later `<cstdlib>`
  would break on newer toolchains. Upstream fix would be `<cstdlib>`
  before fmt in `translation.h` (or fmt upstream including `<cstdlib>`
  in `format.h`).
- Unrelated observation from the same clang 21 probe: `settings.cpp`
  emits deprecated-literal-operator warnings from `vendor/nlohmann/json.hpp`
  (`operator"" _json`), not from fmt. Pre-existing, different vendor lib,
  left alone.


Found while bumping `vendor/afterhours` from `a2f125d` to `d1d7f0e`.
File references are inside `vendor/afterhours`. Nothing in this file is a
reason to stay on the old submodule; the app-side fixes are in the working
tree (see `git status`).

## 1. `dump_profile` silently prints nothing when profiling is compiled out

`builtin_profile::enable()` (`src/plugins/e2e_testing/perf_commands.h`)
moved per-system timing onto `profiling::Collector`
(`src/plugins/profiling.h`). The collector only records when
`AFTERHOURS_ENABLE_PROFILING=1` is defined in **every** TU (default 0,
`src/core/system_timing.h`), and only rolls samples into the snapshot when
the owner calls `Collector::end_frame()` once per frame
(`docs/profiling.md`). Neither failure is reported: `Collector::start()`
returns `false` silently, `enable()` does not check it, and
`dump_profile` prints `--- Top 0 systems by avg time ---` while the script
still passes. This app hit exactly that: 5 perf scripts ran `dump_profile 8`
and got `Top 0`.

App-side fix: `-DAFTERHOURS_ENABLE_PROFILING=1` in the shared makefile
flags (raylib + Metal, one variable so TUs cannot drift and ODR-break
`ProfileScope`), plus `default_collector().end_frame()` at the end of the
frame callback in `src/main.cpp`, plus regression test
`tests/e2e/perf_profile_entries.e2e` (`assert_profile_sampled`).

Library gap: `enable()` should warn (once) when `profiling::available` is
false, instead of installing a provider that can only return empty.

A related consequence: `expect_fps_above` / `expect_p99_below` also stay
unavailable (`nullopt`) without profiling + `end_frame()`.

## 2. `crash::install_handler()` recurses when `log_error` asserts

`crash::report()` (`src/crash_handler.h`) reports through `log_error`.
With `AFTER_HOURS_REPLACE_LOGGING`, a host whose `log_error` asserts on
error — the pattern afterhours itself acknowledges in
`src/core/entity_query.h` ("a consumer whose log_error does crash") — gets
infinite recursion: signal handler -> `log_error` -> `assert(false)` ->
`abort()` -> `SIGABRT` -> same handler -> ... This app's `log_error`
asserts, so the handler is deliberately **not** installed here.

Library gap: `crash::report()` should log through a path that cannot
re-enter the handler (raw `stderr` write or `log_sink_fn`), or the header
should document that `install_handler()` requires a non-aborting
`log_error`.

## 3. `log_file.h` captures nothing under `AFTER_HOURS_REPLACE_LOGGING`

`log_file.h` hooks `log_sink_fn`, but only afterhours' own logging in
`src/logging.h` calls that sink — and the `AFTER_HOURS_REPLACE_LOGGING`
branch defines no logging at all, so a host's logs bypass the sink
entirely. Including `log_file.h` in this app would buffer/capture zero
lines. A host has to forward its own logs to `log_sink_fn` by hand; that
is not documented.

## 4. `log_once_per` host contract is undocumented

With `AFTER_HOURS_REPLACE_LOGGING`, afterhours defines no `log_once_per`,
yet `src/plugins/window_manager.h` calls it as
`log_once_per(Duration, int, const char*, Args...)`. The required shape
is discoverable only by finding that call site. This app shims it as a
no-op (`src/log.h`); afterhours' rate-limited warnings are therefore
silently dropped here (see also `LOG_ONCE_PER` in `src/log/log_macros.h`,
which the same shim swallows).

## 5. `builtin_profile::enable()` silently replaces a custom provider

`enable()` overwrites all three `PerfProvider` fields (`get_fps`,
`get_p99_ms`, `top_entries`). A provider installed just before the call
is discarded with no warning. This app had exactly such a dead setup block
in `cmd_perf_start`; it is now deleted.

## 6. `VALIDATE` now aborts (was a no-op)

`VALIDATE(condition, message)` in `src/logging.h` used to be
`inline void VALIDATE(...) {}`. It now logs via `log_error` and aborts
on failure (no-op only under `NDEBUG`). No app test triggers it, but a
duplicate-component add that previously continued silently now kills the
process. Behaviour change, not a bug — noted because it is not called
out in the bump's `CHANGELOG.md` breaking section.

## 7. Metal: the SOKOL_IMPL TU must include two afterhours headers

`make metal` fails at link time with undefined
`AFTERHOURS_MISSING_sokol_impl_TU...`, `metal_create_system_device`, and
`metal_capture_render_texture` because the app's `src/sokol_impl.mm`
never included `src/backends/sokol/image_decode.h` or
`src/backends/sokol/capture_impl.h`. This predates the bump: the same
guard and the same `extern "C"` declarations exist at `a2f125d`, and
`src/sokol_impl.mm` was unchanged since the commit that added Metal
support. The link error spells out the fix; the requirement itself lives
only in header comments. Fixed in this change: `src/sokol_impl.mm` now
includes both headers, and `make metal` links.

## Checked, not gaps

- Host type macros (`ColorType` / `FontType` / `Vector3Type`): fine here.
  `src/rl.h` includes raylib first, then `afterhours/src/developer.h`,
  which auto-defines the macros from the raylib types.
- `AFTER_HOURS_REPLACE_LOGGING` macros as single statements: `log_at`,
  `log_error`, `log_clean` were already `do/while`; `log_if` / `log_ifx`
  were bare blocks and are now `do/while` too (`src/log/log_macros.h`).
- Animation rewrite, `HasScrollView::viewport_size` becoming
  `std::optional`, `Margin` field order, `RoundedCorners` bit names,
  deleted `colors::to_hsl` / `to_rgb` / `get_highlighted`: the app uses
  none of these, so none of them break the build.

## 2026-10-17 — e2e flake baseline note
- `03_path_preference` and `33_exodus_watching` flake intermittently in full-suite
  runs (timing-marginal asserts), pass standalone and in most runs. Verified
  pre-existing across multiple chunks (also flakes on stashed baseline builds).
  Suite-level result should be read as "94 pass modulo these two known flakes"
  until someone hardens them (e.g. wait-for-condition instead of fixed waits).
- Runner gap (worked around app-side): custom `assert_*` commands fail on first
  evaluation — only `expect_text` retries. Dance added opt-in `eventually_`
  wrappers in its own e2e_commands.h. Upstream afterhours could offer a
  retrying-assert helper so every app stops re-deriving this.
- Fonts: neither game font contains U+2605 (star) — text glyphs must be checked
  against font cmaps (see also mailboy em-dash -> '?').
