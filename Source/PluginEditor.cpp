#include "PluginEditor.h"

TB303Editor::TB303Editor (TB303Processor& p)
    : AudioProcessorEditor (&p), proc ((ui::setHue (0.f), p)), roll (p),
      groove   (p, "GROOVE",   "slides",  "accents", tb::dSlides, tb::dAccents, ui::green,  "SLIDES",  "ACCENTS", true),
      filter   (p, "FILTER",   "cutoff",  "reso",    tb::dCutoff, tb::dReso,    ui::green,  "CUTOFF",  "RESO",    false),
      envelope (p, "ENVELOPE", "envmod",  "decay",   tb::dEnvMod, tb::dDecay,   ui::violet, "ENV MOD", "DECAY",   false),
      notes     (p, "NOTES",     "notes",     tb::dNotes,      ui::green,  true),
      octave    (p, "OCTAVE",    "octave",    tb::dOctave,     ui::cyan,   true),
      delTime   (p, "DLY TIME",  "deltime",   -1,              ui::cyan,   false),
      revAmt    (p, "REV AMT",   "revamt",    -1,              ui::violet, false),
      distSend  (p, "D.SEND",    "distdrive", -1,              ui::pink,   false),
      distColor (p, "COLOR",     "distcolor", -1,              ui::silver, false),
      bars      (p, "BARS",      "bars",      -1,              ui::cyan,   true),
      variation (p, "VARIATION", "variation", tb::dVariation,  ui::violet, true),
      bpm       (p, "",          "tempo",     -1,              ui::silver, false),
      mixBass   (p, "BASS",      "volume",    tb::dVolume,     ui::green,  false),
      mixKick   (p, "KICK",      "kickvol",   tb::dKick,       ui::cyan,   false),
      mixHat    (p, "HATS",      "hatvol",    tb::dHat,        ui::violet, false),
      mixDel    (p, "DELAY",     "delsend",   -1,              ui::cyan,   false),
      mixRev    (p, "REVERB",    "revsend",   -1,              ui::violet, false),
      mixDist   (p, "DIST",      "distlevel", -1,              ui::pink,   false)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (content);
    content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    content.onDown  = [this] (juce::Point<float> pt)
    {
        const auto q = pt.toInt();
        if (focus == fNone || focus == fGen)   if (genTitleR.contains (q))   { toggleFocus (fGen);   return; }
        if (focus == fNone || focus == fSynth) if (synthTitleR.contains (q)) { toggleFocus (fSynth); return; }
    };

    setResizable (true, true);
    setResizeLimits (480, 400, 8000, 6000);                 // any shape: the layout adapts (see layoutAll), so full screen uses every pixel

    content.addAndMakeVisible (roll);
    for (auto* c : { &groove, &filter, &envelope }) content.addAndMakeVisible (*c);
    for (auto* f : { &bars, &variation, &bpm }) content.addAndMakeVisible (*f);
    for (auto* h : { &delTime, &revAmt, &distSend, &distColor }) content.addAndMakeVisible (*h);
    for (auto* v : { &mixBass, &mixKick, &mixHat, &mixDel, &mixRev, &mixDist }) content.addAndMakeVisible (*v);
    {   // touching a fader of an effect that is OFF switches it on (the horizontal faders do not)
        auto wake = [this] (const char* id)
        {
            if (auto* p = proc.apvts.getParameter (id))
                if (p->getValue() < 0.5f) { p->beginChangeGesture(); p->setValueNotifyingHost (1.f); p->endChangeGesture(); }
        };
        mixDel.onGestureStart  = [wake] { wake ("delon"); };
        mixRev.onGestureStart  = [wake] { wake ("revon"); };
        mixDist.onGestureStart = [wake] { wake ("diston"); };
    }
    content.addAndMakeVisible (notes); content.addAndMakeVisible (octave);
    hookGestures (groove); hookGestures (notes); hookGestures (octave);
    hookGestures (bars);    hookGestures (variation);

    for (int i = 0; i < 4; ++i)
    {
        lfos[(size_t) i] = std::make_unique<LfoCard> (proc, i);
        lfos[(size_t) i]->onTitleClick = [this] (int n) { toggleFocus (fLfo0 + n); };
        content.addAndMakeVisible (*lfos[(size_t) i]);
    }

    // BPM lock: locked by default so the tempo fader can't be nudged by accident
    bpm.locked = true;
    lockBtn.setToggleState (true, juce::dontSendNotification);
    lockBtn.onClick = [this] { bpm.locked = lockBtn.getToggleState(); bpm.repaint(); };
    content.addAndMakeVisible (lockBtn);

    setupCombo (rootBox, "root");
    setupCombo (scaleBox, "scale");
    setupCombo (waveBox, "wave");
    rootAtt  = std::make_unique<ComboAtt> (proc.apvts, "root", rootBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);
    waveAtt  = std::make_unique<ComboAtt> (proc.apvts, "wave", waveBox);
    for (auto* cb : { &rootBox, &scaleBox, &waveBox }) cb->setWantsKeyboardFocus (false);
    rootBox.setVisible (false);                                     // ROOT / SCALE are button rows now; the combos only carry the host parameters
    scaleBox.setVisible (false);

    {   // ROOT: random + 12 notes; SCALE: random + the five scales
        static const char* const rn[13] = { "R", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 13; ++i)
        {
            auto b = std::make_unique<NButton> (rn[i], ui::cyan);
            b->onClick = [this, i] { if (auto* p = proc.apvts.getParameter ("root")) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) i)); p->endChangeGesture(); } };
            content.addAndMakeVisible (*b);
            rootBtns.push_back (std::move (b));
        }
        static const char* const sn[6] = { "R", "MINOR PENT", "PHRYGIAN", "NAT MINOR", "PHRYG DOM", "HARM MINOR" };
        for (int i = 0; i < 6; ++i)
        {
            auto b = std::make_unique<NButton> (sn[i], ui::violet);
            b->onClick = [this, i] { if (auto* p = proc.apvts.getParameter ("scale")) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) i)); p->endChangeGesture(); } };
            content.addAndMakeVisible (*b);
            scaleBtns.push_back (std::move (b));
        }
    }

    for (auto* b : { &styleBtn, &keysBtn, &heartBtn, &renBtn }) content.addAndMakeVisible (*b);
    renBtn.setVisible (false);
    styleBtn.onClick = [this] { stylePage.open(); };
    keysBtn.onClick  = [this] { keysPress(); };
    heartBtn.onClick = [this] { heartPress(); };
    renBtn.onClick   = [this] { renamePress(); };
    keysPage.onUse = [this] (int r, unsigned m) { proc.setKeys (r, (std::uint16_t) m); refreshUndo(); };
    keysPage.onOff = [this] { proc.keysOff(); };
    stylePage.onBack = [this] { grabKeyboardFocus(); };
    stylePage.onSavedFolder = [this] { proc.learnFromFolder (proc.saveFolder()); };
    stylePage.onFolder = [this] { styleFromFiles (true); };
    stylePage.onFiles  = [this] { styleFromFiles (false); };
    stylePage.onSave   = [this] { saveStylePress(); };
    stylePage.onLoad   = [this] { loadStylePress(); };

    for (auto* b : { &runBtn, &muteBass, &muteKick, &muteHat, &onDel, &onRev, &onDist, &soloBass, &soloKick, &soloHat, &soloDel, &soloRev, &soloDist })
    {
        b->setClickingTogglesState (true);
        content.addAndMakeVisible (*b);
    }
    {
        struct B { NButton* b; const char* id; };
        for (const auto& m : { B { &muteBass, "mutebass" }, B { &muteKick, "mutekick" }, B { &muteHat, "mutehat" },
                               B { &onDel, "delon" }, B { &onRev, "revon" }, B { &onDist, "diston" },
                               B { &soloBass, "solobass" }, B { &soloKick, "solokick" }, B { &soloHat, "solohat" },
                               B { &soloDel, "solodel" }, B { &soloRev, "solorev" }, B { &soloDist, "solodist" } })
            mixAtt.push_back (std::make_unique<ButtonAtt> (proc.apvts, m.id, *m.b));
    }
    runAtt   = std::make_unique<ButtonAtt> (proc.apvts, "run", runBtn);

    generateBtn.primary = true;
    generateBtn.pulse = true;
    generateBtn.onClick = [this]
    {
        proc.generatePattern(); refreshUndo();
        if (firstGenerate)                                          // the first GENERATE after the editor opens also starts the preview loop
        {
            firstGenerate = false;
            if (auto* run = proc.apvts.getParameter ("run"))
                if (run->getValue() < 0.5f) run->setValueNotifyingHost (1.f);
        }
    };
    previousBtn.onClick = [this] { proc.previousPattern(); refreshUndo(); };
    nextBtn.onClick     = [this] { proc.nextPattern(); refreshUndo(); };
    loadBtn.onClick     = [this] { doLoad(); };
    downloadBtn.onClick = [this] { doDownload(); };
    folderBtn.onClick   = [this] { doSaveToFolder(); };
    undoBtn.onClick     = [this] { proc.undoTweak(); refreshUndo(); };
    redoBtn.onClick     = [this] { proc.redoTweak(); refreshUndo(); };
    closeBtn.onClick    = [this] { setFocus (fNone); };

    content.addAndMakeVisible (zoomBar);
    content.addAndMakeVisible (titleHit);
    titleHit.onClick = [this] (int clicks)
    {
        if (clicks >= 3) rainbow = ! rainbow;                       // triple-click: start / stop the slow rainbow
        else if (! rainbow) shiftColours (false);                   // single click: a step round the wheel
    };
    roll.onEgg = [this] { rainbow = false; shiftColours (true); };
    content.addAndMakeVisible (fitBtn);
    content.addAndMakeVisible (fullBtn);
    fullBtn.setVisible (juce::JUCEApplicationBase::isStandaloneApp());
    fullBtn.onClick = [this] { toggleFullScreen(); };
    zoomBar.onChange = [this] (float z) { applyZoom (z); };
    fitBtn.onClick   = [this] { fitToScreen(); };

    for (auto* b : { &generateBtn, &previousBtn, &nextBtn, &loadBtn, &downloadBtn, &folderBtn, &undoBtn, &redoBtn, &closeBtn })
        content.addAndMakeVisible (*b);
    closeBtn.setVisible (false);

    content.addChildComponent (keysPage);
    content.addChildComponent (stylePage);
    content.addChildComponent (modal);
    modal.setVisible (false);

    setWantsKeyboardFocus (true);
    refreshUndo();
    {   // opens already fitted to the main screen
        float z = 0.85f;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            z = juce::jmin ((float) (d->userArea.getWidth() - 24) / (float) kW, (float) (d->userArea.getHeight() - 96) / (float) kH);
        z = std::floor (juce::jlimit (ZoomBar::kMin, ZoomBar::kMax, z) * 100.f) / 100.f;
        setSize (juce::roundToInt ((float) kW * z), juce::roundToInt ((float) kH * z));
    }
    //                                 // last: resized() needs every child to exist
    startTimerHz (60);
}

