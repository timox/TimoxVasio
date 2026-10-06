# Routing strategies — alternatives

[Français](ROUTING_STRATEGIES.md) | **English**

> Historical exploration. Its VASIO1–VASIO4 examples no longer describe the active product. The current contract is in [API.en.md](API.en.md), and the current architecture is in [STRUCTURE.en.md](STRUCTURE.en.md).

## Approach 1: Routing matrix

The most visual and intuitive approach, similar to an audio mixing console.

```yaml
# routing_matrix.yaml
matrix:
  VASIO1:
    to: [VASIO2, VASIO3]
    gain: [1.0, 0.5]
  VASIO2:
    to: [VASIO4]
    gain: [1.0]

channels:
  VASIO1:1 -> [VASIO2:1 (1.0), VASIO3:2 (0.5)]
  VASIO1:2 -> [VASIO2:2 (1.0), VASIO3:3 (0.5)]
```

**Advantages:** Easy graphical interface, visible gain per route, and clear logic.

**Implementation:**

```cpp
struct MatrixRoute {
    std::string sourceDriver;
    int sourceChannel;
    std::vector<std::pair<std::string, int>> destinations;
    std::vector<float> gains;
};
```

## Approach 2: Rule-based routing

Dynamic conditions and filters.

```ini
[rules]
# If the source is VASIO1, route to VASIO2
if source == VASIO1 then route to VASIO2

# Route all even-numbered channels to VASIO3
if channel % 2 == 0 then route to VASIO3

# Apply channel-dependent attenuation
channel <= 3 ? gain=1.0 : gain=0.5
```

**Advantages:** Very flexible, supports complex filters, and allows automation.

**Implementation:**

```cpp
struct RoutingRule {
    std::string condition;  // "source == VASIO1"
    std::string action;     // "route to VASIO2 with gain 0.5"
};
```

## Approach 3: Graph-based routing

Like a DAW, with nodes and connections.

```json
{
  "graph": {
    "nodes": [
      {"id": "vasio1", "type": "input", "channels": 6},
      {"id": "vasio2", "type": "output", "channels": 6},
      {"id": "mixer", "type": "processor", "algorithm": "sum"},
      {"id": "eq", "type": "processor", "algorithm": "equalizer"}
    ],
    "edges": [
      {"from": "vasio1:0", "to": "mixer:0", "gain": 1.0},
      {"from": "mixer:0", "to": "eq:0"},
      {"from": "eq:0", "to": "vasio2:0"}
    ]
  }
}
```

**Advantages:** Very flexible, supports processors such as EQ and compression, follows DAW-like logic, and can be visualized as a patchbay.

**Implementation:**

```cpp
struct Node {
    std::string id;
    std::string type;  // input, output, processor
    std::vector<Port> inputs, outputs;
};

struct Edge {
    std::string fromNode, toNode;
    int fromPort, toPort;
    float gain;
};
```

## Approach 4: Real-time control (OSC/WebSocket)

Dynamic configuration without restart.

```text
OSC (Open Sound Control):
/vasio/route/add "VASIO1" 1 "VASIO2" 1 0.8
/vasio/route/remove "VASIO1" 1 "VASIO2" 1
/vasio/driver/gain "VASIO1" 1 0.5
```

```json
WebSocket JSON:
{
  "action": "add_route",
  "source": "VASIO1:1",
  "destination": "VASIO2:1",
  "gain": 0.8
}
```

**Advantages:** Live control during playback, MIDI/OSC controller compatibility, standard OSC integration, and no restart required.

**Implementation:**

```cpp
class OSCServer {
    void onOSCMessage(const std::string& address, const std::vector<float>& args);
    void handleRouteAdd(const std::string& source, const std::string& dest, float gain);
};
```

## Approach 5: Web interface (HTTP/REST API)

Browser-based control.

```text
GET  /api/drivers                    → List drivers
GET  /api/routes                     → List routes
POST /api/routes                     → Add a route
PATCH /api/routes/{id}/gain          → Change gain
DELETE /api/routes/{id}              → Delete a route
```

**Advantages:** Interactive web interface, remote accessibility, and straightforward integration.

**Implementation:**

```cpp
#include <nlohmann/json.hpp>  // JSON library

class WebServer {
    void get_drivers(const Request& req, Response& res);
    void post_route(const Request& req, Response& res);
    void patch_gain(const Request& req, Response& res);
};
```

## Approach 6: Visual patchbay (GUI)

Native VST/AU-style graphical interface.

```text
┌─────────┐         ┌─────────┐
│ VASIO1  │───────→ │ VASIO2  │
│ 1,2,3   │   ╳ ╳   │ 1,2,3   │
│ 4,5,6   │───────→ │ 4,5,6   │
└─────────┘         └─────────┘

           ┌─────────┐
           │ VASIO3  │
           │ 1,2,3   │
           │ 4,5,6   │
           └─────────┘
```

**Advantages:** Very intuitive, visual gain management, and drag-and-drop connections.

**Implementation:**

```cpp
#include <imgui.h>  // ImGui library

class PatchbayUI {
    void DrawDriver(const std::string& name, int x, int y);
    void DrawConnections();
    void HandleDragDrop();
};
```

## Recommendation from the historical exploration

**Combine several approaches:**

1. **INI file (initial configuration)** for simple startup.
2. **JSON graph (persistence)** for complex save/load.
3. **OSC/WebSocket (runtime)** for live control.
4. **Web UI (visualization)** for management.

**Proposed stack:**

```text
┌─────────────────────────────────────┐
│    Web UI (React/Vue)               │
│  (http://localhost:8888)            │
└──────────────┬──────────────────────┘
               │ HTTP / WebSocket
┌──────────────▼──────────────────────┐
│  Routing engine (C++)               │
│  - Loads INI at startup             │
│  - Accepts OSC/WebSocket            │
│  - Stores JSON                       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  Audio processing                   │
│  (PortAudio / ASIO)                  │
└─────────────────────────────────────┘
```

Which approach would you prefer to implement first?
