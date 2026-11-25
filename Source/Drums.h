#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

// 606-style kick: short sine with a fast pitch drop, tight decay, tiny click.
class Kick606
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        pitchCoef = std::exp (-1.f / (0.018f * sr));
        ampCoef   = std::exp (-1.f / (0.06f * sr));
        clickCoef = std::exp (-1.f / (0.0015f * sr));
        env = 0.f;
    }

    void trigger() { phase = 0.f; env = pitchEnv = clickEnv = 1.f; }

    float process()
    {
        if (env < 1.0e-4f) return 0.f;

        const float freq = 48.f + 130.f * pitchEnv;
        pitchEnv *= pitchCoef;
        phase += freq / sr;
        if (phase >= 1.f) phase -= 1.f;

        rng = rng * 1664525u + 1013904223u;
        const float noise = (float) (int32_t) rng / 2147483648.f;
        const float click = noise * clickEnv * 0.35f;
        clickEnv *= clickCoef;

        const float out = std::tanh (1.5f * std::sin (6.2831853f * phase) * env + click);
        env *= ampCoef;
        return out * 0.9f;
    }

private:
    float sr = 44100.f, pitchCoef = 0.99f, ampCoef = 0.999f, clickCoef = 0.9f;
    float phase = 0.f, env = 0.f, pitchEnv = 0.f, clickEnv = 0.f;
    uint32_t rng = 22222u;
};

// Closed hat: six detuned square oscillators + noise, high-passed, ~35 ms decay.
class Hat606
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        ampCoef = std::exp (-1.f / (0.035f * sr));
        hpA = std::exp (-6.2831853f * 7000.f / sr);
        env = 0.f;
    }

    void trigger() { env = 1.f; }

    float process()
    {
        if (env < 1.0e-4f) return 0.f;

        static const float freqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f };
        float sum = 0.f;
        for (int i = 0; i < 6; ++i)
        {
            ph[i] += freqs[i] / sr;
            if (ph[i] >= 1.f) ph[i] -= 1.f;
            sum += ph[i] < 0.5f ? 1.f : -1.f;
        }

        rng = rng * 1664525u + 1013904223u;
        const float noise = (float) (int32_t) rng / 2147483648.f;
        const float x = sum / 6.f * 0.8f + noise * 0.35f;

        // two one-pole high-pass stages
        const float a = hpA * (h1 + x - x1);   x1 = x;  h1 = a;
        const float b = hpA * (h2 + a - a1);   a1 = a;  h2 = b;

        const float out = b * env;
        env *= ampCoef;
        return out * 3.5f;
    }

private:
    float sr = 44100.f, ampCoef = 0.99f, hpA = 0.5f, env = 0.f;
    float ph[6] {};
    float x1 = 0.f, h1 = 0.f, a1 = 0.f, h2 = 0.f;
    uint32_t rng = 777u;
};
