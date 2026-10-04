// Prototype historique non compilé par le build principal.
// Il crée encore VASIO1–VASIO4 à six canaux; le moteur actif est
// TimoxVirtualAsioEngine et son contrat de configuration passe par l’API.
#include "../include/vasio_host.h"
#include "../include/config_loader.h"
#include <iostream>
#include <iomanip>

// Programme utilisant PortAudio (projet existant)
// https://github.com/PortAudio/portaudio

int main(int argc, char* argv[]) {
    std::cout << "=== Virtual ASIO Drivers (PortAudio Backend) ===" << std::endl;
    std::cout << "Version 1.0 - Compilable Windows" << std::endl;
    std::cout << std::endl;

    VASIOHost& host = VASIOHost::GetInstance();

    if (!host.Initialize()) {
        std::cerr << "Erreur: Impossible d'initialiser PortAudio" << std::endl;
        return 1;
    }

    std::cout << "PortAudio initialise avec succes" << std::endl;
    std::cout << std::endl;

    // Lister les appareils audio disponibles
    std::cout << "=== Appareils audio detectes ===" << std::endl;
    auto devices = host.EnumerateDevices();

    for (const auto& dev : devices) {
        std::cout << std::setw(3) << dev.deviceIndex << ": "
                  << dev.name << " ("
                  << dev.inputChannels << "in / "
                  << dev.outputChannels << "out @ "
                  << dev.sampleRate << " Hz)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Creation des pilotes virtuels ===" << std::endl;

    // Creer 4 pilotes ASIO virtuels
    for (int i = 1; i <= 4; ++i) {
        std::string driverName = "VASIO" + std::to_string(i);
        if (host.CreateVirtualDriver(driverName, 6)) {
            std::cout << "✓ " << driverName << " (6 canaux)" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "=== Chargement configuration ===" << std::endl;

    ConfigLoader config("config/routing.ini");
    if (config.LoadConfiguration()) {
        const auto& routes = config.GetRoutes();
        std::cout << "Routes chargees: " << routes.size() << std::endl;
        for (const auto& route : routes) {
            std::cout << "  Route '" << route.name << "': "
                      << route.sourceDriver << ":" << route.sourceChannel
                      << " -> " << route.destDriver << ":" << route.destChannel << std::endl;
        }
    } else {
        std::cout << "Pas de fichier routing.ini ou fichier vide" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Resumé ===" << std::endl;
    std::cout << "Les pilotes suivants sont maintenant disponibles dans vos DAW:" << std::endl;
    std::cout << "  - VASIO1 (6 canaux: VASIO11-VASIO16)" << std::endl;
    std::cout << "  - VASIO2 (6 canaux: VASIO21-VASIO26)" << std::endl;
    std::cout << "  - VASIO3 (6 canaux: VASIO31-VASIO36)" << std::endl;
    std::cout << "  - VASIO4 (6 canaux: VASIO41-VASIO46)" << std::endl;
    std::cout << std::endl;
    std::cout << "Configurez le routage dans config/routing.ini" << std::endl;
    std::cout << std::endl;
    std::cout << "Appuyez sur Entree pour terminer..." << std::endl;

    std::cin.get();

    host.Shutdown();
    std::cout << "Programme ferme" << std::endl;

    return 0;
}
