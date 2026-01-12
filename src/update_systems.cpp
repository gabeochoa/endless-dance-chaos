#include "systems.h"
#include "components.h"
#include "vec_util.h"
#include "afterhours/src/core/entity_query.h"
#include <queue>
#include <unordered_set>

// Calculate signposts using BIDIRECTIONAL BFS from facilities
// This ensures EVERY node has directions to EVERY facility (Disney-style signposts)
void calculate_path_signposts() {
    auto path_nodes = EntityQuery()
        .whereHasComponent<PathNode>()
        .gen();
    
    // Build BIDIRECTIONAL adjacency list
    // Every edge goes both ways: A->B in PathNode means both A<->B for BFS
    std::unordered_map<int, std::vector<int>> adjacency;
    
    // Store node positions for co-location checks
    std::unordered_map<int, vec2> node_positions;
    
    for (Entity& node : path_nodes) {
        // Add PathSignpost component if not present
        if (!node.has<PathSignpost>()) {
            node.addComponent<PathSignpost>();
        }
        
        if (node.has<Transform>()) {
            node_positions[node.id] = node.get<Transform>().position;
        }
        
        PathNode& pn = node.get<PathNode>();
        if (pn.next_node_id >= 0) {
            // Forward edge: this node -> next_node
            adjacency[node.id].push_back(pn.next_node_id);
            // Backward edge: next_node -> this node  
            adjacency[pn.next_node_id].push_back(node.id);
        }
    }
    
    // Add co-located edges (hub junctions where multiple paths meet at same position)
    for (Entity& node_a : path_nodes) {
        if (!node_a.has<Transform>()) continue;
        vec2 pos_a = node_a.get<Transform>().position;
        
        for (Entity& node_b : path_nodes) {
            if (node_a.id == node_b.id) continue;
            if (!node_b.has<Transform>()) continue;
            vec2 pos_b = node_b.get<Transform>().position;
            
            if (vec::distance(pos_a, pos_b) < 0.1f) {
                adjacency[node_a.id].push_back(node_b.id);
            }
        }
    }
    
    
    // For each facility type, BFS from terminal to ALL nodes
    auto facilities = EntityQuery()
        .whereHasComponent<Transform>()
        .whereHasComponent<Facility>()
        .gen();
    
    for (Entity& facility : facilities) {
        FacilityType type = facility.get<Facility>().type;
        vec2 facility_pos = facility.get<Transform>().position;
        
        // Find the terminal PathNode closest to this facility
        int terminal_node_id = -1;
        float best_dist = std::numeric_limits<float>::max();
        
        for (Entity& node : path_nodes) {
            if (!node.has<Transform>()) continue;
            float dist = vec::distance(node.get<Transform>().position, facility_pos);
            if (dist < best_dist) {
                best_dist = dist;
                terminal_node_id = node.id;
            }
        }
        
        
        if (terminal_node_id < 0) continue;
        
        // BFS from terminal node through BIDIRECTIONAL graph
        // For each node: record which neighbor leads toward the facility
        std::queue<std::pair<int, int>> bfs_queue;  // (node_id, next_toward_facility)
        std::set<int> visited;
        
        bfs_queue.push({terminal_node_id, -1});  // -1 means "arrived at facility"
        
        while (!bfs_queue.empty()) {
            auto [current_id, next_id] = bfs_queue.front();
            bfs_queue.pop();
            
            if (visited.count(current_id)) continue;
            visited.insert(current_id);
            
            auto current = EntityHelper::getEntityForID(current_id);
            if (!current.valid() || !current->has<PathSignpost>() || !current->has<Transform>()) continue;
            
            // Set the signpost: "to reach this facility type, go to next_id"
            PathSignpost& signpost = current->get<PathSignpost>();
            signpost.next_node_for[type] = next_id;
            
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
                            is_colocated = vec::distance(current_pos, neighbor_pos_it->second) < 0.1f;
                        }
                        
                        // Co-located nodes share the SAME next_id (don't point to each other)
                        // Non-co-located neighbors point to current node
                        int propagated_next = is_colocated ? next_id : current_id;
                        bfs_queue.push({neighbor_id, propagated_next});
                    }
                }
            }
        }
        
    }
    
    log_info("Calculated path signposts for {} path nodes", path_nodes.size());
}

struct CameraInputSystem : System<ProvidesCamera> {
    void for_each_with(Entity&, ProvidesCamera& cam, float dt) override {
        cam.cam.handle_input(dt);
    }
};

