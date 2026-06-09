#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace {

std::string read_api_key_from_config(const std::string& path) {
    std::ifstream config_file(path);
    if (!config_file) {
        return "";
    }

    nlohmann::json config = nlohmann::json::parse(config_file, nullptr, false);
    if (config.is_discarded() || !config.contains("tba_auth_key")) {
        return "";
    }

    return config.value("tba_auth_key", "");
}

std::string load_api_key() {
    const char* api_key = std::getenv("TBA_AUTH_KEY");
    if (api_key && std::string(api_key).size() > 0) {
        return api_key;
    }

    return read_api_key_from_config("config.json");
}

}  // namespace

int main() {
    const std::string api_key = load_api_key();
    if (api_key.empty() || api_key == "your_key_here") {
        std::cerr << "Missing TBA API key.\n";
        std::cerr << "Set TBA_AUTH_KEY or copy config.example.json to config.json.\n";
        return 1;
    }

    cpr::Header headers{{"X-TBA-Auth-Key", api_key}};
    cpr::Response response = cpr::Get(
        cpr::Url{"https://www.thebluealliance.com/api/v3/status"},
        headers);

    if (response.error) {
        std::cerr << "HTTP error: " << response.error.message << "\n";
        return 1;
    }

    if (response.status_code != 200) {
        std::cerr << "Unexpected status code: " << response.status_code << "\n";
        std::cerr << response.text << "\n";
        return 1;
    }

    nlohmann::json payload = nlohmann::json::parse(response.text, nullptr, false);
    if (payload.is_discarded()) {
        std::cerr << "Failed to parse JSON response.\n";
        return 1;
    }

    std::cout << "TBA Status: " << payload.dump(2) << "\n";
    return 0;
}
