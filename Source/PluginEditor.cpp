#include "PluginEditor.h"

TB303Editor::TB303Editor (TB303Processor& p)
    : AudioProcessorEditor (&p), proc (p), roll (p),
      density  (p, "DENSITY",  "notes",   "accents", tb::dNotes,  tb::dAccents, ui::green,  "NOTES",   "ACCENTS", true),
      feel     (p, "FEEL",     "slides",  "octave",  tb::dSlides, tb::dOctave,  ui::cyan,   "SLIDES",  "OCTAVE",  true),
      filter   (p, "FILTER",   "cutoff",  "reso",    tb::dCutoff, tb::dReso,    ui::green,  "CUTOFF",  "RESO",    false),
      envelope (p, "ENVELOPE", "envmod",  "decay",   tb::dEnvMod, tb::dDecay,   ui::violet, "ENV MOD", "DECAY",   false),
      kit      (p, "KIT",      "kickvol", "hatvol",  tb::dKick,   tb::dHat,     ui::cyan,   "KICK",    "HAT",     false),
      bars      (p, "BARS",      "bars",      -1,              ui::cyan,   true),
      variation (p, "VARIATION", "variation", tb::dVariation,  ui::violet, true),
      volume    (p, "VOLUME",    "volume",    tb::dVolume,     ui::silver, false),
      bpm       (p, "",          "tempo",     -1,              ui::silver, false)
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
    setResizeLimits (550, 484, 2200, 1936);                 // 50 % .. 200 %
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);

    content.addAndMakeVisible (roll);
    for (auto* c : { &density, &feel, &filter, &envelope, &kit }) content.addAndMakeVisible (*c);
    for (auto* f : { &bars, &variation, &volume, &bpm }) content.addAndMakeVisible (*f);
    hookGestures (density); hookGestures (feel);
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

    for (auto* b : { &synthBtn, &drumsBtn, &runBtn })
    {
        b->setClickingTogglesState (true);
        content.addAndMakeVisible (*b);
    }
    synthAtt = std::make_unique<ButtonAtt> (proc.apvts, "synth", synthBtn);
    drumsAtt = std::make_unique<ButtonAtt> (proc.apvts, "drums", drumsBtn);
    runAtt   = std::make_unique<ButtonAtt> (proc.apvts, "run", runBtn);

    generateBtn.primary = true;
    generateBtn.pulse = true;
    generateBtn.onClick = [this] { proc.generatePattern(); refreshUndo(); };
    previousBtn.onClick = [this] { proc.previousPattern(); refreshUndo(); };
    nextBtn.onClick     = [this] { proc.nextPattern(); refreshUndo(); };
    loadBtn.onClick     = [this] { doLoad(); };
    downloadBtn.onClick = [this] { doDownload(); };
    folderBtn.onClick   = [this] { doSaveToFolder(); };
    undoBtn.onClick     = [this] { proc.undoTweak(); refreshUndo(); };
    redoBtn.onClick     = [this] { proc.redoTweak(); refreshUndo(); };
    closeBtn.onClick    = [this] { setFocus (fNone); };

    content.addAndMakeVisible (zoomBar);
    content.addAndMakeVisible (fitBtn);
    zoomBar.onChange = [this] (float z) { applyZoom (z); };
    fitBtn.onClick   = [this] { fitToScreen(); };

    for (auto* b : { &generateBtn, &previousBtn, &nextBtn, &loadBtn, &downloadBtn, &folderBtn, &undoBtn, &redoBtn, &closeBtn })
        content.addAndMakeVisible (*b);
    closeBtn.setVisible (false);

    content.addChildComponent (modal);
    modal.setVisible (false);

    setWantsKeyboardFocus (true);
    refreshUndo();
    {
        const float z = juce::jlimit (ZoomBar::kMin, ZoomBar::kMax, proc.editorZoom.load());
        setSize (juce::roundToInt ((float) kW * z), juce::roundToInt ((float) kH * z));
    }
    //                                 // last: resized() needs every child to exist
    startTimerHz (60);
}

TB303Editor::~TB303Editor()
{
    stopTimer();
    chooser.reset();
    setLookAndFeel (nullptr);
}

void TB303Editor::hookGestures (XYCard& c)
{
    c.onGestureStart = [this] { proc.beginTweakGesture(); };
    c.onGestureEnd   = [this] { proc.endTweakGesture(); refreshUndo(); };
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

void TB303Editor::refreshUndo()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    previousBtn.setEnabled (proc.historyIndex() > 0);
    nextBtn.setEnabled (proc.historyIndex() < proc.historyCount() - 1);
}

