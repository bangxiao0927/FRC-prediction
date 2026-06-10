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

}  // namespace

int main() {
    return test_alliance_counts_include_teams_without_stats();
}
