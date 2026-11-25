#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include "AcidVoice.h"
#include "Drums.h"
#include "Fx.h"
#include "Pattern.h"
#include "Style.h"
#include "Modulation.h"

// a line plus the dice behind it
struct BassLine
{
    tb::Pattern pat;
    tb::Seed seed {};
    bool hasSeed = false;
    int root = 36, scale = 0;
    std::uint16_t cmask = 0;                             // intervals of the custom (KEYS) scale, when scale == tb::kCustomScale
    bool useStyle = false;                               // generated with STYLE on: the dice are read through sty
    tb::Style sty {};
};

class TB303Processor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
{
public:
    TB303Processor();
    ~TB303Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BASSLINER303"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- UI-facing
    void generatePattern();
    void previousPattern();
    void nextPattern();
    int historyIndex() const { return histPos; }
    int historyCount() const { return (int) history.size(); }
    tb::Pattern getPatternCopy() const;                  // the line on screen / exported
    tb::Pattern getPlayingCopy() const;                  // the line the audio thread is playing right now
    juce::String getLoadedName() const { return loadedName; }
    int currentRoot() const { return lastRoot.load(); }
    int currentScale() const { return lastScale.load(); }

    // ---- KEYS: pick the notes on a keyboard; they replace root + scale for GENERATE
    bool keysActive() const { return keysOn && keysPcs != 0; }
    int keysRootPc() const { return keysRoot; }
    std::uint16_t keysPcMask() const { return keysPcs; }
    void setKeys (int rootPc, std::uint16_t pcMask);     // on + re-reads the line on screen in these notes (undoable)
    void keysOff();

    // ---- STYLE: learn the taste of a folder of .mid files
    bool styleLoaded() const { return styleHas; }
    bool styleIsOn() const { return styleOn && styleHas; }
    bool styleIsReady() const { return styleHas && styleReady; }
    bool styleIsBusy() const { return styleBusy.load(); }
    juce::String styleBusyText() const;                  // "READING 12 / 40"
    juce::String styleSummary() const;
    void setStyleOn (bool on);
    void forgetStyle();
    void learnFromFiles (const juce::Array<juce::File>& files);          // runs in the background; a notice appears when done
    void learnFromFolder (const juce::File& dir);
    bool saveStyleTo (const juce::File& file) const;
    bool loadStyleFrom (const juce::File& file);
    juce::Array<juce::File> savedStyleFiles() const;
    struct Notice { bool has = false, error = false; juce::String title, text; };
    Notice takeNotice();                                 // one message from the background work, for the editor to show

    // ---- saved loops: plays the .mid files of the save folder, oldest first
    bool jukeIsOn() const { return jukeOn; }
    int jukeCount() const { return jukeList.size(); }
    juce::String jukeFileName() const { return jukeOn && jukeIdx < jukeList.size() ? jukeList[jukeIdx].getFileName() : juce::String(); }
    juce::String jukeStart();                            // "" = playing, otherwise why not
    void jukeStop();
    void jukeStep (int d);
    juce::String jukeRemove();                           // moves the file on screen to TB303Gen_DELETED; "" = ok
    juce::String jukeRename (const juce::String& added); // adds a name to the end of the file name; "" = ok

    // tweaker mode is always on: every generator fader reshapes the current line live, with undo/redo per gesture
    void beginTweakGesture();
    void endTweakGesture();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undoTweak();
    void redoTweak();

    // files
    juce::File saveFolder() const;                       // ~/Music/TB303 Gen
    juce::String suggestedMidiName() const;
    bool saveMidiTo (const juce::File& file);
    bool saveToFolder (juce::File& written);
    juce::File makeTempMidiFile();                       // .mid of the current line, for dragging out of the window
    struct LoadResult { bool ok = false; juce::String error, text; };
    LoadResult loadMidiFile (const juce::File& file);

