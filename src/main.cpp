#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include <chrono>
#include <ctime>
#include <csignal>
#include <thread>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "config.h"
#include "predictor.h"
#include "opr.h"
#include "roles.h"
#include "synergy.h"
#include "history.h"
#include "picklist.h"
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
    std::cout << "Usage: frc_prediction [--event EVENT_KEY] [--status|--matches|--rankings|--teams|--event-options|--events-year YEAR|--stats|--stats-json|--roles|--predict MATCH_KEY|--predict-upcoming|--evaluate|--live|--picklist TEAM_KEY|--alliance TEAMS] [--vs TEAMS] [--top N] [--json] [--use-history] [--history-teams TEAMS] [--output FILE] [--stats-csv FILE] [--phase qm|elim|all] [--before MATCH_KEY] [--strategy balanced|offense|consistency] [--exclude TEAMS] [--picklist-csv FILE] [--eval-json FILE] [--eval-csv FILE] [--live-interval SECONDS]\n";
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

// Finds a match by key (accepting shorthand) in the event's match list.
nlohmann::json find_match_by_key(const nlohmann::json& matches,
                                 const std::string& event_key,
                                 const std::string& raw_key) {
    const std::string resolved = normalize_match_key(event_key, raw_key);
    for (const auto& entry : matches) {
        if (entry.contains("key") && entry["key"].is_string() &&
            entry["key"].get<std::string>() == resolved) {
            return entry;
        }
    }
    return nlohmann::json(nullptr);
}

// Maps a --strategy name to picklist weights. Returns false for unknown names.
bool resolve_strategy(const std::string& strategy, PicklistWeights& out) {
    if (strategy.empty() || strategy == "balanced") {
        out = PicklistWeights{0.45, 0.25, 0.1, 0.25, 0.15};
        return true;
    }
    if (strategy == "offense") {
        out = PicklistWeights{0.6, 0.15, 0.1, 0.3, 0.1};
        return true;
    }
    if (strategy == "consistency") {
        out = PicklistWeights{0.3, 0.5, 0.1, 0.3, 0.1};
        return true;
    }
    return false;
}

// Parses a comma-separated team list into normalized team keys (e.g. "254" or
// "frc254" -> "frc254").
std::set<std::string> parse_team_set(const std::string& csv) {
    std::set<std::string> teams;
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        // Trim surrounding whitespace.
        const size_t start = token.find_first_not_of(" \t");
        const size_t end = token.find_last_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        std::string key = token.substr(start, end - start + 1);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key.rfind("frc", 0) != 0) {
            key = "frc" + key;
        }
        teams.insert(key);
    }
    return teams;
}

// Like parse_team_set but preserves input order and duplicates, for an alliance
// lineup where position and exact membership matter.
std::vector<std::string> parse_team_list(const std::string& csv) {
    std::vector<std::string> teams;
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const size_t start = token.find_first_not_of(" \t");
        const size_t end = token.find_last_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        std::string key = token.substr(start, end - start + 1);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key.rfind("frc", 0) != 0) {
            key = "frc" + key;
        }
        teams.push_back(key);
    }
    return teams;
}

std::string normalize_team_key(const std::string& raw) {
    std::string key = raw;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (key.rfind("frc", 0) != 0) {
        key = "frc" + key;
    }
    return key;
}

// Team keys participating in a match (both alliances), in red-then-blue order.
std::vector<std::string> match_team_keys(const nlohmann::json& match) {
    std::vector<std::string> teams;
    if (!match.contains("alliances") || !match["alliances"].is_object()) {
        return teams;
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
        for (const auto& key : alliance["team_keys"]) {
            if (key.is_string()) {
                teams.push_back(key.get<std::string>());
            }
        }
    }
    return teams;
}

// Best available timestamp for a match (scheduled or actual).
double match_time_value(const nlohmann::json& match) {
    // TBA frequently returns these fields as null (not just absent); json::value
    // throws on a present-but-null value, so read defensively.
    for (const char* key : {"time", "actual_time", "predicted_time"}) {
        if (!match.contains(key) || !match[key].is_number()) {
            continue;
        }
        const double when = match[key].get<double>();
        if (when > 0.0) {
            return when;
        }
    }
    return 0.0;
}

// Earliest known timestamp across an event's matches, i.e. roughly when the event
// started. Used as a leak-free history cutoff when the target match itself has no
// usable time: anything another event played before this event began is safely
// "prior", while matches at or after the event start are excluded.
double event_start_time(const nlohmann::json& matches) {
    double earliest = 0.0;
    if (!matches.is_array()) {
        return earliest;
    }
    for (const auto& match : matches) {
        const double when = match_time_value(match);
        if (when > 0.0 && (earliest == 0.0 || when < earliest)) {
            earliest = when;
        }
    }
    return earliest;
}

// Subset of an event's matches whose timestamp is strictly before `before`.
// Matches without a usable time are dropped when a cutoff is in force, so a
// partially-overlapping event never leaks matches at/after the cutoff.
nlohmann::json matches_before_time(const nlohmann::json& matches, double before) {
    if (!matches.is_array() || before <= 0.0) {
        return matches;
    }
    nlohmann::json out = nlohmann::json::array();
    for (const auto& match : matches) {
        const double when = match_time_value(match);
        if (when > 0.0 && when < before) {
            out.push_back(match);
        }
    }
    return out;
}

