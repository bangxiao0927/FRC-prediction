#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.h"
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

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const std::string& arg : args) {
        if (arg == flag) {
            return true;
        }
    }
    return false;
}

void print_usage() {
    std::cout << "Usage: frc_prediction [--event EVENT_KEY] [--status|--matches|--rankings|--teams|--stats]\n";
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

    if (!show_status && !show_matches && !show_rankings && !show_teams && !show_stats) {
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

    if (show_stats) {
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

        std::cout << "Team Stats (" << event_key << "):\n";
        for (const auto& entry : stats) {
            const TeamStats& team_stats = entry.second;
            std::cout << "  " << entry.first
                      << " | matches=" << team_stats.matches_played
                      << " total=" << team_stats.total_score
                      << " avg=" << team_stats.average_score
                      << "\n";
        }
    }

    return 0;
}
