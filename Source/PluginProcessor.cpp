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

    auto pct = [] (const char* id, const char* name, float def, float lo = 0.f)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (lo, 1.f, 0.001f), def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.f)) + "%"; })
                .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.f; }));
    };

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "generate", 1 }, "Generate", false));
    p.push_back (pct ("notes",     "Notes",     0.75f));
    p.push_back (pct ("accents",   "Accents",   0.45f));
    p.push_back (pct ("slides",    "Slides",    0.31f));
    p.push_back (pct ("octave",    "Octave",    0.00f));
    p.push_back (pct ("variation", "Variation", 0.40f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bars", 1 }, "Bars",
                    NormalisableRange<float> (4.f, 64.f, 4.f), 8.f,
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
    p.push_back (pct ("volume", "Volume", 0.15f));

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
    lfoMask[0] = 1u << tb::dCutoff;                      // LFO 1 -> cutoff, LFO 2 -> env mod to start with
    lfoMask[1] = 1u << tb::dEnvMod;

    generatePattern();
    apvts.addParameterListener ("generate", this);
    apvts.addParameterListener ("root", this);
    apvts.addParameterListener ("scale", this);
    for (auto* id : kGenIds) apvts.addParameterListener (id, this);
}

TB303Processor::~TB303Processor()
{
    alive->store (false);
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
            juce::Random rng;
            const int ri = (int) param ("root"), si = (int) param ("scale");
            const int nr = ri == 0 ? 36 + rng.nextInt (12) : 36 + (ri - 1);
            const int ns = si == 0 ? rng.nextInt ((int) tb::scales().size()) : si - 1;
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
    voice.prepare (sampleRate);
    kick.prepare (sampleRate);
    hat.prepare (sampleRate);
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

void TB303Processor::generatePattern()
{
    juce::Random rng;

    const int rootIdx = (int) param ("root");
    const int root = rootIdx == 0 ? 36 + rng.nextInt (12) : 36 + (rootIdx - 1);
    const int scaleIdx = (int) param ("scale");
    const int sc = scaleIdx == 0 ? rng.nextInt ((int) tb::scales().size()) : scaleIdx - 1;

    auto line = std::make_unique<BassLine>();
    line->seed = tb::makeSeed (rng);
    line->hasSeed = true;
    line->root = root;
    line->scale = sc;
    line->pat = tb::resolve (line->seed, toParams (currentGen()), root, tb::scales()[(size_t) sc]);

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

void TB303Processor::previousPattern() { showHistory (histPos - 1); }
void TB303Processor::nextPattern()     { showHistory (histPos + 1); }

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
    const auto pat = tb::resolve (shared.seed, toParams (currentGen()), root, tb::scales()[(size_t) scale]);
    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        shared.pat = pat;
        shared.root = root;
        shared.scale = scale;
        immediateSwap = true;
    }
    ++patternVersion;

    if (histPos >= 0 && histPos < (int) history.size())      // PREVIOUS/NEXT will bring back the tweaked state
    {
        auto& h = history[(size_t) histPos];
        h.line.pat = pat;
        h.line.root = root;
        h.line.scale = scale;
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
           + tb::scaleNames()[lastScale.load()].replace (" ", "-") + "_"
           + juce::String (juce::roundToInt (lastBpm.load())) + "bpm_"
           + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".mid";
}

juce::String TB303Processor::suggestedMidiName() const { return midiFileName(); }

juce::File TB303Processor::saveFolder() const
{
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
    LoadResult res;
    const auto m = tb::readMidiFile (file);
    if (! m.ok)
    {
        res.error = m.error.isEmpty() ? juce::String ("Could not read this file.") : m.error;
        return res;
    }

    const auto& r = m.imp;
    auto line = std::make_unique<BassLine>();
    line->pat = r.pat;
    line->hasSeed = false;                                // no dice: the generator faders leave a loaded line alone
    line->root = juce::jlimit (36, 47, r.root);
    line->scale = r.scale;

    const bool bpmOk = r.bpm >= 60.0 && r.bpm <= 200.0;
    if (bpmOk)
        if (auto* t = apvts.getParameter ("tempo"))
            t->setValueNotifyingHost (t->convertTo0to1 ((float) std::round (r.bpm)));

    loadedName = file.getFileName();
    clearUndo();
    lastRoot = line->root;
    lastScale = line->scale;
    setRootScaleParams (line->root, line->scale);
    commit (*line, false);
    pushHistory (*line);

    res.ok = true;
    res.text = juce::String (r.count) + " notes over " + juce::String (r.bars) + (r.bars > 1 ? " bars" : " bar") + " in "
               + juce::MidiMessage::getMidiNoteName (r.root, true, false, 4) + " " + tb::scaleNames()[r.scale]
               + (bpmOk ? " at " + juce::String (juce::roundToInt (r.bpm)) + " bpm" : juce::String())
               + ". The faders only reshape generated lines, so press GENERATE to go back to those.";
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
                audioLine.pat = tb::resolve (audioLine.seed, gp, audioLine.root, tb::scales()[(size_t) audioLine.scale]);
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

    // internal synth renders the very same events; its modulated controls are re-read every 32 samples
    if (param ("synth") > 0.5f)
    {
        const bool square = param ("wave") > 0.5f;
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
                voice.setParams (mr.eff[tb::dCutoff], mr.eff[tb::dReso], mr.eff[tb::dEnvMod], mr.eff[tb::dDecay], square, mr.eff[tb::dVolume]);
                voice.setDetuneCents (mr.eff[tb::dPitch]);
                for (; i < m; ++i)
                {
                    const float s = voice.process();
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
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        int ti = 0;
        for (int i = 0; i < n; ++i)
        {
            while (ti < nt && trig[(size_t) ti].pos <= i)
            {
                if (trig[(size_t) ti].isKick) kick.trigger(); else hat.trigger();
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
    if (auto* run = apvts.getParameter ("run"))          // a project always opens stopped
        run->setValueNotifyingHost (0.f);

    const auto masks = juce::StringArray::fromTokens (vt.getProperty ("lfoMasks").toString(), ",", "");
    if (masks.size() == 4)
        for (int i = 0; i < 4; ++i)
            lfoMask[(size_t) i] = (std::uint32_t) masks[i].getLargeIntValue();

    const auto tokens = juce::StringArray::fromTokens (vt.getProperty ("pattern").toString(), ",", "");
    const int len = juce::jlimit (tb::kStepsPerBar, tb::kMaxSteps, (int) vt.getProperty ("patLen", 256));
    const int root = juce::jlimit (36, 47, (int) vt.getProperty ("lastRoot", 36));
    const int scale = juce::jlimit (0, (int) tb::scales().size() - 1, (int) vt.getProperty ("lastScale", 0));
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

        juce::MemoryBlock mb;
        line->hasSeed = mb.fromBase64Encoding (vt.getProperty ("seed").toString()) && mb.getSize() == sizeof (tb::Seed);
        if (line->hasSeed) std::memcpy (&line->seed, mb.getData(), sizeof (tb::Seed));

        clearUndo();
        commit (*line, false);
        pushHistory (*line);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
