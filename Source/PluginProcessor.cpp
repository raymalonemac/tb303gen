#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
juce::StringArray rootChoices()
{
    return { "Random", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray scaleChoices()
{
    juce::StringArray s { "Random" };
    s.addArray (tb::scaleNames());
    return s;
}

juce::StringArray rateChoices()
{
    juce::StringArray s;
    for (int i = 0; i < tb::kNumDivs; ++i) s.add (tb::divName (i));
    return s;
}

int encodeStep (const tb::Step& s)
{
    return (int) s.pitch | (s.accent ? 1 << 8 : 0) | (s.slide ? 1 << 9 : 0) | (s.active ? 1 << 10 : 0);
}

tb::Step decodeStep (int v)
{
    tb::Step s;
    s.pitch = (std::uint8_t) (v & 0x7F); s.accent = (v >> 8) & 1; s.slide = (v >> 9) & 1; s.active = (v >> 10) & 1;
    return s;
}

const char* const kGenIds[6] = { "notes", "accents", "slides", "octave", "variation", "bars" };
const double kRateDefault[4] = { 0, 3, 5, 1 };     // 8 bar, 1 bar, 1/4, 4 bar
const float  kDepthDefault[4] = { 0.03f, 1.00f, 0.50f, 0.50f };
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TB303Processor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (const char* id, const char* name, float def, float lo = 0.f, float hi = 1.f)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.001f), def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.f)) + "%"; })
                .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.f; }));
    };

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "generate", 1 }, "Generate", false));
    p.push_back (pct ("notes",     "Notes",     0.75f));
    p.push_back (pct ("accents",   "Accents",   0.45f));
    p.push_back (pct ("slides",    "Slides",    0.31f));
    p.push_back (pct ("octave",    "Octave",    0.05f));
    p.push_back (pct ("variation", "Variation", 0.05f, 0.01f, 0.40f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bars", 1 }, "Bars",
                    NormalisableRange<float> (4.f, 64.f, 4.f), 4.f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "root", 1 },  "Root",  rootChoices(),  0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleChoices(), 0));

    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "synth", 1 }, "Internal synth", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "wave", 1 },  "Wave", StringArray { "Saw", "Square" }, 0));

    // cutoff: logarithmic, so the pad's 0..1 position matches the LFO maths exactly
    NormalisableRange<float> cutRange (40.f, 8000.f,
        [] (float s, float e, float n) { return s * std::pow (e / s, n); },
        [] (float s, float e, float v) { return std::log (jmax (s, v) / s) / std::log (e / s); },
        [] (float s, float e, float v) { return jlimit (s, e, v); });
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", cutRange, 1396.f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    p.push_back (pct ("reso",   "Resonance", 0.92f));
    p.push_back (pct ("envmod", "Env mod",   0.64f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay",
                    NormalisableRange<float> (0.05f, 1.5f, 0.001f), 1.32f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume",     // 0 .. 0.15 inside, shown as 0 .. 100 %
                    NormalisableRange<float> (0.f, 0.15f, 0.0001f), 0.15f,
                    AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v / 0.15f * 100.f)) + "%"; })
                        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.f * 0.15f; })));

    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "drums", 1 }, "Drums", true));
    p.push_back (pct ("kickvol", "Kick volume", 0.71f));
    p.push_back (pct ("hatvol",  "Hat volume",  1.0f));

    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "run", 1 },   "Run (no host)", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo (no host)",
                    NormalisableRange<float> (60.f, 200.f, 1.f), 130.f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

    for (int n = 1; n <= 4; ++n)
    {
        const String pre = "lfo" + String (n);
        const String nm = "LFO " + String (n) + " ";
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pre + "shape", 1 }, nm + "shape",
                        StringArray { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pre + "rate", 1 }, nm + "rate", rateChoices(), (int) kRateDefault[n - 1]));
        p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { pre + "depth", 1 }, nm + "depth",
                        NormalisableRange<float> (-1.f, 1.f, 0.001f), kDepthDefault[n - 1],
                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.f)) + "%"; })));
    }

    // ---- appended last (mixer mutes): existing parameters keep their IDs and positions, so old projects and presets still load.
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "mutekick", 1 }, "Mute kick", false));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "mutehat", 1 },  "Mute hats", false));
    // ---- mixer effects (appended after everything else, so older projects and presets still load)
    for (const char* id : { "mutebass", "solobass", "solokick", "solohat", "solodel", "solorev", "solodist" })
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, String ("Mixer ") + id, false));
    for (const char* id : { "delon", "revon", "diston" })                                     // delay, reverb, distortion start OFF
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, String ("FX ") + id, false));
    p.push_back (pct ("delsend",   "Delay send",       0.25f));
    p.push_back (pct ("revsend",   "Reverb send",      0.25f));
    p.push_back (pct ("distlevel", "Distortion level", 0.25f));
    {
        StringArray dl; for (int k = 0; k < tb::kNumDly; ++k) dl.add (tb::dlyName (k));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "deltime", 1 }, "Delay time", dl, 2));    // 1/8.
    }
    p.push_back (pct ("revamt",    "Reverb amount",    0.5f));
    p.push_back (pct ("distdrive", "Distortion send",  0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "distcolor", 1 }, "Distortion color", NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.34f ? String ("dark") : v < 0.67f ? String ("warm") : String ("bright"); })));

    return { p.begin(), p.end() };
}

TB303Processor::TB303Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int d = 0; d < tb::kDests; ++d)
        rawDest[(size_t) d] = (d == tb::dPitch || d == tb::dMaster) ? nullptr : apvts.getRawParameterValue (tb::destDef (d).id);

    for (int n = 0; n < 4; ++n)
    {
        const juce::String pre = "lfo" + juce::String (n + 1);
        rawLfo[(size_t) n] = { apvts.getRawParameterValue (pre + "shape"), apvts.getRawParameterValue (pre + "rate"),
                               apvts.getRawParameterValue (pre + "depth") };
        lfoMask[(size_t) n] = 0;
        uiLfoVal[(size_t) n] = 0.f;
        uiLfoPhase[(size_t) n] = 0.f;
    }
    // all four LFOs start unlinked (no destinations)

    scaleBuf.reserve (16);
    generatePattern();
    apvts.addParameterListener ("generate", this);
    apvts.addParameterListener ("root", this);
    apvts.addParameterListener ("scale", this);
    for (auto* id : kGenIds) apvts.addParameterListener (id, this);
    autoStyle();                                          // the save folder is read quietly so STYLE is ready when it is needed
}

