// ============================================================
//  HttpServer.cpp
// ============================================================
#include "HttpServer.h"
#include "DashboardHtml.h"
#include "TrainingMonitor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>

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
static void setTimeouts(sock_t s, int ms) {
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
}
static const int SEND_FLAGS = 0;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t = int;
static const int INVALID_SOCKET = -1;
static int  closeSock(sock_t s) { return ::close(s); }
static bool sockValid(sock_t s) { return s >= 0; }
static void setTimeouts(sock_t s, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));   // macOS: kein SIGPIPE
#endif
}
#ifdef MSG_NOSIGNAL
static const int SEND_FLAGS = MSG_NOSIGNAL;                       // Linux: kein SIGPIPE
#else
static const int SEND_FLAGS = 0;
#endif
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

    constexpr size_t MAX_HEADER = 16 * 1024;
    constexpr size_t MAX_BODY = 4 * 1024 * 1024;   // Schutzgrenze fuer POST-Bodies
    constexpr int    IO_TIMEOUT_MS = 5000;         // haengende Clients blockieren den Server nicht

    void sendAll(sock_t s, const char* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            int n = (int)::send(s, data + sent, (int)std::min<size_t>(len - sent, 1 << 20), SEND_FLAGS);
            if (n <= 0) break;
            sent += (size_t)n;
        }
    }

    void sendResponse(sock_t s, const char* status, const char* contentType, const std::string& body) {
        std::string head;
        head.reserve(256);
        head += "HTTP/1.1 ";
        head += status;
        head += "\r\nContent-Type: ";
        head += contentType;
        head += "\r\nContent-Length: ";
        head += std::to_string(body.size());
        head += "\r\nCache-Control: no-store";
        head += "\r\nX-Content-Type-Options: nosniff";
        head += "\r\nConnection: close\r\n\r\n";
        sendAll(s, head.data(), head.size());
        sendAll(s, body.data(), body.size());
    }

    void sendJson(sock_t s, const char* status, const std::string& body) {
        sendResponse(s, status, "application/json", body);
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    struct Request {
        std::string method, path, body;
        std::string host, origin;
        bool ok = false;
    };

    // Liest Header + Body (Content-Length-basiert) und zerlegt den Request.
    Request readRequest(sock_t s) {
        Request r;
        std::string buf;
        char tmp[4096];
        size_t headerEnd = std::string::npos;
        size_t contentLen = 0;

        while (true) {
            int n = (int)::recv(s, tmp, (int)sizeof(tmp), 0);
            if (n <= 0) return r;                          // Timeout/Abbruch
            buf.append(tmp, (size_t)n);

            if (headerEnd == std::string::npos) {
                headerEnd = buf.find("\r\n\r\n");
                if (headerEnd == std::string::npos) {
                    if (buf.size() > MAX_HEADER) return r; // Header zu gross
                    continue;
                }
                if (headerEnd > MAX_HEADER) return r;
                // Header-Zeilen auswerten
                size_t lineEnd = buf.find("\r\n");
                std::string reqLine = buf.substr(0, lineEnd);
                size_t sp1 = reqLine.find(' ');
                size_t sp2 = sp1 == std::string::npos ? std::string::npos : reqLine.find(' ', sp1 + 1);
                if (sp1 == std::string::npos || sp2 == std::string::npos) return r;
                r.method = reqLine.substr(0, sp1);
                r.path = reqLine.substr(sp1 + 1, sp2 - sp1 - 1);

                size_t pos = lineEnd + 2;
                while (pos < headerEnd) {
                    size_t e = buf.find("\r\n", pos);
                    if (e == std::string::npos || e > headerEnd) e = headerEnd;
                    std::string line = buf.substr(pos, e - pos);
                    pos = e + 2;
                    size_t c = line.find(':');
                    if (c == std::string::npos) continue;
                    std::string key = lower(line.substr(0, c));
                    size_t vb = line.find_first_not_of(" \t", c + 1);
                    std::string val = vb == std::string::npos ? "" : line.substr(vb);
                    while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();
                    if (key == "content-length") {
                        char* endp = nullptr;
                        unsigned long long v = std::strtoull(val.c_str(), &endp, 10);
                        if (endp == val.c_str() || v > MAX_BODY) return r;
                        contentLen = (size_t)v;
                    }
                    else if (key == "host") r.host = lower(val);
                    else if (key == "origin") r.origin = lower(val);
                }
            }
            if (buf.size() - (headerEnd + 4) >= contentLen) break;   // komplett
        }
        r.body = buf.substr(headerEnd + 4, contentLen);
        size_t q = r.path.find('?');
        if (q != std::string::npos) r.path.resize(q);
        r.ok = true;
        return r;
    }

    // Erlaubt nur Zugriffe, die wirklich vom lokalen Dashboard stammen.
    bool hostAllowed(const std::string& hostPort, int port) {
        const std::string p = ":" + std::to_string(port);
        return hostPort == "localhost" + p || hostPort == "127.0.0.1" + p
            || hostPort == "localhost" || hostPort == "127.0.0.1";
    }
    bool originAllowed(const Request& r, int port) {
        if (r.origin.empty()) return true;                 // z. B. curl, gleiche Herkunft bei GET
        const std::string http = "http://";
        if (r.origin.compare(0, http.size(), http) != 0) return false;
        return hostAllowed(r.origin.substr(http.size()), port);
    }

    // Modell-ID aus /api/models/{id}/<suffix>; leer, wenn das Muster nicht passt.
    std::string modelIdFor(const std::string& path, const std::string& suffix) {
        const std::string prefix = "/api/models/";
        if (path.size() <= prefix.size() + suffix.size()) return "";
        if (path.compare(0, prefix.size(), prefix) != 0) return "";
        if (path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) return "";
        std::string id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
        for (char c : id)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return "";
        return id;
    }

} // namespace

