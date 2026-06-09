// ============================================================
//  Config.cpp  -  Definition globaler veraenderlicher Zustaende
// ============================================================
#include "Config.h"
#include <mutex>

// Startet einfach (0.0) und wird im Training hochgefahren.
float g_curriculum_difficulty = 0.0f;

// RuntimeConfig-Storage
static RuntimeConfig g_runtime_config;
static std::mutex g_runtime_config_mutex;

RuntimeConfig getRuntimeConfig() {
	std::lock_guard<std::mutex> lk(g_runtime_config_mutex);
	return g_runtime_config;
}

void setRuntimeConfig(const RuntimeConfig& cfg) {
	std::lock_guard<std::mutex> lk(g_runtime_config_mutex);
	g_runtime_config = cfg;
}