TB303Processor::~TB303Processor()
{
    alive->store (false);
    if (styleThread != nullptr) styleThread->stopThread (4000);
    cancelPendingUpdate();
    apvts.removeParameterListener ("generate", this);
    apvts.removeParameterListener ("root", this);
    apvts.removeParameterListener ("scale", this);
    for (auto* id : kGenIds) apvts.removeParameterListener (id, this);
}

void TB303Processor::parameterChanged (const juce::String& id, float value)
{
    if (id == "root" || id == "scale")
    {
        // a new root / scale re-reads the same dice, live
        if (applyingRS) return;
        auto flag = alive;
        juce::MessageManager::callAsync ([this, flag]
        {
            if (! flag->load() || ! shared.hasSeed) return;
            keysOn = false;                              // picking a root / scale by hand switches the keyboard off
            juce::Random rng;
            const int ri = (int) param ("root"), si = (int) param ("scale");
            const int nr = ri == 0 ? 36 + rng.nextInt (12) : 36 + (ri - 1);
            const int ns = pickScale (si, rng);
            if (nr == lastRoot.load() && ns == lastScale.load()) return;

            undoStack.push_back (currentGen());          // snapshot with the old root / scale
            if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
            redoStack.clear();
            lastRoot = nr;
            lastScale = ns;
            retweak();
        });
        return;
    }

    if (id != "generate")
    {
        if (! applyingGen) triggerAsyncUpdate();                            // a generator fader moved (UI, host or automation)
        return;
    }

    if (value < 0.5f)
        return;

    // rising edge from the host (MIDI map / automation): roll a new line, then release the switch
    auto flag = alive;
    juce::MessageManager::callAsync ([this, flag]
    {
        if (! flag->load())
            return;
        generatePattern();
        if (auto* p = apvts.getParameter ("generate"))
            p->setValueNotifyingHost (0.0f);
    });
}

void TB303Processor::handleAsyncUpdate() { retweak(); }

void TB303Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    scaleBuf.reserve (16);
    voice.prepare (sampleRate);
    kick.prepare (sampleRate);
    hat.prepare (sampleRate);
    distFx.prepare (sampleRate);
    delayFx.prepare (sampleRate);               // delay lines are allocated here, never on the audio thread
    reverbFx.prepare (sampleRate);
    smDry = 1.f; smWet = smDel = smRev = smRevOut = 0.f; smDir = smSend = 1.f;
    sounding.fill (0);
    np = 0;
    wasPlaying = false;
    internalBeat = expectedBeat = lfoFree = 0.0;
}

bool TB303Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// ------------------------------------------------------------------ pattern

void TB303Processor::commit (const BassLine& line, bool immediate)
{
    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        shared = line;
        immediateSwap = immediate;
    }
    lastRoot = line.root;
    lastScale = line.scale;
    lastMask = line.cmask;
    ++patternVersion;
}

tb::Pattern TB303Processor::getPatternCopy() const
{
    const juce::SpinLock::ScopedLockType lock (patternLock);
    return shared.pat;
}

tb::Pattern TB303Processor::getPlayingCopy() const
{
    const juce::SpinLock::ScopedLockType lock (uiLock);
    return uiPlayingPattern;
}

int TB303Processor::pickScale (int scaleIdx, juce::Random& rng) const
{
    if (scaleIdx == 0)                                                   // Random: the scales the analysed files lean to, when STYLE is on
        return (styleOn && styleHas) ? tb::styleScale (style, rng.nextFloat()) : rng.nextInt ((int) tb::scales().size());
    return scaleIdx - 1;
}

std::uint16_t TB303Processor::keysIvMask() const
{
    std::uint16_t m = 0;
    for (int pc = 0; pc < 12; ++pc)
        if ((keysPcs >> pc) & 1) m = (std::uint16_t) (m | (1u << ((pc - keysRoot + 12) % 12)));
    return (std::uint16_t) (m | 1u);
}

void TB303Processor::generatePattern()
{
    jukeStop();
    juce::Random rng;

    const int rootIdx = (int) param ("root");
    int root = rootIdx == 0 ? 36 + rng.nextInt (12) : 36 + (rootIdx - 1);
    const int scaleIdx = (int) param ("scale");
    int sc = pickScale (scaleIdx, rng);

    auto line = std::make_unique<BassLine>();
    if (keysActive()) { root = 36 + keysRoot; sc = tb::kCustomScale; line->cmask = keysIvMask(); }
    else if (sc == tb::kCustomScale) line->cmask = lastMask.load();
    if (styleOn && styleHas) { line->useStyle = true; line->sty = style; }
    line->seed = tb::makeSeed (rng);
    line->hasSeed = true;
    line->root = root;
    line->scale = sc;
    std::vector<int> buf;
    line->pat = tb::resolve (line->seed, toParams (currentGen()), root, tb::scaleVec (sc, line->cmask, buf), line->useStyle ? &line->sty : nullptr);
    if (keysActive() || sc == tb::kCustomScale) setRootScaleParams (root, sc);

    loadedName = {};
    clearUndo();
    commit (*line, false);
    pushHistory (*line);
}

void TB303Processor::pushHistory (const BassLine& line)
{
    Gen g = currentGen();
    g[6] = (float) line.root;
    g[7] = (float) line.scale;
    history.push_back ({ line, g, loadedName });
    if (history.size() > 64) history.erase (history.begin());
    histPos = (int) history.size() - 1;
}

void TB303Processor::showHistory (int index)
{
    if (index < 0 || index >= (int) history.size()) return;
    histPos = index;
    const auto h = history[(size_t) index];               // copy: applyGen below fires listeners
    loadedName = h.name;
    clearUndo();
    applyGen (h.gen);                                     // put the faders (and root / scale) back where they were
    commit (h.line, false);
}

