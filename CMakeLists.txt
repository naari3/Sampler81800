cmake_minimum_required(VERSION 3.22)

project(OtoMadSampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# =====================================================================
#  依存の取得 (FetchContent)  — DESIGN.md §0.1
# =====================================================================
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15          # JUCE 8。バージョンを上げるときはここだけ変える
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# =====================================================================
#  プラグイン本体
# =====================================================================
juce_add_plugin(OtoMadSampler
    PRODUCT_NAME                "OtoMadSampler"
    COMPANY_NAME                "neon-uriel"
    BUNDLE_ID                   "com.neonuriel.otomadsampler"
    PLUGIN_MANUFACTURER_CODE    Nuri              # 4文字・先頭大文字1つ
    PLUGIN_CODE                 Otms              # 4文字・一意
    FORMATS                     VST3 Standalone   # CLAP は Phase 6 (DESIGN.md §7)
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Instrument" "Sampler")

target_sources(OtoMadSampler PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/core/SampleLoader.cpp
    src/core/SourceReader.cpp
    src/core/Voice.cpp
    src/core/VoiceManager.cpp
    src/core/PortamentoGenerator.cpp
    src/core/GlideMatcher.cpp
    src/pitch/VarispeedEngine.cpp
    src/pitch/WsolaEngine.cpp
    src/pitch/PhaseVocoderEngine.cpp
    src/pitch/ReaperPitchShiftEngine.cpp
    src/pitch/PitchCache.cpp
    src/pitch/EngineResources.cpp
    src/pitch/dsp/Fft.cpp
    src/host/ReaperApi.cpp
    src/gui/DropZone.cpp
    src/gui/WaveformView.cpp)

target_include_directories(OtoMadSampler PRIVATE src)

# ロゴ等の埋め込みリソース → BinaryData::logo_png
juce_add_binary_data(OtoMadSamplerAssets SOURCES resources/logo.png)
target_link_libraries(OtoMadSampler PRIVATE OtoMadSamplerAssets)

target_compile_features(OtoMadSampler PRIVATE cxx_std_20)

# ソースは UTF-8。MSVC は既定でシステムコードページ(日本語環境ではCP932)として解釈し、
# 日本語コメント末尾の 0x5C が行継続扱いになって構文が壊れる。/utf-8 で明示する。
target_compile_options(OtoMadSampler PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(OtoMadSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # JUCE_DISPLAY_SPLASH_SCREEN=0 は商用ライセンス時のみ可 (DESIGN.md §0.3)。
    # 無償(AGPLv3)枠では既定(スプラッシュ表示)のままにすること。
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(OtoMadSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_vst3_headers   # VST3 SDK ヘッダ（REAPER連携の IHostApplication 取得用, §5.2）
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# =====================================================================
#  テスト (Catch2)  — DESIGN.md §8
# =====================================================================
enable_testing()
add_subdirectory(tests)
