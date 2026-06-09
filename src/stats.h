#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

struct TeamStats {
    int matches_played = 0;
    int total_score = 0;
    double average_score = 0.0;
};

std::map<std::string, TeamStats> compute_team_stats(const nlohmann::json& matches_json);
