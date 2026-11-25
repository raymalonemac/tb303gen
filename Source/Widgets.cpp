#include "PluginEditor.h"
#include <cmath>

// ============================================================================================
//  look and feel + buttons
// ============================================================================================
void NebuleLookAndFeel::refresh()
{
    using namespace ui;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, fg);
    setColour (juce::ComboBox::backgroundColourId, panel2);
    setColour (juce::ComboBox::textColourId, fg);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, green);
    setColour (juce::PopupMenu::backgroundColourId, panel2);
    setColour (juce::PopupMenu::textColourId, fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, green);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, panel2);
    setColour (juce::TextButton::textColourOffId, fg);
}

void NebuleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (3.f);
    juce::Path path;
    path.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (ui::panel2.interpolatedWith (ui::green, down ? 0.30f : (over ? 0.16f : 0.05f)));
    g.fillPath (path);
    ui::glowStroke (g, path, ui::green, 1.2f);
}

void NebuleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (ui::green);
    g.setFont (ui::font (12.f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void NebuleLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    g.setColour (ui::panel2);
    g.fillRoundedRectangle (r, (float) h * 0.5f);
    g.setColour (ui::line.brighter (0.25f));
    g.drawRoundedRectangle (r, (float) h * 0.5f, 1.f);

    juce::Path arrow;
    const float ax = (float) w - 16.f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
    g.setColour (ui::green);
    g.fillPath (arrow);
}

namespace
{
float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.f, 0.f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

void drawIcon (juce::Graphics& g, NButton::Icon icon, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    switch (icon)
    {
        case NButton::dice:
        {
            const auto sq = r.reduced (0.8f);
            g.drawRoundedRectangle (sq, sq.getWidth() * 0.26f, 1.4f);
            const float d = sq.getWidth() * 0.24f, rr = 1.1f;
            for (auto p : { juce::Point<float> (sq.getX() + d, sq.getY() + d), juce::Point<float> (sq.getRight() - d, sq.getY() + d),
                            juce::Point<float> (sq.getCentreX(), sq.getCentreY()),
                            juce::Point<float> (sq.getX() + d, sq.getBottom() - d), juce::Point<float> (sq.getRight() - d, sq.getBottom() - d) })
                g.fillEllipse (p.x - rr, p.y - rr, rr * 2.f, rr * 2.f);
            break;
        }
        case NButton::prev:
        case NButton::next:
        case NButton::play:
        {
            juce::Path t;
            const bool left = icon == NButton::prev;
            if (left) t.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
            else      t.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
            g.fillPath (t);
            break;
        }
        case NButton::stop:
            g.fillRect (r.reduced (1.5f));
            break;
        case NButton::cross:
        {
            const auto q = r.reduced (2.f);
            g.drawLine (q.getX(), q.getY(), q.getRight(), q.getBottom(), 1.8f);
            g.drawLine (q.getX(), q.getBottom(), q.getRight(), q.getY(), 1.8f);
            break;
        }
        default: break;
    }
}
} // namespace

void NButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool enabled = isEnabled();
    const bool filled = enabled && (primary || getToggleState());
    const auto c = enabled ? ui::rot (neon) : ui::muted.withAlpha (0.5f);
    const auto r = getLocalBounds().toFloat().reduced (4.f);
    const float corner = r.getHeight() * 0.5f;

    juce::Path pill;
    pill.addRoundedRectangle (r, corner);

    if (filled)
    {
        float strength = 1.f;
        if (pulse) strength = 0.8f + 0.35f * (0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounterHiRes() * 0.0028f));
        ui::glowRect (g, r, c, corner, strength);
        g.setColour (c.withAlpha (down ? 0.75f : (over ? 1.f : 0.92f)));
        g.fillPath (pill);
    }
    else if (enabled)
    {
        g.setColour (ui::panel2.interpolatedWith (c, down ? 0.30f : (over ? 0.16f : 0.05f)));
        g.fillPath (pill);
        ui::glowStroke (g, pill, c, over ? 1.5f : 1.2f);
    }
    else
    {
        g.setColour (ui::panel2.withAlpha (0.6f));
        g.fillPath (pill);
        g.setColour (ui::line);
        g.strokePath (pill, juce::PathStrokeType (1.f));
    }

    const auto textCol = filled ? juce::Colour (0xff04120e) : c;
    const auto f = ui::font (11.5f, true).withExtraKerningFactor (0.10f);
    const auto text = getButtonText();
    const float tw = text.isEmpty() ? 0.f : textWidth (f, text);
    const float iconW = icon == none ? 0.f : 11.f, gap = (icon != none && text.isNotEmpty()) ? 7.f : 0.f;
    float x = r.getCentreX() - (tw + iconW + gap) * 0.5f;
    const auto iconR = [&] (float ix) { return juce::Rectangle<float> (ix, r.getCentreY() - iconW * 0.5f, iconW, iconW); };

    if (icon != none && icon != next) { drawIcon (g, icon, iconR (x), textCol); x += iconW + gap; }
    if (text.isNotEmpty())
    {
        g.setColour (textCol);
        g.setFont (f);
        g.drawText (text, juce::Rectangle<float> (x, r.getY(), tw + 2.f, r.getHeight()), juce::Justification::centredLeft, false);
        x += tw + gap;
    }
    if (icon == next) drawIcon (g, icon, iconR (x), textCol);
}

void LockButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool locked = getToggleState();
    const auto c = locked ? juce::Colour (0xffffc857) : ui::cyan;
    const auto b = getLocalBounds().toFloat();

    const float s = b.getHeight() * 0.98f / 24.f;
    const auto t = juce::AffineTransform::scale (s).translated (b.getX() + 7.f, b.getCentreY() - 12.f * s);

    juce::Path body, shackle;
    body.addRoundedRectangle (6.f, 11.f, 12.f, 9.f, 2.f);
    const float hp = juce::MathConstants<float>::halfPi;
    if (locked)
    {
        shackle.startNewSubPath (8.5f, 11.f);
        shackle.lineTo (8.5f, 8.f);
        shackle.addCentredArc (12.f, 8.f, 3.5f, 3.5f, 0.f, -hp, hp, false);
        shackle.lineTo (15.5f, 11.f);
    }
    else
    {
        shackle.startNewSubPath (8.5f, 11.f);
        shackle.lineTo (8.5f, 5.f);
        shackle.addCentredArc (12.f, 5.f, 3.5f, 3.5f, 0.f, -hp, hp, false);
        shackle.lineTo (15.5f, 8.f);                    // right leg lifted out of the body
    }
    body.applyTransform (t);
    shackle.applyTransform (t);

    g.setColour (c.withAlpha (locked ? 0.28f : 0.10f));
    g.fillPath (body);
    ui::glowStroke (g, body, c, 1.4f);
    ui::glowStroke (g, shackle, c, 1.4f);

    ui::caption (g, captionText, b.withTrimmedLeft (b.getHeight() + 8.f), over ? ui::fg : ui::muted, 9.5f);
}

// ============================================================================================
//  ghosts: where the LFOs push a control right now
// ============================================================================================
void collectGhosts (TB303Processor& pr, int destX, int destY, float nx, float ny, std::vector<Ghost>& out)
{
    out.clear();
    for (int l = 0; l < 4; ++l)
    {
        float cx = 0.f, cy = 0.f;
        const bool mx = destX >= 0 && pr.lfoContrib (l, destX, cx);
        const bool my = destY >= 0 && pr.lfoContrib (l, destY, cy);
        if (! mx && ! my) continue;
        out.push_back ({ ui::lfoColour (l), juce::jlimit (0.f, 1.f, nx + (mx ? cx : 0.f)), juce::jlimit (0.f, 1.f, ny + (my ? cy : 0.f)) });
    }
}

