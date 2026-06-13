#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../src/predictor.h"
#include "../src/stats.h"
#include "../src/picklist.h"
#include "../src/opr.h"

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

int test_cutoff_excludes_target_and_later_matches() {
    auto qual_match = [](int number, const std::string& red, int red_score,
                         const std::string& blue, int blue_score) {
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {red}}, {"score", red_score}}},
                {"blue", {{"team_keys", {blue}}, {"score", blue_score}}}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(qual_match(1, "frcA", 100, "frcB", 50));
    nlohmann::json target = qual_match(2, "frcA", 80, "frcC", 90);
    matches.push_back(target);
    matches.push_back(qual_match(3, "frcA", 200, "frcD", 10));

    std::map<std::string, TeamStats> stats =
        compute_team_stats_before(matches, MatchFilter::QualificationOnly, target);

    int failures = 0;
    failures += expect_true(stats.count("frcA") == 1 && stats["frcA"].matches_played == 1,
                            "only qm1 should count before qm2");
    failures += expect_true(stats.count("frcA") == 1 && stats["frcA"].total_score == 100,
                            "qm2 and qm3 scores must not leak into pre-qm2 stats");
    failures += expect_true(stats.count("frcC") == 0,
                            "the target match's teams must not be counted from the target");
    failures += expect_true(stats.count("frcD") == 0,
                            "later matches must not be counted");
    return failures;
}

int test_picklist_ranks_and_excludes() {
    auto qual_match = [](int number, const std::string& red, int red_score,
                         const std::string& blue, int blue_score) {
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {red}}, {"score", red_score}}},
                {"blue", {{"team_keys", {blue}}, {"score", blue_score}}}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    // frcStrong consistently scores high; frcWeak scores low; frcMid in between.
    matches.push_back(qual_match(1, "frcStrong", 150, "frcWeak", 40));
    matches.push_back(qual_match(2, "frcStrong", 150, "frcMid", 90));
    matches.push_back(qual_match(3, "frcMid", 95, "frcWeak", 45));
    matches.push_back(qual_match(4, "frcStrong", 150, "frcWeak", 35));
    matches.push_back(qual_match(5, "frcMid", 85, "frcStrong", 150));
    // frcSelf is the requesting team; it must have data so the picklist can be
    // anchored to its performance, but it must never rank in its own list.
    matches.push_back(qual_match(6, "frcSelf", 70, "frcWeak", 40));
    matches.push_back(qual_match(7, "frcSelf", 75, "frcMid", 85));

    PicklistWeights weights;  // balanced default
    PicklistSummary picklist =
        compute_picklist(matches, MatchFilter::QualificationOnly,
                         nlohmann::json(nullptr), {}, weights, 6, "frcSelf");

    int failures = 0;
    failures += expect_true(!picklist.entries.empty()
                                && picklist.entries.front().team_key == "frcStrong",
                            "the strongest, steadiest team should rank first");
    failures += expect_true(picklist.self_team_key == "frcSelf"
                                && picklist.self_performance.matches_played == 2,
                            "self performance should be reported for the requesting team");

    // The requesting team must never appear among its own candidates.
    bool self_present = false;
    for (const auto& entry : picklist.entries) {
        if (entry.team_key == "frcSelf") {
            self_present = true;
        }
    }
    failures += expect_true(!self_present, "the requesting team must be excluded from its own picklist");

    // Excluding the top team should drop it from the result.
    PicklistSummary without_strong =
        compute_picklist(matches, MatchFilter::QualificationOnly,
                         nlohmann::json(nullptr), {"frcStrong"}, weights, 6, "frcSelf");
    bool strong_present = false;
    for (const auto& entry : without_strong.entries) {
        if (entry.team_key == "frcStrong") {
            strong_present = true;
        }
    }
    failures += expect_true(!strong_present, "excluded teams must not appear in the picklist");
    return failures;
}