// How many of the given matches included `team` on either alliance.
int count_team_matches(const nlohmann::json& matches, const std::string& team) {
    int count = 0;
    if (!matches.is_array()) {
        return count;
    }
    for (const auto& match : matches) {
        const std::vector<std::string> teams = match_team_keys(match);
        if (std::find(teams.begin(), teams.end(), team) != teams.end()) {
            count += 1;
        }
    }
    return count;
}

// A team's cross-event historical prior, decomposed per phase. At each OTHER
// event it played this season (restricted to matches before the cutoff) we
// compute the team's auto / teleop / endgame OPR and its total OPR, then average
// those across events weighted by matches played. The per-phase profile lets the
// blender trust each phase independently; `scalar` is the foul-free scoring sum
// (or total OPR when an event lacks a breakdown) used when phase data is absent.
struct TeamHistory {
    bool has_any = false;       // any usable prior at all
    bool has_phase = false;     // a per-phase profile is available
    PhaseRatings phases;        // averaged auto/teleop/endgame (valid if has_phase)
    double scalar = 0.0;        // averaged scoring/total OPR (valid if has_any)
};

TeamHistory team_history(TbaClient& client,
                         const nlohmann::json& season,
                         const std::string& team,
                         const std::string& event_key,
                         double before) {
    TeamHistory history;
    if (!season.is_array()) {
        return history;
    }
    // Distinct other events where the team has at least one match before cutoff.
    std::set<std::string> other_events;
    for (const auto& match : season) {
        const std::string ev = match.value("event_key", "");
        if (ev.empty() || ev == event_key) {
            continue;
        }
        const double when = match_time_value(match);
        if (when > 0.0 && when < before) {
            other_events.insert(ev);
        }
    }

    double scalar_sum = 0.0;
    double scalar_weight = 0.0;
    PhaseRatings phase_sum;
    double phase_weight = 0.0;
    for (const auto& ev : other_events) {
        const nlohmann::json full = client.get_event_matches(ev);
        const nlohmann::json prior_matches = matches_before_time(full, before);
        const std::map<std::string, TeamRole> roles =
            compute_team_roles(prior_matches, MatchFilter::AllPlayed);
        const auto it = roles.find(team);
        if (it == roles.end()) {
            continue;
        }
        const TeamRole& role = it->second;
        const int weight = count_team_matches(prior_matches, team);
        if (weight <= 0) {
            continue;
        }
        const double w = static_cast<double>(weight);
        const double scoring = role.auto_phase + role.teleop_phase + role.endgame_phase;
        // Prefer the foul-free scoring OPR; fall back to total OPR (offense) when
        // this event has no usable phase breakdown.
        const bool event_has_phase = role.has_phase_data && scoring > 0.0;
        scalar_sum += (event_has_phase ? scoring : role.offense) * w;
        scalar_weight += w;
        if (event_has_phase) {
            phase_sum.autonomous += role.auto_phase * w;
            phase_sum.teleop += role.teleop_phase * w;
            phase_sum.endgame += role.endgame_phase * w;
            phase_weight += w;
        }
    }

    if (scalar_weight > 0.0) {
        history.has_any = true;
        history.scalar = scalar_sum / scalar_weight;
        if (phase_weight > 0.0) {
            history.has_phase = true;
            history.phases.autonomous = phase_sum.autonomous / phase_weight;
            history.phases.teleop = phase_sum.teleop / phase_weight;
            history.phases.endgame = phase_sum.endgame / phase_weight;
        }
        return history;
    }

    // Fallback: the cruder per-team score form (still points-per-team scale).
    const TeamForm form = compute_team_form(season, team, event_key, before);
    if (form.matches > 0) {
        history.has_any = true;
        history.scalar = form.per_team_score;
    }
    return history;
}

