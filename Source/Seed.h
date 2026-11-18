#pragma once
// Pattern types + the "dice" model. Std-only (no JUCE) so it can be unit-tested anywhere.
//
// GENERATE rolls a Seed once: random numbers per step. The line you hear is then a pure
// function  resolve (seed, fader values, root, scale)  - so moving a fader re-reads the same
// dice and notes appear/disappear smoothly instead of the line re-rolling, and undo is just
// "put the faders back and resolve again". The seed holds enough dice for 64 bars, so the BARS
// fader can grow or shrink the line while the first bars stay put.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tb
{
constexpr int    kStepsPerBar = 16, kMinBars = 4, kMaxBars = 64, kDefaultBars = 16;
constexpr int    kMaxSteps = kStepsPerBar * kMaxBars;    // 1024
constexpr int    kPhrases = kMaxBars / 4;                // one phrase per four bars
constexpr double kStepBeats = 0.25;                      // one 16th
constexpr double kGateBeats = kStepBeats * 0.55;
constexpr double kOverlapBeats = 12.0 / 480.0;           // slide: held past the next note-on
constexpr int    kVelNormal = 80, kVelAccent = 124;

struct Step { std::uint8_t pitch = 0; bool active = false, accent = false, slide = false; };

// a line of 1..64 bars; only the first `len` steps are used
struct Pattern
{
    std::array<Step, kMaxSteps> s {};
    int len = kStepsPerBar * kDefaultBars;

    Step&       operator[] (size_t i)       { return s[i]; }
    const Step& operator[] (size_t i) const { return s[i]; }
    int bars() const { return std::max (1, len / kStepsPerBar); }
    const Step& at (long long k) const { return s[(size_t) (((k % len) + len) % len)]; }   // wraps
};

struct GenParams { float notes, accents, slides, octave, variation; int bars; };

inline const std::vector<std::vector<int>>& scales()
{
    static const std::vector<std::vector<int>> s {
        { 0, 3, 5, 7, 10 },            // minor pentatonic
        { 0, 1, 3, 5, 7, 8, 10 },      // phrygian
        { 0, 2, 3, 5, 7, 8, 10 },      // natural minor
        { 0, 1, 4, 5, 7, 8, 10 },      // phrygian dominant
        { 0, 2, 3, 5, 7, 8, 11 } };    // harmonic minor
    return s;
}

// five dice per step, all in [0,1)
struct Roll { float degree, note, accent, slide, octave; };

struct Seed
{
    std::array<std::array<Roll, kStepsPerBar>, kPhrases> base {};   // up to sixteen different phrases (one bar each)
    std::array<Roll, kMaxSteps> alt {};                             // replacement step used by "varied" bars
    std::array<float, kMaxSteps> varRoll {};                        // which steps get replaced
    std::array<float, kPhrases> phraseRoll {};                      // when a new phrase starts
};
static_assert (sizeof (Seed) == sizeof (float) * (kPhrases * kStepsPerBar * 5 + kMaxSteps * 5 + kMaxSteps + kPhrases),
               "Seed must be tightly packed");

template <class Rng>   // Rng needs: float nextFloat()  in [0,1)
Seed makeSeed (Rng& r)
{
    auto roll = [&r] { Roll x; x.degree = r.nextFloat(); x.note = r.nextFloat(); x.accent = r.nextFloat();
                       x.slide = r.nextFloat(); x.octave = r.nextFloat(); return x; };
    Seed s;
    for (auto& bar : s.base) for (auto& x : bar) x = roll();
    for (auto& x : s.alt) x = roll();
    for (auto& x : s.varRoll) x = r.nextFloat();
    for (auto& x : s.phraseRoll) x = r.nextFloat();
    return s;
}

inline Step stepFrom (const Roll& r, const GenParams& P, int stepInBar, bool forceOn, int root, const std::vector<int>& scale)
{
    float p = P.notes;
    if (stepInBar % 4 == 0 && p > 0.f) p = std::min (1.f, p + 0.15f);       // downbeats a bit denser

    Step s;
    if (r.note >= p && ! (forceOn && P.notes > 0.05f))
        return s;

    const int n = (int) scale.size();
    const int total = 5 + 2 * (n - 1);                                       // favour the root
    const int k = std::min (total - 1, (int) (r.degree * (float) total));
    const int idx = k < 5 ? 0 : 1 + (k - 5) / 2;

    s.active = true;
    s.pitch  = (std::uint8_t) std::clamp (root + scale[(size_t) idx] + (r.octave < P.octave ? 12 : 0), 0, 127);
    s.accent = r.accent < P.accents;
    s.slide  = r.slide < P.slides;
    return s;
}

inline Pattern resolve (const Seed& sd, const GenParams& P, int root, const std::vector<int>& scale)
{
    Pattern out;
    const int nb = std::clamp (P.bars, kMinBars, kMaxBars);
    out.len = nb * kStepsPerBar;
    int baseIdx = 0;
    for (int bar = 0; bar < nb; ++bar)
    {
        const int g = bar / 4;
        if (bar % 4 == 0 && g > 0 && sd.phraseRoll[(size_t) g] < P.variation * 0.6f)
            baseIdx = g;                                                     // fresh phrase

        for (int i = 0; i < kStepsPerBar; ++i)
        {
            const size_t k = (size_t) (bar * kStepsPerBar + i);
            const bool varied = bar % 4 != 0 && sd.varRoll[k] < P.variation * 0.5f;
            const Roll& r = varied ? sd.alt[k] : sd.base[(size_t) baseIdx][(size_t) i];
            out[k] = stepFrom (r, P, i, i == 0 && ! varied, root, scale);    // bar line always has a note
        }
    }
    return out;
}
} // namespace tb
