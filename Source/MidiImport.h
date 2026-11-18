#pragma once
// .mid -> one-line pattern. Std-only so it can be tested anywhere.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "Seed.h"

namespace tb
{
struct RawNote { double start, end; int pitch, velocity, channel; };      // times in ticks, channel 0-based

struct Imported
{
    Pattern pat;
    int root = 36, scale = 0, bars = 0, count = 0;
    double bpm = 0.0;
    bool ok = false;
    std::string error;
};

inline Imported notesToPattern (std::vector<RawNote> all, double division, double bpm)
{
    Imported r;
    std::vector<RawNote> ns;
    for (auto& n : all) if (n.channel != 9) ns.push_back (n);            // skip the drum channel when there is anything else
    if (ns.empty()) ns = all;
    if (ns.empty()) { r.error = "No notes found in this file"; return r; }

    const double stepT = division / 4.0;                                  // one 16th
    int vmin = 127, vmax = 0;
    for (auto& n : ns) { vmin = std::min (vmin, n.velocity); vmax = std::max (vmax, n.velocity); }
    auto accent = [&] (int v) { return vmax - vmin >= 16 ? (double) v >= (vmin + vmax) / 2.0 : v >= 110; };

    struct Slot { int p, v; double len; };
    std::map<int, Slot> slots;                                            // monophonic: lowest note wins a shared step
    for (auto& n : ns)
    {
        const int i = std::max (0, (int) std::lround (n.start / stepT));
        if (i >= kMaxSteps) continue;
        auto it = slots.find (i);
        if (it == slots.end() || n.pitch < it->second.p)
            slots[i] = { n.pitch, n.velocity, (n.end - n.start) / stepT };
    }
    if (slots.empty()) { r.error = "No notes found in this file"; return r; }

    const int last = slots.rbegin()->first;
    r.bars = std::clamp ((last + 1 + kStepsPerBar - 1) / kStepsPerBar, 1, kMaxBars);
    r.pat.len = r.bars * kStepsPerBar;

    std::vector<double> lens ((size_t) r.pat.len, 0.0);
    for (auto& kv : slots)
        if (kv.first < r.pat.len)
        {
            Step s; s.active = true; s.pitch = (std::uint8_t) std::clamp (kv.second.p, 0, 127); s.accent = accent (kv.second.v);
            r.pat[(size_t) kv.first] = s;
            lens[(size_t) kv.first] = kv.second.len;
        }
    for (int i = 0; i < r.pat.len; ++i)                                   // a note held into the very next step = slide
        if (r.pat[(size_t) i].active)
            r.pat[(size_t) i].slide = lens[(size_t) i] >= 0.95 && i + 1 < r.pat.len && r.pat[(size_t) (i + 1)].active;

    int hist[12] = {};                                                    // guess root + scale from the notes
    for (int i = 0; i < r.pat.len; ++i) if (r.pat[(size_t) i].active) ++hist[r.pat[(size_t) i].pitch % 12];
    int rootPc = 0;
    for (int k = 1; k < 12; ++k) if (hist[k] > hist[rootPc]) rootPc = k;
    int best = 0, bestScore = -1;
    for (int k = 0; k < (int) scales().size(); ++k)
    {
        bool in[12] = {};
        for (int x : scales()[(size_t) k]) in[(rootPc + x) % 12] = true;
        int score = 0;
        for (int i = 0; i < r.pat.len; ++i) if (r.pat[(size_t) i].active && in[r.pat[(size_t) i].pitch % 12]) ++score;
        if (score > bestScore) { bestScore = score; best = k; }
    }
    r.root = 36 + rootPc;
    r.scale = best;
    r.bpm = bpm;
    r.count = (int) slots.size();
    r.ok = true;
    return r;
}
} // namespace tb
