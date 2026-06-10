#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stats.h"

struct MatchPrediction {
    std::vector<std::string> red_teams;
    std::vector<std::string> blue_teams;
    int red_team_count = 0;
    int blue_team_count = 0;
    int red_total_matches = 0;
    int blue_total_matches = 0;
    double red_average_matches = 0.0;
    double blue_average_matches = 0.0;
    double red_score_estimate = 0.0;
    double blue_score_estimate = 0.0;
    double red_score_total_estimate = 0.0;
    double blue_score_total_estimate = 0.0;
    double score_diff_estimate = 0.0;
    double red_win_probability = 0.5;
    double blue_win_probability = 0.5;
    double red_confidence = 0.0;
    double blue_confidence = 0.0;
};

MatchPrediction predict_match(const nlohmann::json& match_json,
                              const std::map<std::string, TeamStats>& team_stats,
                              int confidence_match_count,
                              double score_diff_scale,
                              double sigmoid_scale);
