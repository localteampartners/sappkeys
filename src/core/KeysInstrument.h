#pragma once
// SappKeys instrument loading: SFZ parse → keys policy injection → sample
// load. The policy step gives the product a handle on library content the
// generic engine treats uniformly:
//
//   * Mechanical noises: every release-triggered region (key-release thumps,
//     damper noise) gets a gain_cc on the reserved internal CC 102, so the
//     KeysEngine mech-noise parameter can scale them from "as recorded" down
//     to silent without touching the sample data.
//
// Control-thread only (parses files, decodes samples). No JUCE.

#include <filesystem>
#include <string>

#include <sapp/sounds/InstrumentDefinition.h>
#include <sapp/sounds/InstrumentLoader.h>

namespace sapp::keys {

// Injects the keys policy into a parsed definition (idempotent).
// Returns the number of release regions tagged.
int applyKeysPolicy(sapp::sounds::InstrumentDefinition& definition);

// v0.12 EP TONE POLICY (MUSIC-QUALITY-PLAN E5). The electric-piano library
// is a small FM multisample with two velocity groups: level changes with
// velocity, tone never does, so every note has the same bark. SappSounds
// 0.4 can filter per voice, so an EP library (name or path says ep / fm /
// rhodes / wurli / electric) gets a low-pass whose cutoff tracks velocity
// (+18 semitones over the range) and keyboard position, plus a short filter
// envelope for the tine attack — on regions that declare no filter of their
// own. Returns the number of regions shaped. Pianos are left alone: a
// 16-layer Salamander carries its own tone in its samples.
int applyEpTonePolicy(sapp::sounds::InstrumentDefinition& definition, const std::string& libraryHint);
bool looksLikeElectricPiano(const std::string& text);

// Parse + policy + sample decode. Mirrors InstrumentLoader::loadSfz.
sapp::sounds::LoadResult loadKeysSfz(const std::filesystem::path& sfzPath,
                                     const sapp::sounds::LoaderOptions& options = {});

} // namespace sapp::keys
