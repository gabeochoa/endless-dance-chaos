const std = @import("std");

// Zig build for dance, mirroring the default (raylib) Makefile target.
//   zig build         -> zig-out/bin/dance
//   zig build run     -> build + run (forwards extra args after `--`)
//   zig build test    -> build + run zig-out/bin/dance --test-mode --headless
//                         --test-script-dir tests/e2e, with cwd = repo root.
//                         No timeout is applied by this step; wrap it
//                         externally if one is wanted, e.g.
//                         perl -e 'alarm 1500; exec @ARGV' -- zig build test
//
// Differences from the Makefile, by design:
// - No PCH: the dance Makefile has no PCH at all (unlike mailboy's, which
//   precompiles src/pch.h). Sources include headers normally, and so does
//   this build: no -include / -include-pch flag of any kind.
// - No vendor C++ TUs: the dance Makefile's OBJ rules compile only
//   src/*.cpp + src/**/*.cpp. Mailboy additionally lists two
//   vendor/afterhours plugin TUs explicitly; dance does not, so neither
//   does this build. src/sokol_impl.mm is Metal-only (the `make metal`
//   target) and is excluded here too.
// - The Metal/Sokol build (`make metal`, -DAFTER_HOURS_USE_METAL) is NOT
//   covered by this file: zig covers the default raylib build only.
// - Optimization/debug flags come from -Doptimize (default Debug, which is
//   -O0 + debug info, matching the Makefile's -g with no -O flag). The C++
//   warning flags below are the Makefile's FLAGS/NOFLAGS verbatim.
// - MCP is off by default, as in the Makefile (only `make MCP=1` defines
//   AFTER_HOURS_ENABLE_MCP). Pass -DAFTER_HOURS_ENABLE_MCP via
//   -D... is not wired; build with make if MCP is needed.
// - raylib is found via zig's built-in pkg-config support
//   (linkSystemLibrary defaults to use_pkg_config = .yes). On this machine
//   that only works when pkg-config can see raylib.pc, e.g. with the
//   Homebrew pkg-config first on PATH, or PKG_CONFIG_PATH pointing at the
//   Homebrew pkgconfig dir. Because this shell's default PATH puts a
//   MacPorts pkg-config first (whose pc_path has no raylib), the build also
//   adds the Homebrew Cellar include/lib dirs as a fallback when they exist
//   on disk (see raylib_homebrew_prefix below). The fallback is inert on
//   machines without that Cellar path. See README "Building with Zig".

const raylib_homebrew_prefix = "/Users/gabeochoa/homebrew/Cellar/raylib/5.5";

