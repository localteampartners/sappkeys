#pragma once
// Deterministic offline render through the full keys chain
// (sampler → touch/tone → resonance → drive → room).

#include <cstdint>
#include <vector>

#include <sapp/sounds/MidiFile.h>

#include "KeysEngine.h"

namespace sapp::keys {

struct KeysRenderOptions {
    double sampleRate = 48000.0;
    int blockFrames = 512;
    double tailSeconds = 3.0;
    uint32_t seed = 0x5A9F00D5;
    KeysParams params;
};

struct KeysRenderOutput {
    std::vector<float> left, right;
    double sampleRate = 48000.0;
    float peak = 0.0f;
    float rms = 0.0f;
};

KeysRenderOutput renderKeys(const sapp::sounds::InstrumentPtr& instrument,
                            const std::vector<sapp::sounds::TimedMidiEvent>& events,
                            const KeysRenderOptions& options);

} // namespace sapp::keys
