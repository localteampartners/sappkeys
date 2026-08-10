#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sapp/sounds/DiagnosticInstrument.h>

#include "core/KeysRender.h"
#include "core/SappLinkCCMap.h"

// The vendored manifest at tests/data/sapplink-manifest.json mirrors the
// SOURCE OF TRUTH at ~/apps/sapptune/sapplink/manifests/sappkeys.json.
// If sapptune's manifest changes, update the vendored copy AND the table in
// src/core/SappLinkCCMap.cpp together — this test makes silent drift fail CI.

using namespace sapp::keys;
using namespace sapp::keys::sapplink;

namespace {

struct ManifestRow {
    int cc = -1;
    std::string id, curve;
    float lo = 0, hi = 0;
};

// Minimal extractor for the known manifest shape (no JSON dependency in the
// core test target): parses each object of the "parameters" array.
std::vector<ManifestRow> loadManifest(const std::string& path)
{
    std::ifstream file(path);
    REQUIRE(file.good());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string text = ss.str();

    auto grabString = [](const std::string& obj, const std::string& key) {
        const auto k = obj.find("\"" + key + "\"");
        if (k == std::string::npos) return std::string();
        const auto q1 = obj.find('"', obj.find(':', k));
        const auto q2 = obj.find('"', q1 + 1);
        return obj.substr(q1 + 1, q2 - q1 - 1);
    };

    std::vector<ManifestRow> rows;
    size_t pos = text.find("\"parameters\"");
    REQUIRE(pos != std::string::npos);
    while ((pos = text.find("{ \"id\"", pos)) != std::string::npos) {
        const auto end = text.find('}', pos);
        const std::string obj = text.substr(pos, end - pos);
        ManifestRow row;
        row.id = grabString(obj, "id");
        row.curve = grabString(obj, "curve");
        row.cc = std::stoi(obj.substr(obj.find(':', obj.find("\"cc\"")) + 1));
        const auto rangeStart = obj.find('[', obj.find("\"range\""));
        const auto comma = obj.find(',', rangeStart);
        row.lo = std::stof(obj.substr(rangeStart + 1, comma - rangeStart - 1));
        row.hi = std::stof(obj.substr(comma + 1, obj.find(']', comma) - comma - 1));
        rows.push_back(row);
        pos = end;
    }
    return rows;
}

} // namespace

TEST_CASE("SappLink table matches the vendored manifest exactly", "[sapplink]")
{
    const auto rows = loadManifest(std::string(SAPPKEYS_TEST_DATA_DIR) + "/sapplink-manifest.json");
    REQUIRE(rows.size() == size_t(kNumMappings));

    for (const auto& row : rows) {
        INFO("cc " << row.cc << " id " << row.id);
        const auto* mapping = findMapping(row.cc);
        REQUIRE(mapping != nullptr);
        REQUIRE(std::string(mapping->paramId) == row.id);
        REQUIRE(mapping->lo == row.lo);
        REQUIRE(mapping->hi == row.hi);
        REQUIRE(std::string(mapping->curve == Curve::Log ? "log" : "linear") == row.curve);
    }

    // No duplicate CC assignments in the table.
    for (const auto& a : mappings())
        REQUIRE(findMapping(a.cc) == &a);
}

TEST_CASE("reserved controllers stay engine-native", "[sapplink]")
{
    // CC 1 dynamics, CC 11 expression, CC 64 sustain: never in the mapping.
    // CC 102 is the internal mech-noise gain lane — also never mapped.
    REQUIRE(findMapping(1) == nullptr);
    REQUIRE(findMapping(11) == nullptr);
    REQUIRE(findMapping(64) == nullptr);
    REQUIRE(findMapping(kMechNoiseInternalCc) == nullptr);
}

TEST_CASE("CC curves interpolate correctly and monotonically", "[sapplink]")
{
    const auto* decay = findMapping(20);  // roomDecay, log 0.2..2.5
    REQUIRE(decay != nullptr);
    REQUIRE(std::abs(ccToEngineering(*decay, 0) - 0.2f) < 1e-4f);
    REQUIRE(std::abs(ccToEngineering(*decay, 127) - 2.5f) < 1e-3f);
    const float mid = ccToEngineering(*decay, 64);  // ≈ geometric mean ~0.71 s
    REQUIRE(mid > 0.55f);
    REQUIRE(mid < 0.9f);

    const auto* soft = findMapping(67);  // unaCorda, linear 0..1
    REQUIRE(soft != nullptr);
    REQUIRE(std::abs(ccToEngineering(*soft, 0) - 0.0f) < 1e-5f);
    REQUIRE(std::abs(ccToEngineering(*soft, 127) - 1.0f) < 1e-5f);

    for (const auto& mapping : mappings()) {
        float previous = ccToEngineering(mapping, 0);
        for (int v = 1; v <= 127; ++v) {
            const float value = ccToEngineering(mapping, v);
            REQUIRE(std::isfinite(value));
            REQUIRE(value >= previous - 1e-6f);
            previous = value;
        }
    }
}

