// ============================================================
//  HttpServer.cpp
//
//  Aenderungen gegenueber der Vorversion:
//   [NEU-1] Robustes Einlesen des Requests: Es wird so lange
//           gelesen, bis der komplette Header UND der per
//           Content-Length angekuendigte Body vorliegen
//           (bis 4 MB). Vorher wurde nur EIN recv() mit 2 KB
//           gemacht - groessere POST-Bodies (z. B. Playground-
//           Karten oder umfangreiche Configs) waeren dabei
//           abgeschnitten worden.
//   [NEU-2] Neue Routen fuer den Home-Bereich des Dashboards:
//             GET  /api/models               - Modellliste
//             GET  /api/models/{id}/replay   - Replay eines Modells
//             POST /api/models/{id}/load     - Modell ins Training
//             POST /api/playground/run       - Episode auf eigener Karte
//           Diese Routen sind bereits geroutet und antworten
//           aktuell mit 501 + JSON-Hinweis. Sobald der
//           TrainingMonitor die markierten Methoden anbietet
//           (siehe TODO-Bloecke), nur die Stub-Zeilen durch die
//           echten Aufrufe ersetzen - das Frontend ist darauf
//           vorbereitet und degradiert bis dahin sauber.
//
//  Alle bestehenden Routen (/, /api/status, /api/replay,
//  /api/config, /api/stop) verhalten sich unveraendert.
// ============================================================
#include "HttpServer.h"
#include "TrainingMonitor.h"
#include "DashboardHtml.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>

// ---- Plattform-Abstraktion ----
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
static int  closeSock(sock_t s) { return closesocket(s); }
static bool sockValid(sock_t s) { return s != INVALID_SOCKET; }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static const int INVALID_SOCKET = -1;
static int  closeSock(sock_t s) { return ::close(s); }
static bool sockValid(sock_t s) { return s >= 0; }
#endif

namespace {

#ifdef _WIN32
    struct WinsockGuard {
        bool ok = false;
        WinsockGuard() { WSADATA d; ok = (WSAStartup(MAKEWORD(2, 2), &d) == 0); }
        ~WinsockGuard() { if (ok) WSACleanup(); }
    };
    WinsockGuard g_winsock; // initialisiert Winsock einmalig
#endif

    void sendAll(sock_t s, const char* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            int n = ::send(s, data + sent, (int)(len - sent), 0);
            if (n <= 0) break;
            sent += (size_t)n;
        }
    }

    void sendResponse(sock_t s, const char* status, const char* contentType, const std::string& body) {
        std::string head;
        head.reserve(160);
        head += "HTTP/1.1 ";
        head += status;
        head += "\r\nContent-Type: ";
        head += contentType;
        head += "\r\nContent-Length: ";
        head += std::to_string(body.size());
        head += "\r\nAccess-Control-Allow-Origin: *";
        head += "\r\nCache-Control: no-store";
        head += "\r\nConnection: close\r\n\r\n";
        sendAll(s, head.data(), head.size());
        sendAll(s, body.data(), body.size());
    }

    void sendNotImplemented(sock_t s, const char* what) {
        std::string body = "{\"ok\":false,\"error\":\"not_implemented\",\"endpoint\":\"";
        body += what;
        body += "\"}";
        sendResponse(s, "501 Not Implemented", "application/json", body);
    }

    // [NEU-1] Liest Header + Body vollstaendig ein (Content-Length-basiert).
    // Gibt false zurueck, wenn gar nichts gelesen werden konnte.
    bool readRequest(sock_t s, std::string& out) {
        static const size_t MAX_REQUEST = 4 * 1024 * 1024;   // 4 MB Schutzgrenze
        char buf[4096];
        out.clear();
        size_t headerEnd = std::string::npos;
        size_t contentLen = 0;
        bool haveLen = false;

        while (out.size() < MAX_REQUEST) {
            int n = ::recv(s, buf, (int)sizeof(buf), 0);
            if (n <= 0) break;
            out.append(buf, (size_t)n);

            if (headerEnd == std::string::npos) {
                headerEnd = out.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    // Content-Length case-insensitiv im Header suchen
                    std::string lower = out.substr(0, headerEnd);
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return (char)std::tolower(c); });
                    size_t p = lower.find("content-length:");
                    if (p != std::string::npos) {
                        p += 15;
                        while (p < lower.size() && lower[p] == ' ') p++;
                        contentLen = (size_t)strtoul(lower.c_str() + p, nullptr, 10);
                        haveLen = true;
                        if (contentLen > MAX_REQUEST) return false;   // zu gross -> ablehnen
                    }
                }
            }
            if (headerEnd != std::string::npos) {
                size_t bodyHave = out.size() - (headerEnd + 4);
                if (!haveLen || bodyHave >= contentLen) return true;  // fertig gelesen
            }
        }
        return !out.empty();
    }

} // namespace

HttpServer::HttpServer(TrainingMonitor& monitor, int port)
    : monitor_(monitor), port_(port), listenSock_(INVALID_SOCKET) {
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
    sock_t ls = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!sockValid(ls)) {
        std::cerr << "[HttpServer] Socket konnte nicht erstellt werden.\n";
        return false;
    }

    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // nur lokal erreichbar
    addr.sin_port = htons((unsigned short)port_);

    if (::bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "[HttpServer] Port " << port_ << " konnte nicht gebunden werden "
            "(evtl. belegt).\n";
        closeSock(ls);
        return false;
    }
    if (::listen(ls, 16) != 0) {
        std::cerr << "[HttpServer] listen() fehlgeschlagen.\n";
        closeSock(ls);
        return false;
    }

    listenSock_ = (long long)ls;
    stopFlag_.store(false);
    thread_ = std::thread([this] { run(); });

    std::cout << "[HttpServer] Dashboard erreichbar unter http://localhost:" << port_ << "\n";
    return true;
}

