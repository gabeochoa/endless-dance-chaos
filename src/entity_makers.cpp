#include "entity_makers.h"

#include <queue>
#include <set>

#include "afterhours/src/core/entity_query.h"
#include "vec_util.h"

// Helper: create entity with Transform at position
static Entity& make_entity(float x, float z) {
    Entity& e = EntityHelper::createEntity();
    e.addComponent<Transform>(x, z);
    return e;
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

Entity& make_path_node(float x, float z, int next_node_id, float width) {
    Entity& e = make_entity(x, z);
    e.addComponent<PathNode>(next_node_id, width);
    return e;
}

Entity& make_hub(float x, float z) { return make_path_node(x, z, -1); }

Entity& make_agent(float x, float z, FacilityType want, int origin_id) {
    Entity& e = make_entity(x, z);
    e.addComponent<Agent>(want, origin_id);
    e.addAll<HasStress, AgentTarget, AgentSteering>();
    return e;
}

Entity& make_camera() {
    Entity& e = EntityHelper::createPermanentEntity();
    e.addComponent<ProvidesCamera>();
    EntityHelper::registerSingleton<ProvidesCamera>(e);
    return e;
}

// Calculate signposts using BIDIRECTIONAL BFS from facilities
// This ensures EVERY node has directions to EVERY facility (Disney-style
// signposts)
void calculate_path_signposts() {
    auto path_nodes = EntityQuery().whereHasComponent<PathNode>().gen();

    // Build BIDIRECTIONAL adjacency list
    // Every edge goes both ways: A->B in PathNode means both A<->B for BFS
    std::unordered_map<int, std::vector<int>> adjacency;

    // Store node positions for co-location checks
    std::unordered_map<int, vec2> node_positions;

    for (Entity& node : path_nodes) {
        node.addComponentIfMissing<PathSignpost>();
        node_positions[node.id] = node.get<Transform>().position;

        PathNode& pn = node.get<PathNode>();
        if (pn.next_node_id >= 0) {
            adjacency[node.id].push_back(pn.next_node_id);
            adjacency[pn.next_node_id].push_back(node.id);
        }
    }

    // Add co-located edges (hub junctions where multiple paths meet at same
    // position)
    for (Entity& node_a : path_nodes) {
        vec2 pos_a = node_a.get<Transform>().position;
        for (Entity& node_b : path_nodes) {
            if (node_a.id == node_b.id) continue;
            if (vec::distance(pos_a, node_b.get<Transform>().position) < 0.1f) {
                adjacency[node_a.id].push_back(node_b.id);
            }
        }
    }

    // For each facility type, BFS from terminal to ALL nodes
    auto facilities = EntityQuery().whereHasComponent<Facility>().gen();

    for (Entity& facility : facilities) {
        FacilityType type = facility.get<Facility>().type;
        vec2 facility_pos = facility.get<Transform>().position;

        // Find the terminal PathNode closest to this facility
        int terminal_node_id = -1;
        float best_dist = std::numeric_limits<float>::max();

        for (Entity& node : path_nodes) {
            float dist =
                vec::distance(node.get<Transform>().position, facility_pos);
            if (dist < best_dist) {
                best_dist = dist;
                terminal_node_id = node.id;
            }
        }

        if (terminal_node_id < 0) continue;

        // BFS from terminal node through BIDIRECTIONAL graph
        std::queue<std::pair<int, int>>
            bfs_queue;  // (node_id, next_toward_facility)
        std::set<int> visited;

        bfs_queue.push(
            {terminal_node_id, -1});  // -1 means "arrived at facility"

        while (!bfs_queue.empty()) {
            auto [current_id, next_id] = bfs_queue.front();
            bfs_queue.pop();

            if (visited.count(current_id)) continue;
            visited.insert(current_id);

            auto current = EntityHelper::getEntityForID(current_id);
            if (!current.valid() || !current->has<PathSignpost>()) continue;

            current->get<PathSignpost>().next_node_for[type] = next_id;
            vec2 current_pos = current->get<Transform>().position;

            // Propagate to all neighbors (bidirectional)
            auto it = adjacency.find(current_id);
            if (it != adjacency.end()) {
                for (int neighbor_id : it->second) {
                    if (!visited.count(neighbor_id)) {
                        // Check if neighbor is co-located with current node
                        auto neighbor_pos_it = node_positions.find(neighbor_id);
                        bool is_colocated = false;
                        if (neighbor_pos_it != node_positions.end()) {
                            is_colocated =
                                vec::distance(current_pos,
                                              neighbor_pos_it->second) < 0.1f;
                        }

                        // Co-located nodes share the SAME next_id (don't point
                        // to each other) Non-co-located neighbors point to
                        // current node
                        int propagated_next =
                            is_colocated ? next_id : current_id;
                        bfs_queue.push({neighbor_id, propagated_next});
                    }
                }
            }
        }
    }

    log_info("Calculated path signposts for {} path nodes", path_nodes.size());
}
