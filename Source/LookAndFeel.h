#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    // The night palette of the visual universe (Brain/brand/visual-universe.md): the spectrum is the night sky
    // and the message is the warm light in it.
    const juce::Colour background { 0xff0a0d1a };   // deep blue-black
    const juce::Colour panel { 0xff10142a };
    const juce::Colour raised { 0xff171c36 };
    const juce::Colour outline { 0xff262c4a };
    const juce::Colour text { 0xfff3ecde };         // bone white
    const juce::Colour dim { 0xff8a8fa8 };
    const juce::Colour faint { 0xff4a5070 };

    const juce::Colour honey { 0xffe4b562 };
    const juce::Colour amber { 0xffd48a3b };
    const juce::Colour violet { 0xff5a3a8e };
    const juce::Colour star { 0xffeef2ff };

    const juce::Colour accent = honey;

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font caps (float height)
    {
        return font (height, true).withExtraKerningFactor (0.12f);
    }

    inline juce::Font serif (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, juce::Font::plain));
    }
}

class ZeroOneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ZeroOneLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override { return Theme::font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::caps (11.0f); }
};
