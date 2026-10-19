
#include <argh.h>

#include <chrono>
#include <cstdlib>
#include <string>

#include "audio.h"
#include "entity_makers.h"
#include "game.h"
#include "gfx3d.h"
#include "mcp_integration.h"
#include "render_helpers.h"
#include "systems.h"

#include "afterhours/src/gestures_macos.h"
#include "afterhours/src/shutdown.h"

#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "afterhours/src/plugins/e2e_testing/harness.h"
#include "afterhours/src/plugins/e2e_testing/test_input.h"
#include "afterhours/src/plugins/profiling.h"

bool g_test_mode = false;

using namespace afterhours;
namespace gfx = afterhours::graphics;

gfx::RenderTextureType g_render_texture;

int main(int argc, char* argv[]) {
    // DANCE_PERF=1 launch timing: main entry, graphics init done (the
    // backend created the window/GL context before cfg.init runs), first
    // completed frame (profiling end_frame), and first input-sampled
    // frame. first_input_ms is the start of frame 0, whose update phase
    // samples input; launch_to_first_frame_ms is the end of frame 0, so
    // the gap between them is the first frame's own duration. If input
    // sampling ever moves off frame 0 (e.g. behind a loading screen),
    // first_input_ms must move with it — that gap is what this pair
    // measures.
    const auto perf_t_main = std::chrono::steady_clock::now();
    const char* perf_env = std::getenv("DANCE_PERF");
    const bool perf_enabled = perf_env && std::string(perf_env) == "1";
    auto perf_ms = [](std::chrono::steady_clock::time_point t0,
                      std::chrono::steady_clock::time_point t1) {
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };
    std::chrono::steady_clock::time_point perf_t_gfx_done{};
    std::chrono::steady_clock::time_point perf_t_first_input{};
    bool perf_logged = false;
    int perf_frame_index = 0;

    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    bool mcp_mode = cmdl[{"--mcp"}];

    // Everything e2e comes from afterhours' harness: --test-mode,
    // --test-script, --test-script-dir, --timeout, --slow, --time-scale
    // (aka --e2e-speed) and --screenshot-dir. argh still owns --mcp.
    auto e2e = testing::parse_e2e_args(argc, argv);
    g_test_mode = e2e.enabled;

    if (mcp_mode) {
        gfx::set_trace_log_level(7);  // LOG_NONE
        g_log_to_stderr = true;
    }

    if (g_test_mode) {
        gfx::set_trace_log_level(7);
    }

    log_info("Starting Endless Dance Chaos v{}", VERSION);

    SystemManager systems;
    testing::E2ERunner runner;

    gfx::RunConfig cfg;
    cfg.width = DEFAULT_SCREEN_WIDTH;
    cfg.height = DEFAULT_SCREEN_HEIGHT;
    cfg.title = "Endless Dance Chaos";
    cfg.target_fps = 500;

    cfg.init = [&]() {
        if (perf_enabled) perf_t_gfx_done = std::chrono::steady_clock::now();
        gfx::set_exit_key(0);

        // Needs the window to exist. Idempotent, and a no-op off macOS.
        afterhours::gestures::install_pinch_monitor();

        // Audio is lazy now (AudioManager::ensure_init on first real
        // sound; no-op in test mode) — launch pays no audio cost.

        g_render_texture =
            load_render_texture(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

        if (mcp_mode) {
            mcp_integration::set_screenshot_texture(&g_render_texture);
            mcp_integration::init();
        }

        register_all_systems(systems);
        make_sophie();
        EntityHelper::merge_entity_arrays();

        auto setup_screenshot_callback = [&]() {
            runner.set_screenshot_callback([&e2e](const std::string& name) {
                std::string path = testing::screenshot_path(
                    e2e, name, "tests/e2e/screenshots");
                std::filesystem::create_directories(
                    std::filesystem::path(path).parent_path());
#ifdef AFTER_HOURS_USE_METAL
                gfx::take_screenshot(path.c_str());
#else
                capture_render_texture(g_render_texture, path);
#endif
                log_info("[E2E] Screenshot saved: {}", path);
            });
        };

        if (g_test_mode && !e2e.script_dir.empty()) {
            runner.load_scripts_from_directory(e2e.script_dir);
            // A whole directory needs longer than the harness's per-script
            // default; an explicit --timeout still wins.
            if (e2e.timeout_seconds == testing::E2EArgs{}.timeout_seconds)
                e2e.timeout_seconds = 60.0f;
            testing::configure_runner(runner, e2e);
            setup_screenshot_callback();
            log_info("[E2E] Loaded test directory: {}", e2e.script_dir);
        } else if (g_test_mode && !e2e.script_path.empty()) {
            runner.load_script(e2e.script_path);
            testing::configure_runner(runner, e2e);
            setup_screenshot_callback();
            log_info("[E2E] Loaded test script: {}", e2e.script_path);
        }

        if (g_test_mode) {
            afterhours::testing::test_input::detail::test_mode = true;
        }
    };

    cfg.frame = [&]() {
        if (perf_enabled && perf_frame_index == 0)
            perf_t_first_input = std::chrono::steady_clock::now();
        if (g_test_mode) {
            afterhours::testing::test_input::reset_frame();
        }

        bool escape_should_quit =
            gfx::is_key_pressed(KEY_ESCAPE) && should_escape_quit();

        // --time-scale / --e2e-speed shortens a run by scaling the whole
        // frame, so `wait <seconds>` and the sim it is waiting on move
        // together. Defaults to 1.
        float dt = gfx::get_frame_time() * e2e.time_scale;
        systems.run(dt);

        if (g_test_mode && runner.has_commands()) {
            runner.tick(dt);
            EntityHelper::merge_entity_arrays();

            if (runner.is_finished()) {
                runner.print_results();
                gfx::request_quit();
            }
        }

        if (escape_should_quit) {
            gfx::request_quit();
        }

        // afterhours' profiler needs one end_frame() per application frame:
        // it is what rolls per-system timings into the snapshot that
        // dump_profile / expect_fps_above read. No-op while nothing is
        // recording (no perf script asked for it).
        afterhours::profiling::default_collector().end_frame();
        if (perf_enabled && !perf_logged) {
            perf_logged = true;
            const auto now = std::chrono::steady_clock::now();
            log_info(
                "[PERF] launch_to_first_frame_ms={:.1f} "
                "first_input_ms={:.1f} graphics_init_ms={:.1f}",
                perf_ms(perf_t_main, now),
                perf_ms(perf_t_main, perf_t_first_input),
                perf_ms(perf_t_main, perf_t_gfx_done));
        }
        perf_frame_index++;
    };

    cfg.cleanup = [&]() {
        mcp_integration::shutdown();
        // shutdown() closes the audio device when it was initialized
        // (lazy init owns the device now; main no longer closes it
        // unconditionally).
        get_audio().shutdown();
        // Entities before the backend. Left to static destruction the order is
        // unspecified, and a component destructor that calls into a dead
        // backend throws on the way out of main().
        afterhours::shutdown();
        unload_render_texture(g_render_texture);
    };

    gfx::run(cfg);

    log_info("Goodbye!");
    return 0;
}
