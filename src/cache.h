#pragma once

#include <string>

#include <nlohmann/json.hpp>

class FileCache {
public:
    explicit FileCache(std::string cache_dir);

    nlohmann::json load(const std::string& key, int max_age_seconds) const;
    bool save(const std::string& key, const nlohmann::json& payload) const;

private:
    std::string cache_dir_;

    std::string path_for_key(const std::string& key) const;
};