void TB303Processor::previousPattern() { if (jukeOn) jukeStep (-1); else showHistory (histPos - 1); }
void TB303Processor::nextPattern()     { if (jukeOn) jukeStep (1);  else showHistory (histPos + 1); }

// ------------------------------------------------------------------ tweaker mode (always on)

TB303Processor::Gen TB303Processor::currentGen() const
{
    return { param ("notes"), param ("accents"), param ("slides"), param ("octave"), param ("variation"), param ("bars"),
             (float) lastRoot.load(), (float) lastScale.load() };
}

void TB303Processor::setRootScaleParams (int root, int scale)
{
    applyingRS = true;                                   // show the root / scale in the combos without re-rolling
    if (auto* r = apvts.getParameter ("root"))  r->setValueNotifyingHost (r->convertTo0to1 ((float) (juce::jlimit (36, 47, root) - 36 + 1)));
    if (auto* s = apvts.getParameter ("scale")) s->setValueNotifyingHost (s->convertTo0to1 ((float) (scale + 1)));
    applyingRS = false;
}

void TB303Processor::applyGen (const Gen& g)
{
    applyingGen = true;                                  // the caller decides how the new line is swapped in
    for (int i = 0; i < 6; ++i)
        if (auto* prm = apvts.getParameter (kGenIds[i]))
            prm->setValueNotifyingHost (prm->convertTo0to1 (g[(size_t) i]));
    applyingGen = false;

    lastRoot  = (int) g[6];
    lastScale = (int) g[7];
    setRootScaleParams (lastRoot.load(), lastScale.load());
}

// same dice, new fader values -> new line, swapped in right away (no waiting for the bar line)
void TB303Processor::retweak()
{
    if (! shared.hasSeed) return;
    const int root = lastRoot.load(), scale = lastScale.load();
    const auto mask = (std::uint16_t) (scale == tb::kCustomScale ? (keysActive() ? keysIvMask() : (unsigned) lastMask.load()) : 0u);
    std::vector<int> buf;
    const auto pat = tb::resolve (shared.seed, toParams (currentGen()), root, tb::scaleVec (scale, mask, buf), shared.useStyle ? &shared.sty : nullptr);
    lastMask = mask;
    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        shared.pat = pat;
        shared.root = root;
        shared.scale = scale;
        shared.cmask = mask;
        immediateSwap = true;
    }
    ++patternVersion;

    if (histPos >= 0 && histPos < (int) history.size())      // PREVIOUS/NEXT will bring back the tweaked state
    {
        auto& h = history[(size_t) histPos];
        h.line.pat = pat;
        h.line.root = root;
        h.line.scale = scale;
        h.line.cmask = mask;
        h.gen = currentGen();
    }
}

void TB303Processor::beginTweakGesture()
{
    if (inGesture) return;
    gestureStart = currentGen();
    inGesture = true;
}

void TB303Processor::endTweakGesture()
{
    if (! inGesture) return;
    inGesture = false;
    if (currentGen() == gestureStart) return;

    undoStack.push_back (gestureStart);
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void TB303Processor::undoTweak()
{
    if (undoStack.empty()) return;
    redoStack.push_back (currentGen());
    const auto g = undoStack.back();
    undoStack.pop_back();
    applyGen (g);
    retweak();
}

void TB303Processor::redoTweak()
{
    if (redoStack.empty()) return;
    undoStack.push_back (currentGen());
    const auto g = redoStack.back();
    redoStack.pop_back();
    applyGen (g);
    retweak();
}

// ------------------------------------------------------------------ files

juce::String TB303Processor::midiFileName() const
{
    return "TB303_" + tb::noteName (lastRoot.load()) + "_"
           + tb::scaleNames()[juce::jlimit (0, tb::scaleNames().size() - 1, lastScale.load())].replace (" ", "-") + "_"
           + juce::String (juce::roundToInt (lastBpm.load())) + "bpm_"
           + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".mid";
}

juce::String TB303Processor::suggestedMidiName() const { return midiFileName(); }

// The same "TB303 Gen" folder the web app uses in Google Drive, when Google Drive for desktop is installed (it syncs the
// folder both ways); otherwise ~/Music/TB303 Gen.
juce::File TB303Processor::saveFolder() const
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    juce::Array<juce::File> roots;
    const auto cloud = home.getChildFile ("Library/CloudStorage");                    // macOS: GoogleDrive-name@gmail.com/My Drive
    if (cloud.isDirectory())
        for (auto& d : cloud.findChildFiles (juce::File::findDirectories, false, "GoogleDrive-*"))
            roots.add (d.getChildFile ("My Drive"));
    roots.add (home.getChildFile ("Google Drive/My Drive"));                          // older Drive for desktop / Windows profile
    roots.add (home.getChildFile ("Google Drive"));
    roots.add (juce::File ("G:\\My Drive"));                                           // Windows drive letter
    for (auto& r : roots)
        if (r.isDirectory())
            return r.getChildFile ("TB303 Gen");
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("TB303 Gen");
}

bool TB303Processor::saveMidiTo (const juce::File& file)
{
    return tb::writeMidiFile (getPatternCopy(), lastBpm.load(), file);
}

bool TB303Processor::saveToFolder (juce::File& written)
{
    const auto dir = saveFolder();
    dir.createDirectory();
    written = dir.getChildFile (midiFileName());
    return saveMidiTo (written);
}

juce::File TB303Processor::makeTempMidiFile()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TB303 Gen");
    dir.createDirectory();
    const auto file = dir.getChildFile (midiFileName());
    return saveMidiTo (file) ? file : juce::File();
}

