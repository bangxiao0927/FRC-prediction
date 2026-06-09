#include "tba_client.h"

#include <iostream>

#include <cpr/cpr.h>

TbaClient::TbaClient(std::string auth_key)
    : auth_key_(std::move(auth_key)),
      base_url_("https://www.thebluealliance.com/api/v3") {}

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

nlohmann::json TbaClient::get_json(const std::string& path) {
    if (auth_key_.empty()) {
        std::cerr << "Missing TBA API key.\n";
        return nlohmann::json::object();
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

    return payload;
}