// Find the nearest Facility matching the agent's want
struct TargetFindingSystem : System<Transform, Agent, AgentTarget> {
    void for_each_with(Entity&, Transform& t, Agent& a, AgentTarget& target, float) override {
        // Skip if we already have a valid target
        if (target.facility_id >= 0) {
            auto existing = EntityHelper::getEntityForID(target.facility_id);
            if (existing.valid() && existing->has<Facility>()) {
                // Check if facility still matches our want
                if (existing->get<Facility>().type == a.want) {
                    return;  // Target is still valid
                }
            }
        }
        
        // Find nearest facility matching our want
        float best_dist = std::numeric_limits<float>::max();
        int best_id = -1;
        vec2 best_pos{0, 0};
        
        auto facilities = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Facility>()
            .gen();
        
        for (const Entity& facility : facilities) {
            const Facility& f = facility.get<Facility>();
            if (f.type != a.want) continue;
            
            const Transform& f_t = facility.get<Transform>();
            float dist = vec::distance(t.position, f_t.position);
            
            if (dist < best_dist) {
                best_dist = dist;
                best_id = facility.id;
                best_pos = f_t.position;
            }
        }
        
        target.facility_id = best_id;
        target.target_pos = best_pos;
    }
};

// Follow signposts - find closest node, go to next node, stay on path
struct PathFollowingSystem : System<Transform, Agent, AgentTarget, AgentSteering> {
    void for_each_with(Entity&, Transform& t, Agent& a, AgentTarget& agent_target, AgentSteering& steering, float) override {
        // Find closest PathNode - but prefer nodes whose NEXT node is closer to our target
        // This prevents oscillation at hub nodes
        float best_score = std::numeric_limits<float>::max();
        int closest_node_id = -1;
        
        auto path_nodes = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<PathNode>()
            .whereHasComponent<PathSignpost>()
            .gen();
        
        for (const Entity& node : path_nodes) {
            const Transform& node_t = node.get<Transform>();
            float dist = vec::distance(t.position, node_t.position);
            
            // Get this node's signpost
            const PathSignpost& sp = node.get<PathSignpost>();
            int next_id = sp.get_next_node(a.want);
            
            // Score: distance to node + distance from next_node to target
            // This prefers nodes that lead us TOWARD our target
            float score = dist;
            if (next_id >= 0 && agent_target.facility_id >= 0) {
                auto next_node = EntityHelper::getEntityForID(next_id);
                if (next_node.valid() && next_node->has<Transform>()) {
                    float next_to_target = vec::distance(next_node->get<Transform>().position, agent_target.target_pos);
                    score = dist + next_to_target * 0.1f;  // Small weight on path quality
                }
            }
            
            if (score < best_score) {
                best_score = score;
                closest_node_id = node.id;
            }
        }
        
        if (closest_node_id < 0) {
            steering.path_direction = {0, 0};
            return;
        }
        
        // Ask this node: "where do I go next?"
        auto closest = EntityHelper::getEntityForID(closest_node_id);
        if (!closest.valid()) {
            steering.path_direction = {0, 0};
            return;
        }
        
        const PathSignpost& signpost = closest->get<PathSignpost>();
        int next_node_id = signpost.get_next_node(a.want);
        
        // -1 means we're at the destination
        if (next_node_id < 0) {
            // Go directly to facility
            if (agent_target.facility_id >= 0) {
                vec2 dir = {agent_target.target_pos.x - t.position.x, agent_target.target_pos.y - t.position.y};
                if (vec::length(dir) > EPSILON) {
                    steering.path_direction = vec::norm(dir);
                    return;
                }
            }
            steering.path_direction = {0, 0};
            return;
        }
        
        // Go toward the next node, but stay close to the path
        auto next_node = EntityHelper::getEntityForID(next_node_id);
        auto current_node = EntityHelper::getEntityForID(closest_node_id);
        if (!next_node.valid() || !next_node->has<Transform>() || !current_node.valid()) {
            steering.path_direction = {0, 0};
            return;
        }
        
        vec2 current_pos = current_node->get<Transform>().position;
        vec2 next_pos = next_node->get<Transform>().position;
        
        // Calculate path segment
        vec2 path_vec = {next_pos.x - current_pos.x, next_pos.y - current_pos.y};
        float path_len = vec::length(path_vec);
        
        if (path_len < EPSILON) {
            // Nodes are co-located, just go to next
            vec2 dir = {next_pos.x - t.position.x, next_pos.y - t.position.y};
            if (vec::length(dir) > EPSILON) {
                steering.path_direction = vec::norm(dir);
            } else {
                steering.path_direction = {0, 0};
            }
            return;
        }
        
        vec2 path_dir = {path_vec.x / path_len, path_vec.y / path_len};
        
        // Find closest point on path segment
        vec2 to_agent = {t.position.x - current_pos.x, t.position.y - current_pos.y};
        float projection = to_agent.x * path_dir.x + to_agent.y * path_dir.y;
        projection = std::max(0.0f, std::min(path_len, projection));
        
        vec2 closest_on_path = {
            current_pos.x + path_dir.x * projection,
            current_pos.y + path_dir.y * projection
        };
        
        // How far are we from the path?
        float dist_from_path = vec::distance(t.position, closest_on_path);
        
        // Blend: pull toward path + move along path
        constexpr float PATH_PULL_RADIUS = 0.3f;  // Pull toward path within this distance
        
        vec2 final_dir;
        if (dist_from_path > PATH_PULL_RADIUS) {
            // Too far - move toward the path
            vec2 to_path = {closest_on_path.x - t.position.x, closest_on_path.y - t.position.y};
            final_dir = vec::norm(to_path);
        } else {
            // Close enough - move along path toward next node
            vec2 to_next = {next_pos.x - t.position.x, next_pos.y - t.position.y};
            final_dir = vec::norm(to_next);
        }
        
        if (vec::length(final_dir) > EPSILON) {
            steering.path_direction = vec::norm(final_dir);
        } else {
            steering.path_direction = {0, 0};
        }
    }
};

