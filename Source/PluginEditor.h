#pragma once
#include "PluginProcessor.h"
#include <map>

// ============================================================================================
//  Look: mint / cyan / violet / pink on deep blue-black, soft glows, XY pads  (same as the web app)
// ============================================================================================
namespace ui
{
// Easter egg: click the title (or triple-click the roll) and every colour moves round the colour wheel, same theme.
// gHue is in turns (0..1). The palette below is rebuilt by setHue(); widgets that keep a colour of their own store the
// base colour and pass it through rot() when they paint. A new editor always starts back at 0.
inline float gHue = 0.f;
inline juce::Colour rot (juce::Colour c) { return gHue == 0.f ? c : c.withRotatedHue (gHue); }

inline juce::Colour bg     { 0xff05090e };
inline juce::Colour panel  { 0xff0a1118 };
inline juce::Colour panel2 { 0xff0f1923 };
inline juce::Colour line   { 0xff1b2a36 };
inline juce::Colour fg     { 0xffe8eef6 };
inline juce::Colour muted  { 0xff7d8aa0 };
inline juce::Colour green  { 0xff5ef2c8 };       // mint
inline juce::Colour cyan   { 0xff4cc9f0 };
inline juce::Colour violet { 0xff7b8cff };
inline juce::Colour silver { 0xffb9c6d8 };
inline juce::Colour pink   { 0xffff5c8a };

inline void setHue (float h)
{
    gHue = h - std::floor (h);
    bg = rot (juce::Colour (0xff05090e));     panel = rot (juce::Colour (0xff0a1118));  panel2 = rot (juce::Colour (0xff0f1923));
    line = rot (juce::Colour (0xff1b2a36));   fg = rot (juce::Colour (0xffe8eef6));     muted = rot (juce::Colour (0xff7d8aa0));
    green = rot (juce::Colour (0xff5ef2c8));  cyan = rot (juce::Colour (0xff4cc9f0));   violet = rot (juce::Colour (0xff7b8cff));
    silver = rot (juce::Colour (0xffb9c6d8)); pink = rot (juce::Colour (0xffff5c8a));
}

inline juce::Colour lfoColour (int i)
{
    static const juce::uint32 c[4] = { 0xff5ef2c8, 0xff4cc9f0, 0xff7b8cff, 0xffe58cff };
    return rot (juce::Colour (c[i & 3]));
}

inline juce::Colour steel (float alpha) { return juce::Colour::fromRGBA (125, 150, 175, (juce::uint8) juce::jlimit (0.f, 255.f, alpha * 255.f)); }

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

// letter-spaced caption
inline void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float h = 10.f,
                     juce::Justification j = juce::Justification::centredLeft, float spacing = 0.22f)
{
    g.setColour (c);
    g.setFont (mono (h).withExtraKerningFactor (spacing));
    g.drawText (text, r, j, false);
}

inline void glowRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float corner = 6.f, float strength = 1.f)
{
    g.setColour (c.withAlpha (0.07f * strength)); g.fillRoundedRectangle (r.expanded (7.f), corner + 6.f);
    g.setColour (c.withAlpha (0.14f * strength)); g.fillRoundedRectangle (r.expanded (3.5f), corner + 3.f);
}

inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w = 1.5f)
{
    const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
    g.setColour (c.withAlpha (0.10f)); g.strokePath (p, juce::PathStrokeType (w * 7.f, juce::PathStrokeType::curved, cap));
    g.setColour (c.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (w * 3.5f, juce::PathStrokeType::curved, cap));
    g.setColour (c.withAlpha (0.95f)); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, cap));
}

// card: dark gradient fill, thin tinted border
inline void card (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, float corner = 14.f, float borderAlpha = 0.30f)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e171f), r.getX(), r.getY(), juce::Colour (0xff080e14), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (tint.withAlpha (borderAlpha));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.f);
}
} // namespace ui

class NebuleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NebuleLookAndFeel() { refresh(); }
    void refresh();                                  // (re)read the palette
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui::mono (11.f); }
    juce::Font getPopupMenuFont() override { return ui::mono (12.f); }
};

// pill button: outline in its own colour with a soft glow; `primary` or toggled-on = filled
class NButton : public juce::TextButton
{
public:
    enum Icon { none, dice, prev, next, play, stop, cross };
    explicit NButton (const juce::String& text, juce::Colour c = ui::green, Icon i = none)
        
