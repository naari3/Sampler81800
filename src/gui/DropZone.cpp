#include "DropZone.h"
#include "PluginProcessor.h"

namespace otomad::gui
{

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
    }
}

void DropZone::mouseDown (const juce::MouseEvent&)
{
    chooseFile();
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
