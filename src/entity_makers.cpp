#include "entity_makers.h"

#include <queue>
#include <set>

#include "afterhours/src/core/entity_query.h"
#include "afterhours/src/plugins/input_system.h"
#include "game.h"
#include "input_mapping.h"
#include "vec_util.h"

static Entity& make_entity(float x, float z) {
    Entity& e = EntityHelper::createEntity();
    e.addComponent<Transform>(x, z);
    return e;
}

Entity& make_sophie() {
    Entity& sophie = EntityHelper::createPermanentEntity();

    sophie.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(sophie);

    sophie.addComponent<GameState>();
    EntityHelper::registerSingleton<GameState>(sophie);

    sophie.addComponent<BuilderState>();
    EntityHelper::registerSingleton<BuilderState>(sophie);

    sophie.addComponent<FestivalProgress>();
    EntityHelper::registerSingleton<FestivalProgress>(sophie);

    sophie.addComponent<DemandHeatmap>();
    EntityHelper::registerSingleton<DemandHeatmap>(sophie);

    // Input system
    afterhours::input::add_singleton_components(sophie, get_mapping());

    log_info("Created Sophie entity with all singletons");
    return sophie;
}

// Helper: create facility of given type
static Entity& make_facility(float x, float z, FacilityType type) {
    Entity& e = make_entity(x, z);
    e.addComponent<Facility>(type);
    return e;
}

Entity& make_stage(float x, float z) {
    Entity& e = make_facility(x, z, FacilityType::Stage);
    e.addComponent<StageInfo>();
    return e;
}

Entity& make_bathroom(float x, float z) {
    return make_facility(x, z, FacilityType::Bathroom);
}

Entity& make_food(float x, float z) {
    return make_facility(x, z, FacilityType::Food);
}

Entity& make_attraction(float x, float z, float spawn_rate, int capacity) {
    Entity& e = make_entity(x, z);
    e.addComponent<Attraction>(spawn_rate, capacity);
    return e;
}

Entity& make_agent(float x, float z, FacilityType want, int origin_id) {
    Entity& e = make_entity(x, z);
    e.addComponent<Agent>(want, origin_id);
    e.addAll<HasStress, AgentTarget, AgentSteering>();
    return e;
}

Entity& make_path_tile(int grid_x, int grid_z) {
    float world_x = grid_x * TILESIZE;
    float world_z = grid_z * TILESIZE;
    Entity& e = make_entity(world_x, world_z);
    e.addComponent<PathTile>(grid_x, grid_z);
    return e;
}

bool find_path_tile_at(int grid_x, int grid_z) {
    return EntityQuery()
        .whereHasComponent<PathTile>()
        .whereLambda([grid_x, grid_z](const Entity& e) {
            const PathTile& pt = e.get<PathTile>();
            return pt.grid_x == grid_x && pt.grid_z == grid_z;
        })
        .has_values();
}

void remove_path_tile_at(int grid_x, int grid_z) {
    auto tiles = EntityQuery()
                     .whereHasComponent<PathTile>()
                     .whereLambda([grid_x, grid_z](const Entity& e) {
                         const PathTile& pt = e.get<PathTile>();
                         return pt.grid_x == grid_x && pt.grid_z == grid_z;
                     })
                     .gen();

    for (Entity& tile : tiles) {
        tile.cleanup = true;
    }
}

static std::set<std::pair<int, int>> placed_tiles_set;

static void place_path_line(int x1, int z1, int x2, int z2) {
    int dx = (x2 > x1) ? 1 : (x2 < x1) ? -1 : 0;
    int dz = (z2 > z1) ? 1 : (z2 < z1) ? -1 : 0;

    int x = x1, z = z1;
    while (true) {
        if (placed_tiles_set.find({x, z}) == placed_tiles_set.end()) {
            make_path_tile(x, z);
            placed_tiles_set.insert({x, z});
        }
        if (x == x2 && z == z2) break;
        x += dx;
        z += dz;
    }
}

