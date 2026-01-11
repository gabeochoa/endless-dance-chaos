

#include "game.h"
#include "camera.h"
#include "mcp_integration.h"
#include <argh.h>

bool running = true;

using namespace afterhours;

void draw_ground_grid(int size, float spacing) {
    raylib::Color gridColor = raylib::Fade(raylib::DARKGRAY, 0.5f);
    
    for (int i = -size; i <= size; i++) {
        raylib::DrawLine3D(
            {(float)i * spacing, 0.0f, (float)-size * spacing},
            {(float)i * spacing, 0.0f, (float)size * spacing},
            gridColor
        );
        raylib::DrawLine3D(
            {(float)-size * spacing, 0.0f, (float)i * spacing},
            {(float)size * spacing, 0.0f, (float)i * spacing},
            gridColor
        );
    }
}

void draw_demo_scene() {
    raylib::DrawCube({0.0f, 0.5f, 0.0f}, 1.0f, 1.0f, 1.0f, raylib::RED);
    raylib::DrawCubeWires({0.0f, 0.5f, 0.0f}, 1.0f, 1.0f, 1.0f, raylib::MAROON);
    
    raylib::DrawCube({2.0f, 0.5f, 2.0f}, 1.0f, 1.0f, 1.0f, raylib::GREEN);
    raylib::DrawCubeWires({2.0f, 0.5f, 2.0f}, 1.0f, 1.0f, 1.0f, raylib::DARKGREEN);
    
    raylib::DrawCube({-2.0f, 0.5f, -2.0f}, 1.0f, 1.0f, 1.0f, raylib::BLUE);
    raylib::DrawCubeWires({-2.0f, 0.5f, -2.0f}, 1.0f, 1.0f, 1.0f, raylib::DARKBLUE);
    
    raylib::DrawCube({2.0f, 0.5f, -2.0f}, 1.0f, 1.0f, 1.0f, raylib::YELLOW);
    raylib::DrawCubeWires({2.0f, 0.5f, -2.0f}, 1.0f, 1.0f, 1.0f, raylib::ORANGE);
    
    raylib::DrawSphere({0.0f, 0.0f, 0.0f}, 0.1f, raylib::WHITE);
}

void game() {
    IsometricCamera cam;
    
    while (running && !raylib::WindowShouldClose()) {
        // Check for MCP exit request
        if (mcp_integration::exit_requested()) {
            running = false;
            break;
        }
        
        // Update MCP server
        mcp_integration::update();
        
        float dt = raylib::GetFrameTime();
        
        // Update camera controls
        cam.handle_input(dt);
        
        // Begin drawing
        raylib::BeginDrawing();
        raylib::ClearBackground(raylib::Color{40, 44, 52, 255});
        
        // 3D rendering
        raylib::BeginMode3D(cam.camera);
        {
            draw_ground_grid(10, TILESIZE);
            draw_demo_scene();
        }
        raylib::EndMode3D();
        
        // UI overlay
        raylib::DrawText("Endless Dance Chaos", 10, 10, 20, raylib::WHITE);
        raylib::DrawText("Controls:", 10, 40, 16, raylib::LIGHTGRAY);
        raylib::DrawText("  WASD/Arrows - Pan", 10, 60, 14, raylib::GRAY);
        raylib::DrawText("  Q/E - Rotate 90 degrees", 10, 78, 14, raylib::GRAY);
        raylib::DrawText("  Scroll - Zoom", 10, 96, 14, raylib::GRAY);
        
        if (mcp_integration::is_enabled()) {
            raylib::DrawText("[MCP Mode Active]", 10, 120, 14, raylib::LIME);
        }
        
        raylib::DrawFPS(DEFAULT_SCREEN_WIDTH - 100, 10);
        
        raylib::EndDrawing();
        
        // Clear MCP frame state
        mcp_integration::clear_frame_state();
    }
}

int main(int argc, char *argv[]) {
    argh::parser cmdl(argc, argv, argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);
    
    log_info("Starting Endless Dance Chaos v{}", VERSION);
    
    // Initialize window
    raylib::InitWindow(DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, "Endless Dance Chaos");
    raylib::SetTargetFPS(60);
    
    // Initialize MCP if requested
    if (cmdl[{"--mcp"}]) {
        mcp_integration::init();
    }
    
    game();
    
    // Cleanup
    mcp_integration::shutdown();
    raylib::CloseWindow();
    
    log_info("Goodbye!");
    return 0;
}
