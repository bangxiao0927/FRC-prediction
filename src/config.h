#pragma once

#include <string>

struct Config {
    std::string tba_auth_key;
    std::string default_event_key;
    std::string cache_dir;
    int cache_ttl_seconds = 60;
    int confidence_match_count = 6;
    double score_diff_scale = 30.0;
    double sigmoid_scale = 1.0;
    std::string model_version = "baseline-v1";
    // When true, predictions use the OPR contribution model instead of the
    // legacy "sum of alliance averages" proxy.
    bool use_opr = true;
    // When true, predictions blend in a team's prior-season form (cross-event
    // history). Off by default because it makes extra TBA calls per prediction.
    bool use_history = false;
    // Per-phase confidence: how many current-event matches a team needs before
    // each phase is trusted entirely on current form (vs. cross-event history).
    int history_auto_matches = 4;
    int history_teleop_matches = 8;
    int history_endgame_matches = 6;
};

Config load_config();