TEST_CASE("vintage answers CC 12, never CC 21", "[sapplink]")
{
    // sappkeys #3: CC 21 is eqAirGain in Sapprack / Sappmaster / Sappedal.
    // CCs are broadcast to every plugin in a chain, so an air-EQ setpoint used
    // to age the piano as a side effect — and no host --set on Vintage could
    // win, because the CC kept re-firing from the clip.
    const auto* vintage = findMapping(12);
    REQUIRE(vintage != nullptr);
    REQUIRE(std::string(vintage->paramId) == "vintage");
    REQUIRE(findMapping(21) == nullptr);

    KeysParams params;
    REQUIRE(applyCcToParams(params, 12, 127));
    REQUIRE(std::abs(params.vintage - 1.0f) < 1e-5f);
    params.vintage = 0.0f;
    REQUIRE_FALSE(applyCcToParams(params, 21, 127));
    REQUIRE(params.vintage == 0.0f);
}

TEST_CASE("clean is on the suite-reserved CC 3", "[sapplink][clean]")
{
    const auto* clean = findMapping(3);
    REQUIRE(clean != nullptr);
    REQUIRE(std::string(clean->paramId) == "clean");
    REQUIRE(clean->lo == 0.0f);
    REQUIRE(clean->hi == 1.0f);
    REQUIRE(clean->curve == Curve::Linear);

    KeysParams params;
    REQUIRE(params.clean == 0.0f);            // default: nothing scaled
    REQUIRE(applyCcToParams(params, 3, 127));
    REQUIRE(std::abs(params.clean - 1.0f) < 1e-5f);
    REQUIRE(applyCcToParams(params, 3, 64));
    REQUIRE(std::abs(params.clean - 64.0f / 127.0f) < 1e-5f);
    REQUIRE(applyCcToParams(params, 3, 0));
    REQUIRE(params.clean == 0.0f);
}

TEST_CASE("CC 3 in a rendered clip removes the mechanical noise", "[sapplink][clean]")
{
    // End-to-end through the offline render path an agent actually uses: a
    // CC 3 in the clip must reach the engine's imperfection scaling.
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto renderWithClean = [&](int ccValue) {
        std::vector<sapp::sounds::TimedMidiEvent> song;
        song.push_back({0.0, 0xB0, 0, 3, uint8_t(ccValue), 0});
        song.push_back({0.2, 0x90, 0, 60, 100, 0});
        song.push_back({1.2, 0x80, 0, 60, 0, 0});
        KeysRenderOptions options;
        options.tailSeconds = 0.3;
        options.params.vintage = 1.0f;   // maximum modeled wear...
        options.params.roomLevel = 0.0f;
        return renderKeys(inst, song, options);
    };

    const auto aged = renderWithClean(0);
    const auto cleaned = renderWithClean(127);
    // ...which CC 3 takes back out: the wear colour changes the render.
    REQUIRE(aged.rms > 0.0f);
    REQUIRE(std::abs(cleaned.rms - aged.rms) > aged.rms * 1e-4f);
}

TEST_CASE("applyCcToParams writes the mapped field and ignores others", "[sapplink]")
{
    KeysParams params;
    REQUIRE(applyCcToParams(params, 67, 127));
    REQUIRE(std::abs(params.unaCorda - 1.0f) < 1e-5f);
    REQUIRE(applyCcToParams(params, 7, 0));
    REQUIRE(std::abs(params.masterGainDb - (-24.0f)) < 1e-4f);
    REQUIRE_FALSE(applyCcToParams(params, 1, 64));    // dynamics is native
    REQUIRE_FALSE(applyCcToParams(params, 74, 64));   // sappsynth's CC, not ours
}

TEST_CASE("CC 7 in a rendered clip scales output level", "[sapplink]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto renderWithMasterCc = [&](int ccValue) {
        std::vector<sapp::sounds::TimedMidiEvent> song;
        song.push_back({0.0, 0xB0, 0, 7, uint8_t(ccValue), 0});
        song.push_back({0.2, 0x90, 0, 60, 100, 0});
        song.push_back({1.2, 0x80, 0, 60, 0, 0});
        KeysRenderOptions options;
        options.tailSeconds = 0.3;
        options.params.limiter = false;
        return renderKeys(inst, song, options);
    };

    const float quiet = renderWithMasterCc(0).rms;    // -24 dB
    const float loud = renderWithMasterCc(127).rms;   // +12 dB
    REQUIRE(loud > quiet * 10.0f);
}

TEST_CASE("CC 67 (una corda) in a rendered clip softens the strike", "[sapplink]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto renderWithSoftPedal = [&](int ccValue) {
        std::vector<sapp::sounds::TimedMidiEvent> song;
        song.push_back({0.0, 0xB0, 0, 67, uint8_t(ccValue), 0});
        song.push_back({0.2, 0x90, 0, 60, 100, 0});
        song.push_back({1.2, 0x80, 0, 60, 0, 0});
        KeysRenderOptions options;
        options.tailSeconds = 0.3;
        options.params.roomLevel = 0.0f;
        return renderKeys(inst, song, options);
    };

    const float open = renderWithSoftPedal(0).rms;
    const float soft = renderWithSoftPedal(127).rms;
    REQUIRE(soft < open * 0.8f);   // quieter strike + darker tilt
    REQUIRE(soft > 0.0f);
}
