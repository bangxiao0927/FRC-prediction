#include "roles.h"

#include <cmath>
#include <vector>

#include "opr.h"

namespace {

// Pull a numeric field from an alliance's score_breakdown, returning false when
// the breakdown (or the field) is missing so the caller can skip the match.
bool breakdown_value(const nlohmann::json& match, bool red_side,
                     const char* key, double& out) {
    if (!match.contains("score_breakdown") || !match["score_breakdown"].is_object()) {
        return false;
    }
    const nlohmann::json& sb = match["score_breakdown"];
    const char* side = red_side ? "red" : "blue";
    if (!sb.contains(side) || !sb[side].is_object()) {
        return false;
    }
    const nlohmann::json& b = sb[side];
    if (!b.contains(key) || !b[key].is_number()) {
        return false;
    }
    out = b[key].get<double>();
    return true;
}

// Endgame total for the alliance. Uses the well-known per-year total key; absent
// that, contributes 0 so teleop/auto phase ratings still work.
double endgame_points(const nlohmann::json& match, bool red_side) {
    double value = 0.0;
    if (breakdown_value(match, red_side, "endGameTotalStagePoints", value)) {
        return value;
    }
    return 0.0;
}

// Field mean and sample standard deviation of a list of values.
void mean_and_stddev(const std::vector<double>& values, double& mean, double& stddev) {
    mean = 0.0;
    stddev = 0.0;
    if (values.empty()) {
        return;
    }
    for (double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());
    if (values.size() < 2) {
        return;
    }
    double sum = 0.0;
    for (double v : values) {
        const double delta = v - mean;
        sum += delta * delta;
    }
    stddev = std::sqrt(sum / static_cast<double>(values.size() - 1));
}

}  // namespace

std::map<std::string, TeamRole> compute_team_roles_before(const nlohmann::json& matches_json,
                                                          MatchFilter filter,
                                                          const nlohmann::json& target_match) {
    std::map<std::string, TeamRole> roles;

    // Offense is the standard OPR (own total score). Defense is the same solve
    // run on the OPPONENT's score, so a low value means this team holds opponents
    // down. Phase ratings decompose the alliance score into auto / teleop /
    // endgame buckets that sum back to the total (foul points aside).
    const std::map<std::string, double> offense =
        compute_team_oprs_before(matches_json, filter, target_match);
    if (offense.empty()) {
        return roles;
    }

    const AllianceValueFn defense_fn =
        [](const nlohmann::json& match, bool red_side, double& out) {
            const nlohmann::json& alliances = match["alliances"];
            const int red_score = alliances["red"].value("score", -1);
            const int blue_score = alliances["blue"].value("score", -1);
            if (red_score < 0 || blue_score < 0) {
                return false;
            }
            // Credit a side's teams with the score they let the opponent put up.
            out = static_cast<double>(red_side ? blue_score : red_score);
            return true;
        };
    const AllianceValueFn auto_fn =
        [](const nlohmann::json& match, bool red_side, double& out) {
            return breakdown_value(match, red_side, "autoPoints", out);
        };
    const AllianceValueFn endgame_fn =
        [](const nlohmann::json& match, bool red_side, double& out) {
            if (!match.contains("score_breakdown") || !match["score_breakdown"].is_object()) {
                return false;
            }
            out = endgame_points(match, red_side);
            return true;
        };
    const AllianceValueFn teleop_fn =
        [](const nlohmann::json& match, bool red_side, double& out) {
            double teleop_total = 0.0;
            if (!breakdown_value(match, red_side, "teleopPoints", teleop_total)) {
                return false;
            }
            // teleopPoints includes the endgame stage points; strip them so the
            // teleop phase reflects only in-match scoring.
            out = teleop_total - endgame_points(match, red_side);
            return true;
        };

    const std::map<std::string, double> defense =
        compute_alliance_ratings_before(matches_json, filter, target_match, defense_fn);
    const std::map<std::string, double> auto_phase =
        compute_alliance_ratings_before(matches_json, filter, target_match, auto_fn);
    const std::map<std::string, double> teleop_phase =
        compute_alliance_ratings_before(matches_json, filter, target_match, teleop_fn);
    const std::map<std::string, double> endgame_phase =
        compute_alliance_ratings_before(matches_json, filter, target_match, endgame_fn);

    const bool has_phase = !auto_phase.empty() || !endgame_phase.empty();

    for (const auto& entry : offense) {
        const std::string& team = entry.first;
        TeamRole role;
        role.offense = entry.second;
        auto find = [&team](const std::map<std::string, double>& m) {
            auto it = m.find(team);
            return it == m.end() ? 0.0 : it->second;
        };
        role.auto_phase = find(auto_phase);
        role.teleop_phase = find(teleop_phase);
        role.endgame_phase = find(endgame_phase);
        role.defense = find(defense);
        role.has_phase_data = has_phase;
        roles.emplace(team, role);
    }

    // Label a primary role by how far each specialization stands out from the
    // field. Auto and endgame reward high values; defense rewards a low DPR.
    std::vector<double> auto_values;
    std::vector<double> endgame_values;
    std::vector<double> defense_values;
    auto_values.reserve(roles.size());
    endgame_values.reserve(roles.size());
    defense_values.reserve(roles.size());
    for (const auto& entry : roles) {
        auto_values.push_back(entry.second.auto_phase);
        endgame_values.push_back(entry.second.endgame_phase);
        defense_values.push_back(entry.second.defense);
    }
    double auto_mean = 0.0, auto_std = 0.0;
    double endgame_mean = 0.0, endgame_std = 0.0;
    double defense_mean = 0.0, defense_std = 0.0;
    mean_and_stddev(auto_values, auto_mean, auto_std);
    mean_and_stddev(endgame_values, endgame_mean, endgame_std);
    mean_and_stddev(defense_values, defense_mean, defense_std);

    const double threshold = 0.6;  // z-score needed to claim a specialization
    for (auto& entry : roles) {
        TeamRole& role = entry.second;
        const double auto_z = auto_std > 0.0 ? (role.auto_phase - auto_mean) / auto_std : 0.0;
        const double endgame_z =
            endgame_std > 0.0 ? (role.endgame_phase - endgame_mean) / endgame_std : 0.0;
        // Defense stands out when a team suppresses opponents (DPR below the mean).
        const double defense_z =
            defense_std > 0.0 ? (defense_mean - role.defense) / defense_std : 0.0;

        role.primary = "offense";
        double best = threshold;
        if (has_phase && auto_z > best) {
            best = auto_z;
            role.primary = "auto";
        }
        if (has_phase && endgame_z > best) {
            best = endgame_z;
            role.primary = "endgame";
        }
        if (defense_z > best) {
            best = defense_z;
            role.primary = "defense";
        }
    }

    return roles;
}

std::map<std::string, TeamRole> compute_team_roles(const nlohmann::json& matches_json,
                                                   MatchFilter filter) {
    return compute_team_roles_before(matches_json, filter, nlohmann::json(nullptr));
}