TB303Processor::LoadResult TB303Processor::loadMidiFile (const juce::File& file)
{
    if (! jukeBusy) jukeStop();                           // loading a file by hand ends the saved-loops mode
    LoadResult res;
    const auto m = tb::readMidiFile (file);
    if (! m.ok)
    {
        res.error = m.error.isEmpty() ? juce::String ("Could not read this file.") : m.error;
        return res;
    }

    const auto& r = m.imp;
    auto line = std::make_unique<BassLine>();
    line->root = juce::jlimit (36, 47, r.root);
    line->scale = r.scale;

    const bool bpmOk = r.bpm >= 60.0 && r.bpm <= 200.0;
    if (bpmOk)
        if (auto* t = apvts.getParameter ("tempo"))
            t->setValueNotifyingHost (t->convertTo0to1 ((float) std::round (r.bpm)));

    // editable straight away: build dice that reproduce the file, and put the faders where that works
    juce::Random rng;
    const auto sd = tb::seedFromPattern (r.pat, line->root, tb::scales()[(size_t) line->scale], rng);
    keysOn = false;
    line->seed = sd.seed;
    line->hasSeed = true;

    loadedName = file.getFileName();
    clearUndo();
    applyGen ({ sd.gen.notes, sd.gen.accents, sd.gen.slides, sd.gen.octave, sd.gen.variation, (float) sd.gen.bars,
                (float) line->root, (float) line->scale });                  // faders + root / scale
    line->pat = tb::resolve (line->seed, toParams (currentGen()), line->root, tb::scales()[(size_t) line->scale]);
    commit (*line, false);
    pushHistory (*line);

    res.ok = true;
    res.text = juce::String (r.count) + " notes over " + juce::String (r.bars) + (r.bars > 1 ? " bars" : " bar") + " in "
               + juce::MidiMessage::getMidiNoteName (r.root, true, false, 4) + " " + tb::scaleNames()[r.scale]
               + (bpmOk ? " at " + juce::String (juce::roundToInt (r.bpm)) + " bpm" : juce::String())
               + ". It is ready to edit: the faders now reshape this line (Variation is at 5% = every bar as loaded).";
    return res;
}

// ------------------------------------------------------------------ LFOs

void TB303Processor::readMod (std::array<float, tb::kDests>& base, std::array<tb::LfoSettings, 4>& lfo) const
{
    for (int d = 0; d < tb::kDests; ++d)
        base[(size_t) d] = rawDest[(size_t) d] != nullptr ? rawDest[(size_t) d]->load()
                                                          : (d == tb::dPitch ? tb::kPitchBase : tb::kMasterBase);
    for (size_t i = 0; i < 4; ++i)
    {
        lfo[i].shape = (int) rawLfo[i][0]->load();
        lfo[i].rate  = (int) rawLfo[i][1]->load();
        lfo[i].depth = rawLfo[i][2]->load();
        lfo[i].mask  = lfoMask[i].load();
    }
}

bool TB303Processor::lfoContrib (int lfo, int dest, float& out) const
{
    if (((lfoMask[(size_t) lfo].load() >> dest) & 1u) == 0) return false;
    out = uiLfoVal[(size_t) lfo].load() * lfoDepth (lfo) * 0.5f;
    return true;
}

// ------------------------------------------------------------------ audio

void TB303Processor::flushNotes (juce::MidiBuffer& midi, int samplePos)
{
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        if (sounding[(size_t) pitch] > 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, pitch), samplePos);
        sounding[(size_t) pitch] = 0;
    }
    np = 0;
}

void TB303Processor::generateEvents (juce::MidiBuffer& midi, double b0, double b1, double bps, int n)
{
    struct Ev { double beat; bool on; int pitch; int vel; };
    std::array<Ev, 192> ev {};
    int ne = 0;
    auto push = [&] (double beat, bool on, int pitch, int vel)
    {
        if (ne < (int) ev.size()) ev[(size_t) ne++] = { beat, on, pitch, vel };
    };

    // note-offs carried over from earlier blocks
    for (int i = 0; i < np;)
    {
        if (pend[(size_t) i].beat < b1)
        {
            push (pend[(size_t) i].beat, false, pend[(size_t) i].pitch, 0);
            pend[(size_t) i] = pend[(size_t) (np - 1)];
            --np;
        }
        else ++i;
    }

    // steps that start inside this block
    const long long k0 = std::max<long long> (0, (long long) std::ceil ((b0 - 1.0e-9) / tb::kStepBeats));
    for (long long k = k0; (double) k * tb::kStepBeats < b1; ++k)
    {
        if (k % tb::kStepsPerBar == 0)                     // bar line
        {
            if (hasPending)                                // a new pattern restarts from its bar 1
            {
                audioLine = pendingLine;
                hasPending = false;
                origin = k;
                audioHasPattern = true;
                publishDirty = true;
            }
            if (audioLine.hasSeed && tb::genModulated (blockLfo.data()))   // LFOs on the generator faders: re-read the dice each bar
            {
                tb::ModResult mr;
                tb::evalMod (blockBase.data(), blockLfo.data(), (double) k * tb::kStepBeats, mr);
                const tb::GenParams gp { mr.eff[tb::dNotes], mr.eff[tb::dAccents], mr.eff[tb::dSlides], mr.eff[tb::dOctave],
                                         mr.eff[tb::dVariation], audioLine.pat.bars() };
                audioLine.pat = tb::resolve (audioLine.seed, gp, audioLine.root, tb::scaleVec (audioLine.scale, audioLine.cmask, scaleBuf),
                                             audioLine.useStyle ? &audioLine.sty : nullptr);
                publishDirty = true;
            }
        }

        const int len = audioLine.pat.len;
        const long long idx = ((k - origin) % len + len) % len;
        const tb::Step& s = audioLine.pat[(size_t) idx];
        if (! s.active) continue;
        const tb::Step& nx = audioLine.pat[(size_t) ((idx + 1) % len)];

        const double start = (double) k * tb::kStepBeats;
        double end = start + tb::kGateBeats;
        if (s.slide && nx.active)
            end = start + tb::kStepBeats + (nx.pitch != s.pitch ? tb::kOverlapBeats : 0.0);   // overlap = slide

        push (start, true, (int) s.pitch, s.accent ? tb::kVelAccent : tb::kVelNormal);
        if (end < b1) push (end, false, (int) s.pitch, 0);
        else if (np < (int) pend.size()) pend[(size_t) np++] = { end, (int) s.pitch };
    }

    std::sort (ev.begin(), ev.begin() + ne, [] (const Ev& a, const Ev& b)
    {
        if (a.beat != b.beat) return a.beat < b.beat;
        return ! a.on && b.on;                    // note-off first on ties
    });

    for (int i = 0; i < ne; ++i)
    {
        const int pos = juce::jlimit (0, n - 1, (int) std::floor ((ev[(size_t) i].beat - b0) / bps));
        const int pitch = ev[(size_t) i].pitch;
        if (ev[(size_t) i].on)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) ev[(size_t) i].vel), pos);
            ++sounding[(size_t) pitch];
        }
        else if (sounding[(size_t) pitch] > 0)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, pitch), pos);
            --sounding[(size_t) pitch];
        }
    }
}

