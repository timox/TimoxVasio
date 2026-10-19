#include "../include/config_loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

ConfigLoader::ConfigLoader(const std::string& path) : configPath(path) {
}

ConfigLoader::~ConfigLoader() {
}

bool ConfigLoader::LoadConfiguration() {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    routes.clear();
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line.find("route") == 0) {
            RouteMapping route;
            if (ParseRouteLine(line, route)) {
                routes.push_back(route);
            }
        }
    }

    file.close();
    return true;
}

bool ConfigLoader::SaveConfiguration() {
    std::ofstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    file << "; Configuration de routage ASIO\n";
    file << "; Format: route <name>: <source> -> <destination>\n\n";

    file << "[routes]\n";
    for (const auto& route : routes) {
        file << FormatRouteLine(route) << "\n";
    }

    file.close();
    return true;
}

bool ConfigLoader::ParseRouteLine(const std::string& line, RouteMapping& route) {
    std::regex routeRegex(R"(route\s+(\w+)\s*:\s*(\w+):(\d+)\s*->\s*(\w+):(\d+))");
    std::smatch match;

    if (!std::regex_search(line, match, routeRegex)) {
        return false;
    }

    route.name = match[1].str();
    route.sourceDriver = match[2].str();
    route.sourceChannel = std::stoi(match[3].str());
    route.destDriver = match[4].str();
    route.destChannel = std::stoi(match[5].str());

    return true;
}

std::string ConfigLoader::FormatRouteLine(const RouteMapping& route) const {
    std::ostringstream oss;
    oss << "route " << route.name << ": "
        << route.sourceDriver << ":" << route.sourceChannel << " -> "
        << route.destDriver << ":" << route.destChannel << ";";
    return oss.str();
}

void ConfigLoader::AddRoute(const RouteMapping& route) {
    auto it = std::find_if(routes.begin(), routes.end(),
        [&route](const RouteMapping& r) { return r.name == route.name; });

    if (it == routes.end()) {
        routes.push_back(route);
    }
}

void ConfigLoader::RemoveRoute(const std::string& routeName) {
    auto it = std::remove_if(routes.begin(), routes.end(),
        [&routeName](const RouteMapping& r) { return r.name == routeName; });

    routes.erase(it, routes.end());
}

void ConfigLoader::ClearRoutes() {
    routes.clear();
}
