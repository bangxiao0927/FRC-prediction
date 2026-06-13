#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "stats.h"

// Offensive Power Rating (OPR): a per-team scoring contribution estimated by
// least squares over alliance scores. For every played alliance we assert that
// the sum of its members' ratings equals the alliance score, then solve the
// resulting (regularized) linear system. Unlike summing each team's alliance
// average, OPR isolates an individual contribution, so an alliance estimate is
// just the sum of its members' OPRs and is directly comparable to a real score.
std::map<std::string, double> compute_team_oprs(const nlohmann::json& matches_json,
                                                MatchFilter filter);

// Same as compute_team_oprs but only uses matches scheduled strictly before the
// target match, so a prediction never sees the match it is predicting or any
// later match (no future-data leakage).
std::map<std::string, double> compute_team_oprs_before(const nlohmann::json& matches_json,
                                                       MatchFilter filter,
                                                       const nlohmann::json& target_match);