void TB303Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // pick up a newly generated line (never block the audio thread)
    const int v = patternVersion.load();
    if (v != audioVersion)
    {
        const juce::SpinLock::ScopedTryLockType lock (patternLock);
        if (lock.isLocked())
        {
            pendingLine = shared;
            hasPending = true;
            audioVersion = v;

            if (immediateSwap.load() && audioHasPattern)      // live tweak: same bar, same position
            {
                audioLine = pendingLine;
                hasPending = false;
                publishDirty = true;
            }
        }
    }

    // transport: host if it gives us a position, otherwise our own clock
    double bpm = param ("tempo"), ppq = 0.0;
    bool havePos = false, playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePos = true; }
            playing = pos->getIsPlaying();
        }
    }
    if (! havePos)
    {
        playing = param ("run") > 0.5f;
        if (! playing) internalBeat = 0.0;
        ppq = internalBeat;
    }
    if (bpm < 1.0) bpm = 120.0;
    lastBpm = bpm;
    uiBpm = bpm;

    const double bps = bpm / (60.0 * sr);             // beats per sample
    const double b0 = ppq, b1 = ppq + n * bps;
    if (! havePos && playing) internalBeat += n * bps;

    // modulation: LFOs follow the beat while playing and keep running (free) while stopped
    readMod (blockBase, blockLfo);
    const double lfoB0 = playing ? b0 : lfoFree;
    lfoFree = playing ? b1 : lfoFree + n * bps;
    tb::ModResult mr0;
    tb::evalMod (blockBase.data(), blockLfo.data(), lfoB0, mr0);
    for (size_t i = 0; i < 4; ++i) { uiLfoVal[i] = mr0.val[i]; uiLfoPhase[i] = mr0.phase[i]; }

    auto applyPending = [this] (long long newOrigin)
    {
        audioLine = pendingLine;
        hasPending = false;
        origin = newOrigin;
        audioHasPattern = true;
        publishDirty = true;
    };
    if (hasPending && (! playing || ! audioHasPattern))     // stopped (or first ever pattern): swap right away
        applyPending (0);

    if (! playing)
    {
        if (wasPlaying) flushNotes (midi, 0);
        wasPlaying = false;
        uiPlaying = false;
    }
    else
    {
        if (! wasPlaying || std::abs (b0 - expectedBeat) > 0.02)    // start, or transport jump/loop
        {
            flushNotes (midi, 0);
            if (hasPending) applyPending (0); else origin = 0;
        }
        wasPlaying = true;
        uiPlaying = true;
        generateEvents (midi, b0, b1, bps, n);
        expectedBeat = b1;

        double stepsF = b0 / tb::kStepBeats - (double) origin;   // position inside the playing pattern, for the UI
        if (stepsF < 0.0) stepsF = 0.0;
        uiStepPos = std::fmod (stepsF, (double) audioLine.pat.len);
        uiPosStamp = juce::Time::getMillisecondCounterHiRes();
    }
    uiHasPending = hasPending;

    // mixer: mute / on-off / solo decide what is heard (same rules as the web app)
    const auto mg = tb::mixGates ({ param ("mutebass") > 0.5f, param ("mutekick") > 0.5f, param ("mutehat") > 0.5f,
                                    param ("delon") > 0.5f, param ("revon") > 0.5f, param ("diston") > 0.5f,
                                    param ("solobass") > 0.5f, param ("solokick") > 0.5f, param ("solohat") > 0.5f,
                                    param ("solodel") > 0.5f, param ("solorev") > 0.5f, param ("solodist") > 0.5f,
                                    param ("delsend"), param ("revsend"), param ("revamt"), param ("distlevel") });
    distFx.setDrive (param ("distdrive"));
    distFx.setColor (param ("distcolor"));
    delayFx.setTimeSamples ((float) (tb::dlyBeats ((int) std::lround (param ("deltime"))) / bps));   // bps = beats per sample

    // internal synth renders the very same events; its modulated controls are re-read every 32 samples
    if (param ("synth") > 0.5f)
    {
        const bool square = param ("wave") > 0.5f;
        const float smK = 1.f - std::exp (-1.f / (0.01f * (float) sr));
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        auto render = [&] (int from, int to)
        {
            int i = from;
            while (i < to)
            {
                const int m = std::min (to, i + 32);
                tb::ModResult mr;
                tb::evalMod (blockBase.data(), blockLfo.data(), lfoB0 + (double) i * bps, mr);
                voice.setParams (mr.eff[tb::dCutoff], mr.eff[tb::dReso], mr.eff[tb::dEnvMod], mr.eff[tb::dDecay], square, 1.f);
                const float vol = mr.eff[tb::dVolume];
                voice.setDetuneCents (mr.eff[tb::dPitch]);
                for (; i < m; ++i)
                {
                    // bass -> distortion (crossfade) -> volume -> direct + delay -> reverb
                    smDry += smK * (mg.distDry - smDry);   smWet += smK * (mg.distWet - smWet);
                    smDir += smK * ((mg.dirGate ? 1.f : 0.f) - smDir);   smSend += smK * ((mg.sendGate ? 1.f : 0.f) - smSend);
                    smDel += smK * (mg.delSend - smDel);   smRev += smK * (mg.revSend - smRev);   smRevOut += smK * (mg.revOut - smRevOut);
                    const float raw = voice.process();
                    const float b = (raw * smDry + distFx.process (raw) * smWet) * vol;
                    const float send = b * smSend;
                    const float dl = delayFx.process (send * smDel);
                    const float s = b * smDir + dl + reverbFx.process (dl + send * smRev) * smRevOut;
                    L[i] += s;
                    if (R != nullptr) R[i] += s;
                }
            }
        };

        int pos = 0;
        for (const auto meta : midi)
        {
            render (pos, meta.samplePosition);
            pos = meta.samplePosition;
            const auto m = meta.getMessage();
            if (m.isNoteOn())       voice.noteOn (m.getNoteNumber(), m.getVelocity() >= 100);
            else if (m.isNoteOff()) voice.noteOff (m.getNoteNumber());
        }
        render (pos, n);
    }
    else
    {
        voice.reset();
    }

    // drums: kick on every beat, closed hat on every off-beat (internal sound only)
    struct Trig { int pos; bool isKick; };
    std::array<Trig, 32> trig {};
    int nt = 0;
    if (playing && param ("drums") > 0.5f)
    {
        // 8th-note grid: even slots = on the beat (kick), odd slots = off-beat (hat)
        for (long long j = std::max<long long> (0, (long long) std::ceil ((b0 - 1.0e-9) / 0.5)); (double) j * 0.5 < b1 && nt < 32; ++j)
        {
            const int pos = juce::jlimit (0, n - 1, (int) std::floor (((double) j * 0.5 - b0) / bps));
            trig[(size_t) nt++] = { pos, j % 2 == 0 };
        }
    }

    {
        const float kv = mr0.eff[tb::dKick], hv = mr0.eff[tb::dHat];
        const bool muteKick = ! mg.audKick, muteHat = ! mg.audHat;
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        int ti = 0;
        for (int i = 0; i < n; ++i)
        {
            while (ti < nt && trig[(size_t) ti].pos <= i)
            {
                if (trig[(size_t) ti].isKick) { if (! muteKick) kick.trigger(); }
                else if (! muteHat) hat.trigger();
                ++ti;
            }
            const float s = kick.process() * kv * 0.5f            // kick 50% quieter
                          + hat.process() * hv * 6.32f;          // hat louder again (+6 dB on top of the earlier +10 dB)
            L[i] += s;
            if (R != nullptr) R[i] += s;
        }
    }


    // master level (the MASTER LFO destination; 0.85 = unity)
    buffer.applyGain (mr0.eff[tb::dMaster] / tb::kMasterBase);

    // safety: soft-clip anything above 0.9 so the louder hat can never overshoot
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (d[i]);
            if (a > 0.9f) d[i] = std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), d[i]);
        }
    }

    if (publishDirty)                                          // let the editor see what is playing now
    {
        const juce::SpinLock::ScopedTryLockType l (uiLock);
        if (l.isLocked()) { uiPlayingPattern = audioLine.pat; publishDirty = false; }
    }
}

