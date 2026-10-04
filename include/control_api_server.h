#pragma once

#include <cstdint>
#include <memory>
#include <string>

class AudioController;
class VasioClientManager;

class ControlApiServer final {
public:
    ControlApiServer(VasioClientManager& clients, AudioController& controller);
    ~ControlApiServer();
    ControlApiServer(const ControlApiServer&) = delete;
    ControlApiServer& operator=(const ControlApiServer&) = delete;

    bool Start(std::uint16_t requestedPort, std::uint16_t& boundPort, std::string& error);
    void Stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