    // LFOs: shape / rate / depth are host parameters; the destination set is plain state
    void setLfoMask (int lfo, std::uint32_t mask) { lfoMask[(size_t) lfo] = mask; }
    std::uint32_t getLfoMask (int lfo) const { return lfoMask[(size_t) lfo].load(); }
    bool lfoContrib (int lfo, int dest, float& out) const;   // normalised offset the LFO adds now (false = not routed)
    float lfoDepth (int lfo) const { return rawLfo[(size_t) lfo][2]->load(); }

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<double> uiStepPos { 0.0 }, uiPosStamp { 0.0 }, uiBpm { 130.0 };
    std::atomic<bool>   uiHasPending { false };          // a new pattern is waiting for the bar line
    std::atomic<bool>   uiPlaying { false };
    std::array<std::atomic<float>, 4> uiLfoVal {}, uiLfoPhase {};
    std::atomic<float> editorZoom { 0.85f };           // editor size / 1100 px, remembered with the project

private:
    // "Generate" is a momentary host parameter: map it in Ableton (Configure / MIDI map) to a pad or key.
    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;                   // coalesces generator-fader moves into one re-read of the dice
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    using Gen = std::array<float, 8>;                    // notes, accents, slides, octave, variation, bars, root, scale
    Gen currentGen() const;
    static tb::GenParams toParams (const Gen& g) { return { g[0], g[1], g[2], g[3], g[4], (int) std::lround (g[5]) }; }
    void applyGen (const Gen& g);
    void setRootScaleParams (int root, int scale);
    void retweak();
    void clearUndo() { undoStack.clear(); redoStack.clear(); inGesture = false; }
    void commit (const BassLine& line, bool immediate);
    void pushHistory (const BassLine& line);
    void showHistory (int index);
    void readMod (std::array<float, tb::kDests>& base, std::array<tb::LfoSettings, 4>& lfo) const;
    void generateEvents (juce::MidiBuffer& midi, double b0, double b1, double beatsPerSample, int numSamples);
    void flushNotes (juce::MidiBuffer& midi, int samplePos);
    juce::String midiFileName() const;
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // line shared between UI and audio thread
    BassLine shared;
    mutable juce::SpinLock patternLock;
    std::atomic<int> patternVersion { 0 };
    int audioVersion = -1;
    std::atomic<bool> immediateSwap { false };           // newest line should replace the playing one at once
    BassLine pendingLine, audioLine;
    bool hasPending = false, audioHasPattern = false, publishDirty = false;
    long long origin = 0;                                // absolute step at which the playing pattern's step 0 sits
    mutable juce::SpinLock uiLock;
    tb::Pattern uiPlayingPattern {};

    // sequencer state (audio thread)
    struct Pending { double beat; int pitch; };
    std::array<Pending, 16> pend {};
    int np = 0;
    std::array<int, 128> sounding {};
    double sr = 44100.0, internalBeat = 0.0, expectedBeat = 0.0, lfoFree = 0.0;
    bool wasPlaying = false;
    std::atomic<double> lastBpm { 130.0 };
    std::array<float, tb::kDests> blockBase {};
    std::array<tb::LfoSettings, 4> blockLfo {};

    std::array<std::atomic<float>*, tb::kDests> rawDest {};                  // null for pitch / master
    std::array<std::array<std::atomic<float>*, 3>, 4> rawLfo {};             // shape, rate, depth
    std::array<std::atomic<std::uint32_t>, 4> lfoMask {};

    AcidVoice voice;
    Kick606 kick;
    Hat606 hat;
    tb::DistFx distFx;
    tb::DelayFx delayFx;
    tb::ReverbFx reverbFx;
    float smDry = 1.f, smWet = 0.f, smDir = 1.f, smSend = 1.f, smDel = 0.f, smRev = 0.f, smRevOut = 0.f;   // smoothed mixer gains (audio thread)

    // previously generated basslines (message thread only)
    struct HistItem { BassLine line; Gen gen; juce::String name; };
    std::vector<HistItem> history;
    int histPos = -1;
    std::atomic<int> lastRoot { 36 }, lastScale { 0 };
    std::atomic<std::uint16_t> lastMask { 0 };
    std::vector<int> scaleBuf;                           // custom-scale intervals on the audio thread (reserved in prepareToPlay)

    bool keysOn = false;
    int keysRoot = 0;
    std::uint16_t keysPcs = 0;
    std::uint16_t keysIvMask() const;

    tb::Style style {};
    bool styleHas = false, styleOn = false, styleReady = false, styleManual = false;
    std::atomic<bool> styleBusy { false };
    std::atomic<int> styleDone { 0 }, styleTotal { 0 };
    std::unique_ptr<juce::Thread> styleThread;
    Notice notice;
    juce::SpinLock noticeLock;
    void startAnalysis (juce::Array<juce::File> files, bool manual);
    void finishAnalysis (const tb::StyleResult& res, bool manual);
    void applyStyleFaders();
    void autoStyle();
    int pickScale (int scaleIdx, juce::Random& rng) const;

    bool jukeOn = false, jukeBusy = false;
    juce::Array<juce::File> jukeList;
    int jukeIdx = 0;
    void jukeApply (int i);
    void purgeHistory (const juce::String& name);
    juce::String loadedName;

    std::vector<Gen> undoStack, redoStack;
    Gen gestureStart {};
    bool inGesture = false;
    bool applyingGen = false;                            // generator faders being put back by code
    bool applyingRS = false;                             // guards root/scale combo echo while undoing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TB303Processor)
};