TB303Editor::~TB303Editor()
{
    stopTimer();
    if (hookedTop != nullptr) hookedTop->removeKeyListener (&keyHook);
    chooser.reset();
    setLookAndFeel (nullptr);
}

void TB303Editor::hookGestures (XYCard& c)
{
    c.onGestureStart = [this] { proc.beginTweakGesture(); };
    c.onGestureEnd   = [this] { proc.endTweakGesture(); refreshUndo(); };
}

void TB303Editor::hookGestures (HFader& f)
{
    f.onGestureStart = [this] { proc.beginTweakGesture(); };
    f.onGestureEnd   = [this] { proc.endTweakGesture(); refreshUndo(); };
}

void TB303Editor::hookGestures (VFader& f)
{
    f.onGestureStart = [this] { proc.beginTweakGesture(); };
    f.onGestureEnd   = [this] { proc.endTweakGesture(); refreshUndo(); };
}

void TB303Editor::setupCombo (juce::ComboBox& box, const char* paramId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    content.addAndMakeVisible (box);
}

void TB303Editor::toggleFullScreen()
{
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        dw->setFullScreen (! dw->isFullScreen());
}

void TB303Editor::hookKeys()
{
    if (! juce::JUCEApplicationBase::isStandaloneApp()) return;          // inside a DAW the host keeps its own shortcuts
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == hookedTop.getComponent()) return;
    if (hookedTop != nullptr) hookedTop->removeKeyListener (&keyHook);
    keyHook.fn = [this] (const juce::KeyPress& k) { return keyPressed (k); };
    top->addKeyListener (&keyHook);
    hookedTop = top;
}

