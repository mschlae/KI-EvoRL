#pragma once
// ============================================================
//  HttpServer.h  -  Minimaler, abhaengigkeitsfreier HTTP-Server
//  Laeuft in einem eigenen Thread, lauscht nur auf 127.0.0.1 und
//  bedient:
//    GET  /                         Dashboard (HTML)
//    GET  /api/status               Trainingsstatus (JSON)
//    GET  /api/replay               Replay des aktuell besten Agenten
//    GET|POST /api/config           Laufzeit-Parameter
//    POST /api/stop                 Training beenden
//    GET  /api/models               Modell-Bibliothek
//    POST /api/models/save          aktuelles Bestmodell speichern
//    GET  /api/models/{id}/replay   Replay eines Modells
//    POST /api/models/{id}/load     Modell in die Population uebernehmen
//    POST /api/playground/run       Modell auf eigener Karte laufen lassen
//  Nutzt Winsock unter Windows und BSD-Sockets unter POSIX.
//
//  Schutz gegen Missbrauch aus dem Browser: Anfragen mit fremdem
//  Host-Header (DNS-Rebinding) oder fremdem Origin (Cross-Site-POST
//  auf /api/stop o. ae.) werden mit 403 abgelehnt.
// ============================================================
#include <atomic>
#include <string>
#include <thread>

class TrainingMonitor;

class HttpServer {
public:
    HttpServer(TrainingMonitor& monitor, int port);
    ~HttpServer();

    bool start();   // bindet Port und startet Accept-Loop im Hintergrund
    void stop();    // beendet Loop und schliesst Socket

private:
    void run();                        // Accept-Schleife
    void handleClient(long long sock); // eine Anfrage bearbeiten

    TrainingMonitor&  monitor_;
    int               port_;
    long long         listenSock_;     // SOCKET/int, plattformneutral als long long gehalten
    std::atomic<bool> stopFlag_{ false };
    std::thread       thread_;
};
