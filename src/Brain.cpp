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

Brain::Brain() { randomize(); sigma = MUTATION_STRENGTH_INIT; }

void Brain::randomize() {
    float scale = sqrtf(2.0f / IN_SIZE); // He-Init
    for (int i = 0; i < HIDDEN; i++)
        for (int j = 0; j < IN_SIZE; j++) {
            W_z[i][j] = randFloat(-scale, scale);
            W_r[i][j] = randFloat(-scale, scale);
            W_h[i][j] = randFloat(-scale, scale);
        }
    float scaleOut = sqrtf(2.0f / HIDDEN);
    for (int o = 0; o < OUTPUTS; o++)
        for (int h = 0; h < HIDDEN; h++)
            W_out[o][h] = randFloat(-scaleOut, scaleOut);
    memset(hidden_state, 0, sizeof(hidden_state));
    for (int i = 0; i < HIDDEN; i++) bias_z[i] = bias_r[i] = bias_h[i] = 0.0f;
    for (int o = 0; o < OUTPUTS; o++) bias_out[o] = 0.0f;
}

void Brain::resetHidden() { memset(hidden_state, 0, sizeof(hidden_state)); }

void Brain::forward(const array<float, INPUTS>& inp, float logits[OUTPUTS]) {
    float newH[HIDDEN];
    for (int i = 0; i < HIDDEN; i++) {
        float sz = bias_z[i], sr = bias_r[i];
        for (int j = 0; j < INPUTS; j++) { sz += inp[j] * W_z[i][j]; sr += inp[j] * W_r[i][j]; }
        for (int j = 0; j < HIDDEN; j++) { sz += hidden_state[j] * W_z[i][INPUTS + j]; sr += hidden_state[j] * W_r[i][INPUTS + j]; }
        float z = sigmoid(sz), r = sigmoid(sr);
        float sh = bias_h[i];
        for (int j = 0; j < INPUTS; j++) sh += inp[j] * W_h[i][j];
        for (int j = 0; j < HIDDEN; j++) sh += r * hidden_state[j] * W_h[i][INPUTS + j];
        newH[i] = (1.0f - z) * hidden_state[i] + z * tanhAct(sh);
    }
    memcpy(hidden_state, newH, sizeof(hidden_state));
    for (int o = 0; o < OUTPUTS; o++) {
        float s = bias_out[o];
        for (int h = 0; h < HIDDEN; h++) s += hidden_state[h] * W_out[o][h];
        logits[o] = s;
    }
}

void Brain::mutate(float globalStrength, float forceSigma) {
    float eff;
    if (forceSigma > 0) {
        eff = forceSigma; // Eliten: fixe, sanfte Staerke
    }
    else {
        eff = sigma;      // selbstadaptive Basis
        // Globale Staerke (inkl. Stagnations-Boost) als Untergrenze
        float floor = globalStrength * MUTATION_FLOOR_FROM_GLOBAL;
        if (floor > eff) eff = floor;
    }
    eff += randFloat(-0.12f, 0.12f) * eff;
    eff = max(MUTATION_STRENGTH_MIN, min(1.0f, eff));
    sigma = eff;

    auto mut = [eff](float& w, float bound = 2.0f) {
        if (randFloat(0, 1) < BASE_MUTATION_RATE) {
            w += randFloat(-eff, eff);
            w = max(-bound, min(bound, w));
        }
        };
    for (int i = 0; i < HIDDEN; i++) {
        for (int j = 0; j < IN_SIZE; j++) { mut(W_z[i][j]); mut(W_r[i][j]); mut(W_h[i][j]); }
        mut(bias_z[i], 1.0f); mut(bias_r[i], 1.0f); mut(bias_h[i], 1.0f);
    }
    for (int o = 0; o < OUTPUTS; o++) {
        for (int h = 0; h < HIDDEN; h++) mut(W_out[o][h]);
        mut(bias_out[o], 1.0f);
    }
}

float Brain::similarity(const Brain& o) const {
    float diff = 0; int cnt = 0;
    for (int i = 0; i < HIDDEN; i++)
        for (int j = 0; j < IN_SIZE; j++) {
            diff += fabsf(W_z[i][j] - o.W_z[i][j]);
            diff += fabsf(W_r[i][j] - o.W_r[i][j]);
            diff += fabsf(W_h[i][j] - o.W_h[i][j]);
            cnt += 3;
        }
    for (int oi = 0; oi < OUTPUTS; oi++)
        for (int h = 0; h < HIDDEN; h++) { diff += fabsf(W_out[oi][h] - o.W_out[oi][h]); cnt++; }
    return 1.0f - (diff / (cnt * 2.0f));
}

Brain Brain::crossover(const Brain& a, const Brain& b) {
    Brain c;
    if (randFloat(0, 1) < CROSSOVER_PROB) {
        for (int i = 0; i < HIDDEN; i++) {
            for (int j = 0; j < IN_SIZE; j++) {
                float al = randFloat(0, 1);
                if (randFloat(0, 1) < 0.5f) {
                    c.W_z[i][j] = al * a.W_z[i][j] + (1 - al) * b.W_z[i][j];
                    c.W_r[i][j] = al * a.W_r[i][j] + (1 - al) * b.W_r[i][j];
                    c.W_h[i][j] = al * a.W_h[i][j] + (1 - al) * b.W_h[i][j];
                }
                else {
                    c.W_z[i][j] = (randFloat(0, 1) < 0.5f) ? a.W_z[i][j] : b.W_z[i][j];
                    c.W_r[i][j] = (randFloat(0, 1) < 0.5f) ? a.W_r[i][j] : b.W_r[i][j];
                    c.W_h[i][j] = (randFloat(0, 1) < 0.5f) ? a.W_h[i][j] : b.W_h[i][j];
                }
            }
            float al = randFloat(0, 1);
            c.bias_z[i] = al * a.bias_z[i] + (1 - al) * b.bias_z[i];
            c.bias_r[i] = al * a.bias_r[i] + (1 - al) * b.bias_r[i];
            c.bias_h[i] = al * a.bias_h[i] + (1 - al) * b.bias_h[i];
        }
        for (int o = 0; o < OUTPUTS; o++) {
            float al = randFloat(0, 1);
            for (int h = 0; h < HIDDEN; h++) {
                c.W_out[o][h] = (randFloat(0, 1) < 0.5f)
                    ? al * a.W_out[o][h] + (1 - al) * b.W_out[o][h]
                    : ((randFloat(0, 1) < 0.5f) ? a.W_out[o][h] : b.W_out[o][h]);
            }
            c.bias_out[o] = al * a.bias_out[o] + (1 - al) * b.bias_out[o];
        }
        c.sigma = (a.sigma + b.sigma) * 0.5f;
    }
    else {
        const Brain& p = (randFloat(0, 1) < 0.5f) ? a : b;
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
