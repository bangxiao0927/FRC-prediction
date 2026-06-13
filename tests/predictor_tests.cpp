#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../src/predictor.h"
#include "../src/stats.h"
#include "../src/picklist.h"
#include "../src/opr.h"
#include "../src/roles.h"
#include "../src/synergy.h"

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

int test_cutoff_orders_by_schedule_across_comp_levels() {
    // The cutoff must use schedule order (comp level, set, match number), not
    // timestamps. A qualification match always precedes a playoff match even if
    // its match_number is larger, and the target playoff match plus anything
    // later (a final) must never leak into the pre-target stats.
    auto make_match = [](const std::string& level, int set_number, int number,
                         const std::string& red, int red_score,
                         const std::string& blue, int blue_score) {
        return nlohmann::json{
            {"comp_level", level},
            {"set_number", set_number},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {red}}, {"score", red_score}}},
                {"blue", {{"team_keys", {blue}}, {"score", blue_score}}}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(make_match("qm", 1, 70, "frcA", 100, "frcB", 50));
    nlohmann::json target = make_match("sf", 1, 1, "frcA", 80, "frcC", 90);
    matches.push_back(target);
    matches.push_back(make_match("f", 1, 1, "frcA", 200, "frcD", 10));

    std::map<std::string, TeamStats> stats =
        compute_team_stats_before(matches, MatchFilter::QualificationPlusElimPlayed, target);

    int failures = 0;
    failures += expect_true(stats.count("frcA") == 1 && stats["frcA"].matches_played == 1,
                            "the earlier qualification match must count even with a smaller level");
    failures += expect_true(stats.count("frcA") == 1 && stats["frcA"].total_score == 100,
                            "the target playoff match score must not leak into pre-target stats");
    failures += expect_true(stats.count("frcC") == 0,
                            "the target match's teams must not be counted from the target");
    failures += expect_true(stats.count("frcD") == 0,
                            "a later final must not be counted");
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

