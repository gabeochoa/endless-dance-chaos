#include "systems.h"
#include "rl.h"
#include "mcp_integration.h"

extern bool running;

struct MCPUpdateSystem : System<> {
    void once(float) override {
        if (mcp_integration::exit_requested()) {
            running = false;
        }
        mcp_integration::update();
    }
};

struct MCPRenderUISystem : System<> {
    void once(float) const override {
        if (mcp_integration::is_enabled()) {
            raylib::DrawText("[MCP Mode Active]", 10, 40, 14, raylib::LIME);
        }
    }
};

struct MCPClearFrameSystem : System<> {
    void once(float) const override {
        mcp_integration::clear_frame_state();
    }
};

void register_mcp_update_systems(SystemManager& sm) {
    sm.register_update_system(std::make_unique<MCPUpdateSystem>());
}

void register_mcp_render_systems(SystemManager& sm) {
    sm.register_render_system(std::make_unique<MCPRenderUISystem>());
    sm.register_render_system(std::make_unique<MCPClearFrameSystem>());
}