// Calculate separation forces
struct SeparationSystem : System<Transform, Agent, AgentSteering> {
    static constexpr float SEPARATION_RADIUS = 0.4f;  // Smaller radius - only push when very close
    
    void for_each_with(Entity& e, Transform& t, Agent&, AgentSteering& steering, float) override {
        vec2 force{0, 0};
        
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .whereNotID(e.id)
            .gen();
        
        for (const Entity& other : nearby) {
            const Transform& other_t = other.get<Transform>();
            float dist = vec::distance(t.position, other_t.position);
            
            if (dist < SEPARATION_RADIUS && dist > EPSILON) {
                vec2 away = {
                    t.position.x - other_t.position.x,
                    t.position.y - other_t.position.y
                };
                away = vec::norm(away);
                float strength = (1.0f - (dist / SEPARATION_RADIUS)) * 1.0f;  // Weaker force
                force.x += away.x * strength;
                force.y += away.y * strength;
            }
        }
        
        steering.separation = force;
    }
};

// Combine steering forces into final velocity - SIMPLE version
struct VelocityCombineSystem : System<Transform, AgentSteering> {
    static constexpr float MOVE_SPEED = 3.0f;
    static constexpr float SEPARATION_WEIGHT = 0.2f;  // Light separation
    
    void for_each_with(Entity&, Transform& t, AgentSteering& steering, float) override {
        // Path direction is primary, separation is secondary
        vec2 move_dir = {
            steering.path_direction.x + steering.separation.x * SEPARATION_WEIGHT,
            steering.path_direction.y + steering.separation.y * SEPARATION_WEIGHT
        };
        
        float move_len = vec::length(move_dir);
        if (move_len > EPSILON) {
            move_dir = vec::norm(move_dir);
            t.velocity.x = move_dir.x * MOVE_SPEED;
            t.velocity.y = move_dir.y * MOVE_SPEED;
        } else {
            t.velocity = {0, 0};
        }
    }
};

struct MovementSystem : System<Transform> {
    void for_each_with(Entity&, Transform& t, float dt) override {
        t.position = t.position + t.velocity * dt;
    }
};

struct AgentLifetimeSystem : System<Agent> {
    void for_each_with(Entity&, Agent& a, float dt) override {
        a.time_alive += dt;
    }
};

struct AttractionSpawnSystem : System<Transform, Attraction> {
    FacilityType random_want() {
        int r = rand() % 100;
        if (r < 40) return FacilityType::Food;
        if (r < 70) return FacilityType::Bathroom;
        return FacilityType::Stage;
    }

    void for_each_with(Entity& e, Transform& t, Attraction& a, float dt) override {
        a.spawn_timer += dt;
        
        float spawn_interval = 1.0f / a.spawn_rate;
        while (a.spawn_timer >= spawn_interval && a.current_count < a.capacity) {
            a.spawn_timer -= spawn_interval;
            
            Entity& agent = EntityHelper::createEntity();
            float offset_x = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            float offset_z = ((float)(rand() % 100) / 100.f - 0.5f) * 0.5f;
            agent.addComponent<Transform>(t.position.x + offset_x, t.position.y + offset_z);
            Agent& ag = agent.addComponent<Agent>();
            ag.origin_id = e.id;
            ag.want = random_want();
            agent.addComponent<HasStress>();
            
            // Components for targeting and steering
            agent.addComponent<AgentTarget>();
            agent.addComponent<AgentSteering>();
            
            a.current_count++;
        }
    }
};

