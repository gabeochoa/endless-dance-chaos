
#include "random_engine.h"

#include <functional>

RandomEngine RandomEngine::instance;
bool RandomEngine::created = false;

void RandomEngine::create() {
    if (!created) {
        instance._set_seed("default_seed");
        created = true;
    }
}

RandomEngine& RandomEngine::get() {
    if (!created) {
        create();
    }
    return instance;
}

void RandomEngine::set_seed(const std::string& new_seed) {
    instance._set_seed(new_seed);
}

void RandomEngine::_set_seed(const std::string& new_seed) {
    seed = new_seed;
    hashed_seed = std::hash<std::string>{}(seed);
    rng_engine.seed(hashed_seed);
    rng_std.seed(static_cast<unsigned int>(hashed_seed));
}

bool RandomEngine::get_bool() { return int_dist(rng_std) % 2 == 0; }

int RandomEngine::get_sign() { return get_bool() ? 1 : -1; }

std::string RandomEngine::get_string(int length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::string result;
    result.reserve(length);

    for (int i = 0; i < length; ++i) {
        result += alphanum[get_int(0, sizeof(alphanum) - 2)];
    }

    return result;
}

int RandomEngine::get_int(int a, int b) {
    if (a > b) std::swap(a, b);
    std::uniform_int_distribution<int> dist(a, b);
    return dist(rng_std);
}

float RandomEngine::get_float(float a, float b) {
    if (a > b) std::swap(a, b);
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng_std);
}

vec2 RandomEngine::get_vec(float mn, float mx) {
    return vec2{get_float(mn, mx), get_float(mn, mx)};
}

vec2 RandomEngine::get_vec(float mn_a, float mx_a, float mn_b, float mx_b) {
    return vec2{get_float(mn_a, mx_a), get_float(mn_b, mx_b)};
}

std::mt19937& RandomEngine::rng() { return instance.rng_std; }