bool TB303Editor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (modal.isVisible()) modal.close();
        else if (focus != fNone) setFocus (fNone);
        return true;
    }
    if (k.getModifiers().isCommandDown())                           // Cmd/Ctrl  +  -  0
    {
        const auto c = k.getTextCharacter();
        const float z = (float) getWidth() / (float) kW;
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
        for (int d : { (int) tb::dCutoff, (int) tb::dReso, (int) tb::dEnvMod, (int) tb::dDecay, (int) tb::dVolume, (int) tb::dPitch, (int) tb::dMaster })
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
    undoBtn.setBounds (896, 10, 92, 38);
    redoBtn.setBounds (990, 10, 92, 38);
    zoomBar.setBounds (560, 12, 256, 34);
    fitBtn.setBounds (822, 10, 68, 38);
    rollR = { 20, 58, 1060, 132 };
    roll.setBounds (rollR);
    infoR = { 22, 196, 1056, 18 };

    {   // action buttons share the row like flex items
        NButton* row[7] = { &generateBtn, &previousBtn, &nextBtn, &runBtn, &loadBtn, &downloadBtn, &folderBtn };
        const float w[7] = { 1.0f, 1.0f, 0.78f, 1.3f, 1.0f, 1.45f, 1.45f };
        float sum = 0.f;
        for (float x : w) sum += x;
        const float avail = 1060.f - 6.f * 8.f;
        float x = 20.f;
        for (int i = 0; i < 7; ++i)
        {
            const float bw = avail * w[i] / sum;
            row[i]->setBounds ((int) x - 4, 220, (int) bw + 8, 46);
            x += bw + 8.f;
        }
    }

    const bool normal = focus == fNone, fgen = focus == fGen, fsyn = focus == fSynth, flfo = focus >= fLfo0;
    const int top = 276;

    genPanel = synthPanel = drumPanel = setupPanel = lfoHeader = linkedLabel = {};
    genTitleR = synthTitleR = {};

    // --- which LFO cards go where
    std::vector<int> shown;
    R lfoRowArea;
    if (normal)
    {
        genPanel   = { 20, top, 520, 252 };
        synthPanel = { 560, top, 520, 252 };
        drumPanel  = { 20, top + 264, 300, 130 };
        setupPanel = { 340, top + 264, 740, 130 };
        lfoHeader  = { 20, top + 404, 1060, 16 };
        lfoRowArea = { 20, top + 424, 1060, 262 };
        shown = { 0, 1, 2, 3 };
    }
    else if (fgen || fsyn)
    {
        shown = linkedLfos (focus);
        const int h = shown.empty() ? 684 : 330;
        (fgen ? genPanel : synthPanel) = { 20, top, 1060, h };
        if (! shown.empty())
        {
            linkedLabel = { 20, top + h + 10, 400, 14 };
            lfoRowArea = { 20, top + h + 30, 1060, kH - (top + h + 30) - 12 };
        }
    }
    else
    {
        shown = { focus - fLfo0 };
        lfoRowArea = { 20, top, 1060, 684 };
    }

    // --- generator + synth panels
    const bool showGen = normal || fgen, showSyn = normal || fsyn;
    for (auto* c : { (juce::Component*) &density, (juce::Component*) &feel, (juce::Component*) &bars, (juce::Component*) &variation })
        c->setVisible (showGen);
    for (auto* c : { (juce::Component*) &filter, (juce::Component*) &envelope, (juce::Component*) &volume })
        c->setVisible (showSyn);
    for (auto* c : { (juce::Component*) &kit, (juce::Component*) &drumsBtn, (juce::Component*) &rootBox, (juce::Component*) &scaleBox,
                     (juce::Component*) &waveBox, (juce::Component*) &synthBtn, (juce::Component*) &bpm, (juce::Component*) &lockBtn })
        c->setVisible (normal);

    if (showGen)
    {
        auto in = genPanel.reduced (16, 0).withTrimmedTop (44).withTrimmedBottom (14);
        const int fw = juce::jlimit (54, 84, in.getWidth() / 9), gap = 10;
        const int cw = (in.getWidth() - 2 * fw - 3 * gap) / 2;
        density.setBounds (in.removeFromLeft (cw));   in.removeFromLeft (gap);
        feel.setBounds (in.removeFromLeft (cw));      in.removeFromLeft (gap);
        bars.setBounds (in.removeFromLeft (fw));      in.removeFromLeft (gap);
        variation.setBounds (in.removeFromLeft (fw));
        genTitleR = { genPanel.getX() + 14, genPanel.getY() + 8, 170, 30 };
    }
    if (showSyn)
    {
        auto in = synthPanel.reduced (16, 0).withTrimmedTop (44).withTrimmedBottom (14);
        const int fw = juce::jlimit (54, 84, in.getWidth() / 9), gap = 10;
        const int cw = (in.getWidth() - fw - 3 * gap) / 2;
        filter.setBounds (in.removeFromLeft (cw));    in.removeFromLeft (gap);
        envelope.setBounds (in.removeFromLeft (cw));  in.removeFromLeft (gap);
        volume.setBounds (in.removeFromLeft (fw));
        synthTitleR = { synthPanel.getX() + 14, synthPanel.getY() + 8, 170, 30 };
    }

    if (normal)
    {
        kit.setBounds (drumPanel.getX() + 14, drumPanel.getY() + 32, 180, 92);
        drumsBtn.setBounds (drumPanel.getX() + 204, drumPanel.getY() + 62, 88, 38);

        const int sx = setupPanel.getX(), sy = setupPanel.getY();
        setupLabels[0] = { sx + 18, sy + 42, 46, 24 };
        rootBox.setBounds (sx + 64, sy + 40, 90, 28);
        setupLabels[1] = { sx + 170, sy + 42, 56, 24 };
        scaleBox.setBounds (sx + 228, sy + 40, 170, 28);
        setupLabels[2] = { sx + 414, sy + 42, 50, 24 };
        waveBox.setBounds (sx + 466, sy + 40, 96, 28);
        synthBtn.setBounds (sx + 14, sy + 78, 150, 40);

        bpm.setBounds (sx + 740 - 100, sy + 8, 76, 116);
        lockBtn.setBounds (sx + 740 - 100, sy + 100, 76, 22);
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

        if (normal)
            c.setBounds (lfoRowArea.getX() + slot * (256 + 12), lfoRowArea.getY(), 256, lfoRowArea.getHeight());
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

    modal.setBounds (0, 0, kW, kH);
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
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b3a36), 90.f, -80.f, juce::Colour (0x000b3a36), 730.f, -80.f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff101c52), (float) kW, (float) kH + 60.f, juce::Colour (0x00101c52), (float) kW - 600.f, (float) kH + 60.f, true));
    g.fillAll();

    g.setColour (ui::fg);
    g.setFont (ui::font (31.f, true));
    g.drawText ("TB-303", 20, 8, 150, 42, juce::Justification::centredLeft);
    ui::caption (g, "ACID LINE GENERATOR", { 176.f, 22.f, 360.f, 18.f }, ui::green, 9.5f, juce::Justification::centredLeft, 0.3f);

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
    panelCard (drumPanel, "DRUMS", ui::cyan, ui::violet, false);
    panelCard (setupPanel, "SETUP", ui::silver, ui::silver, false);

    if (focus == fNone)
    {
        ui::caption (g, "ROOT",  setupLabels[0].toFloat(), ui::muted, 9.f);
        ui::caption (g, "SCALE", setupLabels[1].toFloat(), ui::muted, 9.f);
        ui::caption (g, "WAVE",  setupLabels[2].toFloat(), ui::muted, 9.f);
        ui::caption (g, "MODULATION - 4 LFOS, 14 DESTINATIONS", lfoHeader.toFloat(), ui::muted, 9.5f, juce::Justification::centredLeft, 0.3f);
    }
    else if (! linkedLabel.isEmpty())
        ui::caption (g, "LINKED LFOS", linkedLabel.toFloat(), ui::muted, 9.5f, juce::Justification::centredLeft, 0.3f);
}

