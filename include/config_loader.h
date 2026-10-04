#pragma once

#include "audio_engine.h"
#include <string>
#include <vector>

class ConfigLoader {
public:
    ConfigLoader(const std::string& configPath);
    ~ConfigLoader();

    bool LoadConfiguration();
    bool SaveConfiguration();

    const std::vector<RouteMapping>& GetRoutes() const { return routes; }

    void AddRoute(const RouteMapping& route);
    void RemoveRoute(const std::string& routeName);
    void ClearRoutes();

private:
    std::string configPath;
    std::vector<RouteMapping> routes;

    bool ParseRouteLine(const std::string& line, RouteMapping& route);
    std::string FormatRouteLine(const RouteMapping& route) const;
};
