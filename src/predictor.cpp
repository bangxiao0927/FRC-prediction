#include "predictor.h"

#include <algorithm>
#include <cmath>

namespace {

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}

double average_alliance_score(const nlohmann::json& alliance,
                              const std::map<std::string, TeamStats>& team_stats) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return 0.0;
    }

    double total = 0.0;
    int count = 0;
    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        std::string team_key = team_key_value.get<std::string>();
        auto it = team_stats.find(team_key);
        if (it == team_stats.end()) {
            continue;
        }
        total += it->second.average_score;
        count += 1;
    }

    if (count == 0) {
        return 0.0;
    }

    return total / static_cast<double>(count);
}

std::vector<std::string> get_team_keys(const nlohmann::json& alliance) {
    std::vector<std::string> teams;
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return teams;
    }

    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        teams.push_back(team_key_value.get<std::string>());
    }

    return teams;
}

double alliance_confidence(const nlohmann::json& alliance,
                           const std::map<std::string, TeamStats>& team_stats,
                           int confidence_match_count) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return 0.0;
    }

    double total_matches = 0.0;
    int count = 0;
    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        std::string team_key = team_key_value.get<std::string>();
        auto it = team_stats.find(team_key);
        if (it == team_stats.end()) {
            continue;
        }
        total_matches += static_cast<double>(it->second.matches_played);
        count += 1;
    }

    if (count == 0) {
        return 0.0;
    }

    double average_matches = total_matches / static_cast<double>(count);
    double confidence = average_matches / static_cast<double>(confidence_match_count);
    return std::clamp(confidence, 0.0, 1.0);
}

}  // namespace

MatchPrediction predict_match(const nlohmann::json& match_json,
                              const std::map<std::string, TeamStats>& team_stats,
                              int confidence_match_count,
                              double score_diff_scale,
                              double sigmoid_scale) {
    MatchPrediction prediction;
    if (!match_json.contains("alliances") || !match_json["alliances"].is_object()) {
        return prediction;
    }

    const nlohmann::json& alliances = match_json["alliances"];
    if (!alliances.contains("red") || !alliances.contains("blue")) {
        return prediction;
    }

    const nlohmann::json& red = alliances["red"];
    const nlohmann::json& blue = alliances["blue"];

    prediction.red_teams = get_team_keys(red);
    prediction.blue_teams = get_team_keys(blue);

    prediction.red_score_estimate = average_alliance_score(red, team_stats);
    prediction.blue_score_estimate = average_alliance_score(blue, team_stats);
    prediction.red_score_total_estimate = prediction.red_score_estimate * prediction.red_teams.size();
    prediction.blue_score_total_estimate = prediction.blue_score_estimate * prediction.blue_teams.size();
    prediction.score_diff_estimate = prediction.red_score_total_estimate - prediction.blue_score_total_estimate;
    prediction.red_confidence = alliance_confidence(red, team_stats, confidence_match_count);
    prediction.blue_confidence = alliance_confidence(blue, team_stats, confidence_match_count);

    const double red_prob = sigmoid((prediction.score_diff_estimate / score_diff_scale) * sigmoid_scale);
    prediction.red_win_probability = red_prob;
    prediction.blue_win_probability = 1.0 - red_prob;

    return prediction;
}
