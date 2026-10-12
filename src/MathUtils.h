#pragma once
// ============================================================
//  MathUtils.h  -  Aktivierungsfunktionen, Softmax, Vektor-Kerne
// ============================================================
#include <cmath>
#include <cstdint>
#include <cstring>

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

// Skalarprodukt mit 8 unabhaengigen Akkumulatoren. Das erlaubt dem Compiler die
// Vektorisierung (SSE/AVX) auch ohne -ffast-math; die Summationsreihenfolge ist fest,
// das Ergebnis daher auf allen Plattformen/Befehlssaetzen identisch.
inline float dot(const float* a, const float* b, int n) {
    float acc[8] = {};
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; k++) acc[k] += a[i + k] * b[i + k];
    float s = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
    for (; i < n; i++) s += a[i] * b[i];
    return s;
}

// Summe der Betragsdifferenzen (L1-Distanz), gleiches Schema.
inline float absDiffSum(const float* a, const float* b, int n) {
    float acc[8] = {};
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; k++) acc[k] += fabsf(a[i + k] - b[i + k]);
    float s = ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
    for (; i < n; i++) s += fabsf(a[i] - b[i]);
    return s;
}

// Schnelle, verzweigungsfreie exp()-Naeherung (Cephes, Fehler ~1e-7 relativ). Anders als
// expf() aus der libm laesst sie sich in Schleifen vektorisieren. Eingabe wird auf
// [-87, 87] begrenzt.
inline float fastExp(float x) {
    x = x < -87.0f ? -87.0f : (x > 87.0f ? 87.0f : x);
    float t = x * 1.44269504089f;
    float n = (t + 12582912.0f) - 12582912.0f;              // auf ganze Zahl runden (ohne floorf)
    float r = x - n * 0.693359375f;                          // ln2 in zwei Teilen (Cody-Waite)
    r -= n * -2.12194440e-4f;
    float p = 1.9875691500e-4f;
    p = p * r + 1.3981999507e-3f;
    p = p * r + 8.3334519073e-3f;
    p = p * r + 4.1665795894e-2f;
    p = p * r + 1.6666665459e-1f;
    p = p * r + 5.0000001201e-1f;
    p = p * r * r + r + 1.0f;
    int32_t bits = ((int32_t)n + 127) << 23;                 // 2^n
    float scale;
    std::memcpy(&scale, &bits, sizeof(scale));
    return p * scale;
}
inline float fastSigmoid(float x) { return 1.0f / (1.0f + fastExp(-x)); }
inline float fastTanh(float x) { return 1.0f - 2.0f / (fastExp(2.0f * x) + 1.0f); }
