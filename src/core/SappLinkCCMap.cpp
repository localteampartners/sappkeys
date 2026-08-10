#include "SappLinkCCMap.h"

#include <algorithm>
#include <cmath>

namespace sapp::keys::sapplink {

// CC assignment follows the SappLink conventions (PROTOCOL.md): standard MMA
// CCs where one exists (7 volume, 67 soft pedal, 91 reverb send), free CCs
// 14–31 otherwise. Ranges are the plugin's real APVTS ranges — the manifest
// mirrors these.
//
// CC 3 is the suite-wide reserved `clean` lane (sapptune #30): every sapp*
// instrument and effect answers it with the same meaning, so one message
// broadcast down a chain takes the modeled imperfection out of all of them.
//
// CC 12 carries `vintage`. It used to be CC 21 — which Sapprack, Sappmaster
// and Sappedal all use for eqAirGain, so an air-EQ setpoint sent to a chain
// also aged the piano and no host --set on Vintage could win (sappkeys #3).
// CC 12 is free across every manifest in the suite.
const std::array<CCMapping, kNumMappings>& mappings()
{
    static const std::array<CCMapping, kNumMappings> table { {
        { 3,  "clean",      &KeysParams::clean,        0.0f,   1.0f,  Curve::Linear },
        { 7,  "masterGain", &KeysParams::masterGainDb, -24.0f, 12.0f, Curve::Linear },
        { 12, "vintage",    &KeysParams::vintage,      0.0f,   1.0f,  Curve::Linear },
        { 14, "touch",      &KeysParams::touch,        0.0f,   1.0f,  Curve::Linear },
        { 15, "lid",        &KeysParams::lid,          0.0f,   1.0f,  Curve::Linear },
        { 16, "resonance",  &KeysParams::resonance,    0.0f,   1.0f,  Curve::Linear },
        { 17, "mechNoise",  &KeysParams::mechNoise,    0.0f,   1.0f,  Curve::Linear },
        { 18, "width",      &KeysParams::width,        0.0f,   2.0f,  Curve::Linear },
        { 19, "roomSize",   &KeysParams::roomSize,     0.6f,   1.4f,  Curve::Linear },
        { 20, "roomDecay",  &KeysParams::roomDecay,    0.2f,   2.5f,  Curve::Log },
        { 22, "drive",      &KeysParams::drive,        0.0f,   1.0f,  Curve::Linear },
        { 67, "unaCorda",   &KeysParams::unaCorda,     0.0f,   1.0f,  Curve::Linear },
        { 91, "roomLevel",  &KeysParams::roomLevel,    0.0f,   1.0f,  Curve::Linear },
    } };
    return table;
}

const CCMapping* findMapping(int cc)
{
    for (const auto& mapping : mappings())
        if (mapping.cc == cc)
            return &mapping;
    return nullptr;
}

float ccToEngineering(const CCMapping& mapping, int ccValue)
{
    const float t = float(std::clamp(ccValue, 0, 127)) / 127.0f;
    if (mapping.curve == Curve::Log)
        return mapping.lo * std::pow(mapping.hi / mapping.lo, t);
    return mapping.lo + (mapping.hi - mapping.lo) * t;
}

bool applyCcToParams(KeysParams& params, int cc, int ccValue)
{
    const auto* mapping = findMapping(cc);
    if (mapping == nullptr)
        return false;
    params.*(mapping->field) = ccToEngineering(*mapping, ccValue);
    return true;
}

} // namespace sapp::keys::sapplink
