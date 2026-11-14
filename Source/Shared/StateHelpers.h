// Ironcurve - Speichern/Laden von Parametern plus gelerntem Spektrum
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/AutoEqFitter.h"

namespace statehelpers
{
    inline juce::String joinDoubles (const std::vector<double>& v)
    {
        juce::StringArray parts;
        for (double d : v)
            parts.add (juce::String (d, 2));
        return parts.joinIntoString (",");
    }

    inline std::vector<double> parseDoubles (const juce::String& s)
    {
        std::vector<double> out;
        for (const auto& t : juce::StringArray::fromTokens (s, ",", ""))
            if (t.isNotEmpty())
                out.push_back (t.getDoubleValue());
        if (out.size() != (size_t) ironcurve::AnalysisGrid::kNumBands)
            out.clear();
        return out;
    }

    inline void save (juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& dest,
                      const std::vector<double>& learned, const std::vector<double>& target)
    {
        auto state = apvts.copyState();
        auto node = state.getOrCreateChildWithName ("Learned", nullptr);
        node.setProperty ("spectrum", joinDoubles (learned), nullptr);
        node.setProperty ("target", joinDoubles (target), nullptr);
        if (auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, dest);
    }

    inline bool load (juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& apvts, const void* data, int size,
                      std::vector<double>& learned, std::vector<double>& target)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (data, size);
        if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
            return false;

        auto tree = juce::ValueTree::fromXml (*xml);
        auto node = tree.getChildWithName ("Learned");
        learned = parseDoubles (node.getProperty ("spectrum").toString());
        target = parseDoubles (node.getProperty ("target").toString());
        apvts.replaceState (tree);
        return true;
    }
}
