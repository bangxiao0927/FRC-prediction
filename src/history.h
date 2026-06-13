#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "stats.h"

// A team's prior-season form, summarized as an average per-team scoring level.
struct TeamForm {
    double per_team_score = 0.0;  // mean (alliance score / alliance size) the team posted
    int matches = 0;              // how many prior matches fed the average
};

// Summarize a team's season form from its full-season match list. Only counts
// matches that are NOT at exclude_event_key, and (when before_time > 0) that
// were played strictly before before_time. This keeps cross-event history
// leak-free: a prediction never sees the current event or anything later.
TeamForm compute_team_form(const nlohmann::json& team_season_matches,
                           const std::string& team_key,
                           const std::string& exclude_event_key,
                           double before_time);

// Blend current-event OPR with a historical per-team prior, weighted by how much
// current-event data each team has: a team with >= confidence_match_count played
// matches is trusted entirely on current form, while a team with no current data
// falls back to its history. Teams present in only one source pass through.
std::map<std::string, double> blend_oprs(
    const std::map<std::string, double>& current_oprs,
    const std::map<std::string, double>& historical_priors,
    const std::map<std::string, TeamStats>& current_stats,
    int confidence_match_count);
