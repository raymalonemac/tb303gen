#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Monophonic acid voice: PolyBLEP saw/square -> 4-pole resonant ladder -> amp.
// Overlapping notes (legato) glide and do not retrigger the filter envelope = 303 slide.
class AcidVoice
{
public:
    void prepare (double newSampleRate)
    {
        sr = (float) newSampleRate;
        glideCoef = 1.f - std::exp (-1.f / (0.06f * sr));
        ampAtk    = 1.f - std::exp (-1.f / (0.0008f * sr));
        ampRel    = 1.f - std::exp (-1.f / (0.006f * sr));
        reset();
    }

    void reset()
    {
        held.fill (false);
        gateCount = 0;
        ampEnv = filtEnv = 0.f;
        y1 = y2 = y3 = y4 = 0.f;
        phase = 0.f;
    }

    void setParams (float cutoffHz, float resonance, float envModAmt, float decaySec, bool useSquare, float vol)
    {
        cutoff = cutoffHz; reso = resonance; envMod = envModAmt;
        decay = std::max (0.02f, decaySec); square = useSquare; volume = vol;
    }

    void setDetuneCents (float cents) { detuneMul = std::exp2 (cents / 1200.f); }   // osc fine-tune (pitch LFO)

    void noteOn (int note, bool accented)
    {
        note = std::clamp (note, 0, 127);
        const bool legato = gateCount > 0;
        targetFreq = 440.f * std::pow (2.f, ((float) note - 69.f) / 12.f);

        if (! held[(size_t) note]) { held[(size_t) note] = true; ++gateCount; }
        accent = accented;

        if (! legato)
        {
            freq = targetFreq;
            filtEnv = 1.f;
            curDecayCoef = std::exp (-1.f / (decay * (accent ? 0.6f : 1.f) * sr));
        }
    }

    void noteOff (int note)
    {
        note = std::clamp (note, 0, 127);
        if (held[(size_t) note]) { held[(size_t) note] = false; --gateCount; }
    }

    float process()
    {
        freq += (targetFreq - freq) * glideCoef;
        ampEnv += ((gateCount > 0 ? 1.f : 0.f) - ampEnv) * (gateCount > 0 ? ampAtk : ampRel);
        filtEnv *= curDecayCoef;

        if (ampEnv < 1.0e-5f && gateCount == 0)
            return 0.f;

        const float dt = std::min (0.45f, freq * detuneMul / sr);
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;

        float osc;
        if (square)
        {
            float p2 = phase + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
            osc = (phase < 0.5f ? 1.f : -1.f) + polyBlep (phase, dt) - polyBlep (p2, dt);
        }
        else
        {
            osc = 2.f * phase - 1.f - polyBlep (phase, dt);
        }

        const float envAmt = envMod * (accent ? 1.5f : 1.f);
        float fc = cutoff * (1.f + envAmt * filtEnv * 8.f);
        fc = std::min (fc, 0.22f * sr);
        const float f = std::min (0.75f, 2.f * std::sin (3.14159265f * fc / sr));
        const float k = reso * 3.8f;

        const float in = std::tanh (osc * 1.2f - k * y4);
        y1 += f * (in - y1);
        y2 += f * (y1 - y2);
        y3 += f * (y2 - y3);
        y4 += f * (y3 - y4);

        const float out = y4 * (1.f + k * 0.35f);
        return out * ampEnv * volume * (accent ? 1.f : 0.75f);
    }

private:
    static float polyBlep (float t, float dt)
    {
        if (t < dt)         { t /= dt;               return t + t - t * t - 1.f; }
        if (t > 1.f - dt)   { t = (t - 1.f) / dt;    return t * t + t + t + 1.f; }
        return 0.f;
    }

    float sr = 44100.f;
    float glideCoef = 0.001f, ampAtk = 0.1f, ampRel = 0.01f, curDecayCoef = 0.9999f;
    float cutoff = 500.f, reso = 0.6f, envMod = 0.6f, decay = 0.3f, volume = 0.7f;
    bool square = false, accent = false;
    float detuneMul = 1.f;
    float freq = 110.f, targetFreq = 110.f, phase = 0.f, ampEnv = 0.f, filtEnv = 0.f;
    float y1 = 0.f, y2 = 0.f, y3 = 0.f, y4 = 0.f;
    int gateCount = 0;
    std::array<bool, 128> held {};
};