// ------------------------------------------------------------------ state

juce::AudioProcessorEditor* TB303Processor::createEditor() { return new TB303Editor (*this); }

void TB303Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();

    BassLine snap;
    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        snap = shared;
    }

    juce::String data;
    for (int i = 0; i < snap.pat.len; ++i)
        data << encodeStep (snap.pat[(size_t) i]) << ",";
    state.setProperty ("patLen", snap.pat.len, nullptr);
    state.setProperty ("pattern", data, nullptr);
    state.setProperty ("lastRoot", lastRoot.load(), nullptr);
    state.setProperty ("lastScale", lastScale.load(), nullptr);
    state.setProperty ("lastMask", (int) lastMask.load(), nullptr);
    state.setProperty ("keys", juce::String (keysOn ? 1 : 0) + "," + juce::String (keysRoot) + "," + juce::String ((int) keysPcs), nullptr);
    if (styleHas) state.setProperty ("style", juce::String (tb::styleToText (style)), nullptr);
    state.setProperty ("loadedName", loadedName, nullptr);
    state.setProperty ("editorZoom", (double) editorZoom.load(), nullptr);
    state.setProperty ("lfoMasks", juce::String ((juce::int64) lfoMask[0].load()) + "," + juce::String ((juce::int64) lfoMask[1].load()) + ","
                                   + juce::String ((juce::int64) lfoMask[2].load()) + "," + juce::String ((juce::int64) lfoMask[3].load()), nullptr);
    if (snap.hasSeed)                                    // so the tweaker still works after reopening a project
        state.setProperty ("seed", juce::MemoryBlock (&snap.seed, sizeof (snap.seed)).toBase64Encoding(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void TB303Processor::setStateInformation (const void* data, int size)
{
    if (wrapperType == wrapperType_Standalone)           // the standalone app always starts fresh (it would otherwise reload its last session)
        return;

    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto vt = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (vt);
    if (param ("drums") < 0.5f)                          // older projects: "Drums off" is now Mute kick + Mute hats
    {
        if (auto* d = apvts.getParameter ("drums"))    d->setValueNotifyingHost (1.f);
        if (auto* k = apvts.getParameter ("mutekick")) k->setValueNotifyingHost (1.f);
        if (auto* h = apvts.getParameter ("mutehat"))  h->setValueNotifyingHost (1.f);
    }
    if (auto* run = apvts.getParameter ("run"))          // a project always opens stopped
        run->setValueNotifyingHost (0.f);

    const auto masks = juce::StringArray::fromTokens (vt.getProperty ("lfoMasks").toString(), ",", "");
    if (masks.size() == 4)
        for (int i = 0; i < 4; ++i)
            lfoMask[(size_t) i] = (std::uint32_t) masks[i].getLargeIntValue();

    const auto tokens = juce::StringArray::fromTokens (vt.getProperty ("pattern").toString(), ",", "");
    const int len = juce::jlimit (tb::kStepsPerBar, tb::kMaxSteps, (int) vt.getProperty ("patLen", 256));
    const int root = juce::jlimit (36, 47, (int) vt.getProperty ("lastRoot", 36));
    const int scale = juce::jlimit (0, tb::scaleNames().size() - 1, (int) vt.getProperty ("lastScale", 0));
    loadedName = vt.getProperty ("loadedName").toString();
    editorZoom = (float) juce::jlimit (0.5, 2.0, (double) vt.getProperty ("editorZoom", 0.85));

    if (tokens.size() >= len)
    {
        auto line = std::make_unique<BassLine>();
        line->pat.len = len;
        for (int i = 0; i < len; ++i)
            line->pat[(size_t) i] = decodeStep (tokens[i].getIntValue());
        line->root = root;
        line->scale = scale;
        line->cmask = (std::uint16_t) juce::jlimit (0, 4095, (int) vt.getProperty ("lastMask", 0));
        {
            const auto k = juce::StringArray::fromTokens (vt.getProperty ("keys").toString(), ",", "");
            if (k.size() == 3) { keysOn = k[0].getIntValue() != 0; keysRoot = juce::jlimit (0, 11, k[1].getIntValue()); keysPcs = (std::uint16_t) juce::jlimit (0, 4095, k[2].getIntValue()); }
            tb::Style st;
            if (tb::styleFromText (vt.getProperty ("style").toString().toStdString(), st)) { style = st; styleHas = true; styleReady = true; styleManual = true; }
            styleOn = false;                                       // STYLE starts OFF each time a project opens
        }

        juce::MemoryBlock mb;
        line->hasSeed = mb.fromBase64Encoding (vt.getProperty ("seed").toString())
                        && (mb.getSize() == sizeof (tb::Seed) || mb.getSize() == tb::kSeedBytesV1);
        if (line->hasSeed) { line->seed = tb::Seed {}; std::memcpy (&line->seed, mb.getData(), mb.getSize()); }

        clearUndo();
        commit (*line, false);
        pushHistory (*line);
    }
}

// ------------------------------------------------------------------ KEYS

void TB303Processor::setKeys (int rootPc, std::uint16_t pcMask)
{
    pcMask = (std::uint16_t) (pcMask & 0xFFF);
    if (pcMask == 0) { keysOff(); return; }
    keysRoot = juce::jlimit (0, 11, rootPc);
    keysPcs = (std::uint16_t) (pcMask | (1u << keysRoot));
    keysOn = true;
    if (! shared.hasSeed) return;

    undoStack.push_back (currentGen());                  // the new notes are undoable like a root / scale change
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
    lastRoot = 36 + keysRoot;
    lastScale = tb::kCustomScale;
    lastMask = keysIvMask();
    setRootScaleParams (lastRoot.load(), lastScale.load());
    retweak();
}

void TB303Processor::keysOff() { keysOn = false; }

// ------------------------------------------------------------------ STYLE

juce::String TB303Processor::styleBusyText() const
{
    return "READING " + juce::String (styleDone.load()) + " / " + juce::String (styleTotal.load());
}

juce::String TB303Processor::styleSummary() const
{
    if (! styleHas) return {};
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; };
    juce::StringArray names;
    for (int i = 0; i < style.nScales; ++i)
        names.add (tb::scaleNames()[juce::jlimit (0, tb::scaleNames().size() - 1, style.scales[(size_t) i].k)]);
    juce::String t = juce::String (style.files) + (style.files > 1 ? " files" : " file") + " / " + juce::String (style.notesTotal) + " notes. Mostly ";
    t << names[0];
    if (names.size() > 1) t << " + " << names[1];
    t << ". Notes " << pct (style.notes) << ", accents " << pct (style.accents) << ", slides " << pct (style.slides)
      << ", octave up " << pct (style.octave);
    if (style.bpm > 0) t << ", around " << style.bpm << " bpm";
    return t << ".";
}

void TB303Processor::applyStyleFaders()
{
    applyingGen = true;
    const float v[4] = { style.notes, style.accents, style.slides, style.octave };
    for (int i = 0; i < 4; ++i)
        if (auto* prm = apvts.getParameter (kGenIds[i]))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v[i]));
    applyingGen = false;
    retweak();
}

