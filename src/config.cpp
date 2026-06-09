#include "config.h"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

Config load_from_environment() {
    Config config;
    const char* api_key = std::getenv("TBA_AUTH_KEY");
    if (api_key) {
        config.tba_auth_key = api_key;
    }
    return config;
}

Config load_from_file(const std::string& path) {
    Config config;
    std::ifstream config_file(path);
    if (!config_file) {
        return config;
    }

    nlohmann::json json = nlohmann::json::parse(config_file, nullptr, false);
    if (json.is_discarded()) {
        return config;
    }

    config.tba_auth_key = json.value("tba_auth_key", "");
    config.default_event_key = json.value("default_event_key", "");
    config.cache_dir = json.value("cache_dir", "");
    config.cache_ttl_seconds = json.value("cache_ttl_seconds", 60);
    config.confidence_match_count = json.value("confidence_match_count", 6);
    config.score_diff_scale = json.value("score_diff_scale", 30.0);
    config.sigmoid_scale = json.value("sigmoid_scale", 1.0);
    config.model_version = json.value("model_version", "baseline-v1");
    return config;
}

Config merge_config(const Config& primary, const Config& fallback) {
    Config merged = fallback;
    if (!primary.tba_auth_key.empty()) {
        merged.tba_auth_key = primary.tba_auth_key;
    }
    if (!primary.default_event_key.empty()) {
        merged.default_event_key = primary.default_event_key;
    }
    if (!primary.cache_dir.empty()) {
        merged.cache_dir = primary.cache_dir;
    }
    return merged;
}

}  // namespace

Config load_config() {
    Config file_config = load_from_file("config.json");
    Config env_config = load_from_environment();
    Config merged = merge_config(env_config, file_config);

    if (merged.default_event_key.empty()) {
        merged.default_event_key = "2024casj";
    }
    if (merged.cache_dir.empty()) {
        merged.cache_dir = "data/cache";
    }
    if (merged.cache_ttl_seconds <= 0) {
        merged.cache_ttl_seconds = 60;
    }
    if (merged.confidence_match_count <= 0) {
        merged.confidence_match_count = 6;
    }
    if (merged.score_diff_scale <= 0.0) {
        merged.score_diff_scale = 30.0;
    }
    if (merged.sigmoid_scale <= 0.0) {
        merged.sigmoid_scale = 1.0;
    }
    if (merged.model_version.empty()) {
        merged.model_version = "baseline-v1";
    }

    return merged;
}
