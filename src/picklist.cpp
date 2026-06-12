#include "picklist.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace {

bool is_qualification(const nlohmann::json& match) {
    return match.value("comp_level", "") == "qm";
}

bool is_elimination(const nlohmann::json& match) {
    const std::string level = match.value("comp_level", "");
    return level == "qf" || level == "sf" || level == "f";
}

bool passes_filter(const nlohmann::json& match, MatchFilter filter) {
    switch (filter) {
        case MatchFilter::QualificationOnly:
            return is_qualification(match);
        case MatchFilter::QualificationPlusElimPlayed:
            return is_qualification(match) || is_elimination(match);
        case MatchFilter::AllPlayed:
        default:
            return true;
    }
}

void add_alliance(std::map<std::string, std::vector<int>>& scores,
                  const nlohmann::json& alliance,
                  int score,
                  const std::set<std::string>& exclude) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return;
    }
    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        std::string team_key = team_key_value.get<std::string>();
        if (exclude.count(team_key) > 0) {
            continue;
        }
        scores[team_key].push_back(score);
    }
}

double mean_of(const std::vector<int>& values, size_t begin, size_t end) {
    if (end <= begin) {
        return 0.0;
    }
    double total = 0.0;
    for (size_t i = begin; i < end; ++i) {
        total += static_cast<double>(values[i]);
    }
    return total / static_cast<double>(end - begin);
}

double population_stddev(const std::vector<int>& values, double mean) {
    if (values.size() < 2) {
        return 0.0;
    }
    double sum_sq = 0.0;
    for (int value : values) {
        const double diff = static_cast<double>(value) - mean;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / static_cast<double>(values.size()));
}

// Min-max normalize to 0..1; if every value is equal, return 0.5 for all.
std::vector<double> normalize(const std::vector<double>& values) {
    std::vector<double> out(values.size(), 0.5);
    if (values.empty()) {
        return out;
    }
    const double min_value = *std::min_element(values.begin(), values.end());
    const double max_value = *std::max_element(values.begin(), values.end());
    const double range = max_value - min_value;
    if (range <= 0.0) {
        return out;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        out[i] = (values[i] - min_value) / range;
    }
    return out;
}

}  // namespace

std::vector<PicklistEntry> compute_picklist(const nlohmann::json& matches_json,
                                            MatchFilter filter,
                                            const nlohmann::json& before_match,
                                            const std::set<std::string>& exclude,
                                            const PicklistWeights& weights,
                                            int confidence_match_count) {
    std::vector<PicklistEntry> result;
    if (!matches_json.is_array()) {
        return result;
    }

    const bool has_cutoff = before_match.is_object();
    const MatchOrderKey cutoff_key =
        has_cutoff ? match_order_key(before_match) : MatchOrderKey{};

    // Collect played, in-scope matches and order them chronologically so the
    // trend (recent vs early) is meaningful.
    std::vector<std::pair<MatchOrderKey, const nlohmann::json*>> ordered;
    for (const auto& match : matches_json) {
        if (!match.contains("alliances") || !match["alliances"].is_object()) {
            continue;
        }
        const nlohmann::json& alliances = match["alliances"];
        if (!alliances.contains("red") || !alliances.contains("blue")) {
            continue;
        }
        if (!passes_filter(match, filter)) {
            continue;
        }
        if (has_cutoff && !match_order_before(match_order_key(match), cutoff_key)) {
            continue;
        }
        if (alliances["red"].value("score", -1) < 0 ||
            alliances["blue"].value("score", -1) < 0) {
            continue;
        }
        ordered.emplace_back(match_order_key(match), &match);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) {
                  return match_order_before(left.first, right.first);
              });

    std::map<std::string, std::vector<int>> team_scores;
    for (const auto& item : ordered) {
        const nlohmann::json& alliances = (*item.second)["alliances"];
        add_alliance(team_scores, alliances["red"], alliances["red"].value("score", 0), exclude);
        add_alliance(team_scores, alliances["blue"], alliances["blue"].value("score", 0), exclude);
    }

    if (team_scores.empty()) {
        return result;
    }

    const double safe_confidence_count =
        confidence_match_count > 0 ? static_cast<double>(confidence_match_count) : 1.0;

    // First pass: raw per-team metrics.
    std::vector<PicklistEntry> entries;
    std::vector<double> strength_raw;
    std::vector<double> consistency_raw;  // negative stddev (higher is steadier)
    std::vector<double> trend_raw;
    entries.reserve(team_scores.size());
    for (const auto& pair : team_scores) {
        const std::vector<int>& scores = pair.second;
        PicklistEntry entry;
        entry.team_key = pair.first;
        entry.matches = static_cast<int>(scores.size());
        entry.average_score = mean_of(scores, 0, scores.size());
        entry.stddev = population_stddev(scores, entry.average_score);
        if (scores.size() >= 2) {
            const size_t half = scores.size() / 2;
            entry.trend = mean_of(scores, half, scores.size()) - mean_of(scores, 0, half);
        }
        entry.confidence = std::clamp(static_cast<double>(entry.matches) / safe_confidence_count,
                                      0.0, 1.0);

        strength_raw.push_back(entry.average_score);
        consistency_raw.push_back(-entry.stddev);
        trend_raw.push_back(entry.trend);
        entries.push_back(std::move(entry));
    }

    const std::vector<double> strength_n = normalize(strength_raw);
    const std::vector<double> consistency_n = normalize(consistency_raw);
    const std::vector<double> trend_n = normalize(trend_raw);

    const double weight_sum =
        weights.strength + weights.consistency + weights.trend;
    const double w_strength = weight_sum > 0.0 ? weights.strength / weight_sum : 0.0;
    const double w_consistency = weight_sum > 0.0 ? weights.consistency / weight_sum : 0.0;
    const double w_trend = weight_sum > 0.0 ? weights.trend / weight_sum : 0.0;

    for (size_t i = 0; i < entries.size(); ++i) {
        const double base = w_strength * strength_n[i] +
                            w_consistency * consistency_n[i] +
                            w_trend * trend_n[i];
        // Damp by confidence so a team with one lucky match cannot top the list.
        entries[i].picklist_score = base * (0.5 + 0.5 * entries[i].confidence);
    }

    std::sort(entries.begin(), entries.end(),
              [](const PicklistEntry& left, const PicklistEntry& right) {
                  return left.picklist_score > right.picklist_score;
              });

    return entries;
}
