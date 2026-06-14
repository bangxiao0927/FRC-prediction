#include "history.h"

#include <algorithm>
#include <set>

namespace {

double match_time(const nlohmann::json& match) {
    double time = match.value("time", 0.0);
    if (time > 0.0) {
        return time;
    }
    return match.value("actual_time", 0.0);
}

// Return the alliance score and team count for the alliance that contains
// team_key, or false if the team is not found / the alliance is malformed.
bool team_alliance_score(const nlohmann::json& match, const std::string& team_key,
                         double& score, int& team_count) {
    if (!match.contains("alliances") || !match["alliances"].is_object()) {
        return false;
    }
    const nlohmann::json& alliances = match["alliances"];
    for (const char* side : {"red", "blue"}) {
        if (!alliances.contains(side) || !alliances[side].is_object()) {
            continue;
        }
        const nlohmann::json& alliance = alliances[side];
        if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
            continue;
        }
        bool found = false;
        int count = 0;
        for (const auto& key : alliance["team_keys"]) {
            if (!key.is_string()) {
                continue;
            }
            count += 1;
            if (key.get<std::string>() == team_key) {
                found = true;
            }
        }
        if (found && count > 0) {
            const int raw_score = alliance.value("score", -1);
            if (raw_score < 0) {
                return false;
            }
            score = static_cast<double>(raw_score);
            team_count = count;
            return true;
        }
    }
    return false;
}

}  // namespace

TeamForm compute_team_form(const nlohmann::json& team_season_matches,
                           const std::string& team_key,
                           const std::string& exclude_event_key,
                           double before_time) {
    TeamForm form;
    if (!team_season_matches.is_array()) {
        return form;
    }

    double total = 0.0;
    int count = 0;
    for (const auto& match : team_season_matches) {
        if (match.value("event_key", "") == exclude_event_key) {
            continue;  // never count the current event as "history"
        }
        if (before_time > 0.0) {
            const double when = match_time(match);
            if (when <= 0.0 || when >= before_time) {
                continue;  // only prior-in-time matches, so no future leakage
            }
        }
        double score = 0.0;
        int team_count = 0;
        if (!team_alliance_score(match, team_key, score, team_count)) {
            continue;
        }
        total += score / static_cast<double>(team_count);
        count += 1;
    }

    if (count > 0) {
        form.per_team_score = total / static_cast<double>(count);
        form.matches = count;
    }
    return form;
}

std::map<std::string, double> blend_oprs(
    const std::map<std::string, double>& current_oprs,
    const std::map<std::string, double>& historical_priors,
    const std::map<std::string, TeamStats>& current_stats,
    int confidence_match_count) {
    std::map<std::string, double> blended;
    const double denom = confidence_match_count > 0
        ? static_cast<double>(confidence_match_count)
        : 1.0;

    std::set<std::string> teams;
    for (const auto& entry : current_oprs) {
        teams.insert(entry.first);
    }
    for (const auto& entry : historical_priors) {
        teams.insert(entry.first);
    }

    for (const auto& team : teams) {
        auto cur_it = current_oprs.find(team);
        auto hist_it = historical_priors.find(team);
        const bool has_cur = cur_it != current_oprs.end();
        const bool has_hist = hist_it != historical_priors.end();

        // Fall back to the other source when one is missing so we never blend
        // toward zero for a team we actually have some signal on.
        const double current = has_cur ? cur_it->second
            : (has_hist ? hist_it->second : 0.0);
        const double historical = has_hist ? hist_it->second
            : (has_cur ? cur_it->second : 0.0);

        int played = 0;
        auto stat_it = current_stats.find(team);
        if (stat_it != current_stats.end()) {
            played = stat_it->second.matches_played;
        }
        const double weight = std::min(1.0, static_cast<double>(played) / denom);
        blended[team] = weight * current + (1.0 - weight) * historical;
    }
    return blended;
}

namespace {

double phase_weight(int played, int confidence_matches) {
    const double denom = confidence_matches > 0 ? static_cast<double>(confidence_matches) : 1.0;
    return std::min(1.0, static_cast<double>(played) / denom);
}

}  // namespace

std::map<std::string, double> blend_phase_oprs(
    const std::map<std::string, double>& current_total,
    const std::map<std::string, PhaseRatings>& current_phases,
    const std::map<std::string, PhaseRatings>& historical_phases,
    const std::map<std::string, TeamStats>& current_stats,
    const PhaseConfidence& confidence) {
    std::map<std::string, double> blended;

    std::set<std::string> teams;
    for (const auto& entry : current_total) {
        teams.insert(entry.first);
    }
    for (const auto& entry : historical_phases) {
        teams.insert(entry.first);
    }

    for (const auto& team : teams) {
        int played = 0;
        const auto stat_it = current_stats.find(team);
        if (stat_it != current_stats.end()) {
            played = stat_it->second.matches_played;
        }

        const auto cur_phase_it = current_phases.find(team);
        const auto hist_phase_it = historical_phases.find(team);
        const auto total_it = current_total.find(team);
        const bool has_cur_phase = cur_phase_it != current_phases.end();
        const bool has_hist_phase = hist_phase_it != historical_phases.end();

        // Best available current total: the model's total OPR, else the current
        // phase sum, else the historical phase sum.
        double current_total_value = 0.0;
        if (total_it != current_total.end()) {
            current_total_value = total_it->second;
        } else if (has_cur_phase) {
            current_total_value = cur_phase_it->second.sum();
        } else if (has_hist_phase) {
            current_total_value = hist_phase_it->second.sum();
        }

        if (has_cur_phase && has_hist_phase) {
            const PhaseRatings& cur = cur_phase_it->second;
            const PhaseRatings& hist = hist_phase_it->second;
            // Carry through whatever the OPR total holds beyond the three phases
            // (e.g. foul points) without blending it.
            const double remainder = current_total_value - cur.sum();
            const double w_auto = phase_weight(played, confidence.auto_matches);
            const double w_teleop = phase_weight(played, confidence.teleop_matches);
            const double w_endgame = phase_weight(played, confidence.endgame_matches);
            const double blended_auto = w_auto * cur.autonomous + (1.0 - w_auto) * hist.autonomous;
            const double blended_teleop = w_teleop * cur.teleop + (1.0 - w_teleop) * hist.teleop;
            const double blended_endgame = w_endgame * cur.endgame + (1.0 - w_endgame) * hist.endgame;
            blended[team] = remainder + blended_auto + blended_teleop + blended_endgame;
        } else if (has_hist_phase) {
            // No current phase breakdown: blend the totals with a single weight.
            const double w = phase_weight(played, confidence.teleop_matches);
            blended[team] = w * current_total_value + (1.0 - w) * hist_phase_it->second.sum();
        } else {
            // No history to add: keep the current total as-is.
            blended[team] = current_total_value;
        }
    }
    return blended;
}