int test_opr_recovers_individual_contributions() {
    // Each team has a fixed true contribution and every alliance score is the
    // exact sum of its members'. With a consistent, well-mixed schedule the OPR
    // solve should recover those individual contributions closely, which the
    // legacy "sum of alliance averages" model cannot do.
    auto two_v_two = [](int number, const std::string& r1, const std::string& r2, int red_score,
                        const std::string& b1, const std::string& b2, int blue_score) {
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {r1, r2}}, {"score", red_score}}},
                {"blue", {{"team_keys", {b1, b2}}, {"score", blue_score}}}
            }}
        };
    };

    // True contributions: A=50, B=30, C=20, D=10.
    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(two_v_two(1, "frcA", "frcB", 80, "frcC", "frcD", 30));
    matches.push_back(two_v_two(2, "frcA", "frcC", 70, "frcB", "frcD", 40));
    matches.push_back(two_v_two(3, "frcA", "frcD", 60, "frcB", "frcC", 50));

    std::map<std::string, double> oprs =
        compute_team_oprs(matches, MatchFilter::QualificationOnly);

    int failures = 0;
    failures += expect_true(oprs.size() == 4, "every team that played should get an OPR");
    // Ordering must follow the true contributions even after ridge shrink.
    failures += expect_true(oprs["frcA"] > oprs["frcB"] && oprs["frcB"] > oprs["frcC"]
                                && oprs["frcC"] > oprs["frcD"],
                            "OPR ordering should match the true contribution ordering");
    // The point of OPR: it isolates a team's own contribution. For every team it
    // must be closer to the truth than the legacy proxy (the team's average
    // alliance score), which double-counts its partners.
    const std::map<std::string, double> truth = {
        {"frcA", 50.0}, {"frcB", 30.0}, {"frcC", 20.0}, {"frcD", 10.0}};
    const std::map<std::string, double> legacy_alliance_average = {
        {"frcA", 70.0}, {"frcB", 170.0 / 3.0}, {"frcC", 50.0}, {"frcD", 130.0 / 3.0}};
    for (const auto& entry : truth) {
        const std::string& team = entry.first;
        const double opr_error = std::abs(oprs[team] - entry.second);
        const double legacy_error = std::abs(legacy_alliance_average.at(team) - entry.second);
        failures += expect_true(opr_error < legacy_error,
                                "OPR must beat the legacy alliance average at recovering " + team);
        failures += expect_true(opr_error < 10.0,
                                "OPR should land within a reasonable margin of the true contribution");
    }
    return failures;
}

int test_opr_cutoff_excludes_target_and_later_matches() {
    auto qual_match = [](int number, const std::string& red, int red_score,
                         const std::string& blue, int blue_score) {
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {red}}, {"score", red_score}}},
                {"blue", {{"team_keys", {blue}}, {"score", blue_score}}}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(qual_match(1, "frcA", 100, "frcB", 50));
    nlohmann::json target = qual_match(2, "frcA", 80, "frcC", 90);
    matches.push_back(target);
    matches.push_back(qual_match(3, "frcA", 200, "frcD", 10));

    std::map<std::string, double> oprs =
        compute_team_oprs_before(matches, MatchFilter::QualificationOnly, target);

    int failures = 0;
    failures += expect_true(oprs.count("frcA") == 1 && oprs.count("frcB") == 1,
                            "teams from matches before the target should be rated");
    failures += expect_true(oprs.count("frcC") == 0,
                            "the target match's teams must not leak into pre-target OPR");
    failures += expect_true(oprs.count("frcD") == 0,
                            "later matches must not leak into pre-target OPR");
    return failures;
}

int test_predict_match_uses_opr_when_supplied() {
    nlohmann::json match = {
        {"alliances", {
            {"red", {{"team_keys", {"frc1", "frc2", "frc3"}}}},
            {"blue", {{"team_keys", {"frc4", "frc5", "frc6"}}}}
        }}
    };

    // Full data so confidence is 1.0 and the adjusted estimate equals the raw
    // OPR sum (no shrink), making the arithmetic exact and easy to assert.
    std::map<std::string, TeamStats> stats = {
        {"frc1", TeamStats{6, 360, 60.0}},
        {"frc2", TeamStats{6, 360, 60.0}},
        {"frc3", TeamStats{6, 360, 60.0}},
        {"frc4", TeamStats{6, 360, 60.0}},
        {"frc5", TeamStats{6, 360, 60.0}},
        {"frc6", TeamStats{6, 360, 60.0}}
    };
    std::map<std::string, double> oprs = {
        {"frc1", 30.0}, {"frc2", 20.0}, {"frc3", 10.0},
        {"frc4", 15.0}, {"frc5", 15.0}, {"frc6", 10.0}
    };

    MatchPrediction prediction = predict_match(match, stats, 1, 30.0, 1.0, oprs);

    int failures = 0;
    failures += expect_true(prediction.uses_opr,
                            "supplying OPRs should switch the model into OPR mode");
    failures += expect_true(almost_equal(prediction.red_score_total_estimate, 60.0),
                            "red alliance estimate should be the sum of member OPRs");
    failures += expect_true(almost_equal(prediction.blue_score_total_estimate, 40.0),
                            "blue alliance estimate should be the sum of member OPRs");
    failures += expect_true(almost_equal(prediction.adjusted_score_diff_estimate, 20.0),
                            "with full confidence the adjusted diff equals the OPR margin");
    failures += expect_true(prediction.red_win_probability > 0.5,
                            "the stronger OPR alliance should be favored");
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += test_alliance_counts_include_teams_without_stats();
    failures += test_event_adjustment_changes_prediction();
    failures += test_total_uses_scheduled_team_count();
    failures += test_cutoff_excludes_target_and_later_matches();
    failures += test_picklist_ranks_and_excludes();
    failures += test_opr_recovers_individual_contributions();
    failures += test_opr_cutoff_excludes_target_and_later_matches();
    failures += test_predict_match_uses_opr_when_supplied();
    return failures;
}
