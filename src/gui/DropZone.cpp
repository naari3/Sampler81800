#include "DropZone.h"
#include "PluginProcessor.h"
#include "core/Params.h"

#include <cmath>

namespace otomad::gui
{

namespace P = otomad::params;

DropZone::DropZone (OtoMadSamplerProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
}

bool DropZone::isSupported (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aiff" || ext == ".aif"
        || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

void DropZone::paint (juce::Graphics& g)
{
    const bool empty = (processor.getActiveSample() == nullptr);

    if (dragHighlight)
    {
        g.setColour (juce::Colour (0x3355ccff));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
    }

    if (empty)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::CharPointer_UTF8 ("\xe3\x83\x89\xe3\x83\xad\xe3\x83\x83\xe3\x83\x97 or \xe3\x82\xaf\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf\xe3\x81\xa7\xe9\x81\xb8\xe6\x8a\x9e"),
                    getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    // トリムハンドルのグリップ（上端の三角）を描いてドラッグ可能なことを示す
    const float w = (float) getWidth();
    const float s = processor.getAPVTS().getRawParameterValue (P::sampleStart)->load();
    const float e = processor.getAPVTS().getRawParameterValue (P::sampleEnd)->load();
    g.setColour (juce::Colours::orange);
    for (float x01 : { s, e })
    {
        const float x = x01 * w;
        juce::Path tri;
        tri.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 8.0f);
        g.fillPath (tri);
    }
}

juce::RangedAudioParameter* DropZone::handleParam (Handle h) const
{
    return processor.getAPVTS().getParameter (h == Handle::Start ? P::sampleStart : P::sampleEnd);
}

DropZone::Handle DropZone::beginDrag (float x01)
{
    const float s = processor.getAPVTS().getRawParameterValue (P::sampleStart)->load();
    const float e = processor.getAPVTS().getRawParameterValue (P::sampleEnd)->load();
    return (std::abs (x01 - s) <= std::abs (x01 - e)) ? Handle::Start : Handle::End;
}

void DropZone::updateDrag (float x01)
{
    x01 = juce::jlimit (0.0f, 1.0f, x01);
    const float s = processor.getAPVTS().getRawParameterValue (P::sampleStart)->load();
    const float e = processor.getAPVTS().getRawParameterValue (P::sampleEnd)->load();
    if (drag == Handle::Start) x01 = juce::jmin (x01, e - 0.001f);
    else                       x01 = juce::jmax (x01, s + 0.001f);

    if (auto* p = handleParam (drag))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, x01));   // レンジ [0,1] なので正規化値=値
}

void DropZone::mouseDown (const juce::MouseEvent& e)
{
    if (processor.getActiveSample() == nullptr)
    {
        chooseFile();     // 未読み込み時のみエクスプローラを開く
        return;
    }
    const float x01 = (float) e.position.x / (float) juce::jmax (1, getWidth());
    drag = beginDrag (x01);
    if (auto* p = handleParam (drag))
        p->beginChangeGesture();
    updateDrag (x01);
}

void DropZone::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Handle::None)
        return;
    updateDrag ((float) e.position.x / (float) juce::jmax (1, getWidth()));
}

void DropZone::mouseUp (const juce::MouseEvent&)
{
    if (drag != Handle::None)
    {
        if (auto* p = handleParam (drag))
            p->endChangeGesture();
        drag = Handle::None;
    }
}

void DropZone::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Select an audio sample",
        juce::File{},
        "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            processor.loadSampleFromFile (file);
    });
}

bool DropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isSupported (f))
            return true;
    return false;
}

void DropZone::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight = true;
    repaint();
}

void DropZone::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void DropZone::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();

    // 複数ドロップ時は先頭の対応ファイルのみ採用（§6.1）
    for (const auto& f : files)
    {
        if (isSupported (f))
        {
            processor.loadSampleFromFile (juce::File (f));
            break;
        }
    }
}

} // namespace otomad::gui
