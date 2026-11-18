#pragma once
// Four tempo-synced LFOs, 14 destinations. Std-only (testable anywhere).
// An LFO's output is  value(-1..1) * depth(-1..1) * 0.5  and is added to the destination's normalised
// position (0..1), so full depth swings +-50% of the range, and a negative depth inverts it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tb
{
enum Dest { dCutoff, dReso, dEnvMod, dDecay, dVolume, dKick, dHat, dPitch, dMaster,
            dNotes, dAccents, dSlides, dOctave, dVariation, kDests };

struct DestDef { const char* id; const char* label; float lo, hi; bool expo; };

inline const DestDef& destDef (int d)
{
    static const DestDef defs[kDests] = {
        { "cutoff",  "CUTOFF",  40.f,   8000.f, true  }, { "reso",    "RESO",    0.f, 1.f, false },
        { "envmod",  "ENV MOD", 0.f,    1.f,    false }, { "decay",   "DECAY",   0.05f, 1.5f, false },
        { "volume",  "VOLUME",  0.f,    1.f,    false }, { "kickvol", "KICK",    0.f, 1.f, false },
        { "hatvol",  "HAT",     0.f,    1.f,    false }, { "pitch",   "PITCH",   -100.f, 100.f, false },
        { "master",  "MASTER",  0.f,    1.f,    false }, { "notes",   "NOTES",   0.f, 1.f, false },
        { "accents", "ACCENTS", 0.f,    1.f,    false }, { "slides",  "SLIDES",  0.f, 1.f, false },
        { "octave",  "OCTAVE",  0.f,    1.f,    false }, { "variation", "VARIAT.", 0.f, 1.f, false } };
    return defs[d];
}

constexpr float kPitchBase = 0.f, kMasterBase = 0.85f;              // no control of their own
constexpr std::uint32_t kGenMask = (1u << dNotes) | (1u << dAccents) | (1u << dSlides) | (1u << dOctave) | (1u << dVariation);

inline float toNorm (int d, float v)
{
    const auto& f = destDef (d);
    const float n = f.expo ? std::log (v / f.lo) / std::log (f.hi / f.lo) : (v - f.lo) / (f.hi - f.lo);
    return std::clamp (n, 0.f, 1.f);
}
inline float toVal (int d, float n)
{
    const auto& f = destDef (d);
    n = std::clamp (n, 0.f, 1.f);
    return f.expo ? f.lo * std::pow (f.hi / f.lo, n) : f.lo + (f.hi - f.lo) * n;
}

// one cycle = this many beats: slow -> fast, straight and triplet
constexpr int kNumDivs = 12;
inline double divBeats (int i)
{
    static const double d[kNumDivs] = { 32, 16, 8, 4, 2, 1, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return d[std::clamp (i, 0, kNumDivs - 1)];
}
inline const char* divName (int i)
{
    static const char* n[kNumDivs] = { "8 bar", "4 bar", "2 bar", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n[std::clamp (i, 0, kNumDivs - 1)];
}

enum Shape { shSine, shTri, shSaw, shSquare, shSH, kShapes };

inline float shapeAt (int shape, double phase, float held)
{
    const float p = (float) phase;
    switch (shape)
    {
        case shSine:   return std::sin (p * 6.2831853f);
        case shTri:    return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
        case shSaw:    return 2.f * p - 1.f;
        case shSquare: return p < 0.5f ? 1.f : -1.f;
        default:       return held;
    }
}

// sample & hold: a new random value each cycle, but a pure function of (cycle, lfo) so it is repeatable
inline float heldValue (long long cycle, int lfo)
{
    std::uint32_t x = (std::uint32_t) (std::int64_t) cycle * 2654435761u ^ (std::uint32_t) (lfo + 1) * 40503u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (float) (x & 0xFFFFFFu) / 8388608.f - 1.f;
}

struct LfoSettings { int shape = shSine; int rate = 2; float depth = 0.35f; std::uint32_t mask = 0; };

struct ModResult
{
    std::array<float, kDests> eff {};                               // modulated values (real units)
    std::array<float, 4> val {}, phase {};                          // raw LFO outputs and phases
    std::array<std::array<float, kDests>, 4> contrib {};            // normalised offset each LFO adds to each destination
};

// base: base values in real units (kDests of them)
inline void evalMod (const float* base, const LfoSettings* lfo, double beat, ModResult& out)
{
    std::array<float, kDests> acc {};
    std::array<bool, kDests> mapped {};
    for (int l = 0; l < 4; ++l)
    {
        const double cyc = beat / divBeats (lfo[l].rate);
        const double fl = std::floor (cyc);
        const double ph = cyc - fl;
        const float v = shapeAt (lfo[l].shape, ph, heldValue ((long long) fl, l));
        out.val[(size_t) l] = v;
        out.phase[(size_t) l] = (float) ph;
        for (int d = 0; d < kDests; ++d)
        {
            const bool on = (lfo[l].mask >> d) & 1u;
            const float c = on ? v * lfo[l].depth * 0.5f : 0.f;
            out.contrib[(size_t) l][(size_t) d] = c;
            if (on) { acc[(size_t) d] += c; mapped[(size_t) d] = true; }
        }
    }
    for (int d = 0; d < kDests; ++d)
        out.eff[(size_t) d] = mapped[(size_t) d] ? toVal (d, toNorm (d, base[d]) + acc[(size_t) d]) : base[d];
}

inline bool genModulated (const LfoSettings* lfo)
{
    for (int l = 0; l < 4; ++l)
        if (std::abs (lfo[l].depth) > 0.001f && (lfo[l].mask & kGenMask)) return true;
    return false;
}
} // namespace tb
