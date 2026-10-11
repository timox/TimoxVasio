#include "cycle_report_sink.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

int main() {
    using Json = nlohmann::json;
    Json report = {{"cycles", Json::array()}};
    std::ostringstream lines;
    LifecycleCycleSink sink(report, lines, 10);
    for (int cycle = 1; cycle <= 1000; ++cycle) {
        if (!sink.append(Json{{"cycle", cycle}, {"callbackCount", 3}})) return 1;
    }
    if (sink.count() != 1000 || report["cycles"].size() != 10 ||
        report["cycles"][0]["cycle"] != 1 || report["cycles"][9]["cycle"] != 10)
        return 2;
    std::istringstream read(lines.str());
    std::string line;
    int observed = 0;
    while (std::getline(read, line)) {
        const auto item = Json::parse(line);
        ++observed;
        if (item["cycle"] != observed) return 3;
    }
    return observed == 1000 ? 0 : 4;
}