void TB303Processor::setStyleOn (bool on)
{
    if (! styleHas) return;
    styleOn = on;
    if (on) applyStyleFaders();
}

void TB303Processor::forgetStyle()
{
    styleHas = styleOn = styleReady = styleManual = false;
    style = {};
}

TB303Processor::Notice TB303Processor::takeNotice()
{
    const juce::SpinLock::ScopedLockType lock (noticeLock);
    auto n = notice;
    notice = {};
    return n;
}

namespace
{
class StyleJob : public juce::Thread
{
public:
    StyleJob (juce::Array<juce::File> f, std::atomic<int>& d, std::atomic<int>& t, std::function<void (tb::StyleResult)> done)
        : juce::Thread ("style"), files (std::move (f)), doneN (d), total (t), onDone (std::move (done)) {}
    void run() override
    {
        std::vector<tb::Imported> all;
        int skipped = 0;
        total = files.size();
        doneN = 0;
        for (auto& f : files)
        {
            if (threadShouldExit()) return;
            const auto m = tb::readMidiFile (f);
            if (m.ok) all.push_back (m.imp); else ++skipped;
            ++doneN;
        }
        auto res = tb::analyzeStyle (all, skipped);
        if (! threadShouldExit()) onDone (std::move (res));
    }
private:
    juce::Array<juce::File> files;
    std::atomic<int>& doneN;
    std::atomic<int>& total;
    std::function<void (tb::StyleResult)> onDone;
};

void collectMidi (const juce::File& dir, juce::Array<juce::File>& out)
{
    out.addArray (dir.findChildFiles (juce::File::findFiles, true, "*.mid;*.midi;*.MID;*.MIDI"));
}
} // namespace

void TB303Processor::startAnalysis (juce::Array<juce::File> files, bool manual)
{
    if (styleThread != nullptr) { styleThread->stopThread (4000); styleThread.reset(); }
    styleBusy = true;
    auto flag = alive;
    styleThread = std::make_unique<StyleJob> (std::move (files), styleDone, styleTotal, [this, flag, manual] (tb::StyleResult res)
    {
        juce::MessageManager::callAsync ([this, flag, manual, res]
        {
            if (! flag->load()) return;
            finishAnalysis (res, manual);
        });
    });
    styleThread->startThread();
}

