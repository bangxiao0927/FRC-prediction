#pragma once

#include <string>

#include <nlohmann/json.hpp>

class TbaClient {
public:
    explicit TbaClient(std::string auth_key);

    nlohmann::json get_status();
    nlohmann::json get_event(const std::string& event_key);
    nlohmann::json get_event_matches(const std::string& event_key);
    nlohmann::json get_event_rankings(const std::string& event_key);
    nlohmann::json get_event_teams(const std::string& event_key);

private:
    nlohmann::json get_json(const std::string& path);

    std::string auth_key_;
    std::string base_url_;
};
