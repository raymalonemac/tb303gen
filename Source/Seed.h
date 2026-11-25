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

// What STYLE learns from a folder of .mid files (see Style.h). With a style on, GENERATE keeps the usual dice but
// reads them through these numbers: rhythm per 16th step, accent habit per step, favourite scale degrees (counted
// from each file's own root, so keys do not matter) and the scales the files lean to.
struct Style
{
    struct SW { int k = 0; float w = 0.f; };
    std::array<float, 16> rel {}, arel {};
    std::array<float, 12> pc {};
    std::array<SW, 4> scales {};
    int nScales = 0, files = 0, notesTotal = 0, bpm = 0;
    float notes = 0.5f, accents = 0.4f, slides = 0.3f, octave = 0.2f;     // the fader positions that match the files
};

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

// scale slot 5 = the notes picked on the KEYS keyboard: intervals from the root as a 12-bit mask (bit 0 = the root)
constexpr int kCustomScale = 5;
inline const std::vector<int>& scaleVec (int scale, unsigned cmask, std::vector<int>& buf)
{
    if (scale >= 0 && scale < (int) scales().size()) return scales()[(size_t) scale];
    buf.clear();
    for (int i = 0; i < 12; ++i) if ((cmask >> i) & 1u) buf.push_back (i);
    if (buf.empty()) buf.push_back (0);
    if (buf.front() != 0) buf.insert (buf.begin(), 0);
    return buf;
}

// five dice per step, all in [0,1)
struct Roll { float degree, note, accent, slide, octave; };

struct Seed
{
    std::array<std::array<Roll, kStepsPerBar>, kPhrases> base {};   // up to sixteen different phrases (one bar each)
    std::array<Roll, kMaxSteps> alt {};                             // replacement step used by "varied" bars
    std::array<float, kMaxSteps> varRoll {};                        // which steps get replaced
    std::array<float, kPhrases> phraseRoll {};                      // when a new phrase starts
    // only used by seeds built from a loaded .mid (zero = not used, so an old / generated seed is unaffected)
    std::array<std::int16_t, kMaxSteps> pin {};                     // a note no scale degree can express: offset from the root + kPinBias
    std::int32_t noForce = 0;                                       // 1 = do not force a note on the bar line
};
constexpr int    kPinBias = 200;
constexpr size_t kSeedBytesV1 = sizeof (float) * (kPhrases * kStepsPerBar * 5 + kMaxSteps * 5 + kMaxSteps + kPhrases);   // saved projects from before pin/noForce
static_assert (sizeof (Seed) == kSeedBytesV1 + sizeof (std::int16_t) * kMaxSteps + sizeof (std::int32_t),
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
    s.pin.fill (0);
    s.noForce = 0;
    return s;
}

inline Step stepFrom (const Roll& r, const GenParams& P, int stepInBar, bool forceOn, int root, const std::vector<int>& scale,
                      const Style* S = nullptr)
{
    float p = P.notes;
    if (S != nullptr) p = std::min (1.f, p * S->rel[(size_t) (stepInBar % 16)]);           // the rhythm of the analysed files
    else if (stepInBar % 4 == 0 && p > 0.f) p = std::min (1.f, p + 0.15f);                 // downbeats a bit denser

    Step s;
    if (r.note >= p && ! (forceOn && P.notes > 0.05f))
        return s;

    const int n = (int) scale.size();
    int idx;
    if (S != nullptr)                                                        // degrees weighted by how often the files use them
    {
        float w[16] = {}, tot = 0.f;
        for (int d = 0; d < n && d < 16; ++d) { w[d] = S->pc[(size_t) (((scale[(size_t) d] % 12) + 12) % 12)] + 0.02f; tot += w[d]; }
        float t = r.degree * tot;
        idx = 0;
        while (idx < n - 1 && t >= w[idx]) { t -= w[idx]; ++idx; }
    }
    else
    {
        const int total = 5 + 2 * (n - 1);                                   // favour the root
        const int k = std::min (total - 1, (int) (r.degree * (float) total));
        idx = k < 5 ? 0 : 1 + (k - 5) / 2;
    }

    s.active = true;
    s.pitch  = (std::uint8_t) std::clamp (root + scale[(size_t) idx] + (r.octave < P.octave ? 12 : 0), 0, 127);
    s.accent = r.accent < (S != nullptr ? std::min (1.f, P.accents * S->arel[(size_t) (stepInBar % 16)]) : P.accents);
    s.slide  = r.slide < P.slides;
    return s;
}

inline Pattern resolve (const Seed& sd, const GenParams& P, int root, const std::vector<int>& scale, const Style* S = nullptr)
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
            out[k] = stepFrom (r, P, i, i == 0 && ! varied && ! sd.noForce, root, scale, S);    // bar line always has a note
            if (out[k].active && sd.pin[k] != 0)                             // a loaded note that no scale degree can express
                out[k].pitch = (std::uint8_t) std::clamp (root + (int) sd.pin[k] - kPinBias, 0, 127);
        }
    }
    return out;
}

