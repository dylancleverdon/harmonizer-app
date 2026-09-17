#pragma once

#include "JazzVoicer.h"

/**
 * Built-in custom chord dictionaries -- full starting points, not single
 * voicings the way JazzChordLibrary's entries are. Each one fills every
 * degree of both major and minor with an explicit voicing in a particular
 * player's or style's vocabulary, so turning it on changes how jazz mode
 * sounds everywhere rather than one chord at a time.
 *
 * Same representation a hand-built custom dictionary uses (CustomDictionary,
 * CustomEntry-per-degree), so loading one is exactly the same operation
 * saving/loading a user preset already is -- just sourced from this table
 * instead of a file on disk, and read-only from the editor's point of view.
 */
namespace jazz {

struct FactoryPreset {
    const char* name;
    const char* description;
    CustomDictionary dict;
};

int factoryPresetCount();
const FactoryPreset& factoryPreset(int index);

}  // namespace jazz
