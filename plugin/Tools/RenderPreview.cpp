// Throwaway dev tool: renders the plugin editor to a PNG off-screen so the
// Logic-native visual redesign can be checked without a real display or any
// external screenshot tooling. Not part of the shipped product.
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outPath = argc > 1 ? argv[1] : "/tmp/preview.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 560;
    const int height = argc > 3 ? juce::String(argv[3]).getIntValue() : 720;

    HarmonizerAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(width, height);

    const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds());

    juce::File outFile(outPath);
    outFile.deleteFile();
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream())) {
        juce::PNGImageFormat png;
        png.writeImageToStream(image, *stream);
    }

    return 0;
}