        : juce::TextButton (text), neon (c), icon (i) { setWantsKeyboardFocus (false); }   // keys always reach the editor's shortcuts      // `neon` is the base colour; painted through ui::rot()
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour neon;
    Icon icon;
    bool primary = false, pulse = false;
};

// padlock + "BPM" caption; toggled on = locked
class LockButton : public juce::Button
{
public:
    explicit LockButton (const juce::String& text) : juce::Button ("lock"), captionText (text)
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    juce::String captionText;
};

// zoom slider, 50..200 %. Drags are measured in screen pixels, so the slider stays steady while the window it lives in resizes.
class ZoomBar : public juce::Component
{
public:
    static constexpr float kMin = 0.5f, kMax = 2.0f;
    std::function<void (float)> onChange;
    void setZoom (float z) { z = juce::jlimit (kMin, kMax, z); if (z != value) { value = z; repaint(); } }   // no callback
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { change (1.0f); }
private:
    juce::Rectangle<float> track() const;
    void change (float z);
    float value = 0.85f, startValue = 0.85f;
};

// ---------------------------------------------------------------------------------------------
//  parameter plumbing shared by the pads and faders
// ---------------------------------------------------------------------------------------------
struct PRef
{
    juce::RangedAudioParameter* p = nullptr;
    float get() const { return p != nullptr ? p->getValue() : 0.f; }
    void set (float n) { if (p != nullptr) p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, n)); }
    void begin() { if (p != nullptr) p->beginChangeGesture(); }
    void end()   { if (p != nullptr) p->endChangeGesture(); }
    juce::String text() const { return p != nullptr ? p->getCurrentValueAsText() : juce::String(); }
};

struct Ghost { juce::Colour c; float x, y; };

// where each LFO currently pushes a control (its normalised position + the LFO's offset)
void collectGhosts (TB303Processor&, int destX, int destY, float nx, float ny, std::vector<Ghost>& out);

// two parameters on one square pad, with a title and two read-outs
class XYCard : public juce::Component
{
public:
    XYCard (TB303Processor&, const juce::String& title, const juce::String& idX, const juce::String& idY, int destX, int destY,
            juce::Colour col, const juce::String& nameX, const juce::String& nameY, bool generator);
    std::function<void()> onGestureStart, onGestureEnd;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool generatorCard() const { return generator; }
    juce::Colour colour0;                                // base colour (see ui::rot)
private:
    juce::Rectangle<float> track() const;                // where the handle can travel
    void update (const juce::MouseEvent&);
    TB303Processor& proc;
    juce::String title, nameX, nameY;
    PRef px, py;
    int dx, dy;
    bool generator;
};

// vertical fader
class VFader : public juce::Component
{
public:
    VFader (TB303Processor&, const juce::String& name, const juce::String& id, int dest, juce::Colour col, bool generator);
    std::function<void()> onGestureStart, onGestureEnd;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool generatorFader() const { return generator; }
    bool locked = false;
private:
    juce::Rectangle<float> track() const;
    void update (const juce::MouseEvent&);
    TB303Processor& proc;
    juce::String name;
    PRef pr;
    int dest;
    juce::Colour colour0;
    bool generator;
};

// horizontal fader: name on the left, value on the right (Notes, Octave)
class HFader : public juce::Component
{
public:
    HFader (TB303Processor&, const juce::String& name, const juce::String& id, int dest, juce::Colour col, bool generator);
    std::function<void()> onGestureStart, onGestureEnd;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> track() const;
    void update (const juce::MouseEvent&);
    TB303Processor& proc;
    juce::String name;
    PRef pr;
    int dest;
    juce::Colour colour0;
    bool generator;
};

// one LFO: shape, scope, rate/depth pad and the 14 destination chips
class LfoCard : public juce::Component
{
public:
    LfoCard (TB303Processor&, int index);
    std::function<void (int)> onTitleClick;
    bool full = false;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool routedTo (int dest) const { return (proc.getLfoMask (index) >> dest) & 1u; }
    XYCard rateDepth;
private:
    void paintScope (juce::Graphics&, juce::Rectangle<float>);
    TB303Processor& proc;
    int index;
    juce::Colour colour0;
    PRef shape;
    juce::Rectangle<float> titleR, scopeR;
    std::array<juce::Rectangle<float>, tb::kShapes> shapeR;
    std::array<juce::Rectangle<float>, tb::kDests> chipR;
};

