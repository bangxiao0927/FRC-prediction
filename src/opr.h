#pragma once

#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "stats.h"

// Attribute a per-alliance value to that alliance's member teams. `red_side` is
// true for the red alliance, false for blue. Fill `out` with the value to credit
// to the named alliance's teams and return true, or return false to skip this
// alliance (e.g. missing score or breakdown). This generalizes the OPR solve so
// the same least-squares machinery can produce offense OPR, per-phase OPR, or a
// defensive rating (by attributing the opponent's score).
using AllianceValueFn =
    std::function<bool(const nlohmann::json& match, bool red_side, double& out)>;

// Generic ridge-regularized least-squares rating over the chosen, cutoff-aware
// matches. See AllianceValueFn for how each alliance contributes an observation.
std::map<std::string, double> compute_alliance_ratings_before(
    const nlohmann::json& matches_json,
    MatchFilter filter,
    const nlohmann::json& target_match,
    const AllianceValueFn& value_fn);

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
