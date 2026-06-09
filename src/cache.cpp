#include "cache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string hash_key(const std::string& key) {
    std::size_t hashed = std::hash<std::string>{}(key);
    std::ostringstream out;
    out << std::hex << hashed;
    return out.str();
}

}  // namespace

FileCache::FileCache(std::string cache_dir)
    : cache_dir_(std::move(cache_dir)) {}

nlohmann::json FileCache::load(const std::string& key, int max_age_seconds) const {
    if (cache_dir_.empty()) {
        return nlohmann::json::object();
    }

    fs::path file_path = path_for_key(key);
    if (!fs::exists(file_path)) {
        return nlohmann::json::object();
    }

    auto last_write = fs::last_write_time(file_path);
    auto now = fs::file_time_type::clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_write).count();
    if (age > max_age_seconds) {
        return nlohmann::json::object();
    }

    std::ifstream file(file_path);
    if (!file) {
        return nlohmann::json::object();
    }

    nlohmann::json payload = nlohmann::json::parse(file, nullptr, false);
    if (payload.is_discarded()) {
        return nlohmann::json::object();
    }

    return payload;
}

bool FileCache::save(const std::string& key, const nlohmann::json& payload) const {
    if (cache_dir_.empty()) {
        return false;
    }

    fs::path file_path = path_for_key(key);
    fs::create_directories(file_path.parent_path());

    std::ofstream file(file_path);
    if (!file) {
        return false;
    }

    file << payload.dump(2);
    return true;
}

std::string FileCache::path_for_key(const std::string& key) const {
    fs::path base(cache_dir_);
    return (base / (hash_key(key) + ".json")).string();
}
