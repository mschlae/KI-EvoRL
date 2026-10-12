// ============================================================
//  Config.cpp  -  Laufzeit-Konfiguration (threadsicher)
// ============================================================
#include "Config.h"
#include <mutex>

namespace {
    RuntimeConfig g_runtime_config;
    std::mutex    g_runtime_config_mutex;
}

RuntimeConfig getRuntimeConfig() {
    std::lock_guard<std::mutex> lk(g_runtime_config_mutex);
    return g_runtime_config;
}

void setRuntimeConfig(const RuntimeConfig& cfg) {
    std::lock_guard<std::mutex> lk(g_runtime_config_mutex);
    g_runtime_config = cfg;
}