// the piano roll: always two bars wide, scrolling past a fixed playhead; slides are soft hanging cables
class RollView : public juce::Component
{
public:
    explicit RollView (TB303Processor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onEgg;                         // triple-click
private:
    bool dragStarted = false;
    struct Pt { float x, y, px, py; };
    struct Rope { std::array<Pt, 12> pts; int seen = 0; };
    void stepRope (Rope&, float x1, float y1, float x2, float y2, float dt);
    void drawCable (juce::Graphics&, const Rope&, float x1, float y1, float x2, float y2, juce::Colour c1, juce::Colour c2, float alpha, float T, float seed);
    TB303Processor& proc;
    std::map<long long, Rope> ropes;
    int ropeFrame = 0;
    double lastT = 0.0, idlePos = 0.0, dragStartPos = 0.0;
    bool wasPlaying = false;
};

// centred message box with buttons
class ModalOverlay : public juce::Component
{
public:
    struct Btn { juce::String label; std::function<void()> fn; bool primary = false; };
    void show (const juce::String& title, const juce::String& file, const juce::String& text, bool error, std::vector<Btn> buttons);
    void close();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
private:
    void activate (int i);
    juce::Rectangle<int> box() const;
    juce::String title, file, text;
    bool isError = false;
    std::vector<Btn> defs;
    std::vector<std::unique_ptr<NButton>> buttons;
};

// KEYS: tap the notes you want on a keyboard; double-tap a chosen note to make it the root (pink)
class KeysOverlay : public juce::Component
{
public:
    KeysOverlay();
    std::function<void (int, unsigned)> onUse;           // root pitch class, pitch-class mask
    std::function<void()> onOff;
    void open (int rootPc, unsigned mask, bool wasOn);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> pianoArea() const;
    juce::Rectangle<float> keyRect (int pc) const;
    int hit (juce::Point<float>) const;
    juce::Rectangle<int> box() const { return juce::Rectangle<int> (0, 0, 720, 380).withCentre (getLocalBounds().getCentre()); }
    int root = 0, lastPc = -1;
    unsigned mask = 0;
    bool wasOn = false;
    double lastT = 0.0;
    NButton clearBtn { "CLEAR", ui::violet }, offBtn { "CANCEL", ui::violet }, useBtn { "USE THESE NOTES", ui::green };
};

// STYLE: a full-screen page with big targets: learn from files, keep, reset
class StyleOverlay : public juce::Component
{
public:
    explicit StyleOverlay (TB303Processor&);
    std::function<void()> onSavedFolder, onFolder, onFiles, onSave, onLoad, onBack;
    void open();
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
private:
    TB303Processor& proc;
    NButton backBtn { "<  BACK", ui::silver }, switchBtn { "STYLE OFF", ui::green },
            savedBtn { "SAVED FOLDER", ui::green }, folderBtn { "PICK A FOLDER", ui::cyan }, filesBtn { "PICK FILES", ui::cyan },
            saveBtn { "SAVE STYLE", ui::green }, loadBtn { "LOAD STYLE", ui::cyan }, forgetBtn { "FORGET STYLE", ui::pink };
    juce::String sig;
};

// ---------------------------------------------------------------------------------------------
class TB303Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TB303Editor (TB303Processor&);
    ~TB303Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { relayout(); hookKeys(); }
    void visibilityChanged() override { relayout(); }
    bool keyPressed (const juce::KeyPress&) override;
    struct KeyHook : juce::KeyListener      // on the window itself, so the shortcuts work whichever control was clicked last (standalone too)
    {
        std::function<bool (const juce::KeyPress&)> fn;
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override
        {
            if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
            return fn && fn (k);
        }
    };

private:
    enum Focus { fNone = -1, fGen = 0, fSynth = 1, fLfo0 = 2 };     // fLfo0 + n = LFO n+1 full screen

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutAll();
    void setFocus (int f);
    void toggleFocus (int f) { setFocus (focus == f ? (int) fNone : f); }
    void refreshUndo();
    void hookGestures (XYCard&);
    void hookGestures (VFader&);
    void hookGestures (HFader&);
    void doLoad();
    void doDownload();
    void doSaveToFolder();
    void askName (const juce::String& title, const juce::String& msg, std::function<void (juce::String)> done);
    void heartPress();
    void renamePress();
    void styleFromFiles (bool folder);
    void saveStylePress();
    void loadStylePress();
    void keysPress();
    void refreshSetupButtons();
    void showNotice (const TB303Processor::Notice&);
    void showSaved (const juce::File&, bool revealFolder);
    void infoText (juce::AttributedString& out) const;
    std::vector<int> linkedLfos (int panelFocus) const;
    void setupCombo (juce::ComboBox&, const char* paramId);
    void applyZoom (float z);
    void shiftColours (bool reset);                                 // easter egg
    void hookKeys();
    KeyHook keyHook;
    juce::Component::SafePointer<juce::Component> hookedTop;
    void relayout() { if (lfos[3] != nullptr) { resized(); repaint(); } }   // some hosts/standalone only give the window its final size after we are shown
    int tickCount = 0;
    bool firstGenerate = true;
    void fitToScreen();

    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // everything lives in a 1100 x 1270 content component that is scaled to the window size
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void (juce::Point<float>)> onDown;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (onDown) onDown (e.position); }
    };
    static constexpr int kW = 1100, kH = 1270;          // single-column design size
    static constexpr int kWideW = 1700, kWideH = 1010;  // wide design size (LFOs in a right-hand column)
    int W = kW, H = kH;                                 // current design size = the window size / scale
    float scaleNow = 0.85f;
    bool wide = false;

    TB303Processor& proc;
    NebuleLookAndFeel laf;
    Content content;
    RollView roll;

    XYCard groove, filter, envelope;
    HFader notes, octave, delTime, revAmt, distSend, distColor;
    VFader bars, variation, bpm, mixBass, mixKick, mixHat, mixDel, mixRev, mixDist;      // the mixer strips are vertical, like the web app
    std::array<std::unique_ptr<LfoCard>, 4> lfos;

    juce::ComboBox rootBox, scaleBox, waveBox;
    NButton generateBtn { "GENERATE", ui::green, NButton::dice },
            previousBtn { "PREVIOUS", ui::cyan, NButton::prev },
            nextBtn     { "NEXT", ui::cyan, NButton::next },
            runBtn      { "PREVIEW LOOP", ui::green, NButton::play },
            loadBtn     { "LOAD .MID", ui::cyan },
            downloadBtn { "DOWNLOAD .MID", ui::violet },
            folderBtn   { "SAVE TO FOLDER", ui::violet },
            undoBtn     { "UNDO", ui::cyan },
            redoBtn     { "REDO", ui::cyan },
            muteBass    { "M", ui::pink },
            muteKick    { "M", ui::pink },
            muteHat     { "M", ui::pink },
            onDel       { "OFF", ui::pink },                            // delay / reverb / distortion: ON / OFF instead of M
            onRev       { "OFF", ui::pink },
            onDist      { "OFF", ui::pink },
            soloBass    { "S", ui::green },
            soloKick    { "S", ui::cyan },
            soloHat     { "S", ui::violet },
            soloDel     { "S", ui::cyan },
            soloRev     { "S", ui::violet },
            soloDist    { "S", ui::pink },
            closeBtn    { "", ui::silver, NButton::cross };
    LockButton lockBtn { "BPM" };
    ZoomBar zoomBar;
    struct TitleHit : juce::Component { std::function<void (int)> onClick; void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e.getNumberOfClicks()); } } titleHit;
    bool rainbow = false;                                           // triple-click on the title: slow hue drift (one lap per minute)
    NButton fitBtn { "FIT", ui::cyan };
    NButton fullBtn { "FULL", ui::violet };                       // standalone only: native full screen
    void toggleFullScreen();
    ModalOverlay modal;
    KeysOverlay keysPage;
    StyleOverlay stylePage { proc };
    NButton styleBtn { "STYLE", ui::violet }, keysBtn { "KEYS", ui::green }, heartBtn { "PLAY SAVED LOOPS", ui::pink }, renBtn { "RENAME", ui::cyan };
    std::vector<std::unique_ptr<NButton>> rootBtns, scaleBtns;          // ROOT / SCALE rows of the setup panel (the combos stay hidden for the host)
    juce::Rectangle<int> rowR2, styleDotR;
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<ComboAtt>  rootAtt, scaleAtt, waveAtt;
    std::unique_ptr<ButtonAtt> runAtt;
    std::vector<std::unique_ptr<ButtonAtt>> mixAtt;               // mute / on-off / solo buttons of the mixer

    int focus = fNone;
    juce::Rectangle<int> genPanel, synthPanel, mixPanel, setupPanel, lfoHeader, genTitleR, synthTitleR;
    juce::Rectangle<int> rollR, infoR, linkedLabel;
    juce::Rectangle<int> setupLabels[2], waveLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TB303Editor)
};