void TB303Editor::refreshUndo()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    const bool juke = proc.jukeIsOn();
    previousBtn.setEnabled (juke ? proc.jukeCount() > 1 : proc.historyIndex() > 0);
    nextBtn.setEnabled (juke ? proc.jukeCount() > 1 : proc.historyIndex() < proc.historyCount() - 1);
}

bool TB303Editor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (modal.isVisible()) modal.close();
        else if (keysPage.isVisible()) keysPage.setVisible (false);
        else if (stylePage.isVisible()) stylePage.setVisible (false);
        else if (focus != fNone) setFocus (fNone);
        else if (fullBtn.isVisible())
            if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>()) if (dw->isFullScreen()) dw->setFullScreen (false);
        return true;
    }
    if (! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown() && ! modal.isVisible() && ! keysPage.isVisible() && ! stylePage.isVisible())
    {
        if (k == juce::KeyPress::spaceKey)  { runBtn.setToggleState (! runBtn.getToggleState(), juce::sendNotificationSync); return true; }
        if (k == juce::KeyPress::returnKey) { generateBtn.onClick(); return true; }
        if (k == juce::KeyPress::leftKey)   { previousBtn.onClick(); return true; }
        if (k == juce::KeyPress::rightKey)  { nextBtn.onClick();     return true; }
        if (k == juce::KeyPress::downKey)   { doDownload();          return true; }
        if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F')
            if (fullBtn.isVisible()) { toggleFullScreen(); return true; }
    }
    if (k.getModifiers().isCommandDown())                           // Cmd/Ctrl  +  -  0
    {
        const auto c = k.getTextCharacter();
        const float z = scaleNow;
        if (c == '+' || c == '=') { applyZoom (z + 0.1f); return true; }
        if (c == '-' || c == '_') { applyZoom (z - 0.1f); return true; }
        if (c == '0')             { applyZoom (1.0f);     return true; }
    }
    return false;
}

// ------------------------------------------------------------------ files

void TB303Editor::showSaved (const juce::File& f, bool revealFolder)
{
    std::vector<ModalOverlay::Btn> b;
    if (revealFolder)
    {
        const auto folder = f.getParentDirectory();
        b.push_back ({ "OPEN FOLDER", [folder] { folder.revealToUser(); }, true });
        b.push_back ({ "OK", nullptr, false });
    }
    else
        b.push_back ({ "OK", nullptr, true });
    modal.show (revealFolder ? "SAVED TO FOLDER" : "MIDI SAVED", f.getFileName(),
                "The bassline is in " + f.getParentDirectory().getFullPathName() + ". Bass only, no drums.", false, std::move (b));
}

void TB303Editor::doLoad()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a MIDI file", proc.saveFolder().exists() ? proc.saveFolder()
                                                   : juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
    juce::Component::SafePointer<TB303Editor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto r = safe->proc.loadMidiFile (f);
        if (r.ok) safe->modal.show ("MIDI LOADED", f.getFileName(), r.text, false, { { "OK", nullptr, true } });
        else      safe->modal.show ("CAN'T LOAD FILE", f.getFileName(), r.error, true, { { "OK", nullptr, true } });
        safe->refreshUndo();
    });
}

void TB303Editor::doDownload()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the bassline as MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (proc.suggestedMidiName()),
                                                   "*.mid");
    juce::Component::SafePointer<TB303Editor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
        if (safe->proc.saveMidiTo (f)) safe->showSaved (f, false);
        else safe->modal.show ("COULD NOT SAVE", f.getFileName(), "Writing the file failed.", true, { { "OK", nullptr, true } });
    });
}

void TB303Editor::doSaveToFolder()
{
    juce::File f;
    if (proc.saveToFolder (f)) showSaved (f, true);
    else modal.show ("COULD NOT SAVE", f.getFileName(), "Writing the file failed.", true, { { "OK", nullptr, true } });
}

// ------------------------------------------------------------------ focus (full-screen sections)

