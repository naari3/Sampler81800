#include "WaveformView.h"
#include "PluginProcessor.h"
#include "core/Params.h"

namespace otomad::gui
{

WaveformView::WaveformView (OtoMadSamplerProcessor& p) : processor (p)
{
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    stopTimer();
}

void WaveformView::timerCallback()
{
    const int v = processor.getSampleVersion();
    if (v != lastSampleVersion)
    {
        lastSampleVersion = v;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0f0f13));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto* sample = processor.getActiveSample();
    if (sample == nullptr || sample->peaks.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("(no sample)", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const auto& peaks = sample->peaks;
    const float w    = bounds.getWidth();
    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.48f;

    // トリム範囲のハイライト
    const float s01 = processor.getAPVTS().getRawParameterValue (params::sampleStart)->load();
    const float e01 = processor.getAPVTS().getRawParameterValue (params::sampleEnd)->load();
    g.setColour (juce::Colour (0x2233aaff));
    g.fillRect (juce::Rectangle<float> (bounds.getX() + s01 * w, bounds.getY(),
                                        (e01 - s01) * w, bounds.getHeight()));

    // 波形
    g.setColour (juce::Colour (0xff5cc8ff));
    const int npx = (int) w;
    juce::Path path;
    for (int x = 0; x < npx; ++x)
    {
        const std::size_t idx = (std::size_t) ((double) x / (double) juce::jmax (1, npx) * (double) peaks.size());
        const auto& mm = peaks[juce::jmin (idx, peaks.size() - 1)];
        const float y1 = midY - mm.second * halfH;
        const float y2 = midY - mm.first  * halfH;
        g.drawVerticalLine (juce::roundToInt (bounds.getX() + (float) x), y1, y2);
    }

    // トリム境界線
    g.setColour (juce::Colours::orange);
    g.drawVerticalLine (juce::roundToInt (bounds.getX() + s01 * w), bounds.getY(), bounds.getBottom());
    g.drawVerticalLine (juce::roundToInt (bounds.getX() + e01 * w), bounds.getY(), bounds.getBottom());
}

} // namespace otomad::gui
