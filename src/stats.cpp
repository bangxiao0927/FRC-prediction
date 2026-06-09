#include "stats.h"

#include <algorithm>

namespace {

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

std::map<std::string, TeamStats> compute_team_stats(const nlohmann::json& matches_json) {
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
