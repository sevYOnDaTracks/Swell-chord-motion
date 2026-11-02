#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// Shared Swell brand style: anthracite panels, orange accents
namespace SwellColours
{
inline const juce::Colour background { 0xff131417 };
inline const juce::Colour panel      { 0xff1b1c20 };
inline const juce::Colour panelLight { 0xff25262b };
inline const juce::Colour control    { 0xff222328 };
inline const juce::Colour border     { 0xff34353b };
inline const juce::Colour track      { 0xff2c2d33 };
inline const juce::Colour orange     { 0xffff8a3d };
inline const juce::Colour text       { 0xffeeeef1 };
inline const juce::Colour dim        { 0xff9a9ba3 };
} // namespace SwellColours

inline juce::Font swellFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// The Swell logo (white on transparent, from Resources/swell_logo.png)
inline void drawSwellLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto logo = juce::ImageCache::getFromMemory (BinaryData::swell_logo_png, BinaryData::swell_logo_pngSize);
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
        setColour (juce::ComboBox::arrowColourId, orange);
        setColour (juce::PopupMenu::backgroundColourId, panelLight);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
        setColour (juce::PopupMenu::highlightedTextColourId, background);
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, orange);
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
        arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.0f);
        g.setColour (orange);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return swellFont (14.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return swellFont (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        using namespace SwellColours;
        const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();

        auto colour = on ? orange : control;
        if (over) colour = colour.brighter (0.08f);
        if (down) colour = colour.darker (0.1f);

        g.setColour (colour);
        g.fillRoundedRectangle (r, 6.0f);

        if (! on)
        {
            g.setColour (border);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return swellFont (13.0f, true); }
};
