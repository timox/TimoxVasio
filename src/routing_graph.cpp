#include "routing_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

bool RoutingGraph::SupportsLink(RoutingEndpointType source, RoutingEndpointType destination) noexcept {
    return (source == RoutingEndpointType::VirtualOutput && destination == RoutingEndpointType::VirtualInput) ||
           (source == RoutingEndpointType::VirtualOutput && destination == RoutingEndpointType::PhysicalOutput) ||
           (source == RoutingEndpointType::PhysicalInput && destination == RoutingEndpointType::VirtualInput);
}

std::unique_ptr<RoutingGraph> RoutingGraph::Create(const std::vector<RoutingEndpoint>& endpoints,
    const std::vector<AudioRoute>& routes, std::uint32_t sampleRate, std::string& error) {
    error.clear();
    if (sampleRate == 0) {
        error = "Routing sample rate must be positive";
        return {};
    }

    std::unordered_map<std::string, std::size_t> indices;
    indices.reserve(endpoints.size());
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        const auto& endpoint = endpoints[index];
        if (endpoint.id.empty() || endpoint.sampleRate != sampleRate ||
            !indices.emplace(endpoint.id, index).second) {
            error = "Endpoint IDs must be unique and all endpoint rates must match the graph";
            return {};
        }
    }

    auto graph = std::unique_ptr<RoutingGraph>(new RoutingGraph(endpoints.size()));
    graph->routes_.reserve(routes.size());
    std::unordered_set<std::string> routeIds;
    routeIds.reserve(routes.size());
    for (const auto& route : routes) {
        if (route.id.empty() || !routeIds.emplace(route.id).second) {
            error = "Route IDs must be non-empty and unique";
            return {};
        }
        if (!std::isfinite(route.gainDb) || route.gainDb < -120.0 || route.gainDb > 24.0) {
            error = "Route gain must be finite and between -120 and +24 dB";
            return {};
        }
        const auto source = indices.find(route.sourceEndpointId);
        const auto destination = indices.find(route.destinationEndpointId);
        if (source == indices.end() || destination == indices.end()) {
            error = "Route references an endpoint that is not present in the inventory";
            return {};
        }
        if (!SupportsLink(endpoints[source->second].type, endpoints[destination->second].type)) {
            error = "Route endpoint directions are not supported";
            return {};
        }
        const float linearGain = static_cast<float>(std::pow(10.0, route.gainDb / 20.0));
        graph->routes_.push_back({source->second, destination->second, linearGain, route.mute});
    }
    return graph;
}

void RoutingGraph::Process(const float* const* sourceBlocks, float* const* destinationBlocks,
                           std::uint32_t frames) const noexcept {
    if (!destinationBlocks) return;
    for (std::size_t endpoint = 0; endpoint < endpointCount_; ++endpoint) {
        if (destinationBlocks[endpoint])
            std::fill_n(destinationBlocks[endpoint], frames, 0.0f);
    }
    if (!sourceBlocks || frames == 0) return;
    for (const auto& route : routes_) {
        if (route.mute) continue;
        const float* source = sourceBlocks[route.sourceIndex];
        float* destination = destinationBlocks[route.destinationIndex];
        if (!source || !destination) continue;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            destination[frame] += source[frame] * route.linearGain;
    }
}
