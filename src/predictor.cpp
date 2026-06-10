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

double event_average_score(const std::map<std::string, TeamStats>& team_stats) {
    double total_score = 0.0;
    int total_matches = 0;
    for (const auto& entry : team_stats) {
        total_score += static_cast<double>(entry.second.total_score);
        total_matches += entry.second.matches_played;
    }

    if (total_matches == 0) {
        return 0.0;
    }

    return total_score / static_cast<double>(total_matches);
}

struct AllianceSample {
    std::vector<std::string> teams;
    int team_count = 0;
    int total_matches = 0;
    double average_matches = 0.0;
    double confidence = 0.0;
};

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

AllianceSample build_alliance_sample(const nlohmann::json& alliance,
                                     const std::map<std::string, TeamStats>& team_stats,
                                     int confidence_match_count) {
    AllianceSample sample;
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return sample;
    }

    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        std::string team_key = team_key_value.get<std::string>();
        sample.teams.push_back(team_key);
        auto it = team_stats.find(team_key);
        if (it == team_stats.end()) {
            continue;
        }
        sample.total_matches += it->second.matches_played;
        sample.team_count += 1;
    }

    if (sample.team_count == 0) {
        return sample;
    }

    sample.average_matches = static_cast<double>(sample.total_matches)
        / static_cast<double>(sample.team_count);
    sample.confidence = sample.average_matches / static_cast<double>(confidence_match_count);
    sample.confidence = std::clamp(sample.confidence, 0.0, 1.0);
    return sample;
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

    AllianceSample red_sample = build_alliance_sample(red, team_stats, confidence_match_count);
    AllianceSample blue_sample = build_alliance_sample(blue, team_stats, confidence_match_count);
    prediction.red_teams = red_sample.teams;
    prediction.blue_teams = blue_sample.teams;
    prediction.red_team_count = red_sample.team_count;
    prediction.blue_team_count = blue_sample.team_count;
    prediction.red_total_matches = red_sample.total_matches;
    prediction.blue_total_matches = blue_sample.total_matches;
    prediction.red_average_matches = red_sample.average_matches;
    prediction.blue_average_matches = blue_sample.average_matches;

    prediction.red_score_estimate = average_alliance_score(red, team_stats);
    prediction.blue_score_estimate = average_alliance_score(blue, team_stats);
    prediction.red_score_total_estimate = prediction.red_score_estimate * prediction.red_teams.size();
    prediction.blue_score_total_estimate = prediction.blue_score_estimate * prediction.blue_teams.size();
    prediction.score_diff_estimate = prediction.red_score_total_estimate - prediction.blue_score_total_estimate;
    prediction.event_average_score = event_average_score(team_stats);
    prediction.red_adjusted_average = prediction.red_score_estimate - prediction.event_average_score;
    prediction.blue_adjusted_average = prediction.blue_score_estimate - prediction.event_average_score;
    prediction.adjusted_score_diff_estimate =
        (prediction.red_adjusted_average * prediction.red_teams.size())
        - (prediction.blue_adjusted_average * prediction.blue_teams.size());
    prediction.red_confidence = red_sample.confidence;
    prediction.blue_confidence = blue_sample.confidence;

    const double red_prob = sigmoid((prediction.adjusted_score_diff_estimate / score_diff_scale) * sigmoid_scale);
    prediction.red_win_probability = red_prob;
    prediction.blue_win_probability = 1.0 - red_prob;

    return prediction;
}
