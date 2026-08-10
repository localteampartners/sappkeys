#pragma once
// SappLink v1 CC-in mapping for SappKeys (framework-free).
//
// Source of truth: ~/apps/sapptune/sapplink/manifests/sappkeys.json —
// the unit test asserts this table matches the vendored copy in tests/data/.
// Parameter identity = the plugin's stable APVTS parameter IDs.
//
// Deliberately ABSENT from this table (engine-native, existing behavior):
//   CC 1   → dynamics    (gentle level/brightness trim, handled by KeysEngine)
//   CC 11  → expression  (handled by KeysEngine)
//   CC 64  → sustain     (real pedal semantics in SappSounds)
//   CC 102 → reserved internal mech-noise gain (KeysEngine injects it;
//            external CC 102 is dropped)
//   pitch bend
//
// Suite-reserved: CC 3 is `clean` in every sapp* manifest (sapptune #30) —
// one broadcast takes the modeled imperfection out of a whole chain.

#include <array>

#include "KeysEngine.h"

namespace sapp::keys::sapplink {

enum class Curve { Linear, Log };

struct CCMapping {
    int cc;
    const char* paramId;           // stable APVTS parameter ID, verbatim
    float KeysParams::* field;     // same parameter in the core struct
    float lo, hi;                  // engineering units at CC 0 and CC 127
    Curve curve;
};

inline constexpr int kNumMappings = 13;
const std::array<CCMapping, kNumMappings>& mappings();

// nullptr if this CC is not part of the SappLink contract.
const CCMapping* findMapping(int cc);

// CC value 0..127 → engineering units through the mapping's curve.
float ccToEngineering(const CCMapping& mapping, int ccValue);

// Offline/CLI path: apply a mapped CC to the params struct.
// Returns true if the CC was part of the mapping.
bool applyCcToParams(KeysParams& params, int cc, int ccValue);

} // namespace sapp::keys::sapplink
