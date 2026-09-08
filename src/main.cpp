
#include <argh.h>

#include "audio.h"
#include "entity_makers.h"
#include "game.h"
#include "gfx3d.h"
#include "mcp_integration.h"
#include "render_helpers.h"
#include "systems.h"

#include "afterhours/src/shutdown.h"

#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "afterhours/src/plugins/e2e_testing/harness.h"
#include "afterhours/src/plugins/e2e_testing/test_input.h"

bool g_test_mode = false;

using namespace afterhours;
namespace gfx = afterhours::graphics;

gfx::RenderTextureType g_render_texture;

int main(int argc, char* argv[]) {
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
        gfx::set_exit_key(0);

        afterhours::InitAudioDevice();
        if (!g_test_mode) {
            get_audio().init();
        }

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
    };

    cfg.cleanup = [&]() {
        mcp_integration::shutdown();
        get_audio().shutdown();
        afterhours::CloseAudioDevice();
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