std::vector<int> TB303Editor::linkedLfos (int panelFocus) const
{
    std::uint32_t mine = 0;
    if (panelFocus == fGen)
        mine = tb::kGenMask;
    else
        for (int d : { (int) tb::dCutoff, (int) tb::dReso, (int) tb::dEnvMod, (int) tb::dDecay, (int) tb::dPitch, (int) tb::dMaster })
            mine |= 1u << d;

    std::vector<int> out;
    for (int l = 0; l < 4; ++l)
        if (proc.getLfoMask (l) & mine) out.push_back (l);
    return out;
}

void TB303Editor::setFocus (int f)
{
    focus = f;
    layoutAll();
    content.repaint();
}

void TB303Editor::layoutAll()
{
    if (lfos[3] == nullptr) return;
    using R = juce::Rectangle<int>;
    const int pw = W - 40;                                    // usable width; everything below stretches with it
    undoBtn.setBounds (W - 204, 10, 92, 38);
    redoBtn.setBounds (W - 110, 10, 92, 38);
    fullBtn.setBounds (W - 204 - 76, 10, 70, 38);
    fitBtn.setBounds (W - 204 - 76 - (fullBtn.isVisible() ? 76 : 0), 10, 68, 38);
    zoomBar.setBounds (fitBtn.getX() - 266, 12, 256, 34);
    titleHit.setBounds (20, 8, 232, 42);
    rollR = { 20, 58, pw, 132 };
    roll.setBounds (rollR);
    infoR = { 22, 196, pw - 4, 18 };

    {   // action buttons share the row like flex items
        NButton* row[7] = { &generateBtn, &previousBtn, &nextBtn, &runBtn, &loadBtn, &downloadBtn, &folderBtn };
        const float w[7] = { 1.0f, 1.0f, 0.78f, 1.3f, 1.0f, 1.45f, 1.45f };
        float sum = 0.f;
        for (float x : w) sum += x;
        const float avail = (float) pw - 6.f * 8.f;
        float x = 20.f;
        for (int i = 0; i < 7; ++i)
        {
            const float bw = avail * w[i] / sum;
            row[i]->setBounds ((int) x - 4, 220, (int) bw + 8, 46);
            x += bw + 8.f;
        }
    }

    const bool normal = focus == fNone, fgen = focus == fGen, fsyn = focus == fSynth, flfo = focus >= fLfo0;
    const int top = 322;
    rowR2 = { 20, 272, pw, 40 };
    styleBtn.setBounds (16, 272, 210, 42);
    keysBtn.setBounds (234, 272, 140, 42);
    heartBtn.setBounds (382, 272, 330, 42);
    renBtn.setBounds (720, 272, 140, 42);
    styleDotR = { 200, 286, 12, 12 };
    keysPage.setBounds (0, 0, W, H);
    stylePage.setBounds (0, 0, W, H);

    genPanel = synthPanel = mixPanel = setupPanel = lfoHeader = linkedLabel = waveLabel = {};
    genTitleR = synthTitleR = {};

    // --- which LFO cards go where
    std::vector<int> shown;
    R lfoRowArea;
    bool lfoGrid = false;                                     // wide window: the four LFOs sit 2 x 2 in a right-hand column
    if (normal && wide)
    {
        const int rw = juce::jmax (540, pw * 32 / 100), lw = pw - rw - 12, half = (lw - 12) / 2;
        genPanel   = { 20, top, half, 252 };
        synthPanel = { 20 + half + 12, top, lw - half - 12, 252 };
        mixPanel   = { 20, top + 264, lw, 236 };
        setupPanel = { 20, top + 512, lw, 130 };
        lfoHeader  = { 20 + lw + 12, top, rw, 16 };
        lfoRowArea = { 20 + lw + 12, top + 20, rw, H - top - 20 - 12 };
        lfoGrid = true;
        shown = { 0, 1, 2, 3 };
    }
    else if (normal)
    {
        const int half = (pw - 20) / 2;
        genPanel   = { 20, top, half, 252 };
        synthPanel = { 20 + half + 20, top, pw - half - 20, 252 };
        mixPanel   = { 20, top + 264, pw, 236 };              // full row: strips on the left, delay / reverb / distortion faders on the right
        setupPanel = { 20, top + 512, pw, 130 };
        lfoHeader  = { 20, top + 654, pw, 16 };
        lfoRowArea = { 20, top + 674, pw, juce::jmax (262, H - (top + 674) - 12) };
        shown = { 0, 1, 2, 3 };
    }
    else if (fgen || fsyn)
    {
        shown = linkedLfos (focus);
        const int h = shown.empty() ? H - top - 12 : juce::jmax (330, (H - top) * 40 / 100);
        (fgen ? genPanel : synthPanel) = { 20, top, pw, h };
        if (! shown.empty())
        {
            linkedLabel = { 20, top + h + 10, 400, 14 };
            lfoRowArea = { 20, top + h + 30, pw, H - (top + h + 30) - 12 };
        }
    }
    else
    {
        shown = { focus - fLfo0 };
        lfoRowArea = { 20, top, pw, H - top - 12 };
    }

    // --- generator + synth panels
    const bool showGen = normal || fgen, showSyn = normal || fsyn;
    for (auto* c : { (juce::Component*) &groove, (juce::Component*) &notes, (juce::Component*) &octave, (juce::Component*) &bars, (juce::Component*) &variation })
        c->setVisible (showGen);
    for (auto* c : { (juce::Component*) &filter, (juce::Component*) &envelope, (juce::Component*) &waveBox })
        c->setVisible (showSyn);
    for (auto& b : rootBtns)  b->setVisible (normal);
    for (auto& b : scaleBtns) b->setVisible (normal);
    for (auto* c : { (juce::Component*) &mixBass, (juce::Component*) &mixKick, (juce::Component*) &mixHat, (juce::Component*) &mixDel, (juce::Component*) &mixRev, (juce::Component*) &mixDist,
                     (juce::Component*) &muteBass, (juce::Component*) &muteKick, (juce::Component*) &muteHat, (juce::Component*) &onDel, (juce::Component*) &onRev, (juce::Component*) &onDist,
                     (juce::Component*) &soloBass, (juce::Component*) &soloKick, (juce::Component*) &soloHat, (juce::Component*) &soloDel, (juce::Component*) &soloRev, (juce::Component*) &soloDist,
                     (juce::Component*) &delTime, (juce::Component*) &revAmt, (juce::Component*) &distSend, (juce::Component*) &distColor,
                     (juce::Component*) &bpm, (juce::Component*) &lockBtn })
        c->setVisible (normal);

    if (showGen)
    {
        auto in = genPanel.reduced (16, 0).withTrimmedTop (44).withTrimmedBottom (14);
        const int fw = juce::jlimit (54, 84, in.getWidth() / 9), gap = 10;
        const int cw = in.getWidth() - 2 * fw - 2 * gap;
        auto left = in.removeFromLeft (cw);                  in.removeFromLeft (gap);
        octave.setBounds (left.removeFromBottom (34));
        notes.setBounds (left.removeFromBottom (34));        left.removeFromBottom (4);
        groove.setBounds (left);
        bars.setBounds (in.removeFromLeft (fw));      in.removeFromLeft (gap);
        variation.setBounds (in.removeFromLeft (fw));
        genTitleR = { genPanel.getX() + 14, genPanel.getY() + 8, 170, 30 };
    }
    if (showSyn)
    {
        auto in = synthPanel.reduced (16, 0).withTrimmedTop (44).withTrimmedBottom (14);
        const int fw = juce::jlimit (54, 84, in.getWidth() / 9), gap = 10;
        const int cw = (in.getWidth() - gap) / 2;
        (void) fw;
        filter.setBounds (in.removeFromLeft (cw));    in.removeFromLeft (gap);
        envelope.setBounds (in.removeFromLeft (cw));
        synthTitleR = { synthPanel.getX() + 14, synthPanel.getY() + 8, 170, 30 };
        const int wx = synthPanel.getRight() - 14 - 96 - (fsyn ? 52 : 0);        // WAVE sits in the synth panel header
        waveLabel = { wx - 54, synthPanel.getY() + 12, 50, 24 };
        waveBox.setBounds (wx, synthPanel.getY() + 10, 96, 28);
    }

    if (normal)
    {
        {   // mixer: six vertical strips (S on top, M or ON / OFF underneath) and the four effect faders on the right
            VFader* strips[6] = { &mixBass, &mixKick, &mixHat, &mixDel, &mixRev, &mixDist };
            NButton* solos[6] = { &soloBass, &soloKick, &soloHat, &soloDel, &soloRev, &soloDist };
            NButton* mutes[6] = { &muteBass, &muteKick, &muteHat, &onDel, &onRev, &onDist };
            const int x0 = mixPanel.getX() + 16, y0 = mixPanel.getY() + 34, pitch = 92;
            for (int i = 0; i < 6; ++i)
            {
                const int x = x0 + i * pitch;
                solos[i]->setBounds (x + 22, y0, 42, 22);
                strips[i]->setBounds (x, y0 + 26, 86, 142);
                mutes[i]->setBounds (x + 22, y0 + 172, 42, 22);
            }
            HFader* f[4] = { &delTime, &revAmt, &distSend, &distColor };
            const int fx = x0 + 6 * pitch + 30, fw = mixPanel.getRight() - 16 - fx;
            for (int i = 0; i < 4; ++i) f[i]->setBounds (fx, y0 + 18 + i * 40, fw, 26);
        }

        const int sx = setupPanel.getX(), sy = setupPanel.getY();
        setupLabels[0] = { sx + 18, sy + 38, 56, 24 };
        setupLabels[1] = { sx + 18, sy + 82, 56, 24 };
        {
            const int x0 = sx + 80, avail = setupPanel.getWidth() - 80 - 120;
            const int rw = avail / 13, sw = avail / 6;
            for (int i = 0; i < 13; ++i) rootBtns[(size_t) i]->setBounds (x0 + i * rw - 2, sy + 30, rw + 4, 38);
            for (int i = 0; i < 6; ++i)  scaleBtns[(size_t) i]->setBounds (x0 + i * sw - 2, sy + 74, sw + 4, 38);
        }

        bpm.setBounds (setupPanel.getRight() - 100, sy + 8, 76, 116);
        lockBtn.setBounds (setupPanel.getRight() - 100, sy + 100, 76, 22);
    }

    // --- LFO cards
    const int n = (int) shown.size();
    for (int i = 0; i < 4; ++i)
    {
        auto& c = *lfos[(size_t) i];
        const int slot = (int) (std::find (shown.begin(), shown.end(), i) - shown.begin());
        const bool vis = slot < n;
        c.setVisible (vis);
        c.full = flfo;
        if (! vis) continue;

        if (normal && lfoGrid)
        {
            const int cw = (lfoRowArea.getWidth() - 12) / 2, ch = (lfoRowArea.getHeight() - 12) / 2;
            c.setBounds (lfoRowArea.getX() + (slot % 2) * (cw + 12), lfoRowArea.getY() + (slot / 2) * (ch + 12), cw, ch);
        }
        else if (normal)
        {
            const int cw = (lfoRowArea.getWidth() - 3 * 12) / 4;
            c.setBounds (lfoRowArea.getX() + slot * (cw + 12), lfoRowArea.getY(), cw, lfoRowArea.getHeight());
        }
        else if (flfo)
            c.setBounds (lfoRowArea);
        else
        {
            const int cw = juce::jmin (420, (lfoRowArea.getWidth() - (n - 1) * 12) / n);
            c.setBounds (lfoRowArea.getX() + slot * (cw + 12), lfoRowArea.getY(), cw, lfoRowArea.getHeight());
        }
        c.resized();
    }

    // --- close button on the focused thing
    closeBtn.setVisible (! normal);
    if (fgen)       closeBtn.setBounds (genPanel.getRight() - 52, genPanel.getY() + 6, 40, 40);
    else if (fsyn)  closeBtn.setBounds (synthPanel.getRight() - 52, synthPanel.getY() + 6, 40, 40);
    else if (flfo)  closeBtn.setBounds (lfoRowArea.getRight() - 52, lfoRowArea.getY() + 4, 40, 40);

    modal.setBounds (0, 0, W, H);
    modal.toFront (false);
}

