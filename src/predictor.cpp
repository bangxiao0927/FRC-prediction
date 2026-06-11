#include "predictor.h"

#include <algorithm>
#include <cmath>

namespace {

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}

struct AllianceSample {
    std::vector<std::string> teams;
    int team_count = 0;
    int total_matches = 0;
    double average_matches = 0.0;
    double confidence = 0.0;
    // Sum of per-team average scores across every scheduled team. Teams that
    // have not played yet are imputed with the event average so the total and
    // the average always use the same denominator (the scheduled team count).
    double score_total = 0.0;
    double score_average = 0.0;
};

AllianceSample build_alliance_sample(const nlohmann::json& alliance,
                                     const std::map<std::string, TeamStats>& team_stats,
                                     int confidence_match_count,
                                     double event_average) {
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
        sample.team_count += 1;
        auto it = team_stats.find(team_key);
        if (it == team_stats.end()) {
            // No data yet for this team: impute the field average so the
            // alliance total is not silently understated.
            sample.score_total += event_average;
            continue;
        }
        sample.total_matches += it->second.matches_played;
        sample.score_total += it->second.average_score;
    }

    if (sample.team_count == 0) {
        return sample;
    }

    sample.average_matches = static_cast<double>(sample.total_matches)
        / static_cast<double>(sample.team_count);
    sample.confidence = sample.average_matches / static_cast<double>(confidence_match_count);
    sample.confidence = std::clamp(sample.confidence, 0.0, 1.0);
    sample.score_average = sample.score_total / static_cast<double>(sample.team_count);
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

    const double event_average = compute_event_average_score(team_stats);
    AllianceSample red_sample =
        build_alliance_sample(red, team_stats, confidence_match_count, event_average);
    AllianceSample blue_sample =
        build_alliance_sample(blue, team_stats, confidence_match_count, event_average);

    prediction.red_teams = red_sample.teams;
    prediction.blue_teams = blue_sample.teams;
    prediction.red_team_count = red_sample.team_count;
    prediction.blue_team_count = blue_sample.team_count;
    prediction.red_total_matches = red_sample.total_matches;
    prediction.blue_total_matches = blue_sample.total_matches;
    prediction.red_average_matches = red_sample.average_matches;
    prediction.blue_average_matches = blue_sample.average_matches;
    prediction.red_confidence = red_sample.confidence;
    prediction.blue_confidence = blue_sample.confidence;
    prediction.event_average_score = event_average;

    // Raw estimate: average and total share the scheduled team count.
    prediction.red_score_estimate = red_sample.score_average;
    prediction.blue_score_estimate = blue_sample.score_average;
    prediction.red_score_total_estimate = red_sample.score_total;
    prediction.blue_score_total_estimate = blue_sample.score_total;
    prediction.score_diff_estimate =
        prediction.red_score_total_estimate - prediction.blue_score_total_estimate;

    // Confidence-weighted shrink toward the field mean. Alliances with little
    // data are pulled toward the event average; full-data alliances keep their
    // raw estimate. Because the shrink depends on each alliance's own sample
    // size, it actually changes the prediction (unlike a constant offset).
    prediction.red_adjusted_average =
        red_sample.confidence * red_sample.score_average
        + (1.0 - red_sample.confidence) * event_average;
    prediction.blue_adjusted_average =
        blue_sample.confidence * blue_sample.score_average
        + (1.0 - blue_sample.confidence) * event_average;
    const double red_adjusted_total =
        prediction.red_adjusted_average * static_cast<double>(red_sample.team_count);
    const double blue_adjusted_total =
        prediction.blue_adjusted_average * static_cast<double>(blue_sample.team_count);
    prediction.adjusted_score_diff_estimate = red_adjusted_total - blue_adjusted_total;

    const double red_prob = sigmoid((prediction.adjusted_score_diff_estimate / score_diff_scale) * sigmoid_scale);
    prediction.red_win_probability = red_prob;
    prediction.blue_win_probability = 1.0 - red_prob;

    return prediction;
}
