#include "FactoryPresets.h"

namespace sappkeys::presets {

// Parameter defaults (for reference — unlisted ids reset to these):
//   touch .5  dynamics 1  expression 1  unaCorda 0  lid .85  resonance .5
//   mechNoise 1  width 1  vintage 0  drive 0  roomLevel .3  roomSize 1
//   roomDecay .9  masterGain 0  limiter on  quality Normal
const std::vector<Preset>& all()
{
    static const std::vector<Preset> bank {
        { "Grand Concert",
          { { "lid", 1.0f }, { "resonance", 0.6f }, { "mechNoise", 0.7f },
            { "width", 1.25f }, { "roomLevel", 0.35f }, { "roomSize", 1.15f },
            { "roomDecay", 1.3f } },
          { { "salamander", "SalamanderGrandPiano-V3" } } },

        { "Intimate Grand",
          { { "touch", 0.4f }, { "lid", 0.55f }, { "resonance", 0.45f },
            { "vintage", 0.15f }, { "width", 0.9f }, { "roomLevel", 0.22f },
            { "roomSize", 0.8f }, { "roomDecay", 0.6f } },
          { { "salamander", "SalamanderGrandPiano-V3" } } },

        { "Bright Grand",
          { { "touch", 0.65f }, { "lid", 1.0f }, { "resonance", 0.4f },
            { "drive", 0.08f }, { "width", 1.3f }, { "roomLevel", 0.25f } },
          { { "salamander", "SalamanderGrandPiano-V3" } } },

        { "EP Mark I",
          { { "touch", 0.55f }, { "lid", 0.7f }, { "resonance", 0.15f },
            { "mechNoise", 0.4f }, { "vintage", 0.5f }, { "drive", 0.15f },
            { "width", 0.85f }, { "roomLevel", 0.18f }, { "roomSize", 0.8f },
            { "roomDecay", 0.5f } },
          { { "fm-piano1", "FM-Piano1" } } },

        { "EP Dyno",
          { { "touch", 0.75f }, { "lid", 0.9f }, { "resonance", 0.1f },
            { "mechNoise", 0.3f }, { "vintage", 0.35f }, { "drive", 0.08f },
            { "width", 1.05f }, { "roomLevel", 0.15f }, { "roomDecay", 0.45f } },
          { { "fm-piano1", "FM-Piano1" } } },

        { "EP Crunchy",
          { { "touch", 0.6f }, { "lid", 0.65f }, { "resonance", 0.1f },
            { "mechNoise", 0.35f }, { "vintage", 0.7f }, { "drive", 0.5f },
            { "width", 0.85f }, { "roomLevel", 0.12f }, { "roomSize", 0.75f },
            { "roomDecay", 0.4f } },
          { { "fm-piano1", "FM-Piano1" } } },

        { "Una Corda Soft",
          { { "unaCorda", 1.0f }, { "touch", 0.3f }, { "lid", 0.45f },
            { "resonance", 0.55f }, { "mechNoise", 0.85f }, { "vintage", 0.1f },
            { "width", 1.1f }, { "roomLevel", 0.4f }, { "roomSize", 1.2f },
            { "roomDecay", 1.6f } },
          { { "salamander", "SalamanderGrandPiano-V3" } } },

        { "Honky Tonk",
          { { "touch", 0.6f }, { "lid", 0.9f }, { "resonance", 0.35f },
            { "vintage", 0.85f }, { "drive", 0.25f }, { "width", 0.7f },
            { "roomLevel", 0.2f }, { "roomSize", 0.7f }, { "roomDecay", 0.5f } },
          { { "old-piano-fb", "" }, { "upright-piano", "UprightPiano" } } },
    };
    return bank;
}

juce::File resolveInstrument(const Preset& preset, const juce::File& samplesRoot)
{
    for (const auto& hint : preset.instruments) {
        const auto dir = samplesRoot.getChildFile(hint.libraryKey);
        if (!dir.isDirectory())
            continue;
        auto files = dir.findChildFiles(juce::File::findFiles, true, "*.sfz");
        // Deterministic pick: alphabetical, same exclusions as the browser.
        files.sort();
        for (const auto& file : files) {
            const auto path = file.getFullPathName().replaceCharacter('\\', '/');
            if (path.contains("/includes/") || path.contains("/modules/") ||
                path.contains("/Data/"))
                continue;
            const juce::String pattern(hint.mustContain);
            if (pattern.isNotEmpty() && !file.getFileName().contains(pattern))
                continue;
            return file;
        }
    }
    return {};
}

} // namespace sappkeys::presets