// ------------------------------------------------------------------ paint

void TB303Editor::infoText (juce::AttributedString& as) const
{
    const auto pat = proc.getPatternCopy();
    int notes = 0, acc = 0, sl = 0;
    for (int i = 0; i < pat.len; ++i)
        if (pat[(size_t) i].active) { ++notes; acc += pat[(size_t) i].accent; sl += pat[(size_t) i].slide; }

    const auto f = ui::mono (10.5f).withExtraKerningFactor (0.18f);
    auto part = [&] (const juce::String& t) { as.append (t, f, ui::green); };
    auto sep = [&] { as.append ("  /  ", f, ui::muted); };

    const auto name = proc.getLoadedName();
    if (name.isNotEmpty()) { part ("LOADED " + name.toUpperCase()); sep(); }
    part (juce::MidiMessage::getMidiNoteName (proc.currentRoot(), true, false, 4).toUpperCase() + " " + tb::scaleNames()[proc.currentScale()].toUpperCase());
    sep(); part (juce::String (pat.bars()) + (pat.bars() == 1 ? " BAR" : " BARS"));
    sep(); part (juce::String (notes) + " NOTES");
    sep(); part (juce::String (acc) + " ACCENTS");
    sep(); part (juce::String (sl) + " SLIDES");
    sep(); part (juce::String (juce::roundToInt (proc.uiBpm.load())) + " BPM");
}

