// Ironcurve - Farben und Schrift (gemeinsam)
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Colours_
{
    const juce::Colour bg        { 0xff111114 };
    const juce::Colour panel     { 0xff1a1b20 };
    const juce::Colour grid      { 0xff2a2c33 };
    const juce::Colour text      { 0xffd8d8dc };
    const juce::Colour dimText   { 0xff8a8c95 };
    const juce::Colour accent    { 0xffe2412b }; // EQ-Kurve, Buttons
    const juce::Colour learned   { 0xfff0a03c }; // gelerntes Spektrum
    const juce::Colour result    { 0xff3fc7b5 }; // Ergebnis nach EQ
    const juce::Colour live      { 0xff5d6a88 }; // Live-Analyzer
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

