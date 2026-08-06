#include "KeysInstrument.h"

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

    InstrumentLoader loader(options);
    LoadResult result = loader.loadSamples(std::move(parsed.instrument));
    result.diagnostics.insert(result.diagnostics.begin(),
                              parsed.diagnostics.begin(), parsed.diagnostics.end());
    return result;
}

} // namespace sapp::keys