// Blend current-event OPR with each match team's prior-season form. Where both a
// current and a historical phase profile exist, blends per phase (each phase gets
// its own confidence weight); otherwise blends the totals with a single weight.
// Falls back to the unblended OPRs when the year can't be derived or no history is
// available. When `history_teams` is non-empty, only those teams get history.
std::map<std::string, double> blended_oprs_with_history(
    TbaClient& client,
    const nlohmann::json& match,
    const nlohmann::json& event_matches,
    MatchFilter filter,
    const std::string& event_key,
    const std::map<std::string, double>& current_oprs,
    const std::map<std::string, TeamStats>& stats,
    int confidence_match_count,
    const PhaseConfidence& phase_confidence,
    const std::set<std::string>& history_teams) {
    int year = 0;
    if (event_key.size() >= 4) {
        try {
            year = std::stoi(event_key.substr(0, 4));
        } catch (...) {
            year = 0;
        }
    }
    if (year <= 0) {
        return current_oprs;
    }
    // Only count history strictly before this match, so backtests stay honest.
    // Upcoming matches often have no timestamp; fall back to the event's start so
    // we never count another event that happened AFTER this one as "history".
    double before = match_time_value(match);
    if (before <= 0.0) {
        before = event_start_time(event_matches);
    }
    if (before <= 0.0) {
        // No usable cutoff at all: skip history rather than risk future leakage.
        return current_oprs;
    }

    // Current-event per-phase OPRs, using the same cutoff as the total OPR.
    const std::map<std::string, TeamRole> current_roles =
        compute_team_roles_before(event_matches, filter, match);
    std::map<std::string, PhaseRatings> current_phases;
    for (const auto& entry : current_roles) {
        const TeamRole& role = entry.second;
        if (role.has_phase_data) {
            current_phases[entry.first] =
                PhaseRatings{role.auto_phase, role.teleop_phase, role.endgame_phase};
        }
    }

    std::map<std::string, PhaseRatings> historical_phases;  // teams with phase history
    std::map<std::string, double> scalar_priors;            // teams with scalar-only history
    for (const auto& team : match_team_keys(match)) {
        // Optional filter: only blend history for the requested robots.
        if (!history_teams.empty() && history_teams.find(team) == history_teams.end()) {
            continue;
        }
        const nlohmann::json season = client.get_team_matches_year(team, year);
        const TeamHistory history = team_history(client, season, team, event_key, before);
        if (!history.has_any) {
            continue;
        }
        // Per-phase blend only when both sides have a phase profile.
        if (history.has_phase && current_phases.count(team) > 0) {
            historical_phases[team] = history.phases;
        } else {
            scalar_priors[team] = history.scalar;
        }
    }
    if (historical_phases.empty() && scalar_priors.empty()) {
        return current_oprs;
    }

    std::map<std::string, double> blended = current_oprs;
    if (!historical_phases.empty()) {
        const std::map<std::string, double> phase_blended = blend_phase_oprs(
            current_oprs, current_phases, historical_phases, stats, phase_confidence);
        for (const auto& team_entry : historical_phases) {
            const auto it = phase_blended.find(team_entry.first);
            if (it != phase_blended.end()) {
                blended[it->first] = it->second;
            }
        }
    }
    if (!scalar_priors.empty()) {
        const std::map<std::string, double> scalar_blended =
            blend_oprs(current_oprs, scalar_priors, stats, confidence_match_count);
        for (const auto& team_entry : scalar_priors) {
            const auto it = scalar_blended.find(team_entry.first);
            if (it != scalar_blended.end()) {
                blended[it->first] = it->second;
            }
        }
    }
    return blended;
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
    const bool show_event_options = has_flag(args, "--event-options");
    const std::string events_year_arg = get_arg_value(args, "--events-year");
    const bool show_events_year = !events_year_arg.empty();
    const bool show_stats = has_flag(args, "--stats");
    const bool show_stats_json = has_flag(args, "--stats-json");
    const bool show_roles = has_flag(args, "--roles");
    const std::string predict_match_key = get_arg_value(args, "--predict");
    const bool predict_upcoming = has_flag(args, "--predict-upcoming");
    const bool output_json = has_flag(args, "--json");
    const bool evaluate_model = has_flag(args, "--evaluate");
    const bool live_mode = has_flag(args, "--live");
    const int live_interval = get_arg_int(args, "--live-interval", 60);
    const std::string picklist_team_key = get_arg_value(args, "--picklist");
    const bool show_picklist = has_flag(args, "--picklist") || !picklist_team_key.empty();
    const std::string output_path = get_arg_value(args, "--output");
    const std::string stats_csv_path = get_arg_value(args, "--stats-csv");
    const std::string phase_arg = get_arg_value(args, "--phase");
    const std::string eval_json_path = get_arg_value(args, "--eval-json");
    const std::string eval_csv_path = get_arg_value(args, "--eval-csv");
    const std::string before_match_arg = get_arg_value(args, "--before");
    const std::string strategy_arg = get_arg_value(args, "--strategy");
    const std::string exclude_arg = get_arg_value(args, "--exclude");
    const std::string picklist_csv_path = get_arg_value(args, "--picklist-csv");
    const std::string alliance_arg = get_arg_value(args, "--alliance");
    const std::string alliance_vs_arg = get_arg_value(args, "--vs");
    const bool show_alliance = !alliance_arg.empty();
    // --history-teams restricts the historical blend to specific robots; passing
    // it also turns history on for convenience.
    const std::set<std::string> history_teams = parse_team_set(get_arg_value(args, "--history-teams"));
    const bool use_history =
        config.use_history || has_flag(args, "--use-history") || !history_teams.empty();
    const int top_count = get_arg_int(args, "--top", 0);

    if (!show_status && !show_matches && !show_rankings && !show_teams && !show_stats && !show_stats_json
        && !show_roles && predict_match_key.empty() && !predict_upcoming && !evaluate_model && !show_picklist
        && !show_alliance && !show_event_options && !show_events_year && !live_mode) {
        print_usage();
        std::cout << "No output flag provided. Try --status or --matches.\n";
        return 1;
    }

    if (live_mode && evaluate_model) {
        std::cerr << "--live and --evaluate are mutually exclusive. Use --live for "
                     "continuous polling or --evaluate for a one-shot backtest.\n";
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

    if (show_events_year) {
        int year = 0;
        try {
            year = std::stoi(events_year_arg);
        } catch (const std::exception&) {
            year = 0;
        }
        if (year <= 0) {
            std::cerr << "Invalid --events-year value: " << events_year_arg << "\n";
            return 1;
        }
        nlohmann::json events = client.get_events_by_year(year);
        // Sort by start date (then name) so the dropdown reads chronologically.
        std::vector<std::pair<std::string, nlohmann::json>> rows;
        if (events.is_array()) {
            for (const auto& event : events) {
                if (!event.contains("key") || !event["key"].is_string()) {
                    continue;
                }
                const std::string start = event.value("start_date", "");
                const std::string name = event.value("name", "");
                rows.push_back({start + "\x01" + name, nlohmann::json{
                    {"key", event["key"].get<std::string>()},
                    {"name", name},
                    {"start_date", start},
                    {"week", event.contains("week") && event["week"].is_number()
                                 ? event["week"].get<int>() : -1}
                }});
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        nlohmann::json event_list = nlohmann::json::array();
        for (const auto& row : rows) {
            event_list.push_back(row.second);
        }
        nlohmann::json output = {{"year", year}, {"events", event_list}};
        std::cout << output.dump(2) << "\n";
    }

    if (show_event_options) {
        // Compact lists for the web dashboard's dropdowns: the event's teams and
        // its matches (schedule-ordered, with friendly labels). Always JSON.
        nlohmann::json teams = client.get_event_teams(event_key);
        nlohmann::json matches = client.get_event_matches(event_key);

        std::vector<std::pair<int, nlohmann::json>> team_rows;
        if (teams.is_array()) {
            for (const auto& team : teams) {
                if (!team.contains("key") || !team["key"].is_string()) {
                    continue;
                }
                const int number = team.value("team_number", 0);
                team_rows.push_back({number, nlohmann::json{
                    {"key", team["key"].get<std::string>()},
                    {"team_number", number},
                    {"nickname", team.value("nickname", "")}
                }});
            }
        }
        std::sort(team_rows.begin(), team_rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        nlohmann::json team_list = nlohmann::json::array();
        for (const auto& row : team_rows) {
            team_list.push_back(row.second);
        }

        auto match_label = [](const nlohmann::json& match) {
            const std::string level = match.value("comp_level", "");
            const int set_number = match.value("set_number", 0);
            const int match_number = match.value("match_number", 0);
            if (level == "qm") {
                return std::string("Qual ") + std::to_string(match_number);
            }
            std::string label = level;
            std::transform(label.begin(), label.end(), label.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return label + " " + std::to_string(set_number) + "-" + std::to_string(match_number);
        };

        // Tag each match with the schedule ordering key used elsewhere, then sort.
        std::vector<std::pair<MatchOrderKey, nlohmann::json>> match_rows;
        if (matches.is_array()) {
            for (const auto& match : matches) {
                if (!match.contains("key") || !match["key"].is_string()) {
                    continue;
                }
                bool played = false;
                if (match.contains("alliances") && match["alliances"].is_object()) {
                    const auto& alliances = match["alliances"];
                    const int red = alliances.contains("red") ? alliances["red"].value("score", -1) : -1;
                    const int blue = alliances.contains("blue") ? alliances["blue"].value("score", -1) : -1;
                    played = red >= 0 && blue >= 0;
                }
                match_rows.push_back({match_order_key(match), nlohmann::json{
                    {"key", match["key"].get<std::string>()},
                    {"label", match_label(match)},
                    {"comp_level", match.value("comp_level", "")},
                    {"played", played}
                }});
            }
        }
        std::sort(match_rows.begin(), match_rows.end(),
                  [](const auto& a, const auto& b) { return match_order_before(a.first, b.first); });
        nlohmann::json match_list = nlohmann::json::array();
        for (const auto& row : match_rows) {
            match_list.push_back(row.second);
        }

        nlohmann::json output = {
            {"event_key", event_key},
            {"teams", team_list},
            {"matches", match_list}
        };
        std::cout << output.dump(2) << "\n";
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
        // --before makes the table reflect only matches before a target match,
        // so the whole dashboard can show "as of this match".
        std::map<std::string, TeamStats> stats;
        if (!before_match_arg.empty()) {
            nlohmann::json target = find_match_by_key(matches, event_key, before_match_arg);
            if (target.is_null()) {
                std::cerr << "Match key not found for --before: "
                          << normalize_match_key(event_key, before_match_arg) << "\n";
                return 1;
            }
            stats = compute_team_stats_before(matches, stats_filter, target);
        } else {
            stats = compute_team_stats(matches, stats_filter);
        }
        // With a cutoff, an early match can legitimately have no prior data; emit
        // an empty (header-only) result instead of failing.
        if (stats.empty() && before_match_arg.empty()) {
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

    if (show_roles) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        MatchFilter roles_filter = MatchFilter::AllPlayed;
        if (!resolve_phase_filter(phase_arg, MatchFilter::AllPlayed, roles_filter)) {
            std::cerr << "Unknown phase: " << phase_arg << ". Use qm, elim, or all.\n";
            return 1;
        }
        // --before lets the role profile reflect only matches before a target
        // match, matching the rest of the dashboard's "as of this match" view.
        nlohmann::json roles_target(nullptr);
        if (!before_match_arg.empty()) {
            roles_target = find_match_by_key(matches, event_key, before_match_arg);
            if (roles_target.is_null()) {
                std::cerr << "Match key not found for --before: "
                          << normalize_match_key(event_key, before_match_arg) << "\n";
                return 1;
            }
        }

        std::map<std::string, TeamRole> roles =
            compute_team_roles_before(matches, roles_filter, roles_target);
        if (roles.empty() && before_match_arg.empty()) {
            std::cerr << "No roles computed for " << event_key << ".\n";
            return 1;
        }

        // Rank by offense (total OPR) for a stable, meaningful default order.
        std::vector<std::pair<std::string, TeamRole>> ordered(roles.begin(), roles.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.second.offense > right.second.offense;
        });
        const int limit = top_count > 0 ? std::min(top_count, static_cast<int>(ordered.size()))
                                        : static_cast<int>(ordered.size());

        if (output_json) {
            nlohmann::json output = nlohmann::json::array();
            for (int index = 0; index < limit; ++index) {
                const TeamRole& role = ordered[index].second;
                output.push_back({
                    {"team_key", ordered[index].first},
                    {"primary_role", role.primary},
                    {"offense", role.offense},
                    {"auto", role.auto_phase},
                    {"teleop", role.teleop_phase},
                    {"endgame", role.endgame_phase},
                    {"defense", role.defense},
                    {"has_phase_data", role.has_phase_data},
                    {"has_endgame_data", role.has_endgame_data}
                });
            }
            std::cout << output.dump(2) << "\n";
        } else {
            std::cout << "Team Roles (" << event_key << "):\n";
            for (int index = 0; index < limit; ++index) {
                const TeamRole& role = ordered[index].second;
                std::cout << "  " << ordered[index].first
                          << " | role=" << role.primary
                          << " offense=" << role.offense
                          << " auto=" << role.auto_phase
                          << " teleop=" << role.teleop_phase
                          << " endgame=" << role.endgame_phase
                          << " defense=" << role.defense
                          << "\n";
            }
            if (!ordered.empty() && !ordered.front().second.has_phase_data) {
                std::cout << "  (note: no score_breakdown available; phase ratings are 0)\n";
            } else if (!ordered.empty() && !ordered.front().second.has_endgame_data) {
                std::cout << "  (note: this season's endgame breakdown is unknown; "
                             "endgame is 0 and teleop still includes endgame points)\n";
            }
        }
    }

    if (show_alliance) {
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        // A what-if lineup is not tied to a scheduled match, so it draws on every
        // played match at the event.
        const std::map<std::string, double> oprs =
            compute_team_oprs(matches, MatchFilter::AllPlayed);
        const std::map<std::string, TeamRole> roles =
            compute_team_roles(matches, MatchFilter::AllPlayed);
        const std::map<std::string, TeamStats> stats =
            compute_team_stats(matches, MatchFilter::AllPlayed);

        // Score the alliance with the SAME model the match predictor uses, so the
        // headline predicted_score and the --vs win probability never disagree.
        // OPR mode: contribution is each team's OPR, baseline is the mean OPR.
        // Legacy mode: contribution is each team's average alliance score, baseline
        // is the event average (matching predict_match's legacy path).
        std::map<std::string, double> contribution;
        double baseline_score = 0.0;
        if (config.use_opr) {
            contribution = oprs;
            double opr_total = 0.0;
            for (const auto& entry : oprs) {
                opr_total += entry.second;
            }
            baseline_score = oprs.empty() ? 0.0 : opr_total / static_cast<double>(oprs.size());
        } else {
            for (const auto& entry : stats) {
                contribution[entry.first] = entry.second.average_score;
            }
            baseline_score = compute_event_average_score(stats);
        }

        const std::vector<std::string> alliance = parse_team_list(alliance_arg);
        if (alliance.empty()) {
            std::cerr << "--alliance needs at least one team key (e.g. frc254,frc1678,frc604).\n";
            return 1;
        }
        const AllianceEvaluation red_eval =
            evaluate_alliance(alliance, contribution, roles, baseline_score);

        const std::vector<std::string> opponent = parse_team_list(alliance_vs_arg);
        const bool has_vs = !opponent.empty();
        AllianceEvaluation blue_eval;
        MatchPrediction matchup;
        if (has_vs) {
            blue_eval = evaluate_alliance(opponent, contribution, roles, baseline_score);
            // Reuse the match predictor by synthesizing a red-vs-blue match.
            nlohmann::json synthetic = {
                {"alliances", {
                    {"red", {{"team_keys", alliance}}},
                    {"blue", {{"team_keys", opponent}}}
                }}
            };
            matchup = predict_match(synthetic, stats, config.confidence_match_count,
                                    config.score_diff_scale, config.sigmoid_scale,
                                    config.use_opr ? oprs : std::map<std::string, double>{});
        }

        auto eval_to_json = [](const AllianceEvaluation& e) {
            return nlohmann::json{
                {"teams", e.teams},
                {"predicted_score", e.predicted_score},
                {"synergy_score", e.synergy_score},
                {"auto", e.auto_total},
                {"teleop", e.teleop_total},
                {"endgame", e.endgame_total},
                {"best_defense", e.best_defense},
                {"has_defense_data", e.has_defense_data},
                {"role_diversity", e.role_diversity},
                {"has_defender", e.has_defender},
                {"endgame_specialists", e.endgame_specialists},
                {"has_phase_data", e.has_phase_data},
                {"note", e.note}
            };
        };

        if (output_json) {
            nlohmann::json output;
            output["event_key"] = event_key;
            output["alliance"] = eval_to_json(red_eval);
            if (has_vs) {
                output["opponent"] = eval_to_json(blue_eval);
                output["red_win_probability"] = matchup.red_win_probability;
                output["blue_win_probability"] = matchup.blue_win_probability;
                output["adjusted_score_diff"] = matchup.adjusted_score_diff_estimate;
            }
            std::cout << output.dump(2) << "\n";
        } else {
            auto print_eval = [](const std::string& label, const AllianceEvaluation& e) {
                std::cout << label << " [";
                for (size_t i = 0; i < e.teams.size(); ++i) {
                    if (i > 0) {
                        std::cout << ",";
                    }
                    std::cout << e.teams[i];
                }
                std::cout << "]\n";
                std::cout << "  predicted_score=" << e.predicted_score
                          << " synergy_score=" << e.synergy_score << "\n";
                std::cout << "  auto=" << e.auto_total
                          << " teleop=" << e.teleop_total
                          << " endgame=" << e.endgame_total
                          << " best_defense=";
                if (e.has_defense_data) {
                    std::cout << e.best_defense;
                } else {
                    std::cout << "n/a";
                }
                std::cout << "\n";
                std::cout << "  roles=" << e.role_diversity
                          << " (" << e.note << ")\n";
            };
            std::cout << "Alliance Evaluation (" << event_key << "):\n";
            print_eval("Alliance", red_eval);
            if (has_vs) {
                print_eval("Opponent", blue_eval);
                std::cout << "Matchup: red_win_prob=" << matchup.red_win_probability
                          << " blue_win_prob=" << matchup.blue_win_probability
                          << " adj_diff=" << matchup.adjusted_score_diff_estimate << "\n";
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

        // OPR uses the same cutoff so the contribution model never sees the
        // match it is predicting or anything later.
        std::map<std::string, double> oprs = config.use_opr
            ? compute_team_oprs_before(matches, filter, match)
            : std::map<std::string, double>{};
        // Optionally blend in each team's prior-season form (cross-event history).
        if (config.use_opr && use_history) {
            const PhaseConfidence phase_confidence{
                config.history_auto_matches,
                config.history_teleop_matches,
                config.history_endgame_matches};
            oprs = blended_oprs_with_history(client, match, matches, filter, event_key, oprs,
                                             stats, config.confidence_match_count,
                                             phase_confidence, history_teams);
        }
        MatchPrediction prediction = predict_match(
            match,
            stats,
            config.confidence_match_count,
            config.score_diff_scale,
            config.sigmoid_scale,
            oprs);
        std::string match_key = match.value("key", "");
        const std::string resolved_output_path = output_path.empty()
            ? default_prediction_output_path(event_key, match_key)
            : output_path;
        if (output_json) {
            nlohmann::json output = {
                {"match_key", match_key},
                {"model_version", config.model_version},
                {"model_uses_opr", prediction.uses_opr},
                {"model_uses_history", config.use_opr && use_history},
                {"history_teams", std::vector<std::string>(history_teams.begin(), history_teams.end())},
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
            output << "  model_uses_opr=" << (prediction.uses_opr ? "true" : "false") << "\n";
            output << "  model_uses_history=" << ((config.use_opr && use_history) ? "true" : "false") << "\n";
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
        // When history is requested, score every match a second time with the
        // cross-event blend so we can report baseline vs. history side by side.
        const bool eval_history = config.use_opr && use_history;
        const PhaseConfidence phase_confidence{
            config.history_auto_matches,
            config.history_teleop_matches,
            config.history_endgame_matches};
        int history_correct_winner = 0;
        double history_total_abs_error = 0.0;
        if (eval_history) {
            std::cerr << "Evaluating with history (--use-history); this makes per-team "
                         "TBA calls per match and may be slow on a cold cache.\n";
        }
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

            std::map<std::string, double> oprs = config.use_opr
                ? compute_team_oprs_before(matches, match_filter, match)
                : std::map<std::string, double>{};
            MatchPrediction prediction = predict_match(
                match,
                stats,
                config.confidence_match_count,
                config.score_diff_scale,
                config.sigmoid_scale,
                oprs);
            double predicted_diff = prediction.adjusted_score_diff_estimate;
            double actual_diff = static_cast<double>(red_score - blue_score);
            total_abs_error += std::abs(actual_diff - predicted_diff);

            bool predicted_red = prediction.red_win_probability >= 0.5;
            bool actual_red = actual_diff >= 0.0;
            if (predicted_red == actual_red) {
                correct_winner += 1;
            }

            // Second pass with cross-event history blended into the OPRs. History
            // excludes the current event and anything at/after this match, so the
            // backtest stays leak-free.
            if (eval_history) {
                const std::map<std::string, double> history_oprs =
                    blended_oprs_with_history(client, match, matches, match_filter, event_key,
                                              oprs, stats, config.confidence_match_count,
                                              phase_confidence, history_teams);
                MatchPrediction history_prediction = predict_match(
                    match, stats, config.confidence_match_count,
                    config.score_diff_scale, config.sigmoid_scale, history_oprs);
                const double history_diff = history_prediction.adjusted_score_diff_estimate;
                history_total_abs_error += std::abs(actual_diff - history_diff);
                if ((history_prediction.red_win_probability >= 0.5) == actual_red) {
                    history_correct_winner += 1;
                }
            }

            evaluated += 1;
        }

        if (evaluated == 0) {
            std::cerr << "No completed matches available for evaluation.\n";
            return 1;
        }

        double mae = total_abs_error / static_cast<double>(evaluated);
        double accuracy = static_cast<double>(correct_winner) / static_cast<double>(evaluated);
        const double history_mae = eval_history
            ? history_total_abs_error / static_cast<double>(evaluated) : 0.0;
        const double history_accuracy = eval_history
            ? static_cast<double>(history_correct_winner) / static_cast<double>(evaluated) : 0.0;
        const std::string timestamp = current_timestamp_iso8601();
        if (!eval_json_path.empty()) {
            nlohmann::json output = {
                {"timestamp", timestamp},
                {"event_key", event_key},
                {"phase", phase_arg.empty() ? "all" : phase_arg},
                {"matches", evaluated},
                {"mae", mae},
                {"winner_accuracy", accuracy},
                {"model_uses_opr", config.use_opr}
            };
            if (eval_history) {
                output["model_uses_history"] = true;
                output["history_mae"] = history_mae;
                output["history_winner_accuracy"] = history_accuracy;
                // Positive = history improved over baseline.
                output["mae_improvement"] = mae - history_mae;
                output["accuracy_improvement"] = history_accuracy - accuracy;
            }
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
                file << "timestamp,event_key,phase,model,matches,mae,winner_accuracy\n";
            }
            auto write_row = [&](const std::string& model, double row_mae, double row_accuracy) {
                file << timestamp << ","
                     << event_key << ","
                     << (phase_arg.empty() ? "all" : phase_arg) << ","
                     << model << ","
                     << evaluated << ","
                     << row_mae << ","
                     << row_accuracy << "\n";
            };
            write_row(config.use_opr ? "opr" : "legacy", mae, accuracy);
            if (eval_history) {
                write_row("opr+history", history_mae, history_accuracy);
            }
            std::cout << "Wrote evaluation CSV to " << eval_csv_path << "\n";
        }

        if (eval_json_path.empty() && eval_csv_path.empty()) {
            std::cout << "Evaluation (" << event_key << "):\n";
            std::cout << "  timestamp=" << timestamp << "\n";
            std::cout << "  phase=" << (phase_arg.empty() ? "all" : phase_arg) << "\n";
            std::cout << "  matches=" << evaluated << "\n";
            std::cout << "  mae=" << mae << "\n";
            std::cout << "  winner_accuracy=" << accuracy << "\n";
            std::cout << "  model_uses_opr=" << (config.use_opr ? "true" : "false") << "\n";
            if (eval_history) {
                std::cout << "  history_mae=" << history_mae
                          << " (improvement " << (mae - history_mae) << ")\n";
                std::cout << "  history_winner_accuracy=" << history_accuracy
                          << " (improvement " << (history_accuracy - accuracy) << ")\n";
            }
        }
    }

    if (live_mode) {
        // Live evaluation: poll TBA continuously and track running accuracy.
        // Use a fresh client with zero cache TTL so every poll fetches live data.
        TbaClient live_client(config.tba_auth_key, config.cache_dir, 0);

        // Catch Ctrl+C for a clean shutdown message.
        std::signal(SIGINT, [](int) {
            std::cerr << "\nInterrupted. Shutting down...\n";
            std::exit(0);
        });

        std::cout << "Live evaluation for " << event_key << "\n";
        std::cout << "Polling every " << live_interval << "s. Press Ctrl+C to stop.\n\n";

        // CSV header if appending to file
        bool live_csv_header_written = false;

        int poll = 0;
        while (true) {
            poll++;
            auto now = std::chrono::system_clock::now();
            std::time_t now_t = std::chrono::system_clock::to_time_t(now);
            char time_buf[16];
            std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now_t));

            nlohmann::json matches = live_client.get_event_matches(event_key);
            if (matches.empty()) {
                std::cout << "[" << time_buf << "] poll #" << poll
                          << ": no matches fetched\n";
                std::this_thread::sleep_for(std::chrono::seconds(live_interval));
                continue;
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

                const std::string level = match.value("comp_level", "");
                const bool is_qm = level == "qm";
                const bool is_elim = level == "qf" || level == "sf" || level == "f";
                if (phase_arg == "qm" && !is_qm) {
                    continue;
                }
                if (phase_arg == "elim" && !is_elim) {
                    continue;
                }

                const MatchFilter match_filter = is_qm
                    ? MatchFilter::QualificationOnly
                    : MatchFilter::QualificationPlusElimPlayed;
                std::map<std::string, TeamStats> stats =
                    compute_team_stats_before(matches, match_filter, match);

                std::map<std::string, double> oprs = config.use_opr
                    ? compute_team_oprs_before(matches, match_filter, match)
                    : std::map<std::string, double>{};
                MatchPrediction prediction = predict_match(
                    match, stats, config.confidence_match_count,
                    config.score_diff_scale, config.sigmoid_scale, oprs);

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
                std::cout << "[" << time_buf << "] poll #" << poll
                          << ": 0 completed matches\n";
                std::this_thread::sleep_for(std::chrono::seconds(live_interval));
                continue;
            }

            double mae = total_abs_error / static_cast<double>(evaluated);
            double accuracy = static_cast<double>(correct_winner) / static_cast<double>(evaluated);

            std::cout << "[" << time_buf << "] poll #" << poll
                      << " | matches=" << evaluated
                      << " | mae=" << mae
                      << " | acc=" << accuracy << std::endl;

            if (!eval_csv_path.empty()) {
                std::string timestamp = current_timestamp_iso8601();
                bool write_header = !live_csv_header_written;
                std::ofstream file(eval_csv_path, std::ios::app);
                if (file) {
                    if (write_header) {
                        file << "timestamp,event_key,phase,model,poll,matches,mae,winner_accuracy\n";
                        live_csv_header_written = true;
                    }
                    file << timestamp << ","
                         << event_key << ","
                         << (phase_arg.empty() ? "all" : phase_arg) << ","
                         << "live,"
                         << poll << ","
                         << evaluated << ","
                         << mae << ","
                         << accuracy << "\n";
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(live_interval));
        }

        return 0;
    }

    if (show_picklist) {
        if (picklist_team_key.empty()) {
            std::cerr << "--picklist requires your team key (e.g. frc254).\n";
            return 1;
        }
        const std::string normalized_picklist_key = normalize_team_key(picklist_team_key);
        nlohmann::json matches = client.get_event_matches(event_key);
        if (matches.empty()) {
            std::cerr << "Failed to fetch event matches for " << event_key << ".\n";
            return 1;
        }

        // Picklists are built from qualification play by default.
        MatchFilter picklist_filter = MatchFilter::QualificationOnly;
        if (!resolve_phase_filter(phase_arg, MatchFilter::QualificationOnly, picklist_filter)) {
            std::cerr << "Unknown phase: " << phase_arg << ". Use qm, elim, or all.\n";
            return 1;
        }

        PicklistWeights weights;
        if (!resolve_strategy(strategy_arg, weights)) {
            std::cerr << "Unknown strategy: " << strategy_arg
                      << ". Use balanced, offense, or consistency.\n";
            return 1;
        }

        nlohmann::json before_match = nlohmann::json(nullptr);
        if (!before_match_arg.empty()) {
            before_match = find_match_by_key(matches, event_key, before_match_arg);
            if (before_match.is_null()) {
                std::cerr << "Match key not found for --before: "
                          << normalize_match_key(event_key, before_match_arg) << "\n";
                return 1;
            }
        }

        const std::set<std::string> exclude = parse_team_set(exclude_arg);

        PicklistSummary picklist = compute_picklist(
            matches, picklist_filter, before_match, exclude, weights,
            config.confidence_match_count, normalized_picklist_key);
        if (picklist.entries.empty()) {
            std::cerr << "No picklist computed for " << event_key
                      << " (not enough match data yet).\n";
            return 1;
        }

        const int limit = top_count > 0
            ? std::min(top_count, static_cast<int>(picklist.entries.size()))
            : static_cast<int>(picklist.entries.size());
        const std::string strategy_name = strategy_arg.empty() ? "balanced" : strategy_arg;

        if (!picklist_csv_path.empty()) {
            std::ofstream file(picklist_csv_path);
            if (!file) {
                std::cerr << "Failed to write picklist CSV to " << picklist_csv_path << ".\n";
                return 1;
            }
            file << "rank,team_key,picklist_score,average_score,stddev,trend,matches,confidence\n";
            for (int i = 0; i < limit; ++i) {
                const PicklistEntry& e = picklist.entries[i];
                file << (i + 1) << "," << e.team_key << "," << e.picklist_score << ","
                     << e.average_score << "," << e.stddev << "," << e.trend << ","
                     << e.matches << "," << e.confidence << "\n";
            }
            std::cout << "Wrote picklist CSV to " << picklist_csv_path << "\n";
        }

        if (output_json) {
            nlohmann::json output = {
                {"event_key", event_key},
                {"strategy", strategy_name},
                {"phase", phase_arg.empty() ? "qm" : phase_arg},
                {"self_team_key", picklist.self_team_key},
                {"self_matches", picklist.self_performance.matches_played},
                {"self_average_score", picklist.self_performance.average_score},
                {"self_stddev", picklist.self_performance.std_dev},
                {"self_recent_average", picklist.self_performance.recent_average},
                {"event_average_score", picklist.event_average_score},
                {"teams", nlohmann::json::array()}
            };
            for (int i = 0; i < limit; ++i) {
                const PicklistEntry& e = picklist.entries[i];
                output["teams"].push_back({
                    {"rank", i + 1},
                    {"team_key", e.team_key},
                    {"picklist_score", e.picklist_score},
                    {"average_score", e.average_score},
                    {"stddev", e.stddev},
                    {"trend", e.trend},
                    {"matches", e.matches},
                    {"confidence", e.confidence}
                });
            }
            std::cout << output.dump(2) << "\n";
        } else if (picklist_csv_path.empty()) {
            std::cout << "Picklist (" << event_key << ", strategy=" << strategy_name << "):\n";
            std::cout << "  self=" << picklist.self_team_key
                      << " avg=" << picklist.self_performance.average_score
                      << " stddev=" << picklist.self_performance.std_dev
                      << " recent=" << picklist.self_performance.recent_average
                      << "\n";
            for (int i = 0; i < limit; ++i) {
                const PicklistEntry& e = picklist.entries[i];
                std::cout << "  " << (i + 1) << ". " << e.team_key
                          << " | score=" << e.picklist_score
                          << " avg=" << e.average_score
                          << " stddev=" << e.stddev
                          << " trend=" << e.trend
                          << " matches=" << e.matches
                          << "\n";
            }
        }
    }

    return 0;
}
