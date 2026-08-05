#include "WaveformView.h"
#include "PluginProcessor.h"
#include "core/Params.h"

namespace otomad::gui
{

WaveformView::WaveformView (OtoMadSamplerProcessor& p, WaveViewState& viewState)
    : processor (p), view (viewState)
{
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    stopTimer();
}

void WaveformView::timerCallback()
{
    // サンプルが差し替わったらズームを全体に戻す
    const int ver = processor.getSampleVersion();
    if (ver != lastSampleVersion)
    {
        view.reset();
        lastSampleVersion = ver;
    }
    // トリムのドラッグやノーマライズに追従させるため毎tick再描画（小さいので軽い）
    repaint();
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

    // トリム範囲のハイライト（ズーム窓 view を通してピクセルへ写像, 画面内にクランプ）
    const float s01 = processor.getAPVTS().getRawParameterValue (params::sampleStart)->load();
    const float e01 = processor.getAPVTS().getRawParameterValue (params::sampleEnd)->load();
    const float sx = juce::jlimit (0.0f, w, (float) view.toView (s01) * w);
    const float ex = juce::jlimit (0.0f, w, (float) view.toView (e01) * w);
    g.setColour (juce::Colour (0x2233aaff));
    g.fillRect (juce::Rectangle<float> (bounds.getX() + sx, bounds.getY(), ex - sx, bounds.getHeight()));

    // 波形（ノーマライズ倍率を反映）。各ピクセルは view 窓内のサンプル位置に対応。
    const float ng = processor.getNormGain();
    g.setColour (juce::Colour (processor.getMainColourARGB()));
    const int npx = (int) w;
    for (int x = 0; x < npx; ++x)
    {
        const double fx  = (double) x / (double) juce::jmax (1, npx);
        const double pos = view.toSample (fx);                       // 0..1（サンプル全体）
        const std::size_t idx = (std::size_t) (pos * (double) peaks.size());
        const auto& mm = peaks[juce::jmin (idx, peaks.size() - 1)];
        const float y1 = midY - juce::jlimit (-1.0f, 1.0f, mm.second * ng) * halfH;
        const float y2 = midY - juce::jlimit (-1.0f, 1.0f, mm.first  * ng) * halfH;
        g.drawVerticalLine (juce::roundToInt (bounds.getX() + (float) x), y1, y2);
    }

    // トリム境界線（窓内のときだけ描く）
    g.setColour (juce::Colours::orange);
    if (s01 >= view.start && s01 <= view.end)
        g.drawVerticalLine (juce::roundToInt (bounds.getX() + sx), bounds.getY(), bounds.getBottom());
    if (e01 >= view.start && e01 <= view.end)
        g.drawVerticalLine (juce::roundToInt (bounds.getX() + ex), bounds.getY(), bounds.getBottom());
}

} // namespace otomad::gui
