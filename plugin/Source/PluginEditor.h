#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginLook.h"
#include "PluginProcessor.h"
#include "PluginUpdater.h"

namespace look = harmonizer::look;

/** Vertical stack of cards, as on each screen of the Android app. */
class Page : public juce::Component {
public:
    look::Card& addCard(const juce::String& title);
    void resized() override;
    int preferredHeight() const;

protected:
    juce::OwnedArray<look::Card> cards_;
};

/** Radio row of pill selectors bound to a choice parameter. */
class ChipGroup final : public juce::Component {
public:
    ChipGroup(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
              const juce::StringArray& labels);
    void resized() override;
    int selectedIndex() const;

private:
    void applyIndex(int index);

    juce::RangedAudioParameter& parameter_;
    juce::OwnedArray<look::Chip> chips_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};

class MainPage;
class SettingsPage;

class HarmonizerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit HarmonizerAudioProcessorEditor(HarmonizerAudioProcessor&);
    ~HarmonizerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage(bool settings);

    HarmonizerAudioProcessor& processor_;
    PluginUpdater updater_;
    look::KnobLookAndFeel knobLook_;

    juce::Label title_, version_;
    juce::TextButton pageButton_{"Settings"}, panicButton_{"Panic"};

    juce::Viewport viewport_;
    std::unique_ptr<MainPage> mainPage_;
    std::unique_ptr<SettingsPage> settingsPage_;
    bool showingSettings_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerAudioProcessorEditor)
};
