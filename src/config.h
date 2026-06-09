#pragma once

#include <string>

struct Config {
    std::string tba_auth_key;
    std::string default_event_key;
    std::string cache_dir;
};

Config load_config();