int test_picklist_before_cutoff_uses_schedule_order() {
    // Regression guard: the picklist cutoff must follow schedule order, not
    // timestamps. The target match (and anything later) carries misleadingly
    // small time fields, while the earlier matches carry larger ones. A
    // timestamp-based cutoff would wrongly drop the real history and leak the
    // target's inflated scores.
    auto qual_match = [](int number, double time, const std::string& red, int red_score,
                         const std::string& blue, int blue_score) {
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"time", time},
            {"alliances", {
                {"red", {{"team_keys", {red}}, {"score", red_score}}},
                {"blue", {{"team_keys", {blue}}, {"score", blue_score}}}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(qual_match(1, 500.0, "frcSelf", 70, "frcCand", 50));
    matches.push_back(qual_match(2, 600.0, "frcSelf", 75, "frcCand", 60));
    // Target match has an earlier timestamp but a later schedule position; its
    // huge scores must never enter the pre-target picklist.
    nlohmann::json target = qual_match(3, 100.0, "frcSelf", 80, "frcCand", 1000);
    matches.push_back(target);
    matches.push_back(qual_match(4, 50.0, "frcSelf", 85, "frcCand", 2000));

    PicklistWeights weights;
    PicklistSummary picklist =
        compute_picklist(matches, MatchFilter::QualificationOnly, target, {}, weights, 6, "frcSelf");

    int failures = 0;
    failures += expect_true(picklist.self_team_key == "frcSelf"
                                && picklist.self_performance.matches_played == 2,
                            "only matches scheduled before the target should anchor self performance");
    failures += expect_true(almost_equal(picklist.self_performance.average_score, 72.5),
                            "self average must exclude the target match score");

    const PicklistEntry* cand = nullptr;
    for (const auto& entry : picklist.entries) {
        if (entry.team_key == "frcCand") {
            cand = &entry;
        }
    }
    failures += expect_true(cand != nullptr && cand->matches == 2,
                            "the candidate should only carry its two pre-target matches");
    failures += expect_true(cand != nullptr && almost_equal(cand->average_score, 55.0),
                            "the target and later spikes must not leak into the candidate average");
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

// Build a 2v2 qualification match whose alliance score_breakdown is the sum of
// its members' phase contributions (auto / teleop / endgame). teleopPoints
// includes endgame stage points, mirroring real TBA data.
nlohmann::json phase_match(int number,
                           const std::string& r1, const std::string& r2,
                           int r_auto, int r_teleop, int r_endgame,
                           const std::string& b1, const std::string& b2,
                           int b_auto, int b_teleop, int b_endgame) {
    auto breakdown = [](int a, int t, int e) {
        return nlohmann::json{
            {"autoPoints", a},
            {"teleopPoints", t + e},
            {"endGameTotalStagePoints", e}
        };
    };
    const int r_score = r_auto + r_teleop + r_endgame;
    const int b_score = b_auto + b_teleop + b_endgame;
    return nlohmann::json{
        {"comp_level", "qm"},
        {"set_number", 1},
        {"match_number", number},
        {"alliances", {
            {"red", {{"team_keys", {r1, r2}}, {"score", r_score}}},
            {"blue", {{"team_keys", {b1, b2}}, {"score", b_score}}}
        }},
        {"score_breakdown", {
            {"red", breakdown(r_auto, r_teleop, r_endgame)},
            {"blue", breakdown(b_auto, b_teleop, b_endgame)}
        }}
    };
}

int test_roles_decompose_phases() {
    // True phase contributions:
    //   frcAuto: auto-heavy   (10/5/0)
    //   frcEnd:  endgame-heavy (2/5/8)
    //   frcMid1/frcMid2: balanced (5/10/1)
    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(phase_match(1, "frcAuto", "frcEnd", 12, 10, 8,
                                     "frcMid1", "frcMid2", 10, 20, 2));
    matches.push_back(phase_match(2, "frcAuto", "frcMid1", 15, 15, 1,
                                     "frcEnd", "frcMid2", 7, 15, 9));
    matches.push_back(phase_match(3, "frcAuto", "frcMid2", 15, 15, 1,
                                     "frcEnd", "frcMid1", 7, 15, 9));

    std::map<std::string, TeamRole> roles =
        compute_team_roles(matches, MatchFilter::QualificationOnly);

    int failures = 0;
    failures += expect_true(roles.size() == 4, "every team that played should get a role profile");
    failures += expect_true(roles["frcAuto"].has_phase_data,
                            "phase ratings should be available when score_breakdown exists");

    // The auto specialist should top the auto phase; the endgame specialist the
    // endgame phase. These orderings survive the ridge shrink.
    failures += expect_true(roles["frcAuto"].auto_phase > roles["frcEnd"].auto_phase
                                && roles["frcAuto"].auto_phase > roles["frcMid1"].auto_phase,
                            "the auto specialist should have the highest auto contribution");
    failures += expect_true(roles["frcEnd"].endgame_phase > roles["frcAuto"].endgame_phase
                                && roles["frcEnd"].endgame_phase > roles["frcMid1"].endgame_phase,
                            "the endgame specialist should have the highest endgame contribution");
    failures += expect_true(roles["frcEnd"].primary == "endgame",
                            "the endgame specialist should be labeled an endgame role");
    failures += expect_true(roles["frcAuto"].primary == "auto",
                            "the auto specialist should be labeled an auto role");
    return failures;
}

int test_roles_unknown_endgame_key_is_flagged() {
    // A season whose score_breakdown has auto/teleop but no endgame key we know.
    // teleopPoints here is the team's full teleop output (endgame not separable).
    auto match = [](int number,
                    const std::string& r1, const std::string& r2, int r_auto, int r_teleop,
                    const std::string& b1, const std::string& b2, int b_auto, int b_teleop) {
        auto breakdown = [](int a, int t) {
            return nlohmann::json{{"autoPoints", a}, {"teleopPoints", t}};
        };
        return nlohmann::json{
            {"comp_level", "qm"},
            {"set_number", 1},
            {"match_number", number},
            {"alliances", {
                {"red", {{"team_keys", {r1, r2}}, {"score", r_auto + r_teleop}}},
                {"blue", {{"team_keys", {b1, b2}}, {"score", b_auto + b_teleop}}}
            }},
            {"score_breakdown", {
                {"red", breakdown(r_auto, r_teleop)},
                {"blue", breakdown(b_auto, b_teleop)}
            }}
        };
    };

    nlohmann::json matches = nlohmann::json::array();
    matches.push_back(match(1, "frcA", "frcB", 12, 30, "frcC", "frcD", 10, 28));
    matches.push_back(match(2, "frcA", "frcC", 14, 26, "frcB", "frcD", 8, 24));
    matches.push_back(match(3, "frcA", "frcD", 14, 26, "frcB", "frcC", 8, 24));

    std::map<std::string, TeamRole> roles =
        compute_team_roles(matches, MatchFilter::QualificationOnly);

    int failures = 0;
    failures += expect_true(roles["frcA"].has_phase_data,
                            "auto/teleop should still be available from score_breakdown");
    failures += expect_true(!roles["frcA"].has_endgame_data,
                            "an unknown season endgame key should flag endgame as unavailable");
    failures += expect_true(almost_equal(roles["frcA"].endgame_phase, 0.0),
                            "endgame should be 0 when the season key is unknown");
    // No team should be labeled endgame purely from a 0 endgame phase.
    for (const auto& entry : roles) {
        failures += expect_true(entry.second.primary != "endgame",
                                "endgame label must not be assigned without endgame data");
    }
    return failures;
}

int test_roles_defense_rating() {
    // 1v1 matches: frcWall holds opponents to single digits; frcOpen lets them
    // score ~90. Defense is a DPR (opponent score share), so frcWall must rate
    // lower and be tagged a defensive role. No score_breakdown is present here.
    auto duel = [](int number, const std::string& red, int red_score,
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
    matches.push_back(duel(1, "frcWall", 50, "frcA", 10));
    matches.push_back(duel(2, "frcWall", 50, "frcB", 12));
    matches.push_back(duel(3, "frcWall", 50, "frcC", 8));
    matches.push_back(duel(4, "frcOpen", 50, "frcD", 88));
    matches.push_back(duel(5, "frcOpen", 50, "frcE", 92));
    matches.push_back(duel(6, "frcOpen", 50, "frcF", 90));

    std::map<std::string, TeamRole> roles =
        compute_team_roles(matches, MatchFilter::QualificationOnly);

    int failures = 0;
    failures += expect_true(roles.count("frcWall") == 1 && roles.count("frcOpen") == 1,
                            "both anchor teams should be rated");
    failures += expect_true(roles["frcWall"].defense < roles["frcOpen"].defense,
                            "the team that suppresses opponents should have a lower defense rating");
    failures += expect_true(roles["frcWall"].primary == "defense",
                            "a clear opponent-suppressing team should be tagged defense");
    failures += expect_true(!roles["frcWall"].has_phase_data,
                            "phase data should be absent when no score_breakdown is provided");
    return failures;
}

int test_roles_cutoff_no_leakage() {
    auto duel = [](int number, const std::string& red, int red_score,
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
    matches.push_back(duel(1, "frcA", 100, "frcB", 50));
    nlohmann::json target = duel(2, "frcA", 80, "frcC", 90);
    matches.push_back(target);
    matches.push_back(duel(3, "frcA", 200, "frcD", 10));

    std::map<std::string, TeamRole> roles =
        compute_team_roles_before(matches, MatchFilter::QualificationOnly, target);

    int failures = 0;
    failures += expect_true(roles.count("frcA") == 1 && roles.count("frcB") == 1,
                            "teams from matches before the target should be rated");
    failures += expect_true(roles.count("frcC") == 0,
                            "the target match's teams must not leak into pre-target roles");
    failures += expect_true(roles.count("frcD") == 0,
                            "later matches must not leak into pre-target roles");
    return failures;
}

int test_synergy_predicted_score_and_imputation() {
    const std::map<std::string, double> oprs = {{"frcA", 30.0}, {"frcB", 20.0}};
    const std::map<std::string, TeamRole> roles;  // no role data -> diversity 0
    // frcX is unknown, so it should be imputed with the baseline OPR.
    AllianceEvaluation eval =
        evaluate_alliance({"frcA", "frcX"}, oprs, roles, 20.0);

    int failures = 0;
    failures += expect_true(almost_equal(eval.predicted_score, 50.0),
                            "predicted score should sum member OPRs and impute unknowns");
    return failures;
}

int test_synergy_rewards_role_diversity() {
    // Two alliances with the SAME raw OPR sum (60) but different composition: a
    // complementary trio should out-synergize a single-role stack.
    const std::map<std::string, double> oprs = {
        {"frcOff", 30.0}, {"frcEnd", 20.0}, {"frcDef", 10.0},
        {"frcE1", 30.0}, {"frcE2", 20.0}, {"frcE3", 10.0}};
    std::map<std::string, TeamRole> roles;
    auto role = [](const std::string& primary) {
        TeamRole r;
        r.primary = primary;
        return r;
    };
    roles["frcOff"] = role("offense");
    roles["frcEnd"] = role("endgame");
    roles["frcDef"] = role("defense");
    roles["frcE1"] = role("endgame");
    roles["frcE2"] = role("endgame");
    roles["frcE3"] = role("endgame");

    AllianceEvaluation diverse =
        evaluate_alliance({"frcOff", "frcEnd", "frcDef"}, oprs, roles, 20.0);
    AllianceEvaluation stacked =
        evaluate_alliance({"frcE1", "frcE2", "frcE3"}, oprs, roles, 20.0);

    int failures = 0;
    failures += expect_true(almost_equal(diverse.predicted_score, stacked.predicted_score),
                            "both alliances should share the same raw OPR sum");
    failures += expect_true(diverse.role_diversity == 3 && diverse.has_defender,
                            "the diverse alliance should cover three roles and a defender");
    // diversity bonus 3*(3-1)=6 plus defender 2 = +8 -> 68.
    failures += expect_true(almost_equal(diverse.synergy_score, 68.0),
                            "complementary alliance synergy should add diversity + defender bonuses");
    // single-role stack: endgame_specialists=3 -> penalty 4*(3-1)=8 -> 52.
    failures += expect_true(stacked.endgame_specialists == 3
                                && almost_equal(stacked.synergy_score, 52.0),
                            "stacking endgame specialists should be penalized");
    failures += expect_true(diverse.synergy_score > stacked.synergy_score,
                            "synergy should favor complementary lineups over redundant ones");
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += test_alliance_counts_include_teams_without_stats();
    failures += test_event_adjustment_changes_prediction();
    failures += test_total_uses_scheduled_team_count();
    failures += test_cutoff_excludes_target_and_later_matches();
    failures += test_cutoff_orders_by_schedule_across_comp_levels();
    failures += test_picklist_ranks_and_excludes();
    failures += test_picklist_before_cutoff_uses_schedule_order();
    failures += test_opr_recovers_individual_contributions();
    failures += test_opr_cutoff_excludes_target_and_later_matches();
    failures += test_predict_match_uses_opr_when_supplied();
    failures += test_roles_decompose_phases();
    failures += test_roles_unknown_endgame_key_is_flagged();
    failures += test_roles_defense_rating();
    failures += test_roles_cutoff_no_leakage();
    failures += test_synergy_predicted_score_and_imputation();
    failures += test_synergy_rewards_role_diversity();
    return failures;
}
