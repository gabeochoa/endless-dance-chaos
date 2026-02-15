
#include <argh.h>

#include "afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "entity_makers.h"
#include "game.h"
#include "mcp_integration.h"
#include "systems.h"

bool running = true;
bool g_test_mode = false;

// Render texture for MCP screenshots
raylib::RenderTexture2D g_render_texture;

using namespace afterhours;

void game(const std::string& test_script) {
    SystemManager systems;
    register_all_systems(systems);

    make_sophie();
    EntityHelper::merge_entity_arrays();

    // E2E test runner setup
    testing::E2ERunner runner;
    if (g_test_mode && !test_script.empty()) {
        runner.load_script(test_script);
        runner.set_timeout(30.0f);

        // Screenshot callback - save PNGs to tests/e2e/screenshots/
        runner.set_screenshot_callback([](const std::string& name) {
            std::filesystem::create_directories("tests/e2e/screenshots");
            std::string path = "tests/e2e/screenshots/" + name + ".png";

            // Capture from render texture
            raylib::Image img =
                raylib::LoadImageFromTexture(g_render_texture.texture);
            raylib::ImageFlipVertical(&img);
            raylib::ExportImage(img, path.c_str());
            raylib::UnloadImage(img);
            log_info("[E2E] Screenshot saved: {}", path);
        });

        log_info("[E2E] Loaded test script: {}", test_script);
    }

    while (running && !raylib::WindowShouldClose()) {
        // Check Escape BEFORE systems run (while pending tiles still exist)
        bool escape_should_quit =
            raylib::IsKeyPressed(raylib::KEY_ESCAPE) && should_escape_quit();

        float dt = raylib::GetFrameTime();
        systems.run(dt);

        // Tick E2E runner in test mode
        if (g_test_mode && runner.has_commands()) {
            runner.tick(dt);
            EntityHelper::merge_entity_arrays();

            if (runner.is_finished()) {
                runner.print_results();
                running = false;
            }
        }

        if (escape_should_quit) {
            running = false;
        }
    }
}

int main(int argc, char* argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

    bool mcp_mode = cmdl[{"--mcp"}];
    g_test_mode = cmdl[{"--test-mode"}];

    std::string test_script;
    cmdl("--test-script") >> test_script;

    // In MCP mode, suppress raylib logs and redirect our logs to stderr
    if (mcp_mode) {
        raylib::SetTraceLogLevel(raylib::LOG_NONE);
        g_log_to_stderr = true;
    }

    // In test mode, suppress raylib logs
    if (g_test_mode) {
        raylib::SetTraceLogLevel(raylib::LOG_NONE);
    }

    log_info("Starting Endless Dance Chaos v{}", VERSION);

    raylib::InitWindow(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT,
                       "Endless Dance Chaos");
    raylib::SetTargetFPS(500);
    raylib::SetExitKey(0);  // Disable Escape from closing window

    // Create render texture for MCP screenshots
    g_render_texture =
        raylib::LoadRenderTexture(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

    if (mcp_mode) {
        mcp_integration::set_screenshot_texture(&g_render_texture);
        mcp_integration::init();
    }

    game(test_script);

    mcp_integration::shutdown();
    raylib::UnloadRenderTexture(g_render_texture);
    raylib::CloseWindow();

    log_info("Goodbye!");
    return 0;
}
