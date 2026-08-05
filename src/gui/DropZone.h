#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class OtoMadSamplerProcessor;   // グローバル名前空間の実クラス

namespace otomad::gui
{

//==============================================================================
/**
    D&D 受け口。ホストがOSのD&Dを渡さない場合に備え、クリックでファイル選択も開く（§6.1）。
    透明オーバーレイとして WaveformView の上に重ねる想定。
*/
class DropZone : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    explicit DropZone (OtoMadSamplerProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

private:
    static bool isSupported (const juce::String& path);
    void chooseFile();

    OtoMadSamplerProcessor& processor;
    bool dragHighlight = false;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DropZone)
};

} // namespace otomad::gui
