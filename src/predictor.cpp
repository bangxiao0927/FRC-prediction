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

// contribution[team] is the per-team score this model attributes to a team
// (OPR or legacy alliance average). baseline_per_team is imputed for teams with
// no entry yet so the alliance total never silently understates a lineup.
AllianceSample build_alliance_sample(const nlohmann::json& alliance,
                                     const std::map<std::string, TeamStats>& team_stats,
                                     const std::map<std::string, double>& contribution,
                                     int confidence_match_count,
                                     double baseline_per_team) {
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
        auto stats_it = team_stats.find(team_key);
        if (stats_it != team_stats.end()) {
            sample.total_matches += stats_it->second.matches_played;
        }
        auto contrib_it = contribution.find(team_key);
        if (contrib_it == contribution.end()) {
            // No data yet for this team: impute the field average so the
            // alliance total is not silently understated.
            sample.score_total += baseline_per_team;
            continue;
        }
        sample.score_total += contrib_it->second;
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
                              double sigmoid_scale,
                              const std::map<std::string, double>& team_oprs) {
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

    // OPR mode: a team's contribution is its OPR and the shrink/imputation
    // baseline is the average OPR, so an alliance total is a real score. Legacy
    // mode: a team's contribution is its average alliance score and the baseline
    // is the event average (the previous "sum of averages" proxy).
    const bool use_opr = !team_oprs.empty();
    prediction.uses_opr = use_opr;
    std::map<std::string, double> legacy_contribution;
    double baseline_per_team = event_average;
    if (use_opr) {
        double opr_total = 0.0;
        for (const auto& entry : team_oprs) {
            opr_total += entry.second;
        }
        baseline_per_team = team_oprs.empty() ? 0.0
            : opr_total / static_cast<double>(team_oprs.size());
    } else {
        for (const auto& entry : team_stats) {
            legacy_contribution[entry.first] = entry.second.average_score;
        }
    }
    const std::map<std::string, double>& contribution =
        use_opr ? team_oprs : legacy_contribution;

    AllianceSample red_sample =
        build_alliance_sample(red, team_stats, contribution, confidence_match_count,
                              baseline_per_team);
    AllianceSample blue_sample =
        build_alliance_sample(blue, team_stats, contribution, confidence_match_count,
                              baseline_per_team);

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
        + (1.0 - red_sample.confidence) * baseline_per_team;
    prediction.blue_adjusted_average =
        blue_sample.confidence * blue_sample.score_average
        + (1.0 - blue_sample.confidence) * baseline_per_team;
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