void TB303Editor::paint (juce::Graphics& g) { g.fillAll (ui::bg); }

void TB303Editor::paintContent (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    g.setGradientFill (juce::ColourGradient (ui::rot (juce::Colour (0xff0b3a36)), 90.f, -80.f, ui::rot (juce::Colour (0x000b3a36)), 730.f, -80.f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (ui::rot (juce::Colour (0xff101c52)), (float) W, (float) H + 60.f, ui::rot (juce::Colour (0x00101c52)), (float) W - 600.f, (float) H + 60.f, true));
    g.fillAll();

    g.setColour (ui::fg);
    g.setFont (ui::font (28.f, true));
    g.drawText ("BASSLINER303", 20, 8, 232, 42, juce::Justification::centredLeft);
    ui::caption (g, "ACID LINE GENERATOR", { 260.f, 22.f, 290.f, 18.f }, ui::green, 9.5f, juce::Justification::centredLeft, 0.3f);

    {
        juce::AttributedString as;
        infoText (as);
        as.setJustification (juce::Justification::centredLeft);
        as.draw (g, infoR.toFloat());
    }

    auto panelCard = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour tint, juce::Colour titleCol, bool expandable)
    {
        if (r.isEmpty()) return;
        ui::card (g, r.toFloat(), tint, 18.f, 0.30f);
        ui::caption (g, title, { (float) r.getX() + 20.f, (float) r.getY() + 12.f, 160.f, 22.f }, titleCol, 11.f, juce::Justification::centredLeft, 0.3f);
        if (expandable)
        {
            const float ix = (float) r.getX() + 20.f + 10.f * (float) title.length() + 10.f, iy = (float) r.getY() + 17.f;
            juce::Path p;
            p.startNewSubPath (ix, iy + 10.f); p.lineTo (ix + 10.f, iy);
            p.startNewSubPath (ix + 4.f, iy); p.lineTo (ix + 10.f, iy); p.lineTo (ix + 10.f, iy + 6.f);
            p.startNewSubPath (ix, iy + 4.f); p.lineTo (ix, iy + 10.f); p.lineTo (ix + 6.f, iy + 10.f);
            g.setColour (tint.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (1.3f));
        }
    };

    panelCard (genPanel, "GENERATOR", ui::green, ui::silver, true);
    panelCard (synthPanel, "SYNTH", ui::violet, ui::violet, true);
    panelCard (mixPanel, "MIXER", ui::cyan, ui::violet, false);
    panelCard (setupPanel, "SETUP", ui::silver, ui::silver, false);

    if (proc.styleIsReady())
    {
        const auto d = styleDotR.toFloat();
        ui::glowRect (g, d, ui::green, 6.f, 1.f);
        g.setColour (ui::green);
        g.fillEllipse (d);
    }
    else { g.setColour (ui::muted.withAlpha (0.6f)); g.fillEllipse (styleDotR.toFloat()); }

    if (focus == fNone)
    {
        ui::caption (g, "ROOT",  setupLabels[0].toFloat(), ui::muted, 9.f);
        ui::caption (g, "SCALE", setupLabels[1].toFloat(), ui::muted, 9.f);
        ui::caption (g, "MODULATION - 4 LFOS, 14 DESTINATIONS", lfoHeader.toFloat(), ui::muted, 9.5f, juce::Justification::centredLeft, 0.3f);
    }
    else if (! linkedLabel.isEmpty())
        ui::caption (g, "LINKED LFOS", linkedLabel.toFloat(), ui::muted, 9.5f, juce::Justification::centredLeft, 0.3f);
    if (! waveLabel.isEmpty())
        ui::caption (g, "WAVE", waveLabel.toFloat(), ui::muted, 9.f);
}

