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

#include <sapp/sounds/InstrumentDefinition.h>
#include <sapp/sounds/InstrumentLoader.h>

namespace sapp::keys {

// Injects the keys policy into a parsed definition (idempotent).
// Returns the number of release regions tagged.
int applyKeysPolicy(sapp::sounds::InstrumentDefinition& definition);

// Parse + policy + sample decode. Mirrors InstrumentLoader::loadSfz.
sapp::sounds::LoadResult loadKeysSfz(const std::filesystem::path& sfzPath,
                                     const sapp::sounds::LoaderOptions& options = {});

} // namespace sapp::keys
