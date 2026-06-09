#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stats.h"

struct MatchPrediction {
    std::vector<std::string> red_teams;
    std::vector<std::string> blue_teams;
    double red_score_estimate = 0.0;
    double blue_score_estimate = 0.0;
    double red_win_probability = 0.5;
    double blue_win_probability = 0.5;
    double red_confidence = 0.0;
    double blue_confidence = 0.0;
};

MatchPrediction predict_match(const nlohmann::json& match_json,
                              const std::map<std::string, TeamStats>& team_stats);