// A loaded .mid becomes a seed too, so it is editable straight away: the dice are chosen so that, at the fader
// values returned in `gen`, resolve() gives back the file exactly (repeated up to a multiple of 4 bars, because
// the Bars fader steps by 4). Moving Notes / Accents / Slides / Octave then adds or removes notes of THAT line,
// and Variation (returned as 5% = every bar as loaded) folds the bars back towards each group's first bar.
// The dice sit just under the thresholds of Variation = 5% (kLoadVariation), so at 5% every bar is as loaded, and
// pulling Variation down towards 1% folds the bars back towards each group's first bar.
// Margins of 0.01 around every threshold keep it exact even after the host rounds the parameter values.
constexpr float kVariationMin = 0.01f, kVariationMax = 0.40f, kLoadVariation = 0.05f;   // Variation fader range, and its value after a .mid load
struct SeededPattern { Seed seed; GenParams gen {}; };

template <class Rng>
SeededPattern seedFromPattern (const Pattern& pat, int root, const std::vector<int>& sc, Rng& rng)
{
    const int srcLen = std::max (1, pat.len);
    const int bars = std::clamp (((std::max (1, srcLen / kStepsPerBar) + 3) / 4) * 4, kMinBars, kMaxBars);
    const int total = bars * kStepsPerBar;
    auto T = [&] (int k) -> const Step& { return pat.s[(size_t) (k % srcLen)]; };

    struct Loc { int idx = -1, up = 0; };
    auto locate = [&] (int q) { Loc l; for (int u = 0; u < 2 && l.idx < 0; ++u)
                                    for (size_t d = 0; d < sc.size(); ++d) if (sc[d] + 12 * u == q) { l.idx = (int) d; l.up = u; break; }
                                return l; };

    int on = 0, acc = 0, sl = 0, up = 0;
    for (int k = 0; k < total; ++k)
    {
        const Step& t = T (k);
        if (! t.active) continue;
        ++on; acc += t.accent; sl += t.slide;
        up += locate ((int) t.pitch - root).up;
    }
    auto q01 = [] (float v, float lo, float hi) { return std::round (std::clamp (v, lo, hi) * 100.f) / 100.f; };
    const float qn = q01 ((float) on / (float) total, 0.05f, 0.80f), qa = q01 ((float) acc / (float) std::max (1, on), 0.05f, 0.95f),
                qs = q01 ((float) sl / (float) std::max (1, on), 0.05f, 0.95f), qo = q01 ((float) up / (float) std::max (1, on), 0.05f, 0.95f);

    const float m = 0.01f;
    auto pick = [&] (bool yes, float q) { const float r = rng.nextFloat(); return yes ? r * (q - m) : (q + m) + r * (1.f - q - m); };
    const int degrees = 5 + 2 * ((int) sc.size() - 1);
    auto rollFor = [&] (const Step& t, int i)
    {
        float pe = qn; if (i % 4 == 0 && pe > 0.f) pe = std::min (1.f, pe + 0.15f);     // same downbeat boost as stepFrom
        Roll r { rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat() };
        r.note = pick (t.active, pe);
        if (t.active)
        {
            r.accent = pick (t.accent, qa);
            r.slide  = pick (t.slide, qs);
            const Loc l = locate ((int) t.pitch - root);
            if (l.idx >= 0)
            {
                const int k = l.idx == 0 ? std::min (4, (int) (rng.nextFloat() * 5.f)) : 5 + 2 * (l.idx - 1) + (rng.nextFloat() < 0.5f ? 0 : 1);
                r.degree = ((float) k + 0.1f + 0.8f * rng.nextFloat()) / (float) degrees;
                r.octave = pick (l.up == 1, qo);
            }
        }
        return r;
    };

    SeededPattern out;
    out.seed = makeSeed (rng);
    out.seed.noForce = 1;
    for (int g = 0; g < bars / 4; ++g)
    {
        for (int i = 0; i < kStepsPerBar; ++i) out.seed.base[(size_t) g][(size_t) i] = rollFor (T (g * 4 * kStepsPerBar + i), i);
        out.seed.phraseRoll[(size_t) g] = rng.nextFloat() * 0.025f;
    }
    for (int k = 0; k < total; ++k)
    {
        const Step& t = T (k);
        out.seed.alt[(size_t) k] = rollFor (t, k % kStepsPerBar);
        out.seed.varRoll[(size_t) k] = rng.nextFloat() * 0.020f;
        if (t.active && locate ((int) t.pitch - root).idx < 0)
            out.seed.pin[(size_t) k] = (std::int16_t) ((int) t.pitch - root + kPinBias);
    }
    out.gen = { qn, qa, qs, qo, kLoadVariation, bars };
    return out;
}
} // namespace tb
