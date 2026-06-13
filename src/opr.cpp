#include "opr.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

bool is_qualification_match(const nlohmann::json& match) {
    return match.value("comp_level", "") == "qm";
}

bool is_elimination_match(const nlohmann::json& match) {
    const std::string level = match.value("comp_level", "");
    return level == "qf" || level == "sf" || level == "f";
}

bool should_include_match(const nlohmann::json& match, MatchFilter filter) {
    if (filter == MatchFilter::AllPlayed) {
        return true;
    }
    if (filter == MatchFilter::QualificationOnly) {
        return is_qualification_match(match);
    }
    if (filter == MatchFilter::QualificationPlusElimPlayed) {
        return is_qualification_match(match) || is_elimination_match(match);
    }
    return true;
}

// One scored alliance: the indices of its member teams plus the score it put up.
struct Observation {
    std::vector<int> team_indices;
    double score = 0.0;
};

// Collect the alliance team keys, returning false if the alliance is malformed.
bool collect_team_keys(const nlohmann::json& alliance, std::vector<std::string>& out) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return false;
    }
    for (const auto& key_value : alliance["team_keys"]) {
        if (key_value.is_string()) {
            out.push_back(key_value.get<std::string>());
        }
    }
    return !out.empty();
}

// Solve the symmetric positive-definite system M x = v in place using Gaussian
// elimination with partial pivoting. M is regularized before this call, so it
// is well-conditioned even when the raw OPR system is rank deficient.
std::vector<double> solve_linear_system(std::vector<std::vector<double>> matrix,
                                        std::vector<double> rhs) {
    const size_t n = rhs.size();
    for (size_t col = 0; col < n; ++col) {
        size_t pivot = col;
        double best = std::abs(matrix[col][col]);
        for (size_t row = col + 1; row < n; ++row) {
            const double candidate = std::abs(matrix[row][col]);
            if (candidate > best) {
                best = candidate;
                pivot = row;
            }
        }
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
            std::swap(rhs[pivot], rhs[col]);
        }
        const double diagonal = matrix[col][col];
        if (std::abs(diagonal) < 1e-12) {
            continue;  // Regularization should prevent this; stay safe regardless.
        }
        for (size_t row = col + 1; row < n; ++row) {
            const double factor = matrix[row][col] / diagonal;
            if (factor == 0.0) {
                continue;
            }
            for (size_t k = col; k < n; ++k) {
                matrix[row][k] -= factor * matrix[col][k];
            }
            rhs[row] -= factor * rhs[col];
        }
    }

    std::vector<double> solution(n, 0.0);
    for (size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (size_t k = i + 1; k < n; ++k) {
            sum -= matrix[i][k] * solution[k];
        }
        const double diagonal = matrix[i][i];
        solution[i] = std::abs(diagonal) < 1e-12 ? 0.0 : sum / diagonal;
    }
    return solution;
}

}  // namespace

std::map<std::string, double> compute_alliance_ratings_before(
    const nlohmann::json& matches_json,
    MatchFilter filter,
    const nlohmann::json& target_match,
    const AllianceValueFn& value_fn) {
    std::map<std::string, double> ratings;
    if (!matches_json.is_array() || !value_fn) {
        return ratings;
    }

    const bool has_cutoff = target_match.is_object();
    const MatchOrderKey target_key =
        has_cutoff ? match_order_key(target_match) : MatchOrderKey{};

    std::map<std::string, int> team_index;
    std::vector<std::string> team_keys;
    std::vector<Observation> observations;
    double total_value = 0.0;
    double total_team_slots = 0.0;

    auto index_for = [&](const std::string& key) {
        auto it = team_index.find(key);
        if (it != team_index.end()) {
            return it->second;
        }
        const int index = static_cast<int>(team_keys.size());
        team_index.emplace(key, index);
        team_keys.push_back(key);
        return index;
    };

    auto add_alliance = [&](const nlohmann::json& alliance, double value) {
        std::vector<std::string> keys;
        if (!collect_team_keys(alliance, keys)) {
            return;
        }
        Observation obs;
        obs.score = value;
        for (const auto& key : keys) {
            obs.team_indices.push_back(index_for(key));
        }
        observations.push_back(std::move(obs));
        total_value += value;
        total_team_slots += static_cast<double>(keys.size());
    };

    for (const auto& match : matches_json) {
        if (!match.contains("alliances") || !match["alliances"].is_object()) {
            continue;
        }
        const nlohmann::json& alliances = match["alliances"];
        if (!alliances.contains("red") || !alliances.contains("blue")) {
            continue;
        }
        if (!should_include_match(match, filter)) {
            continue;
        }
        if (has_cutoff && !match_order_before(match_order_key(match), target_key)) {
            continue;
        }

        // The value attributed to each alliance is supplied by the caller, so the
        // same solve produces offense OPR, per-phase OPR, or a defensive rating.
        double red_value = 0.0;
        double blue_value = 0.0;
        if (value_fn(match, true, red_value)) {
            add_alliance(alliances["red"], red_value);
        }
        if (value_fn(match, false, blue_value)) {
            add_alliance(alliances["blue"], blue_value);
        }
    }

    const size_t n = team_keys.size();
    if (n == 0 || observations.empty()) {
        return ratings;
    }

    // Ridge-to-prior regularization. The prior is the average single-team
    // contribution (mean alliance score / mean alliance size). Sparse teams are
    // pulled toward this average team instead of toward zero, mirroring the
    // confidence shrink used elsewhere and keeping the system solvable when only
    // a handful of matches have been played.
    const double mean_alliance_size = total_team_slots > 0.0
        ? total_team_slots / static_cast<double>(observations.size())
        : 3.0;
    const double mean_alliance_value = total_value / static_cast<double>(observations.size());
    const double prior = mean_alliance_size > 0.0 ? mean_alliance_value / mean_alliance_size : 0.0;
    const double lambda = 1.0;

    std::vector<std::vector<double>> normal(n, std::vector<double>(n, 0.0));
    std::vector<double> rhs(n, 0.0);
    for (const auto& obs : observations) {
        for (const int i : obs.team_indices) {
            rhs[i] += obs.score;
            for (const int j : obs.team_indices) {
                normal[i][j] += 1.0;
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        normal[i][i] += lambda;
        rhs[i] += lambda * prior;
    }

    const std::vector<double> solution = solve_linear_system(std::move(normal), std::move(rhs));
    for (size_t i = 0; i < n; ++i) {
        ratings[team_keys[i]] = solution[i];
    }
    return ratings;
}

std::map<std::string, double> compute_team_oprs_before(const nlohmann::json& matches_json,
                                                       MatchFilter filter,
                                                       const nlohmann::json& target_match) {
    // OPR attributes each alliance's own total score to its members. Both scores
    // must be present so the match is a valid, fully-scored observation.
    const AllianceValueFn own_score =
        [](const nlohmann::json& match, bool red_side, double& out) {
            const nlohmann::json& alliances = match["alliances"];
            const int red_score = alliances["red"].value("score", -1);
            const int blue_score = alliances["blue"].value("score", -1);
            if (red_score < 0 || blue_score < 0) {
                return false;
            }
            out = static_cast<double>(red_side ? red_score : blue_score);
            return true;
        };
    return compute_alliance_ratings_before(matches_json, filter, target_match, own_score);
}

std::map<std::string, double> compute_team_oprs(const nlohmann::json& matches_json,
                                                MatchFilter filter) {
    return compute_team_oprs_before(matches_json, filter, nlohmann::json(nullptr));
}
