
#include <argh.h>

#include "audio.h"
#include "entity_makers.h"
#include "game.h"
#include "gfx3d.h"
#include "mcp_integration.h"
#include "render_helpers.h"
#include "systems.h"

#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "afterhours/src/plugins/e2e_testing/test_input.h"

bool g_test_mode = false;

using namespace afterhours;
namespace gfx = afterhours::graphics;

gfx::RenderTextureType g_render_texture;

int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    bool mcp_mode = cmdl[{"--mcp"}];
    g_test_mode = cmdl[{"--test-mode"}];

    std::string test_script;
    cmdl("--test-script") >> test_script;

    std::string test_dir;
    cmdl("--test-dir") >> test_dir;

    if (mcp_mode) {
        gfx::set_trace_log_level(7);  // LOG_NONE
        g_log_to_stderr = true;
    }

    if (g_test_mode) {
        gfx::set_trace_log_level(7);
    }

    if (!test_dir.empty()) {
        g_test_mode = true;
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
            runner.set_screenshot_callback([](const std::string& name) {
                std::filesystem::create_directories("tests/e2e/screenshots");
                std::string path = "tests/e2e/screenshots/" + name + ".png";
                capture_render_texture(g_render_texture, path);
                log_info("[E2E] Screenshot saved: {}", path);
            });
        };

        if (g_test_mode && !test_dir.empty()) {
            runner.load_scripts_from_directory(test_dir);
            runner.set_timeout(60.0f);
            setup_screenshot_callback();
            log_info("[E2E] Loaded test directory: {}", test_dir);
        } else if (g_test_mode && !test_script.empty()) {
            runner.load_script(test_script);
            runner.set_timeout(30.0f);
            setup_screenshot_callback();
            log_info("[E2E] Loaded test script: {}", test_script);
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

        float dt = gfx::get_frame_time();
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
        unload_render_texture(g_render_texture);
    };

    gfx::run(cfg);

    log_info("Goodbye!");
    return 0;
}