void TB303Editor::shiftColours (bool reset)
{
    static juce::Random rng;
    if (reset) ui::setHue (0.f);                                   // triple-click on the roll: original colours
    else       ui::setHue (ui::gHue + 0.10f + 0.25f * rng.nextFloat());      // click on the title: a good step round the wheel
    laf.refresh();
    sendLookAndFeelChange();
    content.repaint();
    repaint();
}

void TB303Editor::applyZoom (float z)
{
    z = juce::jlimit (ZoomBar::kMin, ZoomBar::kMax, z);
    const float f = z / juce::jmax (0.01f, scaleNow);                   // keeps the window's shape, scales it
    setSize (juce::roundToInt ((float) getWidth() * f), juce::roundToInt ((float) getHeight() * f));
}

// largest zoom whose window still fits on the screen this editor is on (room left for the title bar / host chrome)
void TB303Editor::fitToScreen()
{
    const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
    if (d == nullptr) return;
    const auto area = d->userArea;
    const float z = juce::jmin ((float) (area.getWidth() - 24) / (float) kW, (float) (area.getHeight() - 96) / (float) kH);
    applyZoom (std::floor (juce::jlimit (ZoomBar::kMin, ZoomBar::kMax, z) * 100.f) / 100.f);

    if (juce::JUCEApplicationBase::isStandaloneApp())               // keep the standalone window on screen
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (this), area]
        {
            if (sp != nullptr)
                if (auto* tl = sp->getTopLevelComponent())
                    tl->setBounds (tl->getBounds().constrainedWithin (area));
        });
}

void TB303Editor::resized()
{
    // Pick the arrangement by window shape: tall / square windows keep the single column, wide windows (and full screen)
    // put the four LFOs in a column on the right. The content is laid out in "design pixels" that fill the window exactly.
    const float w = (float) juce::jmax (1, getWidth()), h = (float) juce::jmax (1, getHeight());
    wide = w / h > 1.45f;
    const float bw = wide ? (float) kWideW : (float) kW, bh = wide ? (float) kWideH : (float) kH;
    const float z = juce::jmin (w / bw, h / bh);
    W = juce::roundToInt (w / z);
    H = juce::roundToInt (h / z);
    content.setBounds (0, 0, W, H);
    content.setTransform (juce::AffineTransform::scale (z));
    scaleNow = z;
    zoomBar.setZoom (z);
    proc.editorZoom = z;
    layoutAll();
}

void TB303Editor::timerCallback()
{
    if (++tickCount == 3 || tickCount == 60)          // belt and braces: lay out again just after opening
    {
        relayout();
        hookKeys();
        if (juce::Component::getCurrentlyFocusedComponent() == nullptr) grabKeyboardFocus();
    }

    if (rainbow && (tickCount % 4) == 0)                            // one full lap of the wheel in 60 s (updated 15x a second)
    {
        ui::setHue (ui::gHue + 4.f / 3600.f);
        laf.refresh();
        sendLookAndFeelChange();
        content.repaint();
    }
    roll.repaint();

    static int tick = 0;
    if ((++tick & 1) == 0)                                          // the rest animates at 30 fps
    {
        for (auto* c : { &groove, &filter, &envelope })
            if (c->isVisible()) c->repaint();
        for (auto* f : { &bars, &variation, &bpm, &mixBass, &mixKick, &mixHat, &mixDel, &mixRev, &mixDist })
            if (f->isVisible()) f->repaint();
        for (auto* h : { &delTime, &revAmt, &distSend, &distColor })
            if (h->isVisible()) h->repaint();
        if (notes.isVisible()) notes.repaint();
        if (octave.isVisible()) octave.repaint();
        for (auto& l : lfos)
            if (l->isVisible()) { l->repaint(); l->rateDepth.repaint(); }
        content.repaint (infoR);
        generateBtn.repaint();
    }

    for (auto* b : { &onDel, &onRev, &onDist })                      // ON / OFF label follows the switch
    {
        const auto t = b->getToggleState() ? juce::String ("ON") : juce::String ("OFF");
        if (b->getButtonText() != t) { b->setButtonText (t); b->repaint(); }
    }

    const bool running = runBtn.getToggleState();
    const auto label = running ? juce::String ("STOP") : juce::String ("PREVIEW LOOP");
    if (runBtn.getButtonText() != label)
    {
        runBtn.setButtonText (label);
        runBtn.icon = running ? NButton::stop : NButton::play;
        runBtn.repaint();
    }
    if ((tickCount % 6) == 0)
    {
        refreshSetupButtons();
        if (stylePage.isVisible()) stylePage.refresh();
        const auto n = proc.takeNotice();
        if (n.has) showNotice (n);
    }
    refreshUndo();
}

