#include "KeysInstrument.h"

#include <cctype>

#include <sapp/sounds/SfzParser.h>

#include "KeysEngine.h"

namespace sapp::keys {

using namespace sapp::sounds;

int applyKeysPolicy(InstrumentDefinition& definition)
{
    int tagged = 0;
    for (auto& region : definition.regions) {
        if (region.trigger != TriggerMode::Release &&
            region.trigger != TriggerMode::ReleaseKey)
            continue;
        bool present = false;
        for (const auto& g : region.gainCc)
            if (int(g.cc) == kMechNoiseInternalCc) present = true;
        if (!present)
            region.gainCc.push_back({uint8_t(kMechNoiseInternalCc), kMechNoiseRangeDb});
        ++tagged;
    }
    return tagged;
}

bool looksLikeElectricPiano(const std::string& text)
{
    std::string t;
    for (const char c : text) t += char(std::tolower(static_cast<unsigned char>(c)));
    return t.find("fm-piano") != std::string::npos || t.find("fm piano") != std::string::npos
        || t.find("rhodes") != std::string::npos || t.find("wurli") != std::string::npos
        || t.find("electric") != std::string::npos || t.find("epiano") != std::string::npos
        || t.find("e-piano") != std::string::npos || t.find("/ep/") != std::string::npos;
}

int applyEpTonePolicy(InstrumentDefinition& definition, const std::string& libraryHint)
{
    if (!looksLikeElectricPiano(libraryHint) && !looksLikeElectricPiano(definition.name))
        return 0;
    int shaped = 0;
    for (auto& region : definition.regions) {
        if (region.trigger == TriggerMode::Release || region.trigger == TriggerMode::ReleaseKey)
            continue;                       // mech noises keep their air
        if (region.filType != RegionDefinition::FilterType::None || region.cutoffHz > 0.0f)
            continue;                       // the library's own filter wins
        region.filType = RegionDefinition::FilterType::Lpf2p;
        region.cutoffHz = 1800.0f;          // a soft note: warm, round
        region.resonanceDb = 1.5f;
        region.filVeltrack = 1800.0f;       // a hard note: 1800 * 2^1.5 ≈ 5.1 kHz, the bark
        region.filKeytrack = 60.0f;         // higher keys open up a little
        region.filKeycenter = 60;
        region.fileg.attack = 0.0f;
        region.fileg.decay = 0.35f;
        region.fileg.sustain = 0.0f;
        region.fileg.release = 0.2f;
        region.filegDepthCents = 700.0f;    // the tine's attack brightness, then the body
        ++shaped;
    }
    return shaped;
}

LoadResult loadKeysSfz(const std::filesystem::path& sfzPath, const LoaderOptions& options)
{
    SfzParser parser(options.parserLimits);
    auto parsed = parser.parseFile(sfzPath);
    if (!parsed.ok || parsed.hasErrors()) {
        LoadResult result;
        result.diagnostics = parsed.diagnostics;
        result.ok = false;
        return result;
    }
    applyKeysPolicy(parsed.instrument);
    applyEpTonePolicy(parsed.instrument, sfzPath.generic_string());

    InstrumentLoader loader(options);
    LoadResult result = loader.loadSamples(std::move(parsed.instrument));
    result.diagnostics.insert(result.diagnostics.begin(),
                              parsed.diagnostics.begin(), parsed.diagnostics.end());
    return result;
}

} // namespace sapp::keys
