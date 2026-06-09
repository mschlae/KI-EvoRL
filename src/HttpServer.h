#pragma once
// ============================================================
//  HttpServer.h  -  Minimaler, abhaengigkeitsfreier HTTP-Server
//  Laeuft in einem eigenen Thread und bedient zwei Routen:
//    GET /            -> Dashboard (HTML)
//    GET /api/status  -> aktueller Trainingsstatus (JSON)
//  Nutzt Winsock unter Windows und BSD-Sockets unter POSIX.
// ============================================================
#include <atomic>
#include <thread>

class TrainingMonitor;

class HttpServer {
public:
    HttpServer(TrainingMonitor& monitor, int port);
    ~HttpServer();

    bool start();   // bindet Port und startet Accept-Loop im Hintergrund
    void stop();    // beendet Loop und schliesst Socket

private:
    void run();                       // Accept-Schleife
    void handleClient(long long sock); // eine Anfrage bearbeiten

    TrainingMonitor& monitor_;
    int              port_;
    long long        listenSock_;     // SOCKET/int, plattformneutral als long long gehalten
    std::atomic<bool> stopFlag_{ false };
    std::thread      thread_;
};