void TB303Editor::refreshSetupButtons()
{
    const int ri = (int) proc.apvts.getRawParameterValue ("root")->load(), si = (int) proc.apvts.getRawParameterValue ("scale")->load();
    for (int i = 0; i < 13; ++i) rootBtns[(size_t) i]->setToggleState (i == ri, juce::dontSendNotification);
    for (int i = 0; i < 6; ++i)  scaleBtns[(size_t) i]->setToggleState (i == si, juce::dontSendNotification);

    const bool juke = proc.jukeIsOn();
    const auto ht = juke ? juce::String ("REMOVE THIS LOOP") : juce::String ("PLAY SAVED LOOPS");
    if (heartBtn.getButtonText() != ht) { heartBtn.setButtonText (ht); heartBtn.repaint(); }
    heartBtn.setToggleState (juke, juce::dontSendNotification);
    renBtn.setVisible (juke);
    keysBtn.setToggleState (proc.keysActive(), juce::dontSendNotification);
    styleBtn.setToggleState (proc.styleIsOn(), juce::dontSendNotification);
}

void TB303Editor::showNotice (const TB303Processor::Notice& n)
{
    std::vector<ModalOverlay::Btn> b;
    if (! n.error) b.push_back ({ "GENERATE", [this] { proc.generatePattern(); refreshUndo(); }, false });
    b.push_back ({ "OK", nullptr, true });
    modal.show (n.title, {}, n.text, n.error, std::move (b));
}

void TB303Editor::askName (const juce::String& title, const juce::String& msg, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, msg, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("n", "", "");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> sw (w);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([sw, done] (int r)
    {
        if (r == 1 && sw != nullptr) done (sw->getTextEditorContents ("n"));
    }), true);
}

void TB303Editor::keysPress()
{
    keysPage.open (proc.keysRootPc(), proc.keysPcMask(), proc.keysActive());
}

void TB303Editor::heartPress()
{
    if (proc.jukeIsOn())
    {
        const auto name = proc.jukeFileName();
        modal.show ("REMOVE FROM SAVED LOOPS?", name,
                    "The file is moved to the TB303Gen_DELETED folder (next to TB303 Gen) and the loops carry on with the next one.", false,
                    { { "CANCEL", nullptr, false },
                      { "REMOVE", [this] { const auto e = proc.jukeRemove(); if (e.isNotEmpty()) modal.show ("MOVE FAILED", {}, e, true, { { "OK", nullptr, true } }); refreshUndo(); }, true } });
        return;
    }
    const auto e = proc.jukeStart();
    if (e.isNotEmpty()) modal.show ("NO FILES YET", {}, e, false, { { "OK", nullptr, true } });
    refreshUndo();
}

void TB303Editor::renamePress()
{
    askName ("RENAME", "This name is added to the end of the file name.", [this] (juce::String v)
    {
        const auto e = proc.jukeRename (v);
        if (e.isNotEmpty()) modal.show ("RENAME FAILED", {}, e, true, { { "OK", nullptr, true } });
    });
}

void TB303Editor::styleFromFiles (bool folder)
{
    chooser = std::make_unique<juce::FileChooser> (folder ? "Pick a folder of .mid files" : "Pick .mid files to learn from",
                                                   proc.saveFolder().exists() ? proc.saveFolder() : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.mid;*.midi");
    juce::Component::SafePointer<TB303Editor> safe (this);
    const int flags = folder ? (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories)
                             : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);
    chooser->launchAsync (flags, [safe, folder] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto res = fc.getResults();
        if (res.isEmpty()) return;
        if (folder) safe->proc.learnFromFolder (res[0]);
        else
        {
            juce::Array<juce::File> files;
            for (auto& f : res) files.add (f);
            safe->proc.learnFromFiles (files);
        }
    });
}

void TB303Editor::saveStylePress()
{
    if (! proc.styleLoaded()) return;
    askName ("SAVE STYLE", "Name this style.", [this] (juce::String v)
    {
        juce::String clean;
        for (auto ch : v.trim())
            clean << (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '-' || ch == '_' ? juce::String::charToString (ch) : (ch == ' ' ? "-" : ""));
        if (clean.isEmpty()) clean = "style";
        proc.saveFolder().createDirectory();
        if (! proc.saveStyleTo (proc.saveFolder().getChildFile ("style_" + clean.substring (0, 40) + ".txt")))
            modal.show ("COULD NOT SAVE", {}, "Writing the style file failed.", true, { { "OK", nullptr, true } });
    });
}

void TB303Editor::loadStylePress()
{
    const auto files = proc.savedStyleFiles();
    if (files.isEmpty())
    {
        modal.show ("NO STYLES YET", {}, "No saved styles in your TB303 Gen folder.", false, { { "OK", nullptr, true } });
        return;
    }
    juce::PopupMenu m;
    m.addSectionHeader ("SAVED STYLES");
    for (int i = 0; i < files.size(); ++i)
        m.addItem (i + 1, files[i].getFileNameWithoutExtension().fromFirstOccurrenceOf ("style_", false, false));
    juce::Component::SafePointer<TB303Editor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stylePage), [safe, files] (int id)
    {
        if (safe == nullptr || id <= 0) return;
        const auto f = files[id - 1];
        if (safe->proc.loadStyleFrom (f))
            safe->modal.show ("STYLE LOADED", f.getFileName(), safe->proc.styleSummary() + " STYLE is ON.",
                              false, { { "GENERATE", [safe] { if (safe != nullptr) { safe->proc.generatePattern(); safe->refreshUndo(); } }, false }, { "OK", nullptr, true } });
        else
            safe->modal.show ("NOT A STYLE FILE", f.getFileName(), "This file could not be read as a style.", true, { { "OK", nullptr, true } });
    });
}