void HttpServer::run() {
    sock_t ls = (sock_t)listenSock_;
    while (!stopFlag_.load()) {
        sockaddr_in cli{};
#ifdef _WIN32
        int clen = sizeof(cli);
#else
        socklen_t clen = sizeof(cli);
#endif
        sock_t c = ::accept(ls, (sockaddr*)&cli, &clen);
        if (!sockValid(c)) {
            if (stopFlag_.load()) break;
            continue;
        }
        handleClient((long long)c);
        closeSock(c);
    }
}

void HttpServer::handleClient(long long sockLL) {
    sock_t s = (sock_t)sockLL;

    // [NEU-1] Request vollstaendig einlesen (Header + Body)
    std::string req;
    if (!readRequest(s, req)) return;

    // Sehr einfacher Request-Parser: "METHOD <pfad> HTTP/1.1"
    std::string method = "GET";
    std::string path = "/";
    {
        size_t sp1 = req.find(' ');
        size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : req.find(' ', sp1 + 1);
        if (sp1 != std::string::npos) {
            method = req.substr(0, sp1);
            if (sp2 != std::string::npos) {
                path = req.substr(sp1 + 1, sp2 - sp1 - 1);
            }
        }
    }
    // Query-String abschneiden
    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);

    // Body extrahieren (leer bei GET)
    std::string body;
    {
        size_t he = req.find("\r\n\r\n");
        if (he != std::string::npos) body = req.substr(he + 4);
    }

    // ---------------- Bestehende Routen ----------------
    if (path == "/" || path == "/index.html") {
        sendResponse(s, "200 OK", "text/html; charset=utf-8", DASHBOARD_HTML);
    }
    else if (path == "/api/status") {
        sendResponse(s, "200 OK", "application/json", monitor_.toJson());
    }
    else if (path == "/api/replay") {
        sendResponse(s, "200 OK", "application/json", monitor_.replayJson());
    }
    else if (path == "/api/config") {
        if (method == "GET") {
            sendResponse(s, "200 OK", "application/json", monitor_.configJson());
        }
        else if (method == "POST") {
            if (!body.empty()) {
                bool ok = monitor_.applyConfigJson(body);
                if (ok) sendResponse(s, "200 OK", "application/json", "{\"ok\":true}");
                else    sendResponse(s, "400 Bad Request", "application/json", "{\"ok\":false}");
            }
            else {
                sendResponse(s, "400 Bad Request", "application/json", "{\"ok\":false}");
            }
        }
        else {
            sendResponse(s, "405 Method Not Allowed", "text/plain; charset=utf-8", "Method Not Allowed");
        }
    }
    else if (path == "/api/stop" && method == "POST") {
        monitor_.requestStop();
        sendResponse(s, "200 OK", "application/json", "{\"ok\":true}");
    }

    // ---------------- [NEU-2] Home-Bereich: Modelle & Playground ----------------
    // Die Routen sind fertig verdrahtet; sobald der TrainingMonitor die
    // jeweilige Methode anbietet, die sendNotImplemented-Zeile durch den
    // markierten Aufruf ersetzen. Das Frontend kommt mit 501 sauber klar.

    else if (path == "/api/models" && method == "GET") {
        sendResponse(s, "200 OK", "application/json", monitor_.modelsJson());
    }
    else if (path == "/api/models/save" && method == "POST") {
        // POST-Body (vom Dashboard, Name optional): {"name":"Sammler v2"}
        sendResponse(s, "200 OK", "application/json", monitor_.saveCurrentModelJson(body));
    }
    else if (path.rfind("/api/models/", 0) == 0 && method == "GET"
        && path.size() > 7 && path.compare(path.size() - 7, 7, "/replay") == 0) {
        // GET /api/models/{id}/replay
        std::string id = path.substr(12, path.size() - 12 - 7);
        sendResponse(s, "200 OK", "application/json", monitor_.modelReplayJson(id));
    }
    else if (path.rfind("/api/models/", 0) == 0 && method == "POST"
        && path.size() > 5 && path.compare(path.size() - 5, 5, "/load") == 0) {
        // POST /api/models/{id}/load
        std::string id = path.substr(12, path.size() - 12 - 5);
        bool ok = monitor_.requestModelLoad(id);
        sendResponse(s, ok ? "200 OK" : "404 Not Found", "application/json",
            ok ? "{\"ok\":true}" : "{\"ok\":false}");
    }
    else if (path == "/api/playground/run" && method == "POST") {
        // POST-Body (vom Dashboard):
        //   { "model":"<id>", "w":W, "h":H,
        //     "walls":[idx,...], "food":[idx,...],
        //     "start":[x,y], "maxSteps":N,
        //     "foodReward":F, "stepCost":F }
        sendResponse(s, "200 OK", "application/json", monitor_.runPlaygroundJson(body));
    }

    else {
        sendResponse(s, "404 Not Found", "text/plain; charset=utf-8", "Not Found");
    }
}

void HttpServer::stop() {
    if (stopFlag_.exchange(true)) return; // bereits gestoppt
    if (listenSock_ != (long long)INVALID_SOCKET) {
        closeSock((sock_t)listenSock_);   // bricht accept() ab
        listenSock_ = (long long)INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
}