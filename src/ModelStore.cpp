// ============================================================
//  ModelStore.cpp
// ============================================================
#include "ModelStore.h"
#include "Json.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    constexpr uint32_t MAGIC_V1 = 0x42524E31u; // "BRN1"
    constexpr uint32_t MAGIC_V2 = 0x42524E32u; // "BRN2"

    template <class T> void put(std::ostream& os, const T& v) {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <class T> bool get(std::istream& is, T& v) {
        is.read(reinterpret_cast<char*>(&v), sizeof(T));
        return (bool)is;
    }
    void putArr(std::ostream& os, const float* p, size_t n) {
        os.write(reinterpret_cast<const char*>(p), std::streamsize(n * sizeof(float)));
    }
    bool getArr(std::istream& is, float* p, size_t n) {
        is.read(reinterpret_cast<char*>(p), std::streamsize(n * sizeof(float)));
        return (bool)is;
    }

    bool finite(const float* p, size_t n) {
        for (size_t i = 0; i < n; i++) if (!(p[i] > -1e6f && p[i] < 1e6f)) return false;
        return true;
    }
}

namespace modelstore {

bool writeBrain(std::ostream& os, const Brain& b) {
    put(os, MAGIC_V2);
    put(os, (uint32_t)INPUTS); put(os, (uint32_t)HIDDEN); put(os, (uint32_t)OUTPUTS);
    constexpr size_t G = size_t(HIDDEN) * Brain::IN_SIZE;
    putArr(os, &b.W_z[0][0], G);
    putArr(os, &b.W_r[0][0], G);
    putArr(os, &b.W_h[0][0], G);
    putArr(os, &b.W_out[0][0], size_t(OUTPUTS) * HIDDEN);
    putArr(os, b.bias_z, HIDDEN);
    putArr(os, b.bias_r, HIDDEN);
    putArr(os, b.bias_h, HIDDEN);
    putArr(os, b.bias_out, OUTPUTS);
    putArr(os, &b.sigma, 1);
    return (bool)os;
}

bool readBrain(std::istream& is, Brain& out) {
    uint32_t magic = 0;
    if (!get(is, magic)) return false;

    if (magic == MAGIC_V1) {                    // Altformat: rohes Struct
        uint32_t sz = 0;
        if (!get(is, sz) || sz != (uint32_t)sizeof(Brain)) return false;
        is.read(reinterpret_cast<char*>(&out), sizeof(Brain));
        return (bool)is;
    }
    if (magic != MAGIC_V2) return false;

    uint32_t in = 0, hid = 0, outs = 0;
    if (!get(is, in) || !get(is, hid) || !get(is, outs)) return false;
    if (in != (uint32_t)INPUTS || hid != (uint32_t)HIDDEN || outs != (uint32_t)OUTPUTS) return false;

    Brain tmp{ Brain::NoInit{} };
    constexpr size_t G = size_t(HIDDEN) * Brain::IN_SIZE;
    bool ok = getArr(is, &tmp.W_z[0][0], G) && getArr(is, &tmp.W_r[0][0], G) && getArr(is, &tmp.W_h[0][0], G)
        && getArr(is, &tmp.W_out[0][0], size_t(OUTPUTS) * HIDDEN)
        && getArr(is, tmp.bias_z, HIDDEN) && getArr(is, tmp.bias_r, HIDDEN) && getArr(is, tmp.bias_h, HIDDEN)
        && getArr(is, tmp.bias_out, OUTPUTS) && getArr(is, &tmp.sigma, 1);
    if (!ok) return false;
    // Beschaedigte Dateien (NaN/Inf) nicht ins Training lassen.
    if (!finite(&tmp.W_z[0][0], G) || !finite(&tmp.W_r[0][0], G) || !finite(&tmp.W_h[0][0], G)
        || !finite(&tmp.W_out[0][0], size_t(OUTPUTS) * HIDDEN))
        return false;
    out = tmp;
    return true;
}

std::string timestampNow() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

std::string newId() {
    static std::atomic<uint32_t> counter{ 0 };
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "m" + std::to_string(ms) + "_" + std::to_string(counter.fetch_add(1));
}

bool save(const std::string& dir, const SavedModel& m) {
    if (!m.brain) return false;
    // IDs werden zu Dateinamen -> nur sichere Zeichen zulassen.
    for (char c : m.id)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    try {
        fs::create_directories(dir);
        fs::path base = fs::path(dir) / m.id;

        auto writeAtomic = [&](const std::string& ext, auto&& writer) {
            fs::path fin = base.string() + ext;
            fs::path tmp = base.string() + ext + ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                writer(f);
                f.flush();
                if (!f) { f.close(); std::error_code ec; fs::remove(tmp, ec); return false; }
            }
            std::error_code ec;
            fs::rename(tmp, fin, ec);
            if (ec) { fs::remove(tmp, ec); return false; }
            return true;
        };

        // Gewichte zuerst: ein .meta ohne .brain waere ein "totes" Modell.
        bool okBrain = writeAtomic(".brain", [&](std::ostream& f) { writeBrain(f, *m.brain); });
        bool okMeta = okBrain && writeAtomic(".meta", [&](std::ostream& f) {
            f << std::fixed << std::setprecision(4)
              << '{' << "\"id\":\"" << json::escape(m.id) << "\","
              << "\"name\":\"" << json::escape(m.name) << "\","
              << "\"generation\":" << m.generation << ','
              << "\"fitness\":" << m.fitness << ','
              << "\"food\":" << m.food << ','
              << "\"steps\":" << m.steps << ','
              << "\"savedAt\":\"" << json::escape(m.savedAt) << "\"}";
        });
        return okBrain && okMeta;
    }
    catch (...) { return false; }
}

std::vector<SavedModel> loadAll(const std::string& dir) {
    std::vector<SavedModel> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const fs::path& p = entry.path();
        if (p.extension() != ".meta") continue;

        std::ifstream meta(p, std::ios::binary);
        if (!meta) continue;
        std::stringstream ss; ss << meta.rdbuf();
        const std::string js = ss.str();

        SavedModel m;
        json::extractString(js, "id", m.id);
        json::extractString(js, "name", m.name);
        json::extractString(js, "savedAt", m.savedAt);
        long lv; float fv;
        if (json::extractInt(js, "generation", lv)) m.generation = (int)lv;
        if (json::extractInt(js, "food", lv)) m.food = (int)lv;
        if (json::extractInt(js, "steps", lv)) m.steps = (int)lv;
        if (json::extractFloat(js, "fitness", fv)) m.fitness = fv;
        // Dateiname ist massgeblich (zusammengehoerige .brain-Datei).
        m.id = p.stem().string();

        fs::path brainPath = p; brainPath.replace_extension(".brain");
        std::ifstream br(brainPath, std::ios::binary);
        if (!br) continue;
        auto b = std::make_shared<Brain>(Brain::NoInit{});
        if (!readBrain(br, *b)) continue;           // Format/Dimensionen passen nicht
        m.brain = std::move(b);
        out.push_back(std::move(m));
    }
    // verzeichnisreihenfolge ist undefiniert -> nach ID (enthaelt Zeitstempel) ordnen
    std::sort(out.begin(), out.end(), [](const SavedModel& a, const SavedModel& b) {
        return a.id.size() != b.id.size() ? a.id.size() < b.id.size() : a.id < b.id;
    });
    return out;
}

} // namespace modelstore
