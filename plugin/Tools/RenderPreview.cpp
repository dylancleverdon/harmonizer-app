// Throwaway dev tool: renders the plugin editor to a PNG off-screen so the
// Logic-native visual redesign can be checked without a real display or any
// external screenshot tooling. Not part of the shipped product.
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {

// The editor starts on the Main page every run, so a page argument just
// needs to fire the same click each of the header nav buttons would --
// found by the component ID set on them in PluginEditor.cpp, and invoked
// directly via Button::onClick (a public std::function) rather than
// Button::triggerClick(), which defers through a Timer that never fires
// here since this tool runs no message loop.
void selectPage(juce::Component& editor, const juce::String& page) {
    juce::String id;
    if (page == "jazz") id = "jazzButton";
    else if (page == "settings") id = "settingsButton";
    else return;  // "main" is the default page on a fresh editor.

    if (auto* btn = dynamic_cast<juce::Button*>(editor.findChildWithID(id))) {
        if (btn->onClick) btn->onClick();
    }
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outPath = argc > 1 ? argv[1] : "/tmp/preview.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 560;
    const int height = argc > 3 ? juce::String(argv[3]).getIntValue() : 720;
    const juce::String page = argc > 4 ? juce::String(argv[4]).toLowerCase() : "main";

    HarmonizerAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(width, height);
    selectPage(*editor, page);

    const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds());

    juce::File outFile(outPath);
    outFile.deleteFile();
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream())) {
        juce::PNGImageFormat png;
        png.writeImageToStream(image, *stream);
    }

    return 0;
}
