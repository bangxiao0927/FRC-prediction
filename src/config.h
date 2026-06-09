#pragma once

#include <string>

struct Config {
    std::string tba_auth_key;
    std::string default_event_key;
    std::string cache_dir;
    int cache_ttl_seconds = 60;
    int confidence_match_count = 6;
};

Config load_config();
