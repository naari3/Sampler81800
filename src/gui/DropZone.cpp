#include "DropZone.h"
#include "PluginProcessor.h"
#include "core/Params.h"

#include <cmath>

namespace otomad::gui
{

namespace P = otomad::params;

DropZone::DropZone (OtoMadSamplerProcessor& p, WaveViewState& viewState)
    : processor (p), view (viewState)
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

    // トリムハンドルのグリップ（上端の三角）を描いてドラッグ可能なことを示す（ズーム窓を反映）
    const float w = (float) getWidth();
    const float s = processor.getAPVTS().getRawParameterValue (P::sampleStart)->load();
    const float e = processor.getAPVTS().getRawParameterValue (P::sampleEnd)->load();
    g.setColour (juce::Colours::orange);
    for (float x01 : { s, e })
    {
        if (x01 < view.start || x01 > view.end)   // 窓外のハンドルは描かない
            continue;
        const float x = (float) view.toView (x01) * w;
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
        if (e.mods.isLeftButtonDown())
            chooseFile();     // 未読み込み時のみエクスプローラを開く
        return;
    }

    // 右クリック（または中クリック）=左右スクロール、左クリック=Start/End 指定
    if (! e.mods.isLeftButtonDown())
    {
        panning  = true;
        panLastX = (float) e.position.x;
        return;
    }

    const float fx  = (float) e.position.x / (float) juce::jmax (1, getWidth());
    const float x01 = (float) view.toSample (fx);   // ズーム窓を通してサンプル位置へ
    drag = beginDrag (x01);
    if (auto* p = handleParam (drag))
        p->beginChangeGesture();
    updateDrag (x01);
}

void DropZone::mouseDrag (const juce::MouseEvent& e)
{
    if (panning)
    {
        const float w = (float) juce::jmax (1, getWidth());
        const double dxFrac = (double) ((float) e.position.x - panLastX) / w;
        view.pan (-dxFrac);          // 掴んで動かす向き（右へドラッグ→内容が右へ）
        panLastX = (float) e.position.x;
        if (auto* parent = getParentComponent())
            parent->repaint (getBoundsInParent());
        return;
    }

    if (drag == Handle::None)
        return;
    const float fx = (float) e.position.x / (float) juce::jmax (1, getWidth());
    updateDrag ((float) view.toSample (fx));
}

void DropZone::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (processor.getActiveSample() == nullptr)
        return;

    const float fx = juce::jlimit (0.0f, 1.0f, (float) e.position.x / (float) juce::jmax (1, getWidth()));

    if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        // 横スクロール（Shift+ホイール or 横ホイール）
        const double d = (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * (wheel.isReversed ? 1.0 : -1.0);
        view.pan (d * 0.25);
    }
    else
    {
        // 縦ホイール: マウス位置を中心にズーム（上=拡大 / 下=縮小）
        const double dir = (wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0));
        const double factor = dir > 0.0 ? (1.0 / 1.2) : 1.2;
        view.zoom (fx, factor);
    }

    // 自分（ハンドル三角）と背後の波形を再描画。両者は親の同じ領域にあるのでまとめて再描画する。
    if (auto* parent = getParentComponent())
        parent->repaint (getBoundsInParent());
    else
        repaint();
}

void DropZone::mouseUp (const juce::MouseEvent&)
{
    if (panning)
    {
        panning = false;
        return;
    }
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
