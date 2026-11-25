#pragma once
// STYLE: learn the taste of a folder of .mid files (std-only). Same maths as the web app.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "MidiImport.h"

namespace tb
{
struct StyleResult { int n = 0, skipped = 0; Style style; };

inline float clampF (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }

// files: already imported (notes -> 16th steps, root + scale guessed). Files with fewer than 4 notes are skipped.
inline StyleResult analyzeStyle (const std::vector<Imported>& files, int alreadySkipped = 0)
{
    StyleResult res;
    res.skipped = alreadySkipped;
    std::array<double, 16> on {}, ac {};
    std::array<double, 12> pc {};
    std::map<int, int> votes;
    int n = 0, bpmN = 0, notesTotal = 0;
    double dens = 0, acc = 0, sli = 0, oct = 0, bpmSum = 0;

    for (const auto& f : files)
    {
        if (! f.ok) { ++res.skipped; continue; }
        const int steps = f.pat.len, rootPc = f.root % 12;
        const double bars = std::max (1, steps / kStepsPerBar);
        std::array<int, 16> o {}, a {};
        std::array<int, 12> h {};
        int cnt = 0, ax = 0, sx = 0, up = 0, lo = 127;
        for (int i = 0; i < steps; ++i)
        {
            const Step& s = f.pat[(size_t) i];
            if (! s.active) continue;
            ++cnt; ++o[(size_t) (i % 16)];
            if (s.accent) { ++ax; ++a[(size_t) (i % 16)]; }
            if (s.slide) ++sx;
            ++h[(size_t) ((((int) s.pitch - rootPc) % 12 + 12) % 12)];
            lo = std::min (lo, (int) s.pitch);
        }
        if (cnt < 4) { ++res.skipped; continue; }
        for (int i = 0; i < steps; ++i) { const Step& s = f.pat[(size_t) i]; if (s.active && (int) s.pitch - lo >= 9) ++up; }
        for (size_t i = 0; i < 16; ++i) { on[i] += o[i] / bars; ac[i] += (double) a[i] / std::max (1, o[i]); }
        for (size_t i = 0; i < 12; ++i) pc[i] += (double) h[i] / cnt;
        ++votes[f.scale];
        dens += (double) cnt / steps; acc += (double) ax / cnt; sli += (double) sx / cnt; oct += (double) up / cnt; notesTotal += cnt;
        if (f.bpm >= 60.0 && f.bpm <= 200.0) { bpmSum += f.bpm; ++bpmN; }
        ++n;
    }
    res.n = n;
    if (n == 0) return res;

    Style& S = res.style;
    double pm = 0, am = 0;
    for (size_t i = 0; i < 16; ++i) { pm += on[i] / n; am += ac[i] / n; }
    pm = pm / 16.0; am = am / 16.0;
    if (pm <= 0) pm = 1; if (am <= 0) am = 1;
    for (size_t i = 0; i < 16; ++i)
    {
        S.rel[i]  = clampF ((float) (on[i] / n / pm), 0.15f, 2.5f);
        S.arel[i] = clampF ((float) (ac[i] / n / am), 0.2f, 3.f);
    }
    for (size_t i = 0; i < 12; ++i) S.pc[i] = (float) (pc[i] / n);
    std::vector<std::pair<int, int>> v (votes.begin(), votes.end());
    std::stable_sort (v.begin(), v.end(), [] (const auto& x, const auto& y) { return x.second > y.second; });
    S.nScales = (int) std::min<size_t> (4, v.size());
    for (int i = 0; i < S.nScales; ++i) S.scales[(size_t) i] = { v[(size_t) i].first, (float) v[(size_t) i].second };
    S.files = n; S.notesTotal = notesTotal; S.bpm = bpmN ? (int) std::lround (bpmSum / bpmN) : 0;
    S.notes    = clampF ((float) (dens / n), 0.1f, 0.9f);
    S.accents  = clampF ((float) (acc / n), 0.05f, 0.95f);
    S.slides   = clampF ((float) (sli / n), 0.02f, 0.95f);
    S.octave   = clampF ((float) (oct / n), 0.02f, 0.95f);
    return res;
}

// a scale chosen the way the files lean (index into scales()); r in [0,1)
inline int styleScale (const Style& S, float r)
{
    float tot = 0.f;
    for (int i = 0; i < S.nScales; ++i) tot += S.scales[(size_t) i].w;
    float t = r * tot;
    for (int i = 0; i < S.nScales; ++i) { if (t < S.scales[(size_t) i].w) return S.scales[(size_t) i].k; t -= S.scales[(size_t) i].w; }
    return S.nScales ? S.scales[0].k : 0;
}

// plain text, comma separated: files, notesTotal, bpm, 4 fader values, nScales, (scale, weight) x4, rel x16, arel x16, pc x12
inline std::string styleToText (const Style& S)
{
    std::string t;
    char b[48];
    auto add = [&] (double v) { std::snprintf (b, sizeof (b), "%.6g,", v); t += b; };
    add (S.files); add (S.notesTotal); add (S.bpm); add (S.notes); add (S.accents); add (S.slides); add (S.octave); add (S.nScales);
    for (const auto& s : S.scales) { add (s.k); add (s.w); }
    for (float v : S.rel) add (v);
    for (float v : S.arel) add (v);
    for (float v : S.pc) add (v);
    return t;
}
inline bool styleFromText (const std::string& text, Style& S)
{
    std::vector<double> v;
    const char* p = text.c_str();
    while (*p)
    {
        char* e = nullptr;
        const double d = std::strtod (p, &e);
        if (e == p) { ++p; continue; }
        v.push_back (d);
        p = e;
    }
    if (v.size() < 8 + 8 + 16 + 16 + 12) return false;
    size_t i = 0;
    Style o;
    o.files = (int) v[i++]; o.notesTotal = (int) v[i++]; o.bpm = (int) v[i++];
    o.notes = (float) v[i++]; o.accents = (float) v[i++]; o.slides = (float) v[i++]; o.octave = (float) v[i++];
    o.nScales = std::clamp ((int) v[i++], 0, 4);
    for (auto& s : o.scales) { s.k = std::clamp ((int) v[i++], 0, (int) scales().size() - 1); s.w = (float) v[i++]; }
    for (auto& x : o.rel) x = clampF ((float) v[i++], 0.05f, 4.f);
    for (auto& x : o.arel) x = clampF ((float) v[i++], 0.05f, 4.f);
    for (auto& x : o.pc) x = clampF ((float) v[i++], 0.f, 1.f);
    if (o.nScales == 0) return false;
    S = o;
    return true;
}
} // namespace tb
