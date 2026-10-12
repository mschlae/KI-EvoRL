#pragma once
// ============================================================
//  Json.h  -  Winzige JSON-Helfer fuer die flachen Bodies der API
//  (kein vollwertiger Parser: sucht Schluessel auf oberster Ebene
//  und liest Zahlen, Strings und Integer-Arrays).
// ============================================================
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace json {

// Findet `"key"` gefolgt von ':' und liefert die Position hinter dem Doppelpunkt
// (std::string::npos, wenn nicht vorhanden). Treffer innerhalb von String-Werten
// ("name":"w") werden uebersprungen, weil dort kein ':' folgt.
inline size_t findValue(const std::string& s, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t p = 0;
    while ((p = s.find(pat, p)) != std::string::npos) {
        size_t q = p + pat.size();
        while (q < s.size() && std::isspace((unsigned char)s[q])) q++;
        if (q < s.size() && s[q] == ':') {
            q++;
            while (q < s.size() && std::isspace((unsigned char)s[q])) q++;
            return q;
        }
        p += pat.size();
    }
    return std::string::npos;
}

inline bool extractFloat(const std::string& s, const std::string& key, float& out) {
    size_t p = findValue(s, key);
    if (p == std::string::npos || p >= s.size()) return false;
    const char* b = s.c_str() + p;
    char* e = nullptr;
    double v = std::strtod(b, &e);
    if (e == b) return false;
    if (!(v > -1e30 && v < 1e30)) return false;    // NaN/Inf/Overflow abweisen
    out = (float)v;
    return true;
}

inline bool extractInt(const std::string& s, const std::string& key, long& out) {
    float f;
    if (!extractFloat(s, key, f)) return false;
    out = (long)f;
    return true;
}

inline bool extractString(const std::string& s, const std::string& key, std::string& out) {
    size_t p = findValue(s, key);
    if (p == std::string::npos || p >= s.size() || s[p] != '"') return false;
    std::string r;
    for (size_t i = p + 1; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') { out = std::move(r); return true; }
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
            case 'n': case 'r': case 't': r += ' '; break;
            case 'b': case 'f': break;
            case 'u': {                         // \uXXXX: nur ASCII uebernehmen
                unsigned v = 0;
                if (i + 4 < s.size() && std::sscanf(s.c_str() + i + 1, "%4x", &v) == 1) {
                    if (v >= 0x20 && v < 0x80) r += (char)v; else r += '?';
                    i += 4;
                }
                break;
            }
            default: r += n;                    // \" \\ \/
            }
        }
        else r += c;
    }
    return false;                               // String nicht terminiert
}

inline std::vector<int> parseIntArray(const std::string& s, const std::string& key) {
    std::vector<int> v;
    size_t p = findValue(s, key);
    if (p == std::string::npos || p >= s.size() || s[p] != '[') return v;
    size_t rb = s.find(']', p);
    if (rb == std::string::npos) return v;
    size_t i = p + 1;
    while (i < rb) {
        while (i < rb && !(std::isdigit((unsigned char)s[i]) || s[i] == '-')) i++;
        if (i >= rb) break;
        size_t j = i + 1;
        while (j < rb && std::isdigit((unsigned char)s[j])) j++;
        if (j - i <= 9) v.push_back(std::atoi(s.substr(i, j - i).c_str()));
        i = j;
    }
    return v;
}

inline std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': case '\r': case '\t': o += ' '; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}

} // namespace json
