#pragma once
// SappLink user presets — the ONE file format shared by every sapp instrument.
// Contract, rationale and storage layout: sapptune/sapplink/PRESETS.md.
//
//   <Documents>/SappSounds/presets/<instrument>/<name>.json
//
// Values are NORMALISED 0..1 (the numbers the host's automation lane stores),
// because that is the only encoding that round-trips exactly through JUCE's
// skewed NormalisableRanges. "encoding": "real" is accepted on load for
// presets a Claude session authored in engineering units; capture never uses
// it. Loading applies exactly the parameters listed and leaves the rest alone
// — resetting unlisted parameters to defaults is FACTORY preset behaviour and
// stays factory preset behaviour.
//
// This file is deliberately identical across sappsynth / sappkeys / sappkit:
// the instrument name is an argument, never baked in. Copy it verbatim into a
// new instrument, add the `preset` AudioParameterChoice last in the layout,
// and route it through that plugin's existing deferred-apply timer.

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>
#include <utility>
#include <vector>

namespace sapp::userpresets {

inline constexpr int kFormatVersion = 1;

/** APVTS id of the host-automatable preset chooser. Never stored inside a
    preset: saving it would make loading a preset re-trigger a load. */
inline constexpr const char* kPresetParamId = "preset";

/** How a user preset is labelled in the `preset` parameter's choice list, so
    it can never be confused with a factory program of the same name. */
inline constexpr const char* kUserSuffix = " (user)";

struct UserPreset
{
    juce::String name, author, created, notes, sfz;
    juce::File file;
    bool realUnits { false };   // true when the file declared "encoding": "real"
    std::vector<std::pair<juce::String, float>> params;
};

/** <Documents>/SappSounds/presets, or $SAPPSOUNDS_PRESETS when set. */
juce::File presetsRoot();
juce::File presetDir(const juce::String& instrument);

/** Filename stem for a preset name: PRESETS.md section 4's character rules. */
juce::String sanitiseFileName(const juce::String& name);

/** Read one preset file. Fails (with a reason) on a bad version, a foreign
    instrument, or an empty param set. */
bool parse(const juce::File& file, const juce::String& instrument,
           UserPreset& out, juce::String& error);

/** Every readable preset for this instrument, sorted case-insensitively by
    name — the same order the `preset` parameter's choice list uses. */
std::vector<UserPreset> scan(const juce::String& instrument);

/** By name, case-insensitive. Tries the sanitised filename first, then a full
    scan so a hand-renamed file still resolves. */
std::optional<UserPreset> findByName(const juce::String& instrument, const juce::String& name);

/** Snapshot every parameter this processor exposes (except `preset`) as
    normalised values. */
UserPreset capture(juce::AudioProcessor& processor, const juce::String& name,
                   const juce::String& notes);

juce::String toJson(const UserPreset& preset, const juce::String& instrument);

/** Write to <presetDir>/<sanitised name>.json, overwriting a same-named
    preset. Sets outFile on success. */
bool save(const UserPreset& preset, const juce::String& instrument,
          juce::File& outFile, juce::String& error);

/** Apply to a live parameter tree. Message thread only. Returns how many
    parameters actually moved (ids the plugin does not have are skipped). */
int apply(const UserPreset& preset, juce::AudioProcessorValueTreeState& apvts);

/** User-preset entries for the `preset` choice list, in scan() order. */
juce::StringArray choiceLabels(const juce::String& instrument);

/** "Evening Rust (user)" -> "Evening Rust". */
juce::String nameFromChoiceLabel(const juce::String& label);

} // namespace sapp::userpresets
