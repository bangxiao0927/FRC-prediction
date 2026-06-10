#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

struct TeamStats {
    int matches_played = 0;
    int total_score = 0;
    double average_score = 0.0;
};

enum class MatchFilter {
    AllPlayed,
    QualificationOnly,
    QualificationPlusElimPlayed
};

std::map<std::string, TeamStats> compute_team_stats(const nlohmann::json& matches_json,
                                                    MatchFilter filter);
double compute_event_average_score(const std::map<std::string, TeamStats>& team_stats);
