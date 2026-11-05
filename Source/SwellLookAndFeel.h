#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// Swell's graphite surfaces, charcoal controls and orange playhead.
namespace SwellColours
{
inline const juce::Colour background   { 0xff161618 };
inline const juce::Colour panel        { 0xff1c1d21 };
inline const juce::Colour panelLight   { 0xff242427 };
inline const juce::Colour control      { 0xff17171a };
inline const juce::Colour border       { 0xff39393f };
inline const juce::Colour track        { 0xff303034 };
inline const juce::Colour orange       { 0xffff8c39 };
inline const juce::Colour text         { 0xfff0eff2 };
inline const juce::Colour dim          { 0xffb7b6bd };
inline const juce::Colour pad          { 0xff222226 };
inline const juce::Colour triadPad     { 0xff303034 };
inline const juce::Colour activePad    { 0xffff8c39 };
inline const juce::Colour accent   { 0xffff8c39 };
inline const juce::Colour bassKey      { 0xffffd39d };
inline const juce::Colour chordKey     { 0xffff8c39 };
inline const juce::Colour cursor       { 0xffff8c39 };
inline const juce::Colour buttonFace   { 0xff26262a };
} // namespace SwellColours

inline juce::Font swellFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Product icon with the original Swell mark above a piano keyboard.
inline void drawSwellLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto logo = juce::ImageCache::getFromMemory (BinaryData::chord_motion_icon_png, BinaryData::chord_motion_icon_pngSize);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, r, juce::RectanglePlacement::centred);
}

class SwellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SwellLookAndFeel()
    {
        using namespace SwellColours;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::ComboBox::backgroundColourId, control);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, panelLight);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, activePad);
        setColour (juce::PopupMenu::highlightedTextColourId, background);
        setColour (juce::TextButton::buttonColourId, buttonFace);
        setColour (juce::TextButton::buttonOnColourId, activePad);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::backgroundColourId, control);
        setColour (juce::Slider::trackColourId, orange.withAlpha (0.85f));
        setColour (juce::Slider::thumbColourId, orange);
        setColour (juce::Label::textColourId, dim);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace SwellColours;
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = radius * 0.14f;
        const float arcR = radius - lineW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path bg;
        bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (track);
        g.strokePath (bg, stroke);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (slider.isEnabled() ? orange : orange.withAlpha (0.3f));
        g.strokePath (value, stroke);

        const float knobR = radius * 0.68f;
        g.setColour (juce::Colour (0xff1d1e22));
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour (juce::Colour (0xff2b2c31));
        g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        g.setColour (slider.isEnabled() ? text : dim);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.35f, angle), centre.getPointOnCircumference (knobR * 0.85f, angle) }, 2.5f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace SwellColours;
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 3.5f, ay - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (arrow, juce::PathStrokeType (1.3f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return swellFont (14.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return swellFont (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& base, bool over, bool down) override
    {
        using namespace SwellColours;
        const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();

        auto colour = on ? button.findColour (juce::TextButton::buttonOnColourId) : base;
        if (over) colour = colour.brighter (0.08f);
        if (down) colour = colour.darker (0.1f);

        g.setColour (colour);
        g.fillRoundedRectangle (r, 6.0f);

        g.setColour (on ? accent.withAlpha (0.65f) : border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos,
                           float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, slider);
            return;
        }
        using namespace SwellColours;
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f);
        g.setColour (control);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        auto strip = r.reduced (6.0f, 3.0f).removeFromBottom (2.0f);
        const float fraction = (float) slider.valueToProportionOfLength (slider.getValue());
        g.setColour (track);
        g.fillRoundedRectangle (strip, 1.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (strip.withWidth (strip.getWidth() * fraction), 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return swellFont (13.0f, true); }
};