struct FacilityAbsorptionSystem : System<Transform, Facility> {
    static constexpr float ABSORPTION_RADIUS = 1.5f;

    void for_each_with(Entity&, Transform& f_t, Facility& f, float dt) override {
        f.absorption_timer += dt;
        
        float absorb_interval = 1.0f / f.absorption_rate;
        
        auto agents = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .gen();
        
        for (Entity& agent : agents) {
            Agent& a = agent.get<Agent>();
            if (a.want != f.type) continue;
            
            Transform& a_t = agent.get<Transform>();
            float dist = vec::distance(f_t.position, a_t.position);
            
            if (dist < ABSORPTION_RADIUS && f.current_occupants < f.capacity) {
                if (f.absorption_timer >= absorb_interval) {
                    f.absorption_timer -= absorb_interval;
                    f.current_occupants++;
                    
                    auto origin = EntityHelper::getEntityForID(a.origin_id);
                    if (origin.valid() && origin->has<Attraction>()) {
                        origin->get<Attraction>().current_count--;
                    }
                    
                    EntityHelper::markIDForCleanup(agent.id);
                }
                break;
            }
        }
        
        if (f.current_occupants > 0) {
            f.current_occupants = std::max(0, f.current_occupants - 1);
        }
    }
};

struct StressBuildupSystem : System<Transform, Agent, HasStress> {
    static constexpr float CROWDING_RADIUS = 1.5f;
    static constexpr int CROWDING_THRESHOLD = 5;
    static constexpr float TIME_STRESS_THRESHOLD = 15.f;

    void for_each_with(Entity& e, Transform& t, Agent& a, HasStress& s, float dt) override {
        float stress_delta = 0.f;
        
        int nearby_count = 0;
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<Agent>()
            .whereNotID(e.id)
            .gen();
        
        for (const Entity& other : nearby) {
            float dist = vec::distance(t.position, other.get<Transform>().position);
            if (dist < CROWDING_RADIUS) {
                nearby_count++;
            }
        }
        
        if (nearby_count > CROWDING_THRESHOLD) {
            stress_delta += (nearby_count - CROWDING_THRESHOLD) * 0.1f;
        }
        
        if (a.time_alive > TIME_STRESS_THRESHOLD) {
            stress_delta += (a.time_alive - TIME_STRESS_THRESHOLD) * 0.02f;
        }
        
        s.stress += stress_delta * dt;
        s.stress = std::clamp(s.stress, 0.f, 1.f);
    }
};

struct StressSpreadSystem : System<Transform, HasStress> {
    static constexpr float SPREAD_RADIUS = 2.0f;
    static constexpr float SPREAD_RATE = 0.1f;

    void for_each_with(Entity& e, Transform& t, HasStress& s, float dt) override {
        if (s.stress < 0.1f) return;
        
        auto nearby = EntityQuery()
            .whereHasComponent<Transform>()
            .whereHasComponent<HasStress>()
            .whereNotID(e.id)
            .gen();
        
        for (Entity& other : nearby) {
            float dist = vec::distance(t.position, other.get<Transform>().position);
            if (dist < SPREAD_RADIUS && dist > EPSILON) {
                HasStress& other_s = other.get<HasStress>();
                float spread_amount = s.stress * SPREAD_RATE * (1.f - dist / SPREAD_RADIUS) * dt;
                other_s.stress = std::min(other_s.stress + spread_amount, 1.f);
            }
        }
    }
};

void register_update_systems(SystemManager& sm) {
    // Core systems
    sm.register_update_system(std::make_unique<CameraInputSystem>());
    sm.register_update_system(std::make_unique<AttractionSpawnSystem>());
    
    // Target finding and path following
    sm.register_update_system(std::make_unique<TargetFindingSystem>());
    sm.register_update_system(std::make_unique<PathFollowingSystem>());
    
    // Movement pipeline
    sm.register_update_system(std::make_unique<SeparationSystem>());
    sm.register_update_system(std::make_unique<VelocityCombineSystem>());
    sm.register_update_system(std::make_unique<MovementSystem>());
    
    // Agent lifecycle
    sm.register_update_system(std::make_unique<AgentLifetimeSystem>());
    sm.register_update_system(std::make_unique<FacilityAbsorptionSystem>());
    
    // Stress systems
    sm.register_update_system(std::make_unique<StressBuildupSystem>());
    sm.register_update_system(std::make_unique<StressSpreadSystem>());
}
