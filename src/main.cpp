#include <iostream>
#include <string>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

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
    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file << contents;
    return true;
}

bool write_stats_csv(const std::string& path,
                     const std::vector<std::pair<std::string, TeamStats>>& ordered,
                     int limit) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    file << "rank,team_key,matches_played,total_score,average_score\n";
    for (int index = 0; index < limit; ++index) {
        const auto& entry = ordered[index];
        const TeamStats& team_stats = entry.second;
        file << (index + 1) << ","
             << entry.first << ","
             << team_stats.matches_played << ","
             << team_stats.total_score << ","
             << team_stats.average_score << "\n";
    }

    return true;
}

void print_usage() {
    std::cout << "Usage: frc_prediction [--event EVENT_KEY] [--status|--matches|--rankings|--teams|--stats|--stats-json|--predict MATCH_KEY|--predict-upcoming] [--top N] [--json] [--output FILE] [--stats-csv FILE]\n";
}

std::string default_prediction_output_path(const std::string& event_key, const std::string& match_key) {
    if (match_key.empty()) {
        return "data/predictions/" + event_key + "_prediction.json";
    }
    return "data/predictions/" + match_key + ".json";
}

}  // namespace

int main(int argc, char** argv) {
    const Config config = load_config();
    if (config.tba_auth_key.empty() || config.tba_auth_key == "your_key_here") {
        std::cerr << "Missing TBA API key.\n";
        std::cerr << "Set TBA_AUTH_KEY or copy config.example.json to config.json.\n";
        return 1;
    }

    const std::vector<std::string> args(argv + 1, argv + argc);
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
    const std::string output_path = get_arg_value(args, "--output");
    const std::string stats_csv_path = get_arg_value(args, "--stats-csv");
    const int top_count = get_arg_int(args, "--top", 0);

    if (!show_status && !show_matches && !show_rankings && !show_teams && !show_stats && !show_stats_json
        && predict_match_key.empty() && !predict_upcoming) {
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

        std::map<std::string, TeamStats> stats = compute_team_stats(matches);
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

        if (!stats_csv_path.empty()) {
            if (!write_stats_csv(stats_csv_path, ordered, limit)) {
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

        std::map<std::string, TeamStats> stats = compute_team_stats(matches);
        if (stats.empty()) {
            std::cerr << "No stats computed for " << event_key << ".\n";
            return 1;
        }

        nlohmann::json match;
        if (!predict_match_key.empty()) {
            for (const auto& entry : matches) {
                if (!entry.contains("key")) {
                    continue;
                }
                if (entry["key"].is_string() && entry["key"].get<std::string>() == predict_match_key) {
                    match = entry;
                    break;
                }
            }

            if (match.is_null()) {
                std::cerr << "Match key not found: " << predict_match_key << "\n";
                return 1;
            }
        } else {
            double best_time = std::numeric_limits<double>::max();
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
                    continue;
                }

                double time = entry.value("time", 0.0);
                if (time <= 0.0) {
                    continue;
                }
                if (time < best_time) {
                    best_time = time;
                    match = entry;
                }
            }

            if (match.is_null()) {
                std::cerr << "No upcoming match found for " << event_key << ".\n";
                return 1;
            }
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
                {"red_score_estimate", prediction.red_score_estimate},
                {"blue_score_estimate", prediction.blue_score_estimate},
                {"red_score_total_estimate", prediction.red_score_total_estimate},
                {"blue_score_total_estimate", prediction.blue_score_total_estimate},
                {"score_diff_estimate", prediction.score_diff_estimate},
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
            output << "  red_estimate=" << prediction.red_score_estimate
                   << " blue_estimate=" << prediction.blue_score_estimate << "\n";
            output << "  red_total=" << prediction.red_score_total_estimate
                   << " blue_total=" << prediction.blue_score_total_estimate
                   << " diff=" << prediction.score_diff_estimate << "\n";
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

    return 0;
}
