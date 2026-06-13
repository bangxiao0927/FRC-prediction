#include "picklist.h"

#include <algorithm>
#include <cmath>

namespace {

double match_time_value(const nlohmann::json& match) {
    double time = match.value("time", 0.0);
    if (time > 0.0) {
        return time;
    }
    return match.value("actual_time", 0.0);
}

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

void add_scores(std::map<std::string, std::vector<std::pair<double, int>>>& scores,
                const nlohmann::json& alliance,
                int score,
                double time) {
    if (!alliance.contains("team_keys") || !alliance["team_keys"].is_array()) {
        return;
    }
    for (const auto& team_key_value : alliance["team_keys"]) {
        if (!team_key_value.is_string()) {
            continue;
        }
        scores[team_key_value.get<std::string>()].push_back({time, score});
    }
}

double compute_std_dev(const std::vector<std::pair<double, int>>& values, double mean) {
    if (values.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& entry : values) {
        double delta = static_cast<double>(entry.second) - mean;
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double compute_recent_average(std::vector<std::pair<double, int>> values, int count) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    int start = std::max(0, static_cast<int>(values.size()) - count);
    double total = 0.0;
    int used = 0;
    for (size_t i = static_cast<size_t>(start); i < values.size(); ++i) {
        total += static_cast<double>(values[i].second);
        used += 1;
    }
    if (used == 0) {
        return 0.0;
    }
    return total / static_cast<double>(used);
}

bool is_before_cutoff(const nlohmann::json& match, const nlohmann::json& cutoff) {
    if (cutoff.is_null()) {
        return true;
    }
    double cutoff_time = match_time_value(cutoff);
    double match_time = match_time_value(match);
    if (cutoff_time > 0.0 && match_time > 0.0) {
        return match_time < cutoff_time;
    }
    return true;
}

}  // namespace

PicklistSummary compute_picklist(
    const nlohmann::json& matches_json,
    MatchFilter filter,
    const nlohmann::json& before_match,
    const std::set<std::string>& exclude,
    const PicklistWeights& weights,
    int confidence_match_count,
    const std::string& my_team_key) {
    PicklistSummary summary;
    summary.self_team_key = my_team_key;
    if (!matches_json.is_array()) {
        return summary;
    }

    std::map<std::string, std::vector<std::pair<double, int>>> scores;
    for (const auto& match : matches_json) {
        if (!match.contains("alliances") || !match["alliances"].is_object()) {
            continue;
        }
        if (!should_include_match(match, filter)) {
            continue;
        }
        if (!is_before_cutoff(match, before_match)) {
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

        const double time = match_time_value(match);
        add_scores(scores, red, red_score, time);
        add_scores(scores, blue, blue_score, time);
    }

    auto self_it = scores.find(my_team_key);
    if (self_it == scores.end()) {
        return summary;
    }

    double event_total = 0.0;
    int event_matches = 0;
    for (const auto& entry : scores) {
        for (const auto& value : entry.second) {
            event_total += static_cast<double>(value.second);
            event_matches += 1;
        }
    }
    const double event_avg = event_matches == 0 ? 1.0
        : event_total / static_cast<double>(event_matches);

    const auto& self_scores = self_it->second;
    double self_total = 0.0;
    for (const auto& value : self_scores) {
        self_total += static_cast<double>(value.second);
    }
    const double self_avg = self_scores.empty() ? 0.0
        : self_total / static_cast<double>(self_scores.size());

    summary.event_average_score = event_avg;
    summary.self_performance.matches_played = static_cast<int>(self_scores.size());
    summary.self_performance.average_score = self_avg;
    summary.self_performance.std_dev = compute_std_dev(self_scores, self_avg);
    summary.self_performance.recent_average = compute_recent_average(self_scores, 3);

    summary.entries.reserve(scores.size());
    for (const auto& entry : scores) {
        const std::string& team_key = entry.first;
        if (team_key == my_team_key || exclude.count(team_key) > 0) {
            continue;
        }
        const auto& values = entry.second;
        double total = 0.0;
        for (const auto& value : values) {
            total += static_cast<double>(value.second);
        }
        const double mean = values.empty() ? 0.0 : total / static_cast<double>(values.size());
        const double stddev = compute_std_dev(values, mean);
        const double recent_avg = compute_recent_average(values, 3);
        const double strength = mean / event_avg;
        const double consistency = 1.0 / (1.0 + stddev);
        double complement = 0.0;
        if (self_avg >= event_avg) {
            complement = event_avg / (mean + event_avg);
        } else {
            complement = mean / (mean + event_avg);
        }
        const double gap = std::abs(mean - self_avg) / event_avg;
        const double overlap = std::max(0.0, 1.0 - gap);
        const double trend = event_avg > 0.0 ? (recent_avg - mean) / event_avg : 0.0;
        const double confidence = std::min(1.0,
                                           static_cast<double>(values.size())
                                               / static_cast<double>(confidence_match_count));

        PicklistEntry pick;
        pick.team_key = team_key;
        pick.average_score = mean;
        pick.stddev = stddev;
        pick.trend = trend;
        pick.matches = static_cast<int>(values.size());
        pick.confidence = confidence;
        pick.complement = complement;
        pick.overlap_penalty = overlap;
        pick.picklist_score = weights.strength * strength
            + weights.consistency * consistency
            + weights.trend * trend
            + weights.complement * complement
            - weights.overlap * overlap;
        summary.entries.push_back(pick);
    }

    std::sort(summary.entries.begin(), summary.entries.end(),
              [](const PicklistEntry& left, const PicklistEntry& right) {
        return left.picklist_score > right.picklist_score;
    });

    return summary;
}
