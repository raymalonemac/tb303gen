#include "../Source/Seed.h"
#include "../Source/MidiImport.h"
#include "../Source/Modulation.h"
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
  // modulation
  float base[tb::kDests]; for(int d=0;d<tb::kDests;d++) base[d]=tb::toVal(d,.5f); base[tb::dPitch]=0; base[tb::dMaster]=.85f;
  tb::LfoSettings l[4]; l[0].mask=1u<<tb::dCutoff; l[0].depth=1; l[0].rate=5; l[1].mask=1u<<tb::dNotes; l[1].shape=tb::shSH; l[1].rate=3; l[1].depth=-1; l[2].depth=.5f;l[3].depth=.5f;
  tb::ModResult m; for(double b: {0.0,.25,.5,.75,1.0}){ tb::evalMod(base,l,b,m); printf("beat %.2f lfo0 %.3f cutoff %.1f notes %.3f held %.3f\n",b,m.val[0],m.eff[tb::dCutoff],m.eff[tb::dNotes],m.val[1]); }
  printf("genMod %d\n", tb::genModulated(l));
  // norm roundtrip
  for(int d=0;d<tb::kDests;d++){ float v=tb::toVal(d,.37f); assert(std::abs(tb::toNorm(d,v)-.37f)<1e-4);} puts("roundtrip ok");
  delete sd;
}
