#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "stats.h"

// A team's tactical profile, each value a least-squares contribution estimate.
// The phase ratings reuse the OPR machinery on per-phase alliance points, so
// they isolate one team's auto / teleop / endgame output. `defense` is a
// Defensive Power Rating: the team's share of the OPPONENT alliance's score, so
// a lower number means opponents scored less with this team on the field.
struct TeamRole {
    double offense = 0.0;      // total scoring contribution (OPR)
    double auto_phase = 0.0;   // autonomous points contribution
    double teleop_phase = 0.0; // teleop scoring contribution (excludes endgame)
    double endgame_phase = 0.0;// endgame points contribution
    double defense = 0.0;      // DPR; lower is stronger defense
    bool has_phase_data = false; // false when score_breakdown was unavailable
    std::string primary;       // "offense" | "auto" | "endgame" | "defense"
};

// Compute role profiles for every team that played in the selected matches,
// honoring the same schedule cutoff as the rest of the model (no future leakage).
std::map<std::string, TeamRole> compute_team_roles_before(const nlohmann::json& matches_json,
                                                          MatchFilter filter,
                                                          const nlohmann::json& target_match);

std::map<std::string, TeamRole> compute_team_roles(const nlohmann::json& matches_json,
                                                   MatchFilter filter);