void TB303Editor::applyZoom (float z)
{
    z = juce::jlimit (ZoomBar::kMin, ZoomBar::kMax, z);
    setSize (juce::roundToInt ((float) kW * z), juce::roundToInt ((float) kH * z));
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
    content.setBounds (0, 0, kW, kH);
    const float z = (float) getWidth() / (float) kW;
    content.setTransform (juce::AffineTransform::scale (z));
    zoomBar.setZoom (z);
    proc.editorZoom = z;
    layoutAll();
}

void TB303Editor::timerCallback()
{
    roll.repaint();

    static int tick = 0;
    if ((++tick & 1) == 0)                                          // the rest animates at 30 fps
    {
        for (auto* c : { &density, &feel, &filter, &envelope, &kit })
            if (c->isVisible()) c->repaint();
        for (auto* f : { &bars, &variation, &volume, &bpm })
            if (f->isVisible()) f->repaint();
        for (auto& l : lfos)
            if (l->isVisible()) { l->repaint(); l->rateDepth.repaint(); }
        content.repaint (infoR);
        generateBtn.repaint();
    }

    const bool running = runBtn.getToggleState();
    const auto label = running ? juce::String ("STOP") : juce::String ("PREVIEW LOOP");
    if (runBtn.getButtonText() != label)
    {
        runBtn.setButtonText (label);
        runBtn.icon = running ? NButton::stop : NButton::play;
        runBtn.repaint();
    }
    refreshUndo();
}
