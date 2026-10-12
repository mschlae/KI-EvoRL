// ============================================================
//  Brain.cpp
// ============================================================
#include "Brain.h"
#include "MathUtils.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace std;

namespace {
    constexpr int N_GATE = HIDDEN * Brain::IN_SIZE;   // Eintraege einer Gate-Matrix
    constexpr int N_OUT = OUTPUTS * HIDDEN;

    // Wird fuer jedes Gewicht gewuerfelt -> Hotspot der Mutation.
    inline void mutateSpan(float* w, int n, float eff, float bound) {
        for (int i = 0; i < n; i++) {
            if (rand01() < BASE_MUTATION_RATE) {
                float v = w[i] + randFloat(-eff, eff);
                w[i] = max(-bound, min(bound, v));
            }
        }
    }

    // Kreuzt zwei Gewichtsvektoren (arithmetisch ODER uniform, je Gewicht).
    inline void crossSpan(float* c, const float* a, const float* b, int n) {
        for (int i = 0; i < n; i++) {
            float al = rand01();
            if (rand01() < 0.5f) c[i] = al * a[i] + (1 - al) * b[i];
            else                 c[i] = (rand01() < 0.5f) ? a[i] : b[i];
        }
    }
}

Brain::Brain() { randomize(); sigma = MUTATION_STRENGTH_INIT; }

Brain::Brain(NoInit) {
    memset(W_z, 0, sizeof(W_z));
    memset(W_r, 0, sizeof(W_r));
    memset(W_h, 0, sizeof(W_h));
    memset(W_out, 0, sizeof(W_out));
    memset(hidden_state, 0, sizeof(hidden_state));
    sigma = MUTATION_STRENGTH_INIT;
}

void Brain::randomize() {
    float scale = sqrtf(2.0f / IN_SIZE); // He-Init
    float* mats[3] = { &W_z[0][0], &W_r[0][0], &W_h[0][0] };
    for (float* m : mats)
        for (int i = 0; i < N_GATE; i++) m[i] = randFloat(-scale, scale);
    float scaleOut = sqrtf(2.0f / HIDDEN);
    for (int i = 0; i < N_OUT; i++) (&W_out[0][0])[i] = randFloat(-scaleOut, scaleOut);
    memset(hidden_state, 0, sizeof(hidden_state));
    memset(bias_z, 0, sizeof(bias_z));
    memset(bias_r, 0, sizeof(bias_r));
    memset(bias_h, 0, sizeof(bias_h));
    memset(bias_out, 0, sizeof(bias_out));
}

void Brain::resetHidden() { memset(hidden_state, 0, sizeof(hidden_state)); }

// GRU (Variante "reset nach Matrixprodukt"):
//   z = s(Wz [x,h] + bz)      r = s(Wr [x,h] + br)
//   c = tanh(Wh_x x + r * (Wh_h h) + bh)
//   h' = (1 - z) h + z c
// Phase 1: Skalarprodukte ueber zusammenhaengende Gewichtszeilen (vektorisiert).
// Phase 2: Aktivierungen in einer eigenen, vektorisierbaren Schleife (kein libm-Aufruf).
void Brain::forward(const array<float, INPUTS>& inp, float logits[OUTPUTS]) {
    float xh[IN_SIZE];
    memcpy(xh, inp.data(), INPUTS * sizeof(float));
    memcpy(xh + INPUTS, hidden_state, HIDDEN * sizeof(float));

    float zp[HIDDEN], rp[HIDDEN], hx[HIDDEN], hh[HIDDEN];
    for (int i = 0; i < HIDDEN; i++) {
        zp[i] = bias_z[i] + dot(W_z[i], xh, IN_SIZE);
        rp[i] = bias_r[i] + dot(W_r[i], xh, IN_SIZE);
        hx[i] = bias_h[i] + dot(W_h[i], xh, INPUTS);
        hh[i] = dot(W_h[i] + INPUTS, xh + INPUTS, HIDDEN);
    }
    for (int i = 0; i < HIDDEN; i++) {
        float z = fastSigmoid(zp[i]);
        float r = fastSigmoid(rp[i]);
        float c = fastTanh(hx[i] + r * hh[i]);
        hidden_state[i] = (1.0f - z) * hidden_state[i] + z * c;
    }
    for (int o = 0; o < OUTPUTS; o++)
        logits[o] = bias_out[o] + dot(W_out[o], hidden_state, HIDDEN);
}

