#pragma once
// ============================================================
//  MathUtils.h  -  Aktivierungsfunktionen / Softmax
// ============================================================
#include <cmath>

inline float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }
inline float tanhAct(float x) { return tanhf(x); }

inline void softmax_inplace(float* v, int n) {
    float m = v[0];
    for (int i = 1; i < n; i++) if (v[i] > m) m = v[i];
    float s = 0;
    for (int i = 0; i < n; i++) { v[i] = expf(v[i] - m); s += v[i]; }
    float inv = 1.0f / s;
    for (int i = 0; i < n; i++) v[i] *= inv;
}
