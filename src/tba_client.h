#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "cache.h"

class TbaClient {
public:
    TbaClient(std::string auth_key, std::string cache_dir, int cache_ttl_seconds);

    nlohmann::json get_status();
    nlohmann::json get_event(const std::string& event_key);
    nlohmann::json get_event_matches(const std::string& event_key);
    nlohmann::json get_event_rankings(const std::string& event_key);
    nlohmann::json get_event_teams(const std::string& event_key);

private:
    nlohmann::json get_json(const std::string& path);

    std::string auth_key_;
    std::string base_url_;
    int cache_ttl_seconds_;
    FileCache cache_;
};
