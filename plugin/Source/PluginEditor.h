#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "PluginUpdater.h"

class HarmonizerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit HarmonizerAudioProcessorEditor(HarmonizerAudioProcessor&);
    ~HarmonizerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void timerCallback() override;
    void refreshUpdatePanel();
    void layoutChordControls();

    void styleRotary(juce::Slider&);

    HarmonizerAudioProcessor& processor_;
    PluginUpdater updater_;

    juce::Label titleLabel_, versionLabel_;

    juce::Slider wetDry_, outputGain_, qualityAmount_;
    juce::ComboBox harmonyMode_, chordDegree_, qualityMode_, fftSize_;
    juce::ToggleButton doubleAnchor_, formant_, adaptLatency_, adaptVoices_, bypass_;

    juce::Label wetDryCaption_, outputGainCaption_, harmonyCaption_, degreeCaption_,
        qualityCaption_, amountCaption_, windowCaption_, statusLabel_, chordHint_;

    juce::TextButton panicButton_{"Panic"};

    // --- updates ------------------------------------------------------------
    juce::Label updateStatus_;
    juce::TextButton checkButton_{"Check for updates"};
    juce::TextButton installButton_{"Download and install"};
    double updateProgress_ = 0.0;
    juce::ProgressBar updateBar_{updateProgress_};

    std::unique_ptr<APVTS::SliderAttachment> aWetDry_, aOutputGain_, aQualityAmount_;
    std::unique_ptr<APVTS::ComboBoxAttachment> aHarmonyMode_, aChordDegree_, aQualityMode_, aFftSize_;
    std::unique_ptr<APVTS::ButtonAttachment> aDoubleAnchor_, aFormant_, aAdaptLatency_,
        aAdaptVoices_, aBypass_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerAudioProcessorEditor)
};