void TB303Processor::finishAnalysis (const tb::StyleResult& res, bool manual)
{
    styleBusy = false;
    if (res.n == 0)
    {
        if (manual)
        {
            const juce::SpinLock::ScopedLockType lock (noticeLock);
            notice = { true, true, "NOTHING TO LEARN FROM",
                       "No usable .mid files were found" + (res.skipped ? " (" + juce::String (res.skipped) + " skipped: too few notes or not readable)" : juce::String()) + "." };
        }
        return;
    }
    if (! manual && styleManual) { styleReady = styleHas; return; }      // a style you made or loaded is never replaced by the quiet one
    style = res.style;
    styleHas = true;
    styleReady = true;
    styleManual = manual;
    if (manual)
    {
        styleOn = true;
        applyStyleFaders();
        const juce::SpinLock::ScopedLockType lock (noticeLock);
        notice = { true, false, "STYLE LEARNED", styleSummary() + (res.skipped ? " (" + juce::String (res.skipped) + " skipped.)" : juce::String()) + " STYLE is ON." };
    }
}

void TB303Processor::learnFromFiles (const juce::Array<juce::File>& files) { startAnalysis (files, true); }

void TB303Processor::learnFromFolder (const juce::File& dir)
{
    juce::Array<juce::File> f;
    collectMidi (dir, f);
    startAnalysis (f, true);
}

void TB303Processor::autoStyle()
{
    juce::Array<juce::File> f;
    const auto dir = saveFolder();
    if (dir.isDirectory()) collectMidi (dir, f);
    if (f.isEmpty()) return;
    startAnalysis (f, false);
}

bool TB303Processor::saveStyleTo (const juce::File& file) const
{
    return styleHas && file.replaceWithText (juce::String (tb::styleToText (style)));
}

bool TB303Processor::loadStyleFrom (const juce::File& file)
{
    tb::Style st;
    if (! tb::styleFromText (file.loadFileAsString().toStdString(), st)) return false;
    style = st; styleHas = styleOn = styleReady = styleManual = true;
    applyStyleFaders();
    return true;
}

juce::Array<juce::File> TB303Processor::savedStyleFiles() const
{
    auto a = saveFolder().findChildFiles (juce::File::findFiles, false, "style_*.txt");
    a.sort();
    return a;
}

// ------------------------------------------------------------------ saved loops

void TB303Processor::purgeHistory (const juce::String& name)
{
    int h2 = histPos;
    for (int k = (int) history.size() - 1; k >= 0; --k)
        if (history[(size_t) k].name == name) { history.erase (history.begin() + k); if (k <= h2) --h2; }
    histPos = juce::jlimit (-1, (int) history.size() - 1, h2);
}

juce::String TB303Processor::jukeStart()
{
    juce::Array<juce::File> list = saveFolder().findChildFiles (juce::File::findFiles, false, "*.mid;*.midi;*.MID;*.MIDI");
    struct Older { static int compareElements (const juce::File& a, const juce::File& b)
    { const auto x = a.getLastModificationTime(), y = b.getLastModificationTime(); return x < y ? -1 : (y < x ? 1 : 0); } } older;
    list.sort (older);
    if (list.isEmpty())
        return "Your \"TB303 Gen\" folder has no .mid files yet. Use SAVE first.";
    jukeList = list;
    jukeOn = true;
    jukeApply (0);
    if (jukeOn)
        if (auto* run = apvts.getParameter ("run")) run->setValueNotifyingHost (1.f);
    return jukeOn ? juce::String() : juce::String ("None of the files could be read.");
}

void TB303Processor::jukeStop() { jukeOn = false; jukeList.clear(); }

void TB303Processor::jukeApply (int i)
{
    while (jukeList.size() > 0)
    {
        i = ((i % jukeList.size()) + jukeList.size()) % jukeList.size();
        jukeBusy = true;
        const auto r = loadMidiFile (jukeList[i]);
        jukeBusy = false;
        if (r.ok) { jukeIdx = i; return; }
        jukeList.remove (i);                              // unreadable: skip it
    }
    jukeStop();
}

void TB303Processor::jukeStep (int d)
{
    if (jukeOn && jukeList.size() > 1) jukeApply (jukeIdx + d);
}

juce::String TB303Processor::jukeRemove()
{
    if (! jukeOn || jukeIdx >= jukeList.size()) return {};
    const auto f = jukeList[jukeIdx];
    const auto trash = saveFolder().getSiblingFile ("TB303Gen_DELETED");
    trash.createDirectory();
    const bool ok = f.moveFileTo (trash.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension()));
    purgeHistory (f.getFileName());
    jukeList.remove (jukeIdx);
    if (jukeList.isEmpty())
    {
        jukeStop();
        if (auto* run = apvts.getParameter ("run")) run->setValueNotifyingHost (0.f);
    }
    else jukeApply (jukeIdx);                             // the next one slides into this place
    return ok ? juce::String() : juce::String ("The file could not be moved: " + f.getFileName());
}

juce::String TB303Processor::jukeRename (const juce::String& added)
{
    if (! jukeOn || jukeIdx >= jukeList.size()) return {};
    auto clean = juce::String();
    for (auto ch : added.trim())
        clean << (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '-' || ch == '_' ? juce::String::charToString (ch) : (ch == ' ' ? "-" : ""));
    clean = clean.substring (0, 30);
    if (clean.isEmpty()) return {};
    const auto f = jukeList[jukeIdx];
    const auto dest = f.getSiblingFile (f.getFileNameWithoutExtension() + "_" + clean + f.getFileExtension());
    if (dest.exists() || ! f.moveFileTo (dest)) return "The file could not be renamed.";
    for (auto& h : history) if (h.name == f.getFileName()) h.name = dest.getFileName();
    if (loadedName == f.getFileName()) loadedName = dest.getFileName();
    jukeList.set (jukeIdx, dest);
    return {};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
