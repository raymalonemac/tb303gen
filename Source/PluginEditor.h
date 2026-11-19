#pragma once
#include "PluginProcessor.h"
#include <map>

// ============================================================================================
//  Look: mint / cyan / violet / pink on deep blue-black, soft glows, XY pads  (same as the web app)
// ============================================================================================
namespace ui
{
const juce::Colour bg     { 0xff05090e };
const juce::Colour panel  { 0xff0a1118 };
const juce::Colour panel2 { 0xff0f1923 };
const juce::Colour line   { 0xff1b2a36 };
const juce::Colour fg     { 0xffe8eef6 };
const juce::Colour muted  { 0xff7d8aa0 };
const juce::Colour green  { 0xff5ef2c8 };       // mint
const juce::Colour cyan   { 0xff4cc9f0 };
const juce::Colour violet { 0xff7b8cff };
const juce::Colour silver { 0xffb9c6d8 };
const juce::Colour pink   { 0xffff5c8a };

inline juce::Colour lfoColour (int i)
{
    static const juce::uint32 c[4] = { 0xff5ef2c8, 0xff4cc9f0, 0xff7b8cff, 0xffe58cff };
    return juce::Colour (c[i & 3]);
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
    NebuleLookAndFeel();
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
        : juce::TextButton (text), neon (c), icon (i) {}
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
    juce::Colour colour;
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
    juce::Colour colour;
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
    juce::Colour colour;
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

// ---------------------------------------------------------------------------------------------
class TB303Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TB303Editor (TB303Processor&);
    ~TB303Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { relayout(); }
    void visibilityChanged() override { relayout(); }
    bool keyPressed (const juce::KeyPress&) override;

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
    void doLoad();
    void doDownload();
    void doSaveToFolder();
    void showSaved (const juce::File&, bool revealFolder);
    void infoText (juce::AttributedString& out) const;
    std::vector<int> linkedLfos (int panelFocus) const;
    void setupCombo (juce::ComboBox&, const char* paramId);
    void applyZoom (float z);
    void relayout() { if (lfos[3] != nullptr) { resized(); repaint(); } }   // some hosts/standalone only give the window its final size after we are shown
    int tickCount = 0;
    void fitToScreen();

    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // everything lives in a 1100 x 968 content component that is scaled to the window size
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void (juce::Point<float>)> onDown;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (onDown) onDown (e.position); }
    };
    static constexpr int kW = 1100, kH = 968;

    TB303Processor& proc;
    NebuleLookAndFeel laf;
    Content content;
    RollView roll;

    XYCard density, feel, filter, envelope, kit;
    VFader bars, variation, volume, bpm;
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
            drumsBtn    { "DRUMS", ui::cyan },
            synthBtn    { "BASS SYNTH", ui::green },
            closeBtn    { "", ui::silver, NButton::cross };
    LockButton lockBtn { "BPM" };
    ZoomBar zoomBar;
    NButton fitBtn { "FIT", ui::cyan };
    ModalOverlay modal;
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<ComboAtt>  rootAtt, scaleAtt, waveAtt;
    std::unique_ptr<ButtonAtt> synthAtt, drumsAtt, runAtt;

    int focus = fNone;
    juce::Rectangle<int> genPanel, synthPanel, drumPanel, setupPanel, lfoHeader, genTitleR, synthTitleR;
    juce::Rectangle<int> rollR, infoR, linkedLabel;
    juce::Rectangle<int> setupLabels[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TB303Editor)
};
