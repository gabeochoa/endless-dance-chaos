# afterhours gaps / limitations

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
