#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "stats.h"

// A team's prior-season form, summarized as an average per-team scoring level.
struct TeamForm {
    double per_team_score = 0.0;  // mean (alliance score / alliance size) the team posted
    int matches = 0;              // how many prior matches fed the average
};

// Summarize a team's season form from its full-season match list. Only counts
// matches that are NOT at exclude_event_key, and (when before_time > 0) that
// were played strictly before before_time. This keeps cross-event history
// leak-free: a prediction never sees the current event or anything later.
TeamForm compute_team_form(const nlohmann::json& team_season_matches,
                           const std::string& team_key,
                           const std::string& exclude_event_key,
                           double before_time);

// Blend current-event OPR with a historical per-team prior, weighted by how much
// current-event data each team has: a team with >= confidence_match_count played
// matches is trusted entirely on current form, while a team with no current data
// falls back to its history. Teams present in only one source pass through.
std::map<std::string, double> blend_oprs(
    const std::map<std::string, double>& current_oprs,
    const std::map<std::string, double>& historical_priors,
    const std::map<std::string, TeamStats>& current_stats,
    int confidence_match_count);

// A team's scoring split into the three phases. Used to blend current and
// historical form per phase rather than as a single lumped score.
struct PhaseRatings {
    double autonomous = 0.0;
    double teleop = 0.0;
    double endgame = 0.0;
    double sum() const { return autonomous + teleop + endgame; }
};

// How many current-event matches a team needs before each phase is trusted
// entirely on current form (vs. its cross-event history). Different phases
// stabilize at different rates — a robot's auto routine is essentially a fixed
// capability that reads true almost immediately, while teleop output is noisier
// and takes more matches to settle — so each phase gets its own count.
struct PhaseConfidence {
    int auto_matches = 4;
    int teleop_matches = 8;
    int endgame_matches = 6;
};

// Per-phase blend of current-event OPR with a historical prior. Each phase is
// weighted independently by how many current matches the team has relative to
// that phase's confidence count, so a stable phase (e.g. auto) locks onto current
// form faster than a noisy one. Any non-phase points in current_total (e.g. foul
// points the OPR attributes) are carried through unchanged. Teams without both a
// current and a historical phase profile fall back to a single-weight blend of
// the total (using the teleop count), or pass through when there is no history.
std::map<std::string, double> blend_phase_oprs(
    const std::map<std::string, double>& current_total,
    const std::map<std::string, PhaseRatings>& current_phases,
    const std::map<std::string, PhaseRatings>& historical_phases,
    const std::map<std::string, TeamStats>& current_stats,
    const PhaseConfidence& confidence);