int Brain::sampleAction(float p[OUTPUTS], Rng& rng) {
    softmax_inplace(p, OUTPUTS);
    float rv = rng.uniform01(), cum = 0;
    for (int i = 0; i < OUTPUTS; i++) { cum += p[i]; if (rv < cum) return i; }
    return OUTPUTS - 1;
}

int Brain::selectAction(const array<float, INPUTS>& inp, Rng& rng) {
    float p[OUTPUTS];
    forward(inp, p);
    return sampleAction(p, rng);
}

void Brain::mutate(float globalStrength, float forceSigma) {
    float eff;
    if (forceSigma > 0) {
        eff = forceSigma; // Eliten: fixe, sanfte Staerke
    }
    else {
        eff = sigma;      // selbstadaptive Basis
        // Globale Staerke (inkl. Stagnations-Boost) als Untergrenze
        float floorV = globalStrength * MUTATION_FLOOR_FROM_GLOBAL;
        if (floorV > eff) eff = floorV;
    }
    eff += randFloat(-0.12f, 0.12f) * eff;
    eff = max(MUTATION_STRENGTH_MIN, min(1.0f, eff));
    sigma = eff;

    mutateSpan(&W_z[0][0], N_GATE, eff, 2.0f);
    mutateSpan(&W_r[0][0], N_GATE, eff, 2.0f);
    mutateSpan(&W_h[0][0], N_GATE, eff, 2.0f);
    mutateSpan(bias_z, HIDDEN, eff, 1.0f);
    mutateSpan(bias_r, HIDDEN, eff, 1.0f);
    mutateSpan(bias_h, HIDDEN, eff, 1.0f);
    mutateSpan(&W_out[0][0], N_OUT, eff, 2.0f);
    mutateSpan(bias_out, OUTPUTS, eff, 1.0f);
}

// Mittlere Betragsdifferenz der Gewichte, auf [~0, 1] skaliert (1 = identisch).
float Brain::similarity(const Brain& o) const {
    float diff = absDiffSum(&W_z[0][0], &o.W_z[0][0], N_GATE)
        + absDiffSum(&W_r[0][0], &o.W_r[0][0], N_GATE)
        + absDiffSum(&W_h[0][0], &o.W_h[0][0], N_GATE)
        + absDiffSum(&W_out[0][0], &o.W_out[0][0], N_OUT);
    int cnt = 3 * N_GATE + N_OUT;
    return 1.0f - (diff / (cnt * 2.0f));
}

Brain Brain::crossover(const Brain& a, const Brain& b) {
    Brain c{ NoInit{} };
    if (rand01() < CROSSOVER_PROB) {
        crossSpan(&c.W_z[0][0], &a.W_z[0][0], &b.W_z[0][0], N_GATE);
        crossSpan(&c.W_r[0][0], &a.W_r[0][0], &b.W_r[0][0], N_GATE);
        crossSpan(&c.W_h[0][0], &a.W_h[0][0], &b.W_h[0][0], N_GATE);
        crossSpan(&c.W_out[0][0], &a.W_out[0][0], &b.W_out[0][0], N_OUT);
        // Biases: rein arithmetisch gemischt
        for (int i = 0; i < HIDDEN; i++) {
            float al = rand01();
            c.bias_z[i] = al * a.bias_z[i] + (1 - al) * b.bias_z[i];
            c.bias_r[i] = al * a.bias_r[i] + (1 - al) * b.bias_r[i];
            c.bias_h[i] = al * a.bias_h[i] + (1 - al) * b.bias_h[i];
        }
        for (int o = 0; o < OUTPUTS; o++) {
            float al = rand01();
            c.bias_out[o] = al * a.bias_out[o] + (1 - al) * b.bias_out[o];
        }
        c.sigma = (a.sigma + b.sigma) * 0.5f;
    }
    else {
        const Brain& p = (rand01() < 0.5f) ? a : b;
        memcpy(c.W_z, p.W_z, sizeof(c.W_z));
        memcpy(c.W_r, p.W_r, sizeof(c.W_r));
        memcpy(c.W_h, p.W_h, sizeof(c.W_h));
        memcpy(c.W_out, p.W_out, sizeof(c.W_out));
        memcpy(c.bias_z, p.bias_z, sizeof(c.bias_z));
        memcpy(c.bias_r, p.bias_r, sizeof(c.bias_r));
        memcpy(c.bias_h, p.bias_h, sizeof(c.bias_h));
        memcpy(c.bias_out, p.bias_out, sizeof(c.bias_out));
        c.sigma = p.sigma;
    }
    return c;
}
