#include "PluginEditor.h"

//==============================================================================
OtoMadSamplerEditor::OtoMadSamplerEditor (OtoMadSamplerProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (520, 260);
}

//==============================================================================
void OtoMadSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("OtoMadSampler",
                getLocalBounds().removeFromTop (140).withTrimmedTop (48),
                juce::Justification::centred, false);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Phase 0 - MIDI note plays a sine (subblock-split render)",
                getLocalBounds().withTrimmedTop (150),
                juce::Justification::centredTop, false);
}

//==============================================================================
void OtoMadSamplerEditor::resized()
{
}