namespace
{
void drawPair (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name, const juce::String& value, juce::Justification j)
{
    juce::AttributedString as;
    as.setJustification (j);
    as.append (name + "  ", ui::mono (8.5f).withExtraKerningFactor (0.15f), ui::muted);
    as.append (value, ui::mono (9.5f), ui::fg);
    as.draw (g, area);
}
} // namespace

// ============================================================================================
//  XY card
// ============================================================================================
XYCard::XYCard (TB303Processor& p, const juce::String& t, const juce::String& idX, const juce::String& idY, int destX, int destY,
                juce::Colour col, const juce::String& nX, const juce::String& nY, bool gen)
    : colour0 (col), proc (p), title (t), nameX (nX), nameY (nY), dx (destX), dy (destY), generator (gen)
{
    px.p = p.apvts.getParameter (idX);
    py.p = p.apvts.getParameter (idY);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::Rectangle<float> XYCard::track() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (22.f).reduced (10.f, 6.f).reduced (16.f, 12.f);
}

void XYCard::paint (juce::Graphics& g)
{
    const auto colour = ui::rot (colour0);
    const auto b = getLocalBounds().toFloat();
    ui::card (g, b, colour, 12.f, 0.35f);
    ui::caption (g, title, b.withHeight (24.f).withTrimmedLeft (14.f), colour, 9.5f);

    const auto padBox = b.withTrimmedTop (24.f).withTrimmedBottom (22.f).reduced (10.f, 6.f);
    const auto t = track();

    g.setColour (juce::Colour (0xff060b10));
    g.fillRoundedRectangle (padBox, 8.f);
    g.setColour (colour.withAlpha (0.16f));
    g.drawRoundedRectangle (padBox, 8.f, 1.f);

    const float nx = px.get(), ny = py.get();
    const float hx = t.getX() + nx * t.getWidth(), hy = t.getBottom() - ny * t.getHeight();

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (padBox, 8.f);
    g.reduceClipRegion (clip);

    g.setColour (ui::steel (0.08f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (padBox.getX() + padBox.getWidth() * (float) i / 4.f), padBox.getY(), padBox.getBottom());
        g.drawHorizontalLine ((int) (padBox.getY() + padBox.getHeight() * (float) i / 4.f), padBox.getX(), padBox.getRight());
    }

    g.setColour (colour.withAlpha (0.12f));                                           // the quadrant under the handle
    g.fillRect (padBox.getX(), hy, hx - padBox.getX(), padBox.getBottom() - hy);
    g.setColour (colour.withAlpha (0.55f));
    g.drawVerticalLine ((int) hx, padBox.getY(), padBox.getBottom());
    g.drawHorizontalLine ((int) hy, padBox.getX(), padBox.getRight());
    g.restoreState();

    std::vector<Ghost> ghosts;
    collectGhosts (proc, dx, dy, nx, ny, ghosts);
    for (const auto& gh : ghosts)                                                     // one ring per LFO that moves this pad
    {
        const float gx = t.getX() + gh.x * t.getWidth(), gy = t.getBottom() - gh.y * t.getHeight();
        g.setColour (gh.c.withAlpha (0.18f)); g.fillEllipse (gx - 7.f, gy - 7.f, 14.f, 14.f);
        g.setColour (gh.c.withAlpha (0.95f)); g.drawEllipse (gx - 7.f, gy - 7.f, 14.f, 14.f, 1.6f);
    }

    const juce::Rectangle<float> h (hx - 14.f, hy - 10.f, 28.f, 20.f);
    ui::glowRect (g, h, colour, 7.f);
    g.setColour (juce::Colour (0xff0b131b));
    g.fillRoundedRectangle (h, 7.f);
    g.setColour (colour);
    g.drawRoundedRectangle (h, 7.f, 1.8f);
    g.drawLine (hx - 6.f, hy, hx + 6.f, hy, 2.f);

    const auto row = b.withTop (b.getBottom() - 22.f).reduced (14.f, 0.f);
    drawPair (g, row, nameX, px.text(), juce::Justification::centredLeft);
    drawPair (g, row, nameY, py.text(), juce::Justification::centredRight);
}

void XYCard::update (const juce::MouseEvent& e)
{
    const auto t = track();
    px.set ((e.position.x - t.getX()) / juce::jmax (1.f, t.getWidth()));
    py.set (1.f - (e.position.y - t.getY()) / juce::jmax (1.f, t.getHeight()));
}

void XYCard::mouseDown (const juce::MouseEvent& e)
{
    if (generator && onGestureStart) onGestureStart();
    px.begin(); py.begin();
    update (e);
}

void XYCard::mouseDrag (const juce::MouseEvent& e) { update (e); }

void XYCard::mouseUp (const juce::MouseEvent&)
{
    px.end(); py.end();
    if (generator && onGestureEnd) onGestureEnd();
}

// ============================================================================================
//  vertical fader
// ============================================================================================
VFader::VFader (TB303Processor& p, const juce::String& n, const juce::String& id, int d, juce::Colour col, bool gen)
    : proc (p), name (n), dest (d), colour0 (col), generator (gen)
{
    pr.p = p.apvts.getParameter (id);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> VFader::track() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (24.f).reduced (0.f, 10.f);
}

void VFader::paint (juce::Graphics& g)
{
    const auto colour = ui::rot (colour0);
    const auto b = getLocalBounds().toFloat();
    const auto t = track();
    const float cx = b.getCentreX();
    const float n = pr.get();
    const float y = t.getBottom() - n * t.getHeight();
    const auto c = locked ? colour.withMultipliedSaturation (0.2f).withMultipliedAlpha (0.45f) : colour;

    g.setColour (ui::fg.withAlpha (locked ? 0.5f : 0.95f));
    g.setFont (ui::mono (10.f));
    g.drawText (pr.text(), b.withHeight (20.f), juce::Justification::centred, false);

    juce::Path groove, fill;
    groove.startNewSubPath (cx, t.getY() - 4.f); groove.lineTo (cx, t.getBottom() + 4.f);
    fill.startNewSubPath (cx, t.getBottom() + 4.f); fill.lineTo (cx, y);
    const auto rounded = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    g.setColour (juce::Colour (0xff111b24));
    g.strokePath (groove, rounded (6.f));
    if (! locked) { g.setColour (c.withAlpha (0.10f)); g.strokePath (fill, rounded (16.f)); }
    g.setColour (c.withAlpha (0.85f));
    g.strokePath (fill, rounded (6.f));

    std::vector<Ghost> ghosts;
    collectGhosts (proc, dest, -1, n, 0.f, ghosts);
    for (const auto& gh : ghosts)
    {
        const float gy = t.getBottom() - gh.x * t.getHeight();
        g.setColour (gh.c.withAlpha (0.2f)); g.fillEllipse (cx + 14.f, gy - 5.f, 10.f, 10.f);
        g.setColour (gh.c);                   g.drawEllipse (cx + 14.f, gy - 5.f, 10.f, 10.f, 1.5f);
    }

    const juce::Rectangle<float> h (cx - 17.f, y - 8.f, 34.f, 16.f);
    if (! locked) ui::glowRect (g, h, c, 6.f);
    g.setColour (juce::Colour (0xff0b131b));
    g.fillRoundedRectangle (h, 6.f);
    g.setColour (locked ? c : ui::silver);
    g.drawRoundedRectangle (h, 6.f, 1.8f);
    g.drawLine (cx - 8.f, y, cx + 8.f, y, 2.f);

    if (name.isNotEmpty())
        ui::caption (g, name, b.withTrimmedTop (b.getHeight() - 20.f), ui::muted, 8.5f, juce::Justification::centred, 0.15f);
}

void VFader::update (const juce::MouseEvent& e)
{
    if (locked) return;
    const auto t = track();
    pr.set (1.f - (e.position.y - t.getY()) / juce::jmax (1.f, t.getHeight()));
}

