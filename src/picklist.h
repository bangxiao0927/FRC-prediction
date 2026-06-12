#pragma once

#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stats.h"

// Relative weights for the picklist ranking. They are normalized internally, so
// only their ratios matter.
struct PicklistWeights {
    double strength = 0.5;     // higher average score is better
    double consistency = 0.3;  // lower score variance is better
    double trend = 0.2;        // improving over the event is better
};

// One ranked team in the picklist.
struct PicklistEntry {
    std::string team_key;
    int matches = 0;
    double average_score = 0.0;   // mean alliance score in the team's matches
    double stddev = 0.0;          // spread of those scores (consistency proxy)
    double trend = 0.0;           // recent-half mean minus early-half mean
    double confidence = 0.0;      // matches / confidence_match_count, clamped 0..1
    double picklist_score = 0.0;  // final 0..1 ranking score
};

// Ranks teams for alliance selection from match scores.
//   filter        : which matches to consider (qualification, etc.)
//   before_match  : if an object, only count matches scheduled before it
//   exclude       : team keys to drop (already picked, your own team, ...)
//   weights       : strength / consistency / trend mix
//   confidence_match_count : matches needed for full confidence
std::vector<PicklistEntry> compute_picklist(const nlohmann::json& matches_json,
                                            MatchFilter filter,
                                            const nlohmann::json& before_match,
                                            const std::set<std::string>& exclude,
                                            const PicklistWeights& weights,
                                            int confidence_match_count);
