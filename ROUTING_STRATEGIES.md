# Stratégies de Routage - Alternatives

> Document d’exploration historique. Ses exemples VASIO1–VASIO4 ne décrivent
> plus le produit actif. Le contrat courant est documenté dans [API.md](API.md)
> et l’architecture actuelle dans [STRUCTURE.md](STRUCTURE.md).

## Approche 1: Matrice de Routage (Mixing Matrix)

La plus visuelle et intuitive - similaire aux consoles audio.

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

**Avantages:**
- Interface graphique facile à implémenter
- Gains par route visibles
- Logique claire

**Implémentation:**
```cpp
struct MatrixRoute {
    std::string sourceDriver;
    int sourceChannel;
    std::vector<std::pair<std::string, int>> destinations;
    std::vector<float> gains;
};
```

## Approche 2: Routage basé sur Règles (Rule-Based)

Conditionnels et filtres dynamiques.

```ini
[rules]
# Si la source est VASIO1, router vers VASIO2
if source == VASIO1 then route to VASIO2

# Router tous les canaux pairs vers VASIO3
if channel % 2 == 0 then route to VASIO3

# Router avec atténuation basée sur le canal
channel <= 3 ? gain=1.0 : gain=0.5
```

**Avantages:**
- Très flexible
- Supporte les filtres complexes
- Automatisation possible

**Implémentation:**
```cpp
struct RoutingRule {
    std::string condition;  // "source == VASIO1"
    std::string action;     // "route to VASIO2 with gain 0.5"
};
```

## Approche 3: Graphe de Routage (Graph-Based)

Comme une DAW - nœuds et connexions.

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

**Avantages:**
- Très flexible
- Support pour les processeurs (EQ, compression, etc.)
- Logique DAW-like
- Visualisable comme un patch bay

**Implémentation:**
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

## Approche 4: Contrôle en Temps-Réel (OSC / WebSocket)

Configuration dynamique sans redémarrage.

```
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

**Avantages:**
- Contrôle en direct pendant la lecture
- Compatible avec contrôleurs MIDI/OSC
- Intégration OSC protocol standard
- Pas de redémarrage nécessaire

**Implémentation:**
```cpp
class OSCServer {
    void onOSCMessage(const std::string& address, const std::vector<float>& args);
    void handleRouteAdd(const std::string& source, const std::string& dest, float gain);
};
```

## Approche 5: Interface Web (HTTP/REST API)

Contrôle via navigateur.

```
GET  /api/drivers                    → Liste les pilotes
GET  /api/routes                     → Liste les routes
POST /api/routes                     → Ajouter une route
PATCH /api/routes/{id}/gain          → Modifier le gain
DELETE /api/routes/{id}              → Supprimer une route
```

**Avantages:**
- Interface web interactive
- Accessible depuis n'importe où
- Intégration facile

**Implémentation:**
```cpp
#include <nlohmann/json.hpp>  // JSON library

class WebServer {
    void get_drivers(const Request& req, Response& res);
    void post_route(const Request& req, Response& res);
    void patch_gain(const Request& req, Response& res);
};
```

## Approche 6: Visual Patchbay (Interface GUI)

Interface graphique natale type VST/AU.

```
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

**Avantages:**
- Très intuitif
- Gestion visuelle des gains
- Drag-and-drop des connexions

**Implémentation:**
```cpp
#include <imgui.h>  // ImGui library

class PatchbayUI {
    void DrawDriver(const std::string& name, int x, int y);
    void DrawConnections();
    void HandleDragDrop();
};
```

## Recommandation

**Combiner plusieurs approches:**

1. **Fichier INI (config initiale)** - pour démarrage simple
2. **JSON Graph (persistance)** - pour sauvegarde/chargement complexe
3. **OSC/WebSocket (runtime)** - pour contrôle en direct
4. **Web UI (visualisation)** - pour management

**Stack proposé:**
```
┌─────────────────────────────────────┐
│    Web UI (React/Vue)               │
│  (http://localhost:8888)            │
└──────────────┬──────────────────────┘
               │ HTTP / WebSocket
┌──────────────▼──────────────────────┐
│  Routing Engine (C++)               │
│  - Charge INI au démarrage          │
│  - Accepte OSC/WebSocket            │
│  - Stocke en JSON                   │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  Audio Processing                   │
│  (PortAudio / ASIO)                 │
└─────────────────────────────────────┘
```

Quelle approche préférez-vous implémenter en priorité ?
