#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include <chrono>
#include <ctime>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "config.h"
#include "predictor.h"
#include "tba_client.h"
#include "stats.h"

namespace {

std::string get_arg_value(const std::vector<std::string>& args, const std::string& flag) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) {
            return args[i + 1];
        }
    }
    return "";
}

int get_arg_int(const std::vector<std::string>& args, const std::string& flag, int fallback) {
    std::string value = get_arg_value(args, flag);
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const std::string& arg : args) {
        if (arg == flag) {
            return true;
        }
    }
    return false;
}

bool write_text_file(const std::string& path, const std::string& contents) {
    std::filesystem::path file_path(path);
    if (file_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(file_path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file << contents;
    return true;
}

std::string current_timestamp_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t time_now = std::chrono::system_clock::to_time_t(now);
    std::tm utc_time{};
#ifdef _WIN32
    gmtime_s(&utc_time, &time_now);
#else
    gmtime_r(&time_now, &utc_time);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
    return std::string(buffer);
}

bool write_stats_csv(const std::string& path,
                     const std::vector<std::pair<std::string, TeamStats>>& ordered,
                     int limit,
                     double event_average_score) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    file << "rank,team_key,matches_played,total_score,average_score,event_average_score\n";
    for (int index = 0; index < limit; ++index) {
        const auto& entry = ordered[index];
        const TeamStats& team_stats = entry.second;
        file << (index + 1) << ","
             << entry.first << ","
             << team_stats.matches_played << ","
             << team_stats.total_score << ","
             << team_stats.average_score << ","
             << event_average_score << "\n";
    }

    return true;
}

void print_usage() {
    std::cout << "Usage: frc_prediction [--event EVENT_KEY] [--status|--matches|--rankings|--teams|--stats|--stats-json|--predict MATCH_KEY|--predict-upcoming|--evaluate] [--top N] [--json] [--output FILE] [--stats-csv FILE] [--phase qm|elim|all] [--eval-json FILE] [--eval-csv FILE]\n";
}

std::string default_prediction_output_path(const std::string& event_key, const std::string& match_key) {
    if (match_key.empty()) {
        return "data/predictions/" + event_key + "_prediction.json";
    }
    return "data/predictions/" + match_key + ".json";
}

// Maps a --phase argument to a stats filter. Returns false for an unknown value.
bool resolve_phase_filter(const std::string& phase_arg,
                          MatchFilter default_filter,
                          MatchFilter& out_filter) {
    if (phase_arg.empty() || phase_arg == "all") {
        out_filter = default_filter;
        return true;
    }
    if (phase_arg == "qm") {
        out_filter = MatchFilter::QualificationOnly;
        return true;
    }
    if (phase_arg == "elim") {
        out_filter = MatchFilter::QualificationPlusElimPlayed;
        return true;
    }
    return false;
}