HttpServer::HttpServer(TrainingMonitor& monitor, int port)
    : monitor_(monitor), port_(port), listenSock_((long long)INVALID_SOCKET) {
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
    sock_t ls = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!sockValid(ls)) {
        std::cerr << "[HttpServer] Socket konnte nicht erstellt werden.\n";
        return false;
    }

#ifndef _WIN32
    // Unter POSIX erlaubt SO_REUSEADDR das schnelle Neustarten; unter Windows wuerde es
    // dagegen erlauben, einen belegten Port zu "stehlen" -> dort bewusst nicht setzen.
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // nur lokal erreichbar
    addr.sin_port = htons((unsigned short)port_);

    if (::bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "[HttpServer] Port " << port_ << " konnte nicht gebunden werden (evtl. belegt).\n";
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
        // Mit Timeout warten: close() weckt ein blockiertes accept() unter Linux nicht auf.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ls, &rfds);
        timeval tv{ 0, 200 * 1000 };
        int ready = ::select((int)ls + 1, &rfds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        sockaddr_in cli{};
#ifdef _WIN32
        int clen = sizeof(cli);
#else
        socklen_t clen = sizeof(cli);
#endif
        sock_t c = ::accept(ls, (sockaddr*)&cli, &clen);
        if (!sockValid(c)) continue;
        setTimeouts(c, IO_TIMEOUT_MS);
        handleClient((long long)c);
        closeSock(c);
    }
}

void HttpServer::handleClient(long long sockLL) {
    sock_t s = (sock_t)sockLL;

    Request req = readRequest(s);
    if (!req.ok) {
        sendResponse(s, "400 Bad Request", "text/plain; charset=utf-8", "Bad Request");
        return;
    }

    // DNS-Rebinding / Cross-Site-Schutz
    if (!hostAllowed(req.host, port_) || !originAllowed(req, port_)) {
        sendResponse(s, "403 Forbidden", "text/plain; charset=utf-8", "Forbidden");
        return;
    }

    const std::string& path = req.path;
    const bool isGet = req.method == "GET";
    const bool isPost = req.method == "POST";
    static const char* OK = "200 OK";

    if (isGet && (path == "/" || path == "/index.html")) {
        sendResponse(s, OK, "text/html; charset=utf-8", DASHBOARD_HTML);
    }
    else if (isGet && path == "/api/status") {
        sendJson(s, OK, monitor_.toJson());
    }
    else if (isGet && path == "/api/replay") {
        sendJson(s, OK, monitor_.replayJson());
    }
    else if (path == "/api/config") {
        if (isGet) sendJson(s, OK, monitor_.configJson());
        else if (isPost) {
            bool ok = !req.body.empty() && monitor_.applyConfigJson(req.body);
            sendJson(s, ok ? OK : "400 Bad Request", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        }
        else sendResponse(s, "405 Method Not Allowed", "text/plain; charset=utf-8", "Method Not Allowed");
    }
    else if (isPost && path == "/api/stop") {
        monitor_.requestStop();
        sendJson(s, OK, "{\"ok\":true}");
    }
    else if (isGet && path == "/api/models") {
        sendJson(s, OK, monitor_.modelsJson());
    }
    else if (isPost && path == "/api/models/save") {
        // Body (optional): {"name":"Sammler v2"}
        sendJson(s, OK, monitor_.saveCurrentModelJson(req.body));
    }
    else if (isGet && !modelIdFor(path, "/replay").empty()) {
        sendJson(s, OK, monitor_.modelReplayJson(modelIdFor(path, "/replay")));
    }
    else if (isPost && !modelIdFor(path, "/load").empty()) {
        bool ok = monitor_.requestModelLoad(modelIdFor(path, "/load"));
        sendJson(s, ok ? OK : "404 Not Found", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    }
    else if (isPost && path == "/api/playground/run") {
        // Body: { "model":"<id>", "w":W, "h":H, "walls":[idx,...], "food":[idx,...],
        //         "start":[x,y], "maxSteps":N, "foodReward":F, "stepCost":F }
        sendJson(s, OK, monitor_.runPlaygroundJson(req.body));
    }
    else {
        sendResponse(s, "404 Not Found", "text/plain; charset=utf-8", "Not Found");
    }
}

void HttpServer::stop() {
    if (stopFlag_.exchange(true)) return; // bereits gestoppt
    if (thread_.joinable()) thread_.join();   // run() beendet sich innerhalb von ~200 ms
    if (listenSock_ != (long long)INVALID_SOCKET) {
        closeSock((sock_t)listenSock_);
        listenSock_ = (long long)INVALID_SOCKET;
    }
}
