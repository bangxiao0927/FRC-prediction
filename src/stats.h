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

// Schedule ordering key for a match. Lower sorts earlier: qualification before
// playoffs, then by set and match number.
struct MatchOrderKey {
    int level = 0;
    int set_number = 0;
    int match_number = 0;
};

MatchOrderKey match_order_key(const nlohmann::json& match);
bool match_order_before(const MatchOrderKey& a, const MatchOrderKey& b);

std::map<std::string, TeamStats> compute_team_stats(const nlohmann::json& matches_json,
                                                    MatchFilter filter);

// Same as compute_team_stats but only counts matches scheduled strictly before
// the target match, so a prediction never sees data from the match it is
// predicting or anything later (no future-data leakage).
std::map<std::string, TeamStats> compute_team_stats_before(const nlohmann::json& matches_json,
                                                           MatchFilter filter,
                                                           const nlohmann::json& target_match);

double compute_event_average_score(const std::map<std::string, TeamStats>& team_stats);