void VFader::mouseDown (const juce::MouseEvent& e)
{
    if (locked) return;
    if (onGestureStart) onGestureStart();            // generator faders: tweak gesture; DELAY / REVERB / DIST: switch the effect on
    pr.begin();
    update (e);
}

void VFader::mouseDrag (const juce::MouseEvent& e) { update (e); }

void VFader::mouseUp (const juce::MouseEvent&)
{
    if (locked) return;
    pr.end();
    if (generator && onGestureEnd) onGestureEnd();
}

// ============================================================================================
//  horizontal fader
// ============================================================================================
HFader::HFader (TB303Processor& p, const juce::String& n, const juce::String& id, int d, juce::Colour col, bool gen)
    : proc (p), name (n), dest (d), colour0 (col), generator (gen)
{
    pr.p = p.apvts.getParameter (id);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

juce::Rectangle<float> HFader::track() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (78.f).withTrimmedRight (56.f).reduced (10.f, 0.f);
}

void HFader::paint (juce::Graphics& g)
{
    const auto colour = ui::rot (colour0);
    const auto b = getLocalBounds().toFloat();
    const auto t = track();
    const float cy = b.getCentreY();
    const float n = pr.get();
    const float x = t.getX() + n * t.getWidth();

    ui::caption (g, name, b.withWidth (74.f).withTrimmedLeft (4.f), colour, 9.5f, juce::Justification::centredLeft, 0.2f);
    g.setColour (ui::fg.withAlpha (0.95f));
    g.setFont (ui::mono (10.f));
    g.drawText (pr.text(), b.withTrimmedLeft (b.getWidth() - 54.f), juce::Justification::centredRight, false);

    juce::Path groove, fill;
    groove.startNewSubPath (t.getX() - 4.f, cy); groove.lineTo (t.getRight() + 4.f, cy);
    fill.startNewSubPath (t.getX() - 4.f, cy);   fill.lineTo (x, cy);
    const auto rounded = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    g.setColour (juce::Colour (0xff111b24));
    g.strokePath (groove, rounded (6.f));
    g.setColour (colour.withAlpha (0.10f)); g.strokePath (fill, rounded (16.f));
    g.setColour (colour.withAlpha (0.85f)); g.strokePath (fill, rounded (6.f));

    std::vector<Ghost> ghosts;
    collectGhosts (proc, dest, -1, n, 0.f, ghosts);
    for (const auto& gh : ghosts)
    {
        const float gx = t.getX() + gh.x * t.getWidth();
        const float gy = cy + juce::jmin (12.f, b.getHeight() * 0.5f - 4.f);
        g.setColour (gh.c.withAlpha (0.2f)); g.fillEllipse (gx - 4.f, gy - 4.f, 8.f, 8.f);
        g.setColour (gh.c);                   g.drawEllipse (gx - 4.f, gy - 4.f, 8.f, 8.f, 1.4f);
    }

    const float th = juce::jmin (34.f, b.getHeight() - 2.f);
    const juce::Rectangle<float> h (x - 8.f, cy - th * 0.5f, 16.f, th);
    ui::glowRect (g, h, colour, 6.f);
    g.setColour (juce::Colour (0xff0b131b));
    g.fillRoundedRectangle (h, 6.f);
    g.setColour (ui::silver);
    g.drawRoundedRectangle (h, 6.f, 1.8f);
    g.drawLine (x, cy - 8.f, x, cy + 8.f, 2.f);
}

void HFader::update (const juce::MouseEvent& e)
{
    const auto t = track();
    pr.set ((e.position.x - t.getX()) / juce::jmax (1.f, t.getWidth()));
}

void HFader::mouseDown (const juce::MouseEvent& e)
{
    if (generator && onGestureStart) onGestureStart();
    pr.begin();
    update (e);
}

void HFader::mouseDrag (const juce::MouseEvent& e) { update (e); }

void HFader::mouseUp (const juce::MouseEvent&)
{
    pr.end();
    if (generator && onGestureEnd) onGestureEnd();
}

void HFader::mouseDoubleClick (const juce::MouseEvent&)
{
    if (pr.p == nullptr) return;
    if (generator && onGestureStart) onGestureStart();
    pr.begin(); pr.set (pr.p->getDefaultValue()); pr.end();
    if (generator && onGestureEnd) onGestureEnd();
}

// ============================================================================================
//  LFO card
// ============================================================================================
namespace
{
void shapeIcon (juce::Path& p, juce::Rectangle<float> r, int shape)
{
    const auto q = r.reduced (5.f, 5.f);
    auto P = [&] (float fx, float fy) { return juce::Point<float> (q.getX() + fx * q.getWidth(), q.getY() + fy * q.getHeight()); };
    switch (shape)
    {
        case tb::shSine:
            p.startNewSubPath (P (0.f, 0.5f));
            for (int i = 1; i <= 16; ++i) { const float f = (float) i / 16.f; p.lineTo (P (f, 0.5f - 0.5f * std::sin (f * 6.2831853f))); }
            break;
        case tb::shTri:    p.startNewSubPath (P (0.f, 1.f)); p.lineTo (P (0.25f, 0.f)); p.lineTo (P (0.75f, 1.f)); p.lineTo (P (1.f, 0.f)); break;
        case tb::shSaw:    p.startNewSubPath (P (0.f, 1.f)); p.lineTo (P (0.5f, 0.f)); p.lineTo (P (0.5f, 1.f)); p.lineTo (P (1.f, 0.f)); break;
        case tb::shSquare: p.startNewSubPath (P (0.f, 1.f)); p.lineTo (P (0.f, 0.f)); p.lineTo (P (0.5f, 0.f)); p.lineTo (P (0.5f, 1.f)); p.lineTo (P (1.f, 1.f)); p.lineTo (P (1.f, 0.f)); break;
        default:           p.startNewSubPath (P (0.f, 0.6f)); p.lineTo (P (0.25f, 0.6f)); p.lineTo (P (0.25f, 0.1f)); p.lineTo (P (0.5f, 0.1f));
                           p.lineTo (P (0.5f, 0.9f)); p.lineTo (P (0.75f, 0.9f)); p.lineTo (P (0.75f, 0.35f)); p.lineTo (P (1.f, 0.35f)); break;
    }
}

float scopeHash (float x, int n)
{
    const double v = std::sin ((double) x * 127.1 + n * 31.7) * 43758.5453;
    return (float) ((v - std::floor (v)) * 2.0 - 1.0);
}
} // namespace

LfoCard::LfoCard (TB303Processor& p, int i)
    : rateDepth (p, "RATE / DEPTH", "lfo" + juce::String (i + 1) + "rate", "lfo" + juce::String (i + 1) + "depth", -1, -1,
                 ui::lfoColour (i), "RATE", "DEPTH", false),
      proc (p), index (i), colour0 (juce::Colour (0))
{
    shape.p = p.apvts.getParameter ("lfo" + juce::String (i + 1) + "shape");
    addAndMakeVisible (rateDepth);
}