fn pathExists(io: std.Io, abs_path: []const u8) bool {
    std.Io.Dir.accessAbsolute(io, abs_path, .{}) catch return false;
    return true;
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    // Default Debug (matches make -O0; known-green). ReleaseSafe exists
    // via -Doptimize=ReleaseSafe and is ~2x faster rendering, BUT mailboy
    // ReleaseSafe deterministically fails tests/upgrades.e2e
    // (expect_day_duration 780 vs 540: caffeine_level read as 0 under
    // optimization — a latent UB/strict-aliasing-class bug exposed by the
    // optimizer, under investigation; do NOT default to it until fixed).
    // Dance ReleaseSafe is unverified — same caution.
    const optimize = b.standardOptimizeOption(.{});

    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });

    // Includes: -Ivendor/ -Isrc/ (Makefile INCLUDES).
    mod.addIncludePath(b.path("vendor"));
    mod.addIncludePath(b.path("src"));

    // Defines: Makefile -D flags.
    // -DAFTER_HOURS_ENABLE_E2E_TESTING (bare -D in the Makefile, i.e. =1).
    mod.addCMacro("AFTER_HOURS_ENABLE_E2E_TESTING", "1");
    // PROFILING_FLAGS = -DAFTERHOURS_ENABLE_PROFILING=1. Required in EVERY
    // TU: afterhours' profiling switches types inside SystemManager's
    // ProfileScope, so a TU built without it would ODR-break the rest.
    mod.addCMacro("AFTERHOURS_ENABLE_PROFILING", "1");
    // GESTURE_FLAGS = -fblocks -DAFTER_HOURS_ENABLE_MACOS_GESTURES.
    // The -fblocks flag is in `flags` below; the macro is here. The
    // gestures header uses ObjC-runtime C API + a block literal, so the
    // two must stay together: dropping -fblocks means dropping this
    // define too (then install_pinch_monitor() is a no-op).
    mod.addCMacro("AFTER_HOURS_ENABLE_MACOS_GESTURES", "1");
    // Deliberately NOT defined, unlike mailboy: dance's src/rl.h falls
    // through to AFTER_HOURS_USE_RAYLIB when neither backend macro is set,
    // and afterhours' developer.h auto-defines the Vector2Type /
    // RectangleType / TextureType / ColorType / FontType macros from the
    // raylib types. Defining them here would duplicate that logic.

    // raylib via pkg-config (zig 0.16 linkSystemLibrary, use_pkg_config=.yes).
    // If pkg-config cannot find raylib, zig falls back to -lraylib by name.
    mod.linkSystemLibrary("raylib", .{});
    // Frameworks: Makefile LIBS (-framework OpenGL -framework AppKit).
    // Dance differs from mailboy here (CoreGraphics -> AppKit): AppKit is
    // what the trackpad pinch monitor links against.
    mod.linkFramework("OpenGL", .{});
    mod.linkFramework("AppKit", .{});
    // Fallback: Homebrew Cellar raylib on this machine, if present.
    // Supplies the -I/-L that pkg-config would have supplied, so the
    // -lraylib fallback above resolves even when pkg-config misses.
    if (pathExists(b.graph.io, raylib_homebrew_prefix ++ "/include/raylib.h")) {
        mod.addSystemIncludePath(.{ .cwd_relative = raylib_homebrew_prefix ++ "/include" });
        mod.addLibraryPath(.{ .cwd_relative = raylib_homebrew_prefix ++ "/lib" });
    }

    // Sources: src/*.cpp + src/**/*.cpp (Makefile SRC_FILES wildcard),
    // enumerated so a stray file can never silently join the build.
    // No vendor/ TUs: the dance Makefile compiles none (verified: its OBJ
    // rules list only src/*.cpp + src/**/*.cpp, unlike mailboy's Makefile
    // which names two vendor/afterhours plugin TUs explicitly).
    const sources = [_][]const u8{
        "src/agent_systems.cpp",
        "src/awards_systems.cpp",
        "src/building_systems.cpp",
        "src/crowd_systems.cpp",
        "src/engine/random_engine.cpp",
        "src/entity_makers.cpp",
        "src/event_systems.cpp",
        "src/lineup_systems.cpp",
        "src/main.cpp",
        "src/mcp_systems.cpp",
        "src/polish_systems.cpp",
        "src/render_debug.cpp",
        "src/render_systems.cpp",
        "src/render_ui.cpp",
        "src/render_world.cpp",
        "src/scenario_systems.cpp",
        "src/schedule_systems.cpp",
        "src/staff_systems.cpp",
        "src/testing/e2e_commands.cpp",
        "src/tycoon_systems.cpp",
        "src/update_systems.cpp",
    };
    // Flags: Makefile FLAGS warnings + NOFLAGS verbatim, plus GESTURE_FLAGS
    // -fblocks. No -g: -Doptimize's Debug default supplies -O0 + debug
    // info, matching the Makefile's -g with no -O flag. Dance NOFLAGS has
    // no -Wno-unused-private-field / -Wno-return-mismatch (those are
    // mailboy-only); they are deliberately not copied here.
    const flags = [_][]const u8{
        "-std=c++23",
        "-Wall",
        "-Wextra",
        "-Wuninitialized",
        "-Wshadow",
        "-Wno-deprecated-volatile",
        "-Wno-missing-field-initializers",
        "-Wno-c99-extensions",
        "-Wno-unused-function",
        "-Wno-sign-conversion",
        "-Wno-implicit-int-float-conversion",
        "-fblocks",
    };
    mod.addCSourceFiles(.{
        .files = &sources,
        .flags = &flags,
    });

    const exe = b.addExecutable(.{
        .name = "dance",
        .root_module = mod,
    });
    b.installArtifact(exe);

    const run_step = b.step("run", "Build and run dance (extra args after -- are forwarded)");
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    run_cmd.setCwd(b.path("."));
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    run_step.dependOn(&run_cmd.step);

    // The e2e suite: ~900-1500s of real-second waits. This step applies
    // no timeout; wrap it externally, e.g.
    // perl -e 'alarm 1500; exec @ARGV' -- zig build test
    // Note: the Makefile's `test` target runs only
    // --test-script-dir tests/e2e (the harness treats that flag alone as
    // enabling test mode). The zig step passes the fuller explicit form
    // --test-mode --headless --test-script-dir tests/e2e. --headless is
    // currently parsed-but-unwired in src/main.cpp (cfg.display is never
    // set from e2e.headless), so both forms run the same way today; the
    // explicit form is kept so the intent survives if main.cpp wires it.
    const test_step = b.step("test", "Build and run the e2e suite: dance --test-mode --headless --test-script-dir tests/e2e (cwd = repo root, no timeout)");
    const test_cmd = b.addRunArtifact(exe);
    test_cmd.step.dependOn(b.getInstallStep());
    test_cmd.setCwd(b.path("."));
    test_cmd.addArgs(&.{ "--test-mode", "--headless", "--test-script-dir", "tests/e2e" });
    test_step.dependOn(&test_cmd.step);
}
