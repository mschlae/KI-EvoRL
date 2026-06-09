// ============================================================
//  HttpServer.cpp
// ============================================================
#include "HttpServer.h"
#include "TrainingMonitor.h"
#include "DashboardHtml.h"

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

} // namespace

HttpServer::HttpServer(TrainingMonitor& monitor, int port)
    : monitor_(monitor), port_(port), listenSock_(INVALID_SOCKET) {}

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

    char buf[2048];
    int n = ::recv(s, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    // Sehr einfacher Request-Parser: "GET <pfad> HTTP/1.1"
    std::string req(buf);
    std::string path = "/";
    {
        size_t sp1 = req.find(' ');
        size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : req.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos)
            path = req.substr(sp1 + 1, sp2 - sp1 - 1);
    }
    // Query-String abschneiden
    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);

    if (path == "/" || path == "/index.html") {
        sendResponse(s, "200 OK", "text/html; charset=utf-8", DASHBOARD_HTML);
    }
    else if (path == "/api/status") {
        sendResponse(s, "200 OK", "application/json", monitor_.toJson());
    }
    else if (path == "/api/replay") {
        sendResponse(s, "200 OK", "application/json", monitor_.replayJson());
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
