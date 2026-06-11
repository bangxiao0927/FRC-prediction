#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../src/predictor.h"

namespace {

bool almost_equal(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

int expect_true(bool condition, const std::string& message) {
    if (condition) {
        return 0;
    }

    std::cerr << "FAILED: " << message << "\n";
    return 1;
}

int test_alliance_counts_include_teams_without_stats() {
    nlohmann::json match = {
        {"alliances", {
            {"red", {
                {"team_keys", {"frc1", "frc2", "frc3"}}
            }},
            {"blue", {
                {"team_keys", {"frc4", "frc5", "frc6"}}
            }}
        }}
    };

    std::map<std::string, TeamStats> stats = {
        {"frc1", TeamStats{5, 500, 100.0}},
        {"frc2", TeamStats{3, 240, 80.0}},
        {"frc4", TeamStats{6, 540, 90.0}},
        {"frc5", TeamStats{6, 600, 100.0}},
        {"frc6", TeamStats{6, 660, 110.0}}
    };

    MatchPrediction prediction = predict_match(match, stats, 6, 30.0, 1.0);

    int failures = 0;
    failures += expect_true(prediction.red_team_count == 3,
                            "red team count should include teams that have no stats yet");
    failures += expect_true(prediction.red_total_matches == 8,
                            "red total matches should sum available samples only");
    failures += expect_true(almost_equal(prediction.red_average_matches, 8.0 / 3.0),
                            "red average matches should divide by scheduled team count");
    failures += expect_true(almost_equal(prediction.red_confidence, (8.0 / 3.0) / 6.0),
                            "red confidence should reflect missing samples");
    return failures;
}

int test_event_adjustment_changes_prediction() {
    // Red has full data (high confidence); blue is sparse (one match each), so
    // the confidence shrink should pull blue toward the field mean and change
    // the prediction relative to the raw score difference.
    nlohmann::json match = {
        {"alliances", {
            {"red", {
                {"team_keys", {"frc1", "frc2", "frc3"}}
            }},
            {"blue", {
                {"team_keys", {"frc4", "frc5", "frc6"}}
            }}
        }}
    };

    std::map<std::string, TeamStats> stats = {
        {"frc1", TeamStats{6, 720, 120.0}},
        {"frc2", TeamStats{6, 720, 120.0}},
        {"frc3", TeamStats{6, 720, 120.0}},
        {"frc4", TeamStats{1, 60, 60.0}},
        {"frc5", TeamStats{1, 60, 60.0}},
        {"frc6", TeamStats{1, 60, 60.0}}
    };

    MatchPrediction prediction = predict_match(match, stats, 6, 30.0, 1.0);

    int failures = 0;
    failures += expect_true(almost_equal(prediction.score_diff_estimate, 180.0),
                            "raw score diff should be the unadjusted alliance gap");
    failures += expect_true(!almost_equal(prediction.adjusted_score_diff_estimate,
                                          prediction.score_diff_estimate),
                            "adjustment must actually change the prediction");
    failures += expect_true(prediction.adjusted_score_diff_estimate <
                                prediction.score_diff_estimate,
                            "sparse blue alliance should be regressed toward the mean");
    failures += expect_true(almost_equal(prediction.adjusted_score_diff_estimate,
                                         51.42857142857143),
                            "adjusted diff should match confidence-weighted shrink");
    failures += expect_true(prediction.red_win_probability < 0.9,
                            "shrink should soften the favorite's win probability");
    return failures;
}

int test_total_uses_scheduled_team_count() {
    // frc3 has no data yet. The alliance total must impute the field average for
    // it so the total and the average stay on the same denominator.
    nlohmann::json match = {
        {"alliances", {
            {"red", {
                {"team_keys", {"frc1", "frc2", "frc3"}}
            }},
            {"blue", {
                {"team_keys", {"frc4", "frc5", "frc6"}}
            }}
        }}
    };

    std::map<std::string, TeamStats> stats = {
        {"frc1", TeamStats{4, 400, 100.0}},
        {"frc2", TeamStats{4, 320, 80.0}},
        {"frc4", TeamStats{4, 360, 90.0}},
        {"frc5", TeamStats{4, 360, 90.0}},
        {"frc6", TeamStats{4, 360, 90.0}}
    };

    MatchPrediction prediction = predict_match(match, stats, 6, 30.0, 1.0);

    int failures = 0;
    // event average = 1800 total score / 20 played team-matches = 90.
    failures += expect_true(almost_equal(prediction.event_average_score, 90.0),
                            "event average should be score-per-team-match");
    failures += expect_true(almost_equal(prediction.red_score_total_estimate, 270.0),
                            "red total should impute the field average for unknown teams");
    failures += expect_true(almost_equal(prediction.red_score_estimate, 90.0),
                            "red average and total must share the scheduled team count");
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += test_alliance_counts_include_teams_without_stats();
    failures += test_event_adjustment_changes_prediction();
    failures += test_total_uses_scheduled_team_count();
    return failures;
}
