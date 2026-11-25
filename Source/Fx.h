#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Bass effects, mirrored from the web app: distortion (hard clip, equal-power mix, tone), a tempo-synced delay
// and a small reverb that comes after the delay. All memory is allocated in prepare(), never on the audio thread.
namespace tb
{
constexpr int kNumDly = 6;
inline float dlyBeats (int i) { static const float b[kNumDly] = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f }; return b[std::clamp (i, 0, kNumDly - 1)]; }
inline const char* dlyName (int i) { static const char* n[kNumDly] = { "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2" }; return n[std::clamp (i, 0, kNumDly - 1)]; }

// hard-clip distortion: drive (0..1) -> 30x .. 1900x into the clipper, then a low-pass whose cutoff follows COLOR
class DistFx
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; lp = 0.f; setColor (0.5f); }
    void setDrive (float d) { gain = 30.f * std::pow (64.f, std::clamp (d, 0.f, 1.f)); }
    void setColor (float c)
    {
        const float fc = std::min (500.f * std::pow (24.f, std::clamp (c, 0.f, 1.f)), 0.45f * sr);
        a = 1.f - std::exp (-6.2831853f * fc / sr);
    }
    float process (float x)
    {
        const float y = std::clamp (x * gain, -1.f, 1.f) * 0.9f;
        lp += a * (y - lp);  lp2 += a * (lp - lp2);                      // two poles ~ the web's 12 dB low-pass
        return lp2;
    }
private:
    float sr = 44100.f, gain = 240.f, a = 0.2f, lp = 0.f, lp2 = 0.f;
};

// mono delay: feedback .45 through a 3.5 kHz low-pass, delay time in samples (fractional, interpolated)
class DelayFx
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        buf.assign ((size_t) (4.0 * sampleRate) + 8, 0.f);  w = 0;  lp = 0.f;
        a = 1.f - std::exp (-6.2831853f * 3500.f / sr);
        timeSamples = 0.375f * sr;
    }
    void setTimeSamples (float s) { timeSamples = std::clamp (s, 16.f, (float) buf.size() - 4.f); }
    float process (float in)
    {
        float rd = (float) w - timeSamples;  if (rd < 0.f) rd += (float) buf.size();
        const int i0 = (int) rd, i1 = (i0 + 1) % (int) buf.size();
        const float fr = rd - (float) i0, d = buf[(size_t) i0] * (1.f - fr) + buf[(size_t) i1] * fr;
        lp += a * (d - lp);
        buf[(size_t) w] = in + lp * 0.45f;
        if (++w >= (int) buf.size()) w = 0;
        return lp;
    }
private:
    std::vector<float> buf;  int w = 0;  float sr = 44100.f, timeSamples = 1000.f, a = 0.3f, lp = 0.f;
};

// Schroeder reverb (4 combs + 2 all-passes), tail ~2.4 s; returns the wet signal
class ReverbFx
{
public:
    void prepare (double sampleRate)
    {
        const double sr = sampleRate;
        const int cl[4] = { 1557, 1617, 1491, 1422 }, al[2] = { 225, 556 };
        for (int i = 0; i < 4; ++i)
        {
            comb[i].len = std::max (2, (int) (cl[i] * sr / 44100.0));  comb[i].buf.assign ((size_t) comb[i].len + 8, 0.f);
            comb[i].pos = 0;  comb[i].lp = 0.f;
            comb[i].g = std::pow (10.f, -3.f * (float) comb[i].len / ((float) sr * 2.4f));
        }
        for (int i = 0; i < 2; ++i) { ap[i].len = std::max (2, (int) (al[i] * sr / 44100.0)); ap[i].buf.assign ((size_t) ap[i].len + 8, 0.f); ap[i].pos = 0; }
    }
    float process (float in)
    {
        float r = 0.f;
        for (auto& c : comb)
        {
            const float y = c.buf[(size_t) c.pos];
            c.lp += 0.35f * (y - c.lp);
            c.buf[(size_t) c.pos] = in * 0.25f + c.lp * c.g;
            if (++c.pos >= c.len) c.pos = 0;
            r += y;
        }
        for (auto& a : ap)
        {
            const float y = a.buf[(size_t) a.pos], x = r;
            a.buf[(size_t) a.pos] = x + y * 0.5f;
            r = y - x * 0.5f;
            if (++a.pos >= a.len) a.pos = 0;
        }
        return r;
    }
private:
    struct Comb { std::vector<float> buf; int len = 2, pos = 0; float g = 0.8f, lp = 0.f; };
    struct Ap   { std::vector<float> buf; int len = 2, pos = 0; };
    Comb comb[4];  Ap ap[2];
};

// the mixer's logic, shared with the web app: mute / on-off + solo decide what is heard
struct MixGates
{
    bool dirGate = true, sendGate = true;
    float delSend = 0.f, revSend = 0.f, revOut = 0.f, distDry = 1.f, distWet = 0.f;
    bool audKick = true, audHat = true;
};
struct MixIn
{
    bool muteBass, muteKick, muteHat, delOn, revOn, distOn;
    bool soloBass, soloKick, soloHat, soloDel, soloRev, soloDist;
    float delSend, revSend, revAmt, distLevel;
};
inline MixGates mixGates (const MixIn& m)
{
    MixGates g;
    const bool any = m.soloBass || m.soloKick || m.soloHat || m.soloDel || m.soloRev || m.soloDist;
    const bool onlyDist = m.soloDist && ! m.soloBass;
    const float lv = m.distOn ? m.distLevel : 0.f;
    g.distWet = std::sin (lv * 1.5707963f) * 0.55f;                       // equal-power crossfade, loudness matched
    g.distDry = onlyDist ? 0.f : std::cos (lv * 1.5707963f);
    g.dirGate  = (! m.muteBass && (! any || m.soloBass)) || (onlyDist && m.distOn);
    g.sendGate = (! m.muteBass && (! any || m.soloBass || m.soloDist)) || m.soloDel || m.soloRev;
    const bool audDel = m.delOn && (! any || m.soloDel), audRev = m.revOn && (! any || m.soloRev);
    g.delSend = audDel ? m.delSend : 0.f;
    g.revSend = audRev ? m.revSend : 0.f;
    g.revOut  = audRev ? m.revAmt * 1.2f : 0.f;
    g.audKick = ! m.muteKick && (! any || m.soloKick);
    g.audHat  = ! m.muteHat && (! any || m.soloHat);
    return g;
}
} // namespace tb