// Pre-place initial path layout matching the original hardcoded paths
void create_initial_path_layout() {
    placed_tiles_set.clear();

    // Attraction at (-5,-5), Hub at (-3,0)
    // Bathroom at (5,-3), Food at (5,0), Stage at (5,3)

    // Attraction to hub: L-shape (horizontal then vertical)
    place_path_line(-5, -5, -3, -5);
    place_path_line(-3, -5, -3, 0);

    // Hub to bathroom: L-shape
    place_path_line(-3, -3, 5, -3);

    // Hub to food: straight horizontal
    place_path_line(-3, 0, 5, 0);

    // Hub to stage: L-shape
    place_path_line(-3, 0, -3, 3);
    place_path_line(-3, 3, 5, 3);

    placed_tiles_set.clear();
    log_info("Created initial path layout with grid tiles");
}

void calculate_path_signposts() {
    auto path_tiles = EntityQuery().whereHasComponent<PathTile>().gen();
    if (path_tiles.empty()) {
        log_info("No path tiles to calculate signposts for");
        return;
    }

    std::unordered_map<int, std::vector<int>> adjacency;
    std::map<std::pair<int, int>, int> grid_to_id;

    for (Entity& tile : path_tiles) {
        const PathTile& pt = tile.get<PathTile>();
        grid_to_id[{pt.grid_x, pt.grid_z}] = tile.id;
    }

    const int dx[] = {1, -1, 0, 0};
    const int dz[] = {0, 0, 1, -1};

    for (Entity& tile : path_tiles) {
        tile.addComponentIfMissing<PathSignpost>();
        // Clear old signpost data before recalculating to remove stale tile IDs
        tile.get<PathSignpost>().next_node_for.clear();
        const PathTile& pt = tile.get<PathTile>();
        for (int i = 0; i < 4; i++) {
            auto neighbor_it =
                grid_to_id.find({pt.grid_x + dx[i], pt.grid_z + dz[i]});
            if (neighbor_it != grid_to_id.end()) {
                adjacency[tile.id].push_back(neighbor_it->second);
            }
        }
    }

    auto facilities = EntityQuery().whereHasComponent<Facility>().gen();

    for (Entity& facility : facilities) {
        FacilityType type = facility.get<Facility>().type;
        vec2 facility_pos = facility.get<Transform>().position;

        int terminal_tile_id = -1;
        float best_dist = std::numeric_limits<float>::max();
        for (Entity& tile : path_tiles) {
            float dist =
                vec::distance(tile.get<Transform>().position, facility_pos);
            if (dist < best_dist) {
                best_dist = dist;
                terminal_tile_id = tile.id;
            }
        }
        if (terminal_tile_id < 0) continue;

        std::queue<std::pair<int, int>> bfs_queue;
        std::set<int> visited;
        bfs_queue.push({terminal_tile_id, -1});

        while (!bfs_queue.empty()) {
            auto [current_id, next_id] = bfs_queue.front();
            bfs_queue.pop();

            if (visited.count(current_id)) continue;
            visited.insert(current_id);

            auto current = EntityHelper::getEntityForID(current_id);
            if (!current.valid() || !current->has<PathSignpost>()) continue;

            current->get<PathSignpost>().next_node_for[type] = next_id;

            auto it = adjacency.find(current_id);
            if (it != adjacency.end()) {
                for (int neighbor_id : it->second) {
                    if (!visited.count(neighbor_id)) {
                        bfs_queue.push({neighbor_id, current_id});
                    }
                }
            }
        }
    }

    log_info("Calculated path signposts for {} path tiles", path_tiles.size());
}

bool should_escape_quit() {
    auto* builder = EntityHelper::get_singleton_cmp<BuilderState>();
    if (builder && builder->has_pending()) {
        return false;  // Don't quit - we have pending tiles to cancel
    }
    return true;
}