void LfoCard::resized()
{
    const auto b = getLocalBounds().toFloat().reduced (10.f, 8.f);
    const float H = (float) getHeight();
    titleR = { b.getX(), b.getY(), 110.f, 22.f };

    const float sw = 26.f, gap = 4.f;
    float x = b.getRight() - (full ? 42.f : 0.f) - (sw * (float) tb::kShapes + gap * (float) (tb::kShapes - 1));
    for (int s = 0; s < tb::kShapes; ++s) { shapeR[(size_t) s] = { x, b.getY(), sw, 22.f }; x += sw + gap; }

    const float scopeH = juce::jmax (30.f, H * 0.12f), xyH = H * 0.37f;
    scopeR = { b.getX(), b.getY() + 26.f, b.getWidth(), scopeH };
    const float xyY = scopeR.getBottom() + 4.f;
    rateDepth.setBounds ({ (int) b.getX(), (int) xyY, (int) b.getWidth(), (int) xyH });

    const float chipsTop = xyY + xyH + 4.f, chipsH = b.getBottom() - chipsTop;
    const int cols = 4, rows = (tb::kDests + cols - 1) / cols;
    const float cg = 3.f, cw = (b.getWidth() - cg * (float) (cols - 1)) / (float) cols, ch = (chipsH - cg * (float) (rows - 1)) / (float) rows;
    for (int d = 0; d < tb::kDests; ++d)
        chipR[(size_t) d] = { b.getX() + (float) (d % cols) * (cw + cg), chipsTop + (float) (d / cols) * (ch + cg), cw, ch };
}

void LfoCard::mouseDown (const juce::MouseEvent& e)
{
    const auto pt = e.position;
    if (titleR.contains (pt)) { if (onTitleClick) onTitleClick (index); return; }
    for (int s = 0; s < tb::kShapes; ++s)
        if (shapeR[(size_t) s].contains (pt))
        {
            shape.begin();
            if (shape.p != nullptr) shape.set (shape.p->convertTo0to1 ((float) s));
            shape.end();
            return;
        }
    for (int d = 0; d < tb::kDests; ++d)
        if (chipR[(size_t) d].contains (pt))
        {
            proc.setLfoMask (index, proc.getLfoMask (index) ^ (1u << d));
            return;
        }
}

void LfoCard::paintScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto colour = ui::lfoColour (index);
    g.setColour (juce::Colour (0xff060b10));
    g.fillRoundedRectangle (r, 7.f);
    g.setColour (colour.withAlpha (0.14f));
    g.drawRoundedRectangle (r, 7.f, 1.f);

    const int shp = juce::jlimit (0, tb::kShapes - 1, juce::roundToInt (shape.get() * (float) (tb::kShapes - 1)));
    const float depth = proc.lfoDepth (index);
    const float mid = r.getCentreY(), amp = (r.getHeight() * 0.5f - 4.f) * (0.25f + 0.75f * std::abs (depth)) * (depth < 0.f ? -1.f : 1.f);

    g.setColour (ui::steel (0.14f));
    g.drawHorizontalLine ((int) mid, r.getX() + 4.f, r.getRight() - 4.f);

    const float W = r.getWidth() - 12.f, x0 = r.getX() + 6.f;
    juce::Path wave;
    for (float x = 0.f; x <= W; x += 2.f)
    {
        const float ph = juce::jmin (0.9999f, x / W);
        const float v = shp == tb::shSH ? scopeHash (std::floor (ph * 6.f), index + 1) : tb::shapeAt (shp, ph, 0.f);
        const float y = mid - v * amp;
        if (x == 0.f) wave.startNewSubPath (x0 + x, y); else wave.lineTo (x0 + x, y);
    }
    ui::glowStroke (g, wave, colour, 1.3f);

    const float ph = proc.uiLfoPhase[(size_t) index].load(), val = proc.uiLfoVal[(size_t) index].load();
    const float dx = x0 + ph * W, dy = mid - val * amp;
    g.setColour (colour.withAlpha (0.35f)); g.fillEllipse (dx - 6.f, dy - 6.f, 12.f, 12.f);
    g.setColour (juce::Colours::white);     g.fillEllipse (dx - 3.f, dy - 3.f, 6.f, 6.f);
}

