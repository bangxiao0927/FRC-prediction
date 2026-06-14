#include "tba_client.h"

#include <iostream>

#include <cpr/cpr.h>

TbaClient::TbaClient(std::string auth_key, std::string cache_dir, int cache_ttl_seconds)
    : auth_key_(std::move(auth_key)),
      base_url_("https://www.thebluealliance.com/api/v3"),
      cache_ttl_seconds_(cache_ttl_seconds),
      cache_(std::move(cache_dir)) {}

nlohmann::json TbaClient::get_status() {
    return get_json("/status");
}

nlohmann::json TbaClient::get_event(const std::string& event_key) {
    return get_json("/event/" + event_key);
}

nlohmann::json TbaClient::get_event_matches(const std::string& event_key) {
    return get_json("/event/" + event_key + "/matches");
}

nlohmann::json TbaClient::get_event_rankings(const std::string& event_key) {
    return get_json("/event/" + event_key + "/rankings");
}

nlohmann::json TbaClient::get_event_teams(const std::string& event_key) {
    return get_json("/event/" + event_key + "/teams");
}

nlohmann::json TbaClient::get_team_matches_year(const std::string& team_key, int year) {
    return get_json("/team/" + team_key + "/matches/" + std::to_string(year));
}

nlohmann::json TbaClient::get_events_by_year(int year) {
    return get_json("/events/" + std::to_string(year) + "/simple");
}

nlohmann::json TbaClient::get_json(const std::string& path) {
    if (auth_key_.empty()) {
        std::cerr << "Missing TBA API key.\n";
        return nlohmann::json::object();
    }

    nlohmann::json cached = cache_.load(path, cache_ttl_seconds_);
    if (!cached.empty()) {
        return cached;
    }

    cpr::Header headers{{"X-TBA-Auth-Key", auth_key_}};
    cpr::Response response = cpr::Get(
        cpr::Url{base_url_ + path},
        headers);

    if (response.error) {
        std::cerr << "HTTP error: " << response.error.message << "\n";
        return nlohmann::json::object();
    }

    if (response.status_code != 200) {
        std::cerr << "Unexpected status code: " << response.status_code << "\n";
        std::cerr << response.text << "\n";
        return nlohmann::json::object();
    }

    nlohmann::json payload = nlohmann::json::parse(response.text, nullptr, false);
    if (payload.is_discarded()) {
        std::cerr << "Failed to parse JSON response.\n";
        return nlohmann::json::object();
    }

    cache_.save(path, payload);

    return payload;
}
