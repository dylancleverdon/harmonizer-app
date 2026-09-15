#pragma once
#include <juce_core/juce_core.h>

/**
 * Per-DAW routing instructions.
 *
 * Which DAW you use barely changes *what* gets installed -- Logic needs the
 * Audio Unit, everything else uses VST3, and both are installed anyway. What it
 * changes is how you get MIDI into an audio effect, which is the step people
 * actually get stuck on.
 */
namespace harmonizer::guides {

struct Daw {
    const char* key;
    const char* name;
    bool macOnly;
    const char* steps;
};

inline const Daw kDaws[] = {
    {"logic", "Logic Pro", true,
     "1. Make a Software Instrument track.\n"
     "2. In its Instrument slot choose:\n"
     "      AU MIDI-controlled Effects > Dylan Cleverdon > Harmonizer\n"
     "3. At the top of the plugin window, set Side Chain to the audio track\n"
     "   your voice or horn is on.\n"
     "4. Play that instrument track's keyboard. Your audio comes in through the\n"
     "   side chain; the notes you play become the harmonies."},

    {"ableton", "Ableton Live", false,
     "1. Drop Harmonizer onto the AUDIO track carrying your voice or horn.\n"
     "2. Make a new MIDI track.\n"
     "3. Set that MIDI track's 'MIDI To' to the audio track, then pick\n"
     "   Harmonizer as the destination underneath it.\n"
     "4. Arm the MIDI track and play.\n"
     "\n"
     "If Harmonizer is not offered as a MIDI destination, try the Audio Unit\n"
     "version instead of the VST3 (both are installed) -- Live is more reliable\n"
     "about MIDI routing to AU music effects."},

    {"flstudio", "FL Studio", false,
     "1. Add Harmonizer to the mixer insert your audio is routed to.\n"
     "2. Click the plugin's gear icon to open the wrapper settings, and under\n"
     "   MIDI set an Input port -- for example 1.\n"
     "3. In the Channel Rack add a MIDI Out channel and set its port to the\n"
     "   same number.\n"
     "4. Play or draw notes on that MIDI Out channel."},

    {"other", "Something else", false,
     "Harmonizer is an audio effect that accepts MIDI notes, not an instrument.\n"
     "Put it on the track carrying your voice or horn, then route a MIDI track\n"
     "or your keyboard to it.\n"
     "\n"
     "Look for wording like 'MIDI-controlled effect', 'receive notes', or a MIDI\n"
     "destination chooser on the track it is sitting on."},
};

inline constexpr int kNumDaws = static_cast<int>(sizeof(kDaws) / sizeof(kDaws[0]));

}  // namespace harmonizer::guides