void LfoCard::paint (juce::Graphics& g)
{
    const auto colour = ui::lfoColour (index);
    const auto b = getLocalBounds().toFloat();
    g.setColour (colour.withAlpha (0.05f));
    g.fillRoundedRectangle (b.expanded (2.f), 18.f);
    ui::card (g, b, colour, 16.f, 0.45f);

    ui::caption (g, "LFO " + juce::String (index + 1), titleR.withWidth (52.f), colour, 10.5f);
    {
        const float ix = titleR.getX() + 54.f, iy = titleR.getCentreY() - 5.f;       // expand arrows
        juce::Path p;
        p.startNewSubPath (ix, iy + 10.f); p.lineTo (ix + 10.f, iy);
        p.startNewSubPath (ix + 4.f, iy); p.lineTo (ix + 10.f, iy); p.lineTo (ix + 10.f, iy + 6.f);
        p.startNewSubPath (ix, iy + 4.f); p.lineTo (ix, iy + 10.f); p.lineTo (ix + 6.f, iy + 10.f);
        g.setColour (colour.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    const int sel = juce::jlimit (0, tb::kShapes - 1, juce::roundToInt (shape.get() * (float) (tb::kShapes - 1)));
    for (int s = 0; s < tb::kShapes; ++s)
    {
        const auto r = shapeR[(size_t) s];
        juce::Path icon;
        shapeIcon (icon, r, s);
        if (s == sel)
        {
            ui::glowRect (g, r, colour, 6.f, 0.8f);
            g.setColour (colour);
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (juce::Colour (0xff04120e));
            g.strokePath (icon, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (ui::panel2);
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (colour.withAlpha (0.45f));
            g.drawRoundedRectangle (r, 6.f, 1.f);
            g.setColour (colour);
            g.strokePath (icon, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    paintScope (g, scopeR);

    const float level = juce::jmin (1.f, std::abs (proc.uiLfoVal[(size_t) index].load() * proc.lfoDepth (index)) * 1.6f);
    const float fs = juce::jlimit (8.f, 13.f, chipR[0].getHeight() * 0.5f);
    for (int d = 0; d < tb::kDests; ++d)
    {
        const auto r = chipR[(size_t) d];
        const bool on = routedTo (d);
        const float corner = r.getHeight() * 0.5f;
        if (on)
        {
            ui::glowRect (g, r, colour, corner, 0.4f + 1.1f * level);
            g.setColour (colour.withAlpha (0.20f + 0.40f * level));
            g.fillRoundedRectangle (r, corner);
            g.setColour (colour);
            g.drawRoundedRectangle (r, corner, 1.2f);
        }
        else
        {
            g.setColour (ui::panel2.withAlpha (0.7f));
            g.fillRoundedRectangle (r, corner);
            g.setColour (ui::line.brighter (0.2f));
            g.drawRoundedRectangle (r, corner, 1.f);
        }
        ui::caption (g, tb::destDef (d).label, r, on ? juce::Colours::white : ui::muted, fs, juce::Justification::centred, 0.04f);

        if (on)                                                                 // dots: which other LFOs also drive this destination
        {
            float dxp = r.getRight() - corner * 0.6f;
            for (int o = 0; o < 4; ++o)
                if (o != index && ((proc.getLfoMask (o) >> d) & 1u))
                {
                    g.setColour (ui::lfoColour (o));
                    g.fillEllipse (dxp - 2.f, r.getCentreY() - 2.f, 4.f, 4.f);
                    dxp -= 6.f;
                }
        }
    }
}

// ============================================================================================
//  roll
// ============================================================================================
namespace
{
constexpr int kVis = 32;
constexpr float kLead = kVis / 2.f;
const juce::Colour kRollBg { 0xff070d13 };

void pillAt (juce::Graphics& g, float x, float yc, float w, float h, juce::Colour c, float glow, float alpha)
{
    const juce::Rectangle<float> r (x, yc - h * 0.5f, w, h);
    if (glow > 0.f)
    {
        g.setColour (c.withAlpha (0.10f * alpha)); g.fillRoundedRectangle (r.expanded (5.f), 6.f);
        g.setColour (c.withAlpha (0.22f * alpha)); g.fillRoundedRectangle (r.expanded (2.5f), 4.5f);
    }
    g.setColour (c.withAlpha (alpha));
    g.fillRoundedRectangle (r, 3.f);
}
} // namespace

void RollView::stepRope (Rope& r, float x1, float y1, float x2, float y2, float dt)
{
    constexpr float grav = 1300.f, drag = 0.985f, bend = 0.12f;
    auto& p = r.pts;
    const int n = (int) p.size();
    const float dist = std::hypot (x2 - x1, y2 - y1);
    const float rest = (dist * 1.12f + 6.f) / (float) (n - 1);                    // a little slack: hangs in a soft U
    constexpr int sub = 3;
    const float h = dt / (float) sub;

    auto pin = [&] { p[0].x = x1; p[0].y = y1; p[(size_t) (n - 1)].x = x2; p[(size_t) (n - 1)].y = y2; };

    for (int s = 0; s < sub; ++s)
    {
        for (int i = 1; i < n - 1; ++i)
        {
            auto& q = p[(size_t) i];
            const float vx = (q.x - q.px) * drag, vy = (q.y - q.py) * drag;
            q.px = q.x; q.py = q.y;
            q.x += vx; q.y += vy + grav * h * h;
        }
        pin();
        for (int it = 0; it < 10; ++it)                                              // keep the segment lengths
        {
            for (int i = 0; i < n - 1; ++i)
            {
                auto& a = p[(size_t) i];
                auto& b = p[(size_t) (i + 1)];
                float dx = b.x - a.x, dy = b.y - a.y;
                const float d = std::max (0.0001f, std::hypot (dx, dy)), diff = (d - rest) / d * 0.5f;
                dx *= diff; dy *= diff;
                if (i > 0)     { a.x += dx; a.y += dy; }
                if (i < n - 2) { b.x -= dx; b.y -= dy; }
            }
            pin();
        }
        for (int i = 1; i < n - 1; ++i)                                              // soft bending stiffness: no kinks
        {
            const auto& a = p[(size_t) (i - 1)];
            const auto& b = p[(size_t) (i + 1)];
            auto& q = p[(size_t) i];
            q.x += ((a.x + b.x) * 0.5f - q.x) * bend;
            q.y += ((a.y + b.y) * 0.5f - q.y) * bend;
        }
    }
}

void RollView::drawCable (juce::Graphics& g, const Rope& r, float x1, float y1, float x2, float y2,
                          juce::Colour c1, juce::Colour c2, float alpha, float T, float seed)
{
    const int n = (int) r.pts.size();
    juce::Path path;
    path.startNewSubPath (x1, y1);
    for (int i = 1; i < n - 1; ++i)
    {
        const auto& a = r.pts[(size_t) i];
        const auto& b = r.pts[(size_t) (i + 1)];
        path.quadraticTo (a.x, a.y, (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    }
    path.lineTo (x2, y2);

    const float th = 1.6f;
    const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
    g.setColour (juce::Colour (0xff020609).withAlpha (0.9f * alpha));                                   // sleeve
    g.strokePath (path, juce::PathStrokeType (th + 2.6f, juce::PathStrokeType::curved, cap));
    g.setColour (ui::green.withAlpha (0.14f * alpha));                                                  // glow
    g.strokePath (path, juce::PathStrokeType (th * 5.f, juce::PathStrokeType::curved, cap));
    g.setGradientFill (juce::ColourGradient (c1.withAlpha (alpha), x1, 0.f, c2.withAlpha (alpha), x2 + 0.01f, 0.f, false));
    g.strokePath (path, juce::PathStrokeType (th, juce::PathStrokeType::curved, cap));                  // cable

    // sparks run along the cable
    std::vector<juce::Point<float>> poly;
    poly.push_back ({ x1, y1 });
    for (int i = 1; i < n - 1; ++i) poly.push_back ({ r.pts[(size_t) i].x, r.pts[(size_t) i].y });
    poly.push_back ({ x2, y2 });
    const float spacing = 10.5f;
    float want = std::fmod (T * 34.f + seed, spacing), walked = 0.f;
    g.setColour (juce::Colour (0xfff2fffb).withAlpha (0.95f * alpha));
    for (size_t i = 0; i + 1 < poly.size(); ++i)
    {
        const float seg = poly[i].getDistanceFrom (poly[i + 1]);
        while (want <= walked + seg)
        {
            const float f = seg > 0.f ? (want - walked) / seg : 0.f;
            const auto pt = poly[i] + (poly[i + 1] - poly[i]) * f;
            g.fillEllipse (pt.x - 1.1f, pt.y - 1.1f, 2.2f, 2.2f);
            want += spacing;
        }
        walked += seg;
    }

    for (const auto& j : { std::make_pair (juce::Point<float> (x1, y1), c1), std::make_pair (juce::Point<float> (x2, y2), c2) })   // jacks
    {
        const float rad = th * 1.25f;
        g.setColour (juce::Colour (0xff05090e).withAlpha (alpha)); g.fillEllipse (j.first.x - rad, j.first.y - rad, rad * 2.f, rad * 2.f);
        g.setColour (j.second.withAlpha (alpha));                  g.drawEllipse (j.first.x - rad, j.first.y - rad, rad * 2.f, rad * 2.f, 1.3f);
    }
}

void RollView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float W = b.getWidth(), H = b.getHeight(), sw = W / (float) kVis;

    juce::Path frame;
    frame.addRoundedRectangle (b.reduced (1.f), 14.f);
    g.setColour (kRollBg);
    g.fillPath (frame);
    g.saveState();
    g.reduceClipRegion (frame);

    // ---- what to show
    const bool playing = proc.uiPlaying.load();
    tb::Pattern curPat, nextPat;
    bool hasNext = false;
    double pos = idlePos;
    int cur = -1;
    if (playing)
    {
        curPat = proc.getPlayingCopy();
        hasNext = proc.uiHasPending.load();
        if (hasNext) nextPat = proc.getPatternCopy();
        const double elapsed = juce::jlimit (0.0, 100.0, juce::Time::getMillisecondCounterHiRes() - proc.uiPosStamp.load());
        pos = std::fmod (proc.uiStepPos.load() + elapsed / 1000.0 * proc.uiBpm.load() / 60.0 * 4.0, (double) curPat.len);
        cur = (int) std::floor (pos);
        idlePos = pos;                                       // when it stops, the view rests where it was
    }
    else
    {
        curPat = proc.getPatternCopy();
        pos = idlePos = 0.0;                                 // stopped: always rests on bar 1
    }
    wasPlaying = playing;

    const int boundary = playing ? (cur / tb::kStepsPerBar + 1) * tb::kStepsPerBar : 0;   // where a waiting pattern takes over
    auto stepAt = [&] (long long k) -> const tb::Step&
    {
        return (hasNext && k >= boundary) ? nextPat.at (k - boundary) : curPat.at (k);
    };
    const double s0 = pos - kLead;
    const long long k0 = (long long) std::floor (s0) - 1, k1 = (long long) std::ceil (s0 + kVis) + 1;
    auto md = [] (long long n, long long L) { return ((n % L) + L) % L; };

    int lo = 127, hi = 0;
    auto scan = [&] (const tb::Pattern& p)
    {
        for (int i = 0; i < p.len; ++i)
            if (p[(size_t) i].active) { lo = std::min (lo, (int) p[(size_t) i].pitch); hi = std::max (hi, (int) p[(size_t) i].pitch); }
    };
    scan (curPat);
    if (hasNext) scan (nextPat);
    const bool any = hi >= lo;
    const float rowH = (H - 42.f) / (float) std::max (1, hi - lo);
    auto noteY = [&] (int p) { return H - 14.f - (float) (p - lo) / (float) std::max (1, hi - lo) * (H - 42.f); };

    // ---- grid + bar numbers
    for (long long k = k0; k <= k1; ++k)
    {
        const float x = std::round ((float) ((double) k - s0) * sw);
        if (md (k, tb::kStepsPerBar) == 0)
        {
            const bool inc = hasNext && k >= boundary;                      // this bar belongs to the pattern that is about to start
            const auto& pat = inc ? nextPat : curPat;
            const long long off = inc ? k - boundary : k, L = std::max (1, pat.len);
            const int barNo = (int) (md (off, L) / tb::kStepsPerBar) + 1;
            if (barNo == 1)                                                  // the line in front of bar 1: thick, always glowing, flashing on the beat
            {
                const double ph = std::fmod (juce::Time::getMillisecondCounterHiRes() / 1000.0 * proc.uiBpm.load() / 60.0, 1.0);
                const float pulse = 0.45f + 0.55f * (float) std::pow (1.0 - ph, 1.5);
                g.setColour (ui::green.withAlpha (0.10f * pulse)); g.fillRect (x - 9.f, 0.f, 20.f, H);
                g.setColour (ui::green.withAlpha (0.22f * pulse)); g.fillRect (x - 5.f, 0.f, 12.f, H);
                g.setColour (ui::green.withAlpha (0.55f + 0.45f * pulse)); g.fillRect (x - 2.f, 0.f, 5.f, H);
            }
            else { g.setColour (ui::steel (0.30f)); g.fillRect (x, 0.f, 2.f, H); }
            const auto label = (inc ? "NEXT - BAR " : "BAR ") + juce::String (barNo);
            const juce::Rectangle<float> lr (x + 10.f, 3.f, 170.f, 20.f);
            g.setFont (ui::mono (15.f));
            g.setColour (ui::green.withAlpha (0.16f));
            for (auto o : { juce::Point<float> (-1.f, 0.f), juce::Point<float> (1.f, 0.f), juce::Point<float> (0.f, -1.f), juce::Point<float> (0.f, 1.f) })
                g.drawText (label, lr.translated (o.x, o.y), juce::Justification::centredLeft, false);
            g.setColour (ui::green);
            g.drawText (label, lr, juce::Justification::centredLeft, false);
        }
        else if (md (k, 4) == 0) { g.setColour (ui::steel (0.14f)); g.fillRect (x, 0.f, 1.f, H); }
        else                     { g.setColour (ui::steel (0.05f)); g.fillRect (x, 0.f, 1.f, H); }
    }

    // ---- pitch rows: the root's rows picked out in mint
    if (any)
    {
        const int span = hi - lo, every = span > 26 ? 12 : 1, rootPc = proc.currentRoot() % 12;
        for (int p = lo; p <= hi; ++p)
        {
            if (every > 1 && p % 12 != rootPc) continue;
            const bool isRoot = p % 12 == rootPc;
            g.setColour (isRoot ? ui::green.withAlpha (0.20f) : ui::steel (0.07f));
            g.fillRect (0.f, std::round (noteY (p)), W, 1.f);
        }
    }

    // ---- notes and slide cables
    const double nowS = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float dt = lastT > 0.0 ? (float) juce::jlimit (1.0 / 240.0, 1.0 / 30.0, nowS - lastT) : 1.f / 60.f;
    lastT = nowS;
    ++ropeFrame;
    const float T = (float) nowS;

    if (any)
    {
        for (long long k = k0; k <= k1; ++k)
        {
            const auto& s = stepAt (k);
            if (! s.active) continue;
            const float al = (long long) k < cur ? 0.38f : 1.f;
            const float x = (float) ((double) k - s0) * sw, y = noteY ((int) s.pitch), w = sw * 0.62f;
            const auto col = s.accent ? ui::pink : ui::cyan;
            const float ph2 = juce::jlimit (5.f, 11.f, rowH * 0.85f);
            pillAt (g, x, y, w, ph2, col, (long long) k < cur ? 0.f : 12.f, al);
            if (hasNext && k >= boundary)                                    // notes of the incoming pattern get a pulsing outline
            {
                g.setColour (juce::Colour (0xffe8fffa).withAlpha (0.55f + 0.45f * std::sin (T * 7.f + (float) k * 0.4f)));
                g.drawRoundedRectangle (x - 1.5f, y - ph2 * 0.5f - 1.5f, w + 3.f, ph2 + 3.f, 4.f, 1.4f);
            }

            const auto& nx = stepAt (k + 1);
            if (s.slide && nx.active)
            {
                const float x1 = x + w, y1 = y, x2 = (float) ((double) (k + 1) - s0) * sw, y2 = noteY ((int) nx.pitch);
                auto it = ropes.find (k);
                if (it == ropes.end())
                {
                    Rope r;
                    for (size_t i = 0; i < r.pts.size(); ++i)         // starts straight and drops under gravity
                    {
                        const float f = (float) i / (float) (r.pts.size() - 1);
                        const float px = x1 + (x2 - x1) * f, py = y1 + (y2 - y1) * f;
                        r.pts[i] = { px, py, px, py };
                    }
                    it = ropes.emplace (k, r).first;
                }
                it->second.seen = ropeFrame;
                stepRope (it->second, x1, y1, x2, y2, dt);
                drawCable (g, it->second, x1, y1, x2, y2, col, nx.accent ? ui::pink : ui::cyan, al, T, (float) k * 1.7f);
            }
        }
        for (auto it = ropes.begin(); it != ropes.end();)             // scrolled out of view
            it = it->second.seen != ropeFrame ? ropes.erase (it) : std::next (it);
    }

    // ---- soft fade at both edges
    const float fw = W * 0.07f;
    g.setGradientFill (juce::ColourGradient (kRollBg, 0.f, 0.f, kRollBg.withAlpha (0.f), fw, 0.f, false));
    g.fillRect (0.f, 0.f, fw, H);
    g.setGradientFill (juce::ColourGradient (kRollBg, W, 0.f, kRollBg.withAlpha (0.f), W - fw, 0.f, false));
    g.fillRect (W - fw, 0.f, fw, H);

    if (! playing)
    {
        ui::caption (g, "DRAG = MIDI", b.withTrimmedRight (16.f).withTrimmedBottom (6.f), ui::steel (0.9f), 8.5f, juce::Justification::bottomRight, 0.12f);
    }

    // ---- playhead, dead centre
    const float px = kLead * sw, a = playing ? 1.f : 0.35f;
    g.setColour (ui::green.withAlpha (0.08f * a)); g.fillRect (px - 7.f, 0.f, 16.f, H);
    g.setColour (ui::green.withAlpha (0.16f * a)); g.fillRect (px - 3.f, 0.f, 8.f, H);
    g.setColour (juce::Colour (0xffe8eef6).withAlpha (a)); g.fillRect (px - 0.5f, 0.f, 2.f, H);
    juce::Path tri;
    tri.addTriangle (px - 6.f, 0.f, px + 7.f, 0.f, px + 0.5f, 8.f);
    g.fillPath (tri);

    g.restoreState();
    g.setColour (ui::green.withAlpha (0.28f));
    g.strokePath (frame, juce::PathStrokeType (1.f));
}

void RollView::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (e.getNumberOfClicks() == 3 && onEgg) onEgg();
}

// the roll never scrolls here: dragging it hands the line to your DAW (or Finder) as a .mid, bass only
void RollView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto file = proc.makeTempMidiFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

// ============================================================================================
//  modal
// ============================================================================================
void ModalOverlay::show (const juce::String& t, const juce::String& f, const juce::String& txt, bool error, std::vector<Btn> b)
{
    close();
    title = t; file = f; text = txt; isError = error; defs = std::move (b);
    for (size_t i = 0; i < defs.size(); ++i)
    {
        auto btn = std::make_unique<NButton> (defs[i].label, defs[i].primary ? ui::green : ui::cyan);
        btn->primary = defs[i].primary;
        juce::Component::SafePointer<ModalOverlay> safe (this);
        btn->onClick = [safe, i]
        {
            juce::MessageManager::callAsync ([safe, i] { if (safe != nullptr) safe->activate ((int) i); });   // never delete a button inside its own click
        };
        addAndMakeVisible (*btn);
        buttons.push_back (std::move (btn));
    }
    setVisible (true);
    toFront (false);
    resized();
    repaint();
}

void ModalOverlay::activate (int i)
{
    std::function<void()> fn;
    if (i >= 0 && i < (int) defs.size()) fn = defs[(size_t) i].fn;
    close();
    if (fn) fn();
}

void ModalOverlay::close()
{
    buttons.clear();
    defs.clear();
    setVisible (false);
}

juce::Rectangle<int> ModalOverlay::box() const
{
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    as.append (text, ui::font (13.f), ui::fg);
    juce::TextLayout layout;
    layout.createLayout (as, 400.f);
    const int h = 26 + 20 + (file.isNotEmpty() ? 26 : 8) + (int) std::ceil (layout.getHeight()) + 24 + 40 + 22;
    return juce::Rectangle<int> (0, 0, 460, h).withCentre (getLocalBounds().getCentre());
}

void ModalOverlay::resized()
{
    const auto bx = box();
    const int n = (int) buttons.size(), bw = 140, gap = 10;
    int x = bx.getCentreX() - (n * bw + (n - 1) * gap) / 2;
    for (auto& b : buttons)
    {
        b->setBounds (x, bx.getBottom() - 22 - 40, bw, 40);
        x += bw + gap;
    }
}

void ModalOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xaa020508));
    const auto bx = box().toFloat();
    const auto tint = isError ? ui::pink : ui::green;
    ui::glowRect (g, bx, tint, 16.f, 0.8f);
    ui::card (g, bx, tint, 16.f, 0.6f);

    ui::caption (g, title, juce::Rectangle<float> (bx.getX() + 30.f, bx.getY() + 24.f, bx.getWidth() - 60.f, 20.f), tint, 11.f);
    float y = bx.getY() + 46.f;
    if (file.isNotEmpty())
    {
        g.setColour (ui::silver);
        g.setFont (ui::mono (11.f));
        g.drawText (file, juce::Rectangle<float> (bx.getX() + 30.f, y, bx.getWidth() - 60.f, 20.f), juce::Justification::centredLeft, true);
        y += 26.f;
    }
    else y += 8.f;

    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    as.append (text, ui::font (13.f), ui::fg);
    as.draw (g, juce::Rectangle<float> (bx.getX() + 30.f, y, bx.getWidth() - 60.f, bx.getBottom() - y - 70.f));
}

// ---------------------------------------------------------------------------------------------
//  ZoomBar
// ---------------------------------------------------------------------------------------------
juce::Rectangle<float> ZoomBar::track() const
{
    return { 50.f, (float) getHeight() * 0.5f - 3.f, (float) getWidth() - 50.f - 52.f, 6.f };
}

void ZoomBar::paint (juce::Graphics& g)
{
    ui::caption (g, "ZOOM", { 0.f, 0.f, 46.f, (float) getHeight() }, ui::muted, 9.5f, juce::Justification::centredLeft, 0.3f);

    const auto t = track();
    const float n = (value - kMin) / (kMax - kMin);
    const float x = t.getX() + n * t.getWidth();
    const float x100 = t.getX() + (1.f - kMin) / (kMax - kMin) * t.getWidth();

    g.setColour (juce::Colour (0xff1a2631));
    g.fillRoundedRectangle (t, 3.f);
    g.setColour (ui::cyan.withAlpha (0.55f));
    g.fillRoundedRectangle (t.withRight (x), 3.f);
    g.setColour (ui::silver.withAlpha (0.5f));                       // tick at 100 %
    g.fillRect (x100 - 0.5f, t.getY() - 4.f, 1.f, t.getHeight() + 8.f);

    const float cy = t.getCentreY();
    g.setColour (ui::cyan.withAlpha (0.25f));
    g.fillEllipse (x - 10.f, cy - 10.f, 20.f, 20.f);
    g.setColour (ui::cyan);
    g.fillEllipse (x - 6.f, cy - 6.f, 12.f, 12.f);

    g.setColour (ui::fg);
    g.setFont (ui::mono (12.f));
    g.drawText (juce::String (juce::roundToInt (value * 100.f)) + "%",
                getLocalBounds().removeFromRight (50), juce::Justification::centredRight);
}

void ZoomBar::change (float z)
{
    z = juce::jlimit (kMin, kMax, std::round (z * 20.f) / 20.f);      // 5 % steps
    if (z == value) return;
    value = z;
    repaint();
    if (onChange) onChange (z);
}

void ZoomBar::mouseDown (const juce::MouseEvent& e)
{
    const auto t = track();
    change (kMin + juce::jlimit (0.f, 1.f, (e.position.x - t.getX()) / t.getWidth()) * (kMax - kMin));
    startValue = value;
}

void ZoomBar::mouseDrag (const juce::MouseEvent& e)
{
    // measured in screen pixels (300 px = the whole range), so resizing the window under the cursor can't feed back
    const float dx = (float) (e.getScreenX() - e.getMouseDownScreenX());
    change (startValue + dx / 300.f * (kMax - kMin));
}

// ============================================================================================
//  KEYS keyboard
// ============================================================================================
namespace
{
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const int kWhitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
bool isBlackPc (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
int blackBoundary (int pc) { return pc == 1 ? 1 : pc == 3 ? 2 : pc == 6 ? 4 : pc == 8 ? 5 : 6; }
}

KeysOverlay::KeysOverlay()
{
    for (auto* b : { &clearBtn, &offBtn, &useBtn }) addAndMakeVisible (*b);
    useBtn.primary = true;
    clearBtn.onClick = [this] { mask = 0; root = 0; repaint(); };
    offBtn.onClick = [this] { setVisible (false); if (wasOn && onOff) onOff(); };
    useBtn.onClick = [this]
    {
        setVisible (false);
        if (mask == 0) { if (onOff) onOff(); return; }
        if (onUse) onUse (root, mask);
    };
    setVisible (false);
}

void KeysOverlay::open (int r, unsigned m, bool on)
{
    root = r; mask = m; wasOn = on; lastPc = -1;
    offBtn.setButtonText (on ? "TURN OFF" : "CANCEL");
    setVisible (true);
    toFront (false);
    resized();
    repaint();
}

juce::Rectangle<float> KeysOverlay::pianoArea() const
{
    const auto b = box().toFloat();
    return { b.getX() + 50.f, b.getY() + 76.f, b.getWidth() - 100.f, 170.f };
}

juce::Rectangle<float> KeysOverlay::keyRect (int pc) const
{
    const auto a = pianoArea();
    const float ww = a.getWidth() / 7.f;
    if (isBlackPc (pc))
    {
        const float cx = a.getX() + ww * (float) blackBoundary (pc);
        return { cx - ww * 0.30f, a.getY(), ww * 0.60f, a.getHeight() * 0.60f };
    }
    int i = 0;
    for (; i < 7; ++i) if (kWhitePc[i] == pc) break;
    return { a.getX() + ww * (float) i, a.getY(), ww, a.getHeight() };
}

int KeysOverlay::hit (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc) if (isBlackPc (pc) && keyRect (pc).contains (p)) return pc;
    for (int pc = 0; pc < 12; ++pc) if (! isBlackPc (pc) && keyRect (pc).contains (p)) return pc;
    return -1;
}

void KeysOverlay::mouseDown (const juce::MouseEvent& e)
{
    const int pc = hit (e.position);
    if (pc < 0) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool on = (mask >> pc) & 1u;
    if (on && lastPc == pc && now - lastT < 380.0) root = pc;                    // double tap: this note becomes the root
    else if (on)
    {
        mask &= ~(1u << pc);
        if (root == pc)
        {
            root = 0;
            for (int i = 0; i < 12; ++i) if ((mask >> i) & 1u) { root = i; break; }
        }
    }
    else
    {
        mask |= 1u << pc;
        if (juce::BigInteger ((int) mask).countNumberOfSetBits() == 1) root = pc;
    }
    lastPc = pc; lastT = now;
    repaint();
}

void KeysOverlay::resized()
{
    const auto b = box();
    const int bw = 190, gap = 12, y = b.getBottom() - 22 - 40;
    int x = b.getCentreX() - (3 * bw + 2 * gap) / 2;
    for (auto* btn : { &clearBtn, &offBtn, &useBtn }) { btn->setBounds (x, y, bw, 40); x += bw + gap; }
}

void KeysOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xcc020508));
    const auto bx = box().toFloat();
    ui::glowRect (g, bx, ui::green, 16.f, 0.8f);
    ui::card (g, bx, ui::green, 16.f, 0.6f);
    ui::caption (g, "KEYBOARD", { bx.getX() + 30.f, bx.getY() + 24.f, bx.getWidth() - 60.f, 20.f }, ui::green, 11.f);
    g.setColour (ui::muted);
    g.setFont (ui::font (12.f));
    g.drawText ("Tap the notes you want. Double-tap a chosen note to make it the root (pink).",
                juce::Rectangle<float> (bx.getX() + 30.f, bx.getY() + 46.f, bx.getWidth() - 60.f, 18.f), juce::Justification::centredLeft);

    for (int pass = 0; pass < 2; ++pass)                                          // white keys first, black on top
        for (int pc = 0; pc < 12; ++pc)
        {
            if (isBlackPc (pc) != (pass == 1)) continue;
            const auto r = keyRect (pc).reduced (1.5f);
            const bool sel = (mask >> pc) & 1u, isRoot = sel && pc == root;
            const auto base = pass == 1 ? juce::Colour (0xff111c26) : juce::Colour (0xff1c2a38);
            const auto col = isRoot ? ui::pink : (sel ? ui::green : base);
            g.setColour (col);
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (sel ? col.brighter (0.2f) : ui::line);
            g.drawRoundedRectangle (r, 6.f, 1.2f);
            g.setColour (sel ? juce::Colour (0xff04120e) : ui::silver);
            g.setFont (ui::mono (pass == 1 ? 10.f : 12.f));
            g.drawText (kNoteNames[pc], r.withTrimmedBottom (6.f), juce::Justification::centredBottom);
        }

    juce::String read;
    if (mask == 0) read = "TAP THE NOTES YOU WANT";
    else
    {
        read = kNoteNames[root];
        for (int i = 0; i < 12; ++i) if (i != root && ((mask >> i) & 1u)) read << "  " << kNoteNames[i];
    }
    ui::caption (g, read, { bx.getX() + 30.f, bx.getY() + 258.f, bx.getWidth() - 60.f, 22.f }, ui::fg, 13.f, juce::Justification::centred, 0.2f);
}

