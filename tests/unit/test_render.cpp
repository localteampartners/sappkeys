#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <sapp/sounds/DiagnosticInstrument.h>

#include "core/KeysRender.h"

using namespace sapp::keys;
using sapp::sounds::TimedMidiEvent;

TEST_CASE("keys offline render is deterministic and audible", "[render]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.5f, 3});

    std::vector<TimedMidiEvent> song;
    song.push_back({0.02, 0xB0, 0, 64, 100, 0});  // pedal down
    for (uint8_t k : {48, 55, 64})
        song.push_back({0.05, 0x90, 0, k, 90, 0});
    for (uint8_t k : {48, 55, 64})
        song.push_back({1.6, 0x80, 0, k, 0, 0});
    song.push_back({2.2, 0xB0, 0, 64, 0, 0});     // pedal up

    KeysRenderOptions options;
    options.tailSeconds = 1.5;
    options.params.vintage = 0.4f;   // exercises random tune + wow path
    options.params.resonance = 0.8f;

    auto a = renderKeys(inst, song, options);
    auto b = renderKeys(inst, song, options);

    REQUIRE(a.left.size() == b.left.size());
    for (size_t i = 0; i < a.left.size(); i += 131)
        REQUIRE(a.left[i] == b.left[i]);
    CHECK(a.peak > 0.02f);
    CHECK(a.peak <= 1.0f);
}

TEST_CASE("different seeds give different takes with vintage detune", "[render]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.5f, 3});

    std::vector<TimedMidiEvent> song;
    song.push_back({0.05, 0x90, 0, 60, 100, 0});
    song.push_back({1.0, 0x80, 0, 60, 0, 0});

    KeysRenderOptions options;
    options.tailSeconds = 0.5;
    options.params.vintage = 1.0f;

    options.seed = 1;
    auto a = renderKeys(inst, song, options);
    options.seed = 2;
    auto b = renderKeys(inst, song, options);

    bool differs = false;
    for (size_t i = 0; i < a.left.size() && !differs; ++i)
        differs = a.left[i] != b.left[i];
    REQUIRE(differs);
}

TEST_CASE("room level parameter adds tail energy after note release", "[render]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 3});

    auto render = [&](float roomLevel) {
        std::vector<TimedMidiEvent> song;
        song.push_back({0.05, 0x90, 0, 60, 110, 0});
        song.push_back({0.6, 0x80, 0, 60, 0, 0});
        KeysRenderOptions options;
        options.tailSeconds = 1.5;
        options.params.roomLevel = roomLevel;
        options.params.roomDecay = 2.0f;
        options.params.resonance = 0.0f;
        return renderKeys(inst, song, options);
    };

    auto tailRms = [](const KeysRenderOutput& out) {
        // Energy from 1.0 s on (after the diagnostic staccato-ish decay).
        double sum = 0.0;
        const size_t a = size_t(1.0 * out.sampleRate);
        for (size_t i = a; i < out.left.size(); ++i)
            sum += double(out.left[i]) * out.left[i];
        return std::sqrt(sum / double(out.left.size() - a));
    };

    const double dry = tailRms(render(0.0f));
    const double wet = tailRms(render(0.9f));
    REQUIRE(wet > dry * 1.2);
}

TEST_CASE("quality draft/normal both render finite audio", "[render]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 3});
    std::vector<TimedMidiEvent> song;
    song.push_back({0.05, 0x90, 0, 61, 100, 0});   // off-root → resampling path
    song.push_back({0.8, 0x80, 0, 61, 0, 0});

    for (int quality : {0, 1}) {
        KeysRenderOptions options;
        options.tailSeconds = 0.5;
        options.params.quality = quality;
        auto out = renderKeys(inst, song, options);
        REQUIRE(out.peak > 0.01f);
        for (float v : out.left) REQUIRE(std::isfinite(v));
    }
}
