#pragma once
// SappKeys factory presets: named starting points selectable from the host
// program API and via MIDI program change (SappLink set_patches). Each preset
// is a plain param-id → value map (real parameter units); anything unlisted
// resets to its default when the preset is applied. A preset may also carry
// instrument hints — candidate SFZ libraries under the shared samples root —
// so "EP Mark I" can swap to an electric-piano library when one is installed.
// Presets are ADDITIVE: they never change parameter IDs, CC mappings, or
// state save/load, and CCs keep working on top after a program change.

#include <vector>

#include <juce_core/juce_core.h>

namespace sappkeys::presets {

struct Value { const char* id; float value; };

// One candidate instrument location: a library folder under the samples root
// (same keys as the GET SOUNDS registry) plus a filename substring to pick
// the main .sfz inside it. Candidates are tried in order; the first one that
// resolves to an installed file wins. No candidate installed = keep the
// currently loaded instrument and apply parameter values only.
struct LibraryHint { const char* libraryKey; const char* mustContain; };

struct Preset {
    const char* name;
    std::vector<Value> values;
    std::vector<LibraryHint> instruments;
};

// Factory bank; program N is all()[N].
const std::vector<Preset>& all();

// Resolve a preset's instrument hints against the samples root (message
// thread — walks the filesystem). Returns an invalid File when nothing
// matching is installed.
juce::File resolveInstrument(const Preset& preset, const juce::File& samplesRoot);

} // namespace sappkeys::presets