// ============================================================================================
//  STYLE page
// ============================================================================================
StyleOverlay::StyleOverlay (TB303Processor& p) : proc (p)
{
    for (auto* b : { &backBtn, &switchBtn, &savedBtn, &folderBtn, &filesBtn, &saveBtn, &loadBtn, &forgetBtn }) addAndMakeVisible (*b);
    backBtn.onClick   = [this] { setVisible (false); if (onBack) onBack(); };
    switchBtn.onClick = [this] { proc.setStyleOn (! proc.styleIsOn()); refresh(); };
    savedBtn.onClick  = [this] { if (onSavedFolder) onSavedFolder(); };
    folderBtn.onClick = [this] { if (onFolder) onFolder(); };
    filesBtn.onClick  = [this] { if (onFiles) onFiles(); };
    saveBtn.onClick   = [this] { if (onSave) onSave(); };
    loadBtn.onClick   = [this] { if (onLoad) onLoad(); };
    forgetBtn.onClick = [this] { proc.forgetStyle(); refresh(); };
    setVisible (false);
}

void StyleOverlay::open()
{
    setVisible (true);
    toFront (false);
    sig = {};
    refresh();
}

void StyleOverlay::refresh()
{
    const bool has = proc.styleLoaded(), on = proc.styleIsOn(), busy = proc.styleIsBusy();
    const auto s = juce::String (has ? 1 : 0) + juce::String (on ? 1 : 0) + juce::String (busy ? 1 : 0) + juce::String (proc.styleIsReady() ? 1 : 0)
                   + (busy ? proc.styleBusyText() : proc.styleSummary());
    if (s == sig) return;
    sig = s;
    switchBtn.setEnabled (has);
    switchBtn.setToggleState (on, juce::dontSendNotification);
    switchBtn.setButtonText (on ? "STYLE ON" : "STYLE OFF");
    saveBtn.setEnabled (has);
    forgetBtn.setEnabled (has);
    for (auto* b : { &savedBtn, &folderBtn, &filesBtn }) b->setEnabled (! busy);
    repaint();
}

