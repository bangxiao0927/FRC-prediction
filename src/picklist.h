#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stats.h"

struct PicklistWeights {
    double strength = 0.5;
    double consistency = 0.3;
    double trend = 0.2;
    double complement = 0.2;
    double overlap = 0.1;
};

struct TeamPerformance {
    int matches_played = 0;
    double average_score = 0.0;
    double std_dev = 0.0;
    double recent_average = 0.0;
};

struct PicklistEntry {
    std::string team_key;
    double picklist_score = 0.0;
    double average_score = 0.0;
    double stddev = 0.0;
    double trend = 0.0;
    int matches = 0;
    double confidence = 0.0;
    double complement = 0.0;
    double overlap_penalty = 0.0;
};

std::vector<PicklistEntry> compute_picklist(
    const nlohmann::json& matches_json,
    MatchFilter filter,
    const nlohmann::json& before_match,
    const std::set<std::string>& exclude,
    const PicklistWeights& weights,
    int confidence_match_count,
    const std::string& my_team_key);
