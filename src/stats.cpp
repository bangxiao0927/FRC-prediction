#include "stats.h"

#include <algorithm>

namespace {

bool is_qualification_match(const nlohmann::json& match) {
    return match.value("comp_level", "") == "qm";
}

bool is_elimination_match(const nlohmann::json& match) {
    const std::string level = match.value("comp_level", "");
    return level == "qf" || level == "sf" || level == "f";
}

bool should_include_match(const nlohmann::json& match, MatchFilter filter) {
    if (filter == MatchFilter::AllPlayed) {
        return true;
    }
    if (filter == MatchFilter::QualificationOnly) {
        return is_qualification_match(match);
    }
    if (filter == MatchFilter::QualificationPlusElimPlayed) {
        return is_qualification_match(match) || is_elimination_match(match);
    }
    return true;
}

void add_alliance_score(std::map<std::string, TeamStats>& stats,
                        const nlohmann::json& alliance,
                        int score) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return;
    }

    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        std::string team_key = team_key_value.get<std::string>();
        TeamStats& team_stats = stats[team_key];
        team_stats.matches_played += 1;
        team_stats.total_score += score;
    }
}

}  // namespace

std::map<std::string, TeamStats> compute_team_stats(const nlohmann::json& matches_json,
                                                    MatchFilter filter) {
    std::map<std::string, TeamStats> stats;
    if (!matches_json.is_array()) {
        return stats;
    }

    for (const auto& match : matches_json) {
        if (!match.contains("alliances") || !match["alliances"].is_object()) {
            continue;
        }
        const nlohmann::json& alliances = match["alliances"];
        if (!alliances.contains("red") || !alliances.contains("blue")) {
            continue;
        }

        if (!should_include_match(match, filter)) {
            continue;
        }

        const nlohmann::json& red = alliances["red"];
        const nlohmann::json& blue = alliances["blue"];

        int red_score = red.value("score", -1);
        int blue_score = blue.value("score", -1);
        if (red_score < 0 || blue_score < 0) {
            continue;
        }

        add_alliance_score(stats, red, red_score);
        add_alliance_score(stats, blue, blue_score);
    }

    for (auto& entry : stats) {
        TeamStats& team_stats = entry.second;
        if (team_stats.matches_played > 0) {
            team_stats.average_score = static_cast<double>(team_stats.total_score) /
                static_cast<double>(team_stats.matches_played);
        }
    }

    return stats;
}

double compute_event_average_score(const std::map<std::string, TeamStats>& team_stats) {
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
