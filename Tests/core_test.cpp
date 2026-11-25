#include "../Source/Seed.h"
#include "../Source/MidiImport.h"
#include "../Source/Modulation.h"
#include "../Source/Drums.h"
#include "../Source/Fx.h"
#include "../Source/Style.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <cassert>
struct R { std::mt19937 g{5}; float nextFloat(){ return std::uniform_real_distribution<float>(0,1)(g);} };
int main(){
  R r; auto* sd = new tb::Seed(tb::makeSeed(r)); printf("seed %zu bytes\n", sizeof(*sd));
  for (int bars : {4,16,40,64}) {
    tb::GenParams P{.75f,.3f,.25f,.14f,.12f,bars};
    auto p = tb::resolve(*sd,P,38,tb::scales()[4]);
    int n=0,a=0,s=0; for(int i=0;i<p.len;i++) if(p[i].active){n++; a+=p[i].accent; s+=p[i].slide;}
    printf("bars %d len %d notes %d acc %d sl %d\n",bars,p.len,n,a,s);
  }
  // prefix stability: 16-bar first 4 bars equal 4-bar
  tb::GenParams A{.75f,.3f,.25f,.14f,.12f,4}, B{.75f,.3f,.25f,.14f,.12f,16};
  auto pa=tb::resolve(*sd,A,38,tb::scales()[4]), pb=tb::resolve(*sd,B,38,tb::scales()[4]);
  bool same=true; for(int i=0;i<64;i++) if(pa[i].active!=pb[i].active||pa[i].pitch!=pb[i].pitch) same=false; printf("prefix same %d\n",same);
  printf("Pattern size %zu\n", sizeof(tb::Pattern));
  // midi import
  std::vector<tb::RawNote> ns; for(int i=0;i<32;i++) ns.push_back({i*120.0,i*120.0+(i%4==0?130.0:60.0),38+(i%3)*7,(i%4==0)?120:80,0});
  ns.push_back({0,10,36,100,9});
  auto im=tb::notesToPattern(ns,480,128); printf("import ok %d bars %d count %d root %d scale %d bpm %.0f\n",im.ok,im.bars,im.count,im.root,im.scale,im.bpm);
  int sl=0,ac=0; for(int i=0;i<im.pat.len;i++){sl+=im.pat[i].slide;ac+=im.pat[i].accent;} printf("slides %d acc %d\n",sl,ac);
  // a loaded .mid is editable: seedFromPattern + resolve must give the file back exactly, even with the fader values nudged
  {
    R rr; int bad = 0, trials = 0;
    for (int t = 0; t < 400; ++t) {
      const int srcBars = 1 + t % 9, root = 36 + t % 12; const auto& sc = tb::scales()[(size_t)(t % 5)];
      tb::Pattern src; src.len = srcBars * 16;
      for (int i = 0; i < src.len; ++i) {
        tb::Step st; st.active = rr.nextFloat() < 0.35f + 0.5f * (t % 3 == 0);
        if (st.active) { st.pitch = (std::uint8_t)(24 + (int)(rr.nextFloat() * 60)); if (t % 2) st.pitch = (std::uint8_t)(root + sc[(size_t)(rr.nextFloat()*sc.size())] + (rr.nextFloat()<.3f?12:0));
                         st.accent = rr.nextFloat() < .4f; st.slide = rr.nextFloat() < .3f; }
        src[(size_t) i] = st;
      }
      auto sp = tb::seedFromPattern(src, root, sc, rr);
      for (float jit : {0.f, 0.004f, -0.004f}) {
        tb::GenParams P = sp.gen; P.notes += jit; P.accents += jit; P.slides += jit; P.octave += jit; P.variation = tb::kLoadVariation + (jit < 0 ? jit : 0.f);
        auto out = tb::resolve(sp.seed, P, root, sc);
        ++trials;
        if (out.len != sp.gen.bars * 16) { ++bad; continue; }
        for (int k = 0; k < out.len; ++k) {
          const auto& a = out[(size_t) k]; const auto& b = src[(size_t)(k % src.len)];
          if (a.active != b.active || (a.active && (a.pitch != b.pitch || a.accent != b.accent || a.slide != b.slide))) { ++bad; break; }
        }
      }
    }
    printf("load-as-seed: %d trials, %d mismatches\n", trials, bad);
    assert(bad == 0);
    printf("Seed bytes %zu\n", sizeof(tb::Seed));
  }
  // modulation
  float base[tb::kDests]; for(int d=0;d<tb::kDests;d++) base[d]=tb::toVal(d,.5f); base[tb::dPitch]=0; base[tb::dMaster]=.85f;
  tb::LfoSettings l[4]; l[0].mask=1u<<tb::dCutoff; l[0].depth=1; l[0].rate=5; l[1].mask=1u<<tb::dNotes; l[1].shape=tb::shSH; l[1].rate=3; l[1].depth=-1; l[2].depth=.5f;l[3].depth=.5f;
  tb::ModResult m; for(double b: {0.0,.25,.5,.75,1.0}){ tb::evalMod(base,l,b,m); printf("beat %.2f lfo0 %.3f cutoff %.1f notes %.3f held %.3f\n",b,m.val[0],m.eff[tb::dCutoff],m.eff[tb::dNotes],m.val[1]); }
  printf("genMod %d\n", tb::genModulated(l));
  // norm roundtrip
  for(int d=0;d<tb::kDests;d++){ float v=tb::toVal(d,.37f); assert(std::abs(tb::toNorm(d,v)-.37f)<1e-4);} puts("roundtrip ok");
  // mixer effects: finite, delay repeats at the right time, reverb tail dies, solo / mute logic
  {
    for (double sr : {44100.0, 48000.0, 96000.0}) {
      tb::DistFx d; d.prepare(sr); d.setDrive(.5f); d.setColor(.5f);
      tb::DelayFx dl; dl.prepare(sr); dl.setTimeSamples((float)(0.375 * sr));
      tb::ReverbFx rv; rv.prepare(sr);
      double peak = 0; for (int i = 0; i < (int)sr; ++i) { float y = d.process(.3f * std::sin(i * .05f)); assert(std::isfinite(y)); peak = std::max(peak, (double)std::abs(y)); }
      assert(peak > .05 && peak < 1.0);
      // delay: an impulse comes back after 0.375 s
      int hit = -1; float mx = 0; dl.process(1.f);
      for (int i = 1; i < (int)sr; ++i) { float y = dl.process(0.f); if (std::abs(y) > mx) { mx = std::abs(y); hit = i; } }
      assert(std::abs(hit - (int)(0.375 * sr)) < sr * 0.02);
      // reverb: finite and dies away
      rv.process(1.f); float last = 1; for (int i = 0; i < (int)(sr * 14); ++i) { last = rv.process(0.f); assert(std::isfinite(last)); }
      assert(std::abs(last) < 1e-3f);
    }
    tb::MixIn m {}; m.delSend = .25f; m.revSend = .25f; m.revAmt = .5f; m.distLevel = .25f;
    auto g0 = tb::mixGates(m);                                    // everything OFF / not muted: dry bass only
    assert(g0.dirGate && g0.sendGate && g0.delSend == 0.f && g0.revSend == 0.f && g0.distWet == 0.f && g0.distDry == 1.f);
    m.delOn = m.revOn = m.distOn = true; auto g1 = tb::mixGates(m);
    assert(g1.delSend == .25f && g1.revSend == .25f && g1.revOut > 0 && g1.distWet > 0 && g1.distDry < 1.f);
    m.soloKick = true; auto g2 = tb::mixGates(m);                 // solo kick: bass and effects silent
    assert(!g2.dirGate && !g2.sendGate && g2.delSend == 0.f && g2.audKick && !g2.audHat);
    m.soloKick = false; m.soloDist = true; auto g3 = tb::mixGates(m);   // solo DIST: only the distortion is heard
    assert(g3.dirGate && g3.distDry == 0.f && !g3.audKick);
    m.soloDist = false; m.muteBass = true; auto g4 = tb::mixGates(m);
    assert(!g4.dirGate && !g4.sendGate);
    puts("fx ok");
  }
  // STYLE: files that only play the off-beats must pull generated notes onto the off-beats; text round trip; custom scale
  {
    std::vector<tb::Imported> files;
    for (int f = 0; f < 3; ++f) { tb::Imported a; a.ok = true; a.root = 38; a.scale = 1; a.bpm = 126; a.pat.len = 64;
      for (int i = 0; i < 64; ++i) if (i % 4 == 2) { a.pat[i].active = true; a.pat[i].pitch = (std::uint8_t) (38 + (i % 8 == 2 ? 0 : 1)); a.pat[i].accent = (i % 16 == 2); } files.push_back(a); }
    tb::Imported bad; bad.ok = true; bad.pat.len = 16; files.push_back(bad);                       // too few notes: skipped
    auto res = tb::analyzeStyle(files);
    assert(res.n == 3 && res.skipped == 1 && res.style.nScales == 1 && res.style.scales[0].k == 1);
    int onB = 0, offB = 0;
    for (int t = 0; t < 40; ++t) { R rr; rr.g.seed(t); auto sd2 = tb::makeSeed(rr);
      auto p = tb::resolve(sd2, {.5f,.4f,.3f,.2f,.1f,4}, 38, tb::scales()[1], &res.style);
      for (int i = 0; i < p.len; ++i) if (p[i].active) { if (i % 4 == 2) ++onB; else ++offB; } }
    printf("style: notes on learned steps %d, elsewhere %d\n", onB, offB);
    assert(onB / 4.0 > 2.5 * offB / 12.0);
    tb::Style back; assert(tb::styleFromText(tb::styleToText(res.style), back));
    assert(back.files == 3 && std::abs(back.rel[2] - res.style.rel[2]) < 1e-3 && back.scales[0].k == 1);
    std::vector<int> buf; auto& cs = tb::scaleVec(tb::kCustomScale, 0b000010010101u, buf);       // 0,2,4,7,... bits 0,2,4,7
    assert((cs == std::vector<int>{0, 2, 4, 7}) && &tb::scaleVec(2, 0, buf) == &tb::scales()[2]);
    puts("style ok");
  }
  delete sd;
}