// Expands user-friendly match keys into full TBA keys, e.g. for event 2024casj:
//   "3"            -> "2024casj_qm3"   (bare number = qualification)
//   "qm3"          -> "2024casj_qm3"
//   "sf2m1"        -> "2024casj_sf2m1"
//   "2024casj_qm3" -> unchanged (already a full key)
std::string normalize_match_key(const std::string& event_key, const std::string& raw) {
    if (raw.empty()) {
        return raw;
    }

    std::string key = raw;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Already a full key (contains the event prefix or any underscore).
    if (key.rfind(event_key + "_", 0) == 0 || key.find('_') != std::string::npos) {
        return key;
    }

    const bool all_digits =
        !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
    if (all_digits) {
        return event_key + "_qm" + key;
    }

    // Comp-level shorthand such as qm3, qf3m1, sf2m1, f1m2.
    const bool starts_with_level =
        key.rfind("qm", 0) == 0 || key.rfind("qf", 0) == 0 ||
        key.rfind("sf", 0) == 0 || key.rfind("f", 0) == 0;
    if (starts_with_level) {
        return event_key + "_" + key;
    }

    return event_key + "_" + key;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (has_flag(args, "--help") || has_flag(args, "-h")) {
        print_usage();
        return 0;
    }

    const Config config = load_config();
    if (config.tba_auth_key.empty() || config.tba_auth_key == "your_key_here") {
        std::cerr << "Missing TBA API key.\n";
        std::cerr << "Set TBA_AUTH_KEY or copy config.example.json to config.json.\n";
        return 1;
    }

    const std::string event_key_arg = get_arg_value(args, "--event");
    const std::string event_key = event_key_arg.empty() ? config.default_event_key : event_key_arg;
    const bool show_status = has_flag(args, "--status");
    const bool show_matches = has_flag(args, "--matches");
    const bool show_rankings = has_flag(args, "--rankings");
    const bool show_teams = has_flag(args, "--teams");
    const bool show_stats = has_flag(args, "--stats");
    const bool show_stats_json = has_flag(args, "--stats-json");
    const std::string predict_match_key = get_arg_value(args, "--predict");
    const bool predict_upcoming = has_flag(args, "--predict-upcoming");
    const bool output_json = has_flag(args, "--json");
    const bool evaluate_model = has_flag(args, "--evaluate");
    const std::string output_path = get_arg_value(args, "--output");
    const std::string stats_csv_path = get_arg_value(args, "--stats-csv");
    const std::string phase_arg = get_arg_value(args, "--phase");
    const std::string eval_json_path = get_arg_value(args, "--eval-json");
    const std::string eval_csv_path = get_arg_value(args, "--eval-csv");
    const int top_count = get_arg_int(args, "--top", 0);

    if (!show_status && !show_matches && !show_rankings && !show_teams && !show_stats && !show_stats_json
        && predict_match_key.empty() && !predict_upcoming && !evaluate_model) {
        print_usage();
        std::cout << "No output flag provided. Try --status or --matches.\n";
        return 1;
    }

    TbaClient client(config.tba_auth_key, config.cache_dir, config.cache_ttl_seconds);
    if (show_status) {
        nlohmann::json status = client.get_status();
        if (status.empty()) {
            std::cerr << "Failed to fetch TBA status.\n";
            return 1;
        }
        std::cout << "TBA Status: " << status.dump(2) << "\n";
    }

    if (show_matches) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }
        std::cout << "Event Matches (" << event_key << "): " << matches.dump(2) << "\n";
    }

    if (show_rankings) {
        nlohmann::json rankings = client.get_event_rankings(event_key);
        if (rankings.empty()) {
            std::cerr << "Failed to fetch event rankings for " << event_key << ".\n";
            return 1;
        }
        std::cout << "Event Rankings (" << event_key << "): " << rankings.dump(2) << "\n";
    }

    if (show_teams) {
        nlohmann::json teams = client.get_event_teams(event_key);
        if (teams.empty()) {
            std::cerr << "Failed to fetch event teams for " << event_key << ".\n";
            return 1;
        }
        std::cout << "Event Teams (" << event_key << "): " << teams.dump(2) << "\n";
    }

    if (show_stats || show_stats_json) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        MatchFilter stats_filter = MatchFilter::AllPlayed;
        if (!resolve_phase_filter(phase_arg, MatchFilter::AllPlayed, stats_filter)) {
            std::cerr << "Unknown phase: " << phase_arg << ". Use qm, elim, or all.\n";
            return 1;
        }
        std::map<std::string, TeamStats> stats = compute_team_stats(matches, stats_filter);
        if (stats.empty()) {
            std::cerr << "No stats computed for " << event_key << ".\n";
            return 1;
        }

        std::vector<std::pair<std::string, TeamStats>> ordered(stats.begin(), stats.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.second.average_score > right.second.average_score;
        });

        int limit = top_count > 0 ? std::min(top_count, static_cast<int>(ordered.size()))
                                  : static_cast<int>(ordered.size());

        double event_average = compute_event_average_score(stats);
        if (!stats_csv_path.empty()) {
            if (!write_stats_csv(stats_csv_path, ordered, limit, event_average)) {
                std::cerr << "Failed to write stats CSV to " << stats_csv_path << ".\n";
                return 1;
            }
            std::cout << "Wrote stats CSV to " << stats_csv_path << "\n";
        }

        if (show_stats_json) {
            nlohmann::json output = nlohmann::json::array();
            for (int index = 0; index < limit; ++index) {
                const auto& entry = ordered[index];
                const TeamStats& team_stats = entry.second;
                output.push_back({
                    {"team_key", entry.first},
                    {"matches_played", team_stats.matches_played},
                    {"total_score", team_stats.total_score},
                    {"average_score", team_stats.average_score}
                });
            }
            std::cout << output.dump(2) << "\n";
        } else {
            std::cout << "Team Stats (" << event_key << "):\n";
            for (int index = 0; index < limit; ++index) {
                const auto& entry = ordered[index];
                const TeamStats& team_stats = entry.second;
                std::cout << "  " << entry.first
                          << " | matches=" << team_stats.matches_played
                          << " total=" << team_stats.total_score
                          << " avg=" << team_stats.average_score
                          << "\n";
            }
        }
    }

    if (!predict_match_key.empty() || predict_upcoming) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        nlohmann::json match;
        if (!predict_match_key.empty()) {
            const std::string resolved_match_key =
                normalize_match_key(event_key, predict_match_key);
            for (const auto& entry : matches) {
                if (!entry.contains("key")) {
                    continue;
                }
                if (entry["key"].is_string() && entry["key"].get<std::string>() == resolved_match_key) {
                    match = entry;
                    break;
                }
            }

            if (match.is_null()) {
                std::cerr << "Match key not found: " << resolved_match_key
                          << " (from \"" << predict_match_key << "\").\n";
                std::cerr << "Try a full key like " << event_key
                          << "_qm3, or a shorthand like 3 / qm3 / sf2m1.\n";
                return 1;
            }
        } else {
            // Pick the "next" unplayed match. TBA does not always return matches
            // in schedule order and `time` is frequently missing (0/null), so we
            // rank candidates instead of skipping anything:
            //   1. matches scheduled in the future (time >= now), earliest first
            //   2. otherwise any timed match, earliest first
            //   3. otherwise the first unscored match in TBA order (schedule order)
            const double now_seconds = static_cast<double>(std::time(nullptr));
            bool have_candidate = false;
            bool best_is_future = false;
            double best_time = 0.0;
            bool best_has_time = false;

            for (const auto& entry : matches) {
                if (!entry.contains("alliances") || !entry["alliances"].is_object()) {
                    continue;
                }
                const nlohmann::json& alliances = entry["alliances"];
                if (!alliances.contains("red") || !alliances.contains("blue")) {
                    continue;
                }
                int red_score = alliances["red"].value("score", -1);
                int blue_score = alliances["blue"].value("score", -1);
                if (red_score >= 0 || blue_score >= 0) {
                    continue;  // already played
                }

                const double time = entry.value("time", 0.0);
                const bool has_time = time > 0.0;
                const bool is_future = has_time && time >= now_seconds;

                if (!have_candidate) {
                    match = entry;
                    have_candidate = true;
                    best_is_future = is_future;
                    best_has_time = has_time;
                    best_time = time;
                    continue;
                }

                // Prefer future matches, then any timed match, then earliest time.
                bool replace = false;
                if (is_future && !best_is_future) {
                    replace = true;
                } else if (is_future == best_is_future) {
                    if (has_time && !best_has_time) {
                        replace = true;
                    } else if (has_time && best_has_time && time < best_time) {
                        replace = true;
                    }
                }

                if (replace) {
                    match = entry;
                    best_is_future = is_future;
                    best_has_time = has_time;
                    best_time = time;
                }
            }

            if (match.is_null()) {
                std::cerr << "No upcoming match found for " << event_key << ".\n";
                return 1;
            }
        }

        MatchFilter filter = MatchFilter::QualificationPlusElimPlayed;
        if (match.value("comp_level", "") == "qm") {
            filter = MatchFilter::QualificationOnly;
        }
        // Only use matches scheduled before this one, so the prediction reflects
        // what was known at match time (and works the same live or in replay).
        std::map<std::string, TeamStats> stats =
            compute_team_stats_before(matches, filter, match);
        if (stats.empty()) {
            std::cerr << "Warning: no prior matches before " << match.value("key", "")
                      << "; prediction will be a coin flip.\n";
        }

        MatchPrediction prediction = predict_match(
            match,
            stats,
            config.confidence_match_count,
            config.score_diff_scale,
            config.sigmoid_scale);
        std::string match_key = match.value("key", "");
        const std::string resolved_output_path = output_path.empty()
            ? default_prediction_output_path(event_key, match_key)
            : output_path;
        if (output_json) {
            nlohmann::json output = {
                {"match_key", match_key},
                {"model_version", config.model_version},
                {"red_teams", prediction.red_teams},
                {"blue_teams", prediction.blue_teams},
                {"red_team_count", prediction.red_team_count},
                {"blue_team_count", prediction.blue_team_count},
                {"red_total_matches", prediction.red_total_matches},
                {"blue_total_matches", prediction.blue_total_matches},
                {"red_average_matches", prediction.red_average_matches},
                {"blue_average_matches", prediction.blue_average_matches},
                {"red_score_estimate", prediction.red_score_estimate},
                {"blue_score_estimate", prediction.blue_score_estimate},
                {"red_score_total_estimate", prediction.red_score_total_estimate},
                {"blue_score_total_estimate", prediction.blue_score_total_estimate},
                {"score_diff_estimate", prediction.score_diff_estimate},
                {"event_average_score", prediction.event_average_score},
                {"red_adjusted_average", prediction.red_adjusted_average},
                {"blue_adjusted_average", prediction.blue_adjusted_average},
                {"adjusted_score_diff_estimate", prediction.adjusted_score_diff_estimate},
                {"red_win_probability", prediction.red_win_probability},
                {"blue_win_probability", prediction.blue_win_probability},
                {"red_confidence", prediction.red_confidence},
                {"blue_confidence", prediction.blue_confidence}
            };
            std::string payload = output.dump(2);
            if (!resolved_output_path.empty()) {
                if (!write_text_file(resolved_output_path, payload)) {
                    std::cerr << "Failed to write output to " << resolved_output_path << ".\n";
                    return 1;
                }
                std::cout << "Wrote prediction JSON to " << resolved_output_path << "\n";
            } else {
                std::cout << payload << "\n";
            }
        } else {
            std::ostringstream output;
            output << "Prediction for " << match_key << ":\n";
            output << "  model_version=" << config.model_version << "\n";
            output << "  red_teams=";
            for (size_t i = 0; i < prediction.red_teams.size(); ++i) {
                if (i > 0) {
                    output << ",";
                }
                output << prediction.red_teams[i];
            }
            output << "\n";
            output << "  blue_teams=";
            for (size_t i = 0; i < prediction.blue_teams.size(); ++i) {
                if (i > 0) {
                    output << ",";
                }
                output << prediction.blue_teams[i];
            }
            output << "\n";
            output << "  red_matches=" << prediction.red_total_matches
                   << " blue_matches=" << prediction.blue_total_matches
                   << " red_avg_matches=" << prediction.red_average_matches
                   << " blue_avg_matches=" << prediction.blue_average_matches << "\n";
            output << "  red_estimate=" << prediction.red_score_estimate
                   << " blue_estimate=" << prediction.blue_score_estimate << "\n";
            output << "  red_total=" << prediction.red_score_total_estimate
                   << " blue_total=" << prediction.blue_score_total_estimate
                   << " diff=" << prediction.score_diff_estimate << "\n";
            output << "  event_avg=" << prediction.event_average_score
                   << " red_adj_avg=" << prediction.red_adjusted_average
                   << " blue_adj_avg=" << prediction.blue_adjusted_average
                   << " adj_diff=" << prediction.adjusted_score_diff_estimate << "\n";
            output << "  red_win_prob=" << prediction.red_win_probability
                   << " blue_win_prob=" << prediction.blue_win_probability << "\n";
            output << "  red_confidence=" << prediction.red_confidence
                   << " blue_confidence=" << prediction.blue_confidence << "\n";

            if (!resolved_output_path.empty()) {
                if (!write_text_file(resolved_output_path, output.str())) {
                    std::cerr << "Failed to write output to " << resolved_output_path << ".\n";
                    return 1;
                }
                std::cout << "Wrote prediction text to " << resolved_output_path << "\n";
            } else {
                std::cout << output.str();
            }
        }
    }

    if (evaluate_model) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        if (!(phase_arg.empty() || phase_arg == "all" ||
              phase_arg == "qm" || phase_arg == "elim")) {
            std::cerr << "Unknown phase: " << phase_arg << ". Use qm, elim, or all.\n";
            return 1;
        }

        int evaluated = 0;
        int correct_winner = 0;
        double total_abs_error = 0.0;
        for (const auto& match : matches) {
            if (!match.contains("alliances") || !match["alliances"].is_object()) {
                continue;
            }
            const nlohmann::json& alliances = match["alliances"];
            if (!alliances.contains("red") || !alliances.contains("blue")) {
                continue;
            }
            int red_score = alliances["red"].value("score", -1);
            int blue_score = alliances["blue"].value("score", -1);
            if (red_score < 0 || blue_score < 0) {
                continue;
            }

            // --phase scopes which matches are evaluated.
            const std::string level = match.value("comp_level", "");
            const bool is_qm = level == "qm";
            const bool is_elim = level == "qf" || level == "sf" || level == "f";
            if (phase_arg == "qm" && !is_qm) {
                continue;
            }
            if (phase_arg == "elim" && !is_elim) {
                continue;
            }

            // Backtest honestly: score each match using only the matches that
            // happened before it, never the match itself or later ones.
            const MatchFilter match_filter = is_qm
                ? MatchFilter::QualificationOnly
                : MatchFilter::QualificationPlusElimPlayed;
            std::map<std::string, TeamStats> stats =
                compute_team_stats_before(matches, match_filter, match);

            MatchPrediction prediction = predict_match(
                match,
                stats,
                config.confidence_match_count,
                config.score_diff_scale,
                config.sigmoid_scale);
            double predicted_diff = prediction.adjusted_score_diff_estimate;
            double actual_diff = static_cast<double>(red_score - blue_score);
            total_abs_error += std::abs(actual_diff - predicted_diff);

            bool predicted_red = prediction.red_win_probability >= 0.5;
            bool actual_red = actual_diff >= 0.0;
            if (predicted_red == actual_red) {
                correct_winner += 1;
            }

            evaluated += 1;
        }

        if (evaluated == 0) {
            std::cerr << "No completed matches available for evaluation.\n";
            return 1;
        }

        double mae = total_abs_error / static_cast<double>(evaluated);
        double accuracy = static_cast<double>(correct_winner) / static_cast<double>(evaluated);
        const std::string timestamp = current_timestamp_iso8601();
        if (!eval_json_path.empty()) {
            nlohmann::json output = {
                {"timestamp", timestamp},
                {"event_key", event_key},
                {"phase", phase_arg.empty() ? "all" : phase_arg},
                {"matches", evaluated},
                {"mae", mae},
                {"winner_accuracy", accuracy}
            };
            if (!write_text_file(eval_json_path, output.dump(2))) {
                std::cerr << "Failed to write evaluation JSON to " << eval_json_path << ".\n";
                return 1;
            }
            std::cout << "Wrote evaluation JSON to " << eval_json_path << "\n";
        }

        if (!eval_csv_path.empty()) {
            bool write_header = !std::ifstream(eval_csv_path).good();
            std::ofstream file(eval_csv_path, std::ios::app);
            if (!file) {
                std::cerr << "Failed to write evaluation CSV to " << eval_csv_path << ".\n";
                return 1;
            }
            if (write_header) {
                file << "timestamp,event_key,phase,matches,mae,winner_accuracy\n";
            }
            file << timestamp << ","
                 << event_key << ","
                 << (phase_arg.empty() ? "all" : phase_arg) << ","
                 << evaluated << ","
                 << mae << ","
                 << accuracy << "\n";
            std::cout << "Wrote evaluation CSV to " << eval_csv_path << "\n";
        }

        if (eval_json_path.empty() && eval_csv_path.empty()) {
            std::cout << "Evaluation (" << event_key << "):\n";
            std::cout << "  timestamp=" << timestamp << "\n";
            std::cout << "  phase=" << (phase_arg.empty() ? "all" : phase_arg) << "\n";
            std::cout << "  matches=" << evaluated << "\n";
            std::cout << "  mae=" << mae << "\n";
            std::cout << "  winner_accuracy=" << accuracy << "\n";
        }
    }

    return 0;
}
