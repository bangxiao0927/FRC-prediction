#include "predictor.h"

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

}  // namespace

MatchPrediction predict_match(const nlohmann::json& match_json,
                              const std::map<std::string, TeamStats>& team_stats) {
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

    prediction.red_score_estimate = average_alliance_score(red, team_stats);
    prediction.blue_score_estimate = average_alliance_score(blue, team_stats);

    const double score_diff = prediction.red_score_estimate - prediction.blue_score_estimate;
    const double red_prob = sigmoid(score_diff / 10.0);
    prediction.red_win_probability = red_prob;
    prediction.blue_win_probability = 1.0 - red_prob;

    return prediction;
}