void StyleOverlay::resized()
{
    backBtn.setBounds (26, 22, 150, 48);
    switchBtn.setBounds (getWidth() - 30 - 260, 128, 260, 64);
    const int w = (getWidth() - 60 - 40) / 3, h = 86;
    auto row = [&] (int y, std::initializer_list<NButton*> bs) { int x = 30; for (auto* b : bs) { b->setBounds (x, y, w, h); x += w + 20; } };
    row (330, { &savedBtn, &folderBtn, &filesBtn });
    row (500, { &saveBtn, &loadBtn });
    row (670, { &forgetBtn });
}

void StyleOverlay::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    ui::caption (g, "STYLE", { 200.f, 26.f, 400.f, 40.f }, ui::fg, 26.f, juce::Justification::centredLeft, 0.3f);

    const juce::Rectangle<float> card (30.f, 100.f, (float) getWidth() - 60.f, 120.f);
    ui::card (g, card, ui::green, 16.f, 0.4f);
    const bool ready = proc.styleIsReady();
    const juce::Rectangle<float> dot (card.getX() + 24.f, card.getCentreY() - 8.f, 16.f, 16.f);
    if (ready) ui::glowRect (g, dot, ui::green, 8.f, 1.f);
    g.setColour (ready ? ui::green : ui::muted.withAlpha (0.5f));
    g.fillEllipse (dot);
    juce::String t = proc.styleLoaded() ? proc.styleSummary()
                                        : (proc.styleIsBusy() ? "Reading the save folder..." : "No style learned yet");
    if (proc.styleIsBusy()) t = proc.styleBusyText() + (proc.styleLoaded() ? "   " + t : juce::String());
    g.setColour (ui::fg);
    g.setFont (ui::font (15.f));
    g.drawFittedText (t, juce::Rectangle<int> ((int) card.getX() + 56, (int) card.getY() + 14, (int) card.getWidth() - 56 - 300, (int) card.getHeight() - 28),
                      juce::Justification::centredLeft, 4);

    auto head = [&] (const char* s, int y) { ui::caption (g, s, { 34.f, (float) y, 300.f, 20.f }, ui::muted, 11.f, juce::Justification::centredLeft, 0.3f); };
    head ("LEARN", 296);
    head ("KEEP", 466);
    head ("RESET", 636);
    g.setColour (ui::muted);
    g.setFont (ui::font (12.f));
    g.drawFittedText ("SAVED FOLDER reads the .mid files of your TB303 Gen folder. GENERATE then follows what the files do: rhythm, accents, favourite notes, scales.",
                      juce::Rectangle<int> (34, 780, getWidth() - 68, 40), juce::Justification::topLeft, 2);
}
