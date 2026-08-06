#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/Resonance.h"

using namespace sapp::keys;

namespace {

// Push an impulse through the resonance layer with a note claimed.
std::vector<float> ring(SympatheticResonance& res, int frames, float level)
{
    std::vector<float> inL(size_t(frames), 0.0f), inR(size_t(frames), 0.0f);
    std::vector<float> outL(size_t(frames), 0.0f), outR(size_t(frames), 0.0f);
    inL[0] = inR[0] = 1.0f;
    res.process(inL.data(), inR.data(), outL.data(), outR.data(), frames, level);
    return outL;
}

float rmsRange(const std::vector<float>& x, size_t a, size_t b)
{
    double sum = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) sum += double(x[i]) * x[i];
    return float(std::sqrt(sum / double(b - a)));
}

} // namespace

TEST_CASE("combs ring only when the pedal is down and a note claimed one", "[resonance]")
{
    SympatheticResonance res;
    res.prepare(48000);

    // No pedal: silent.
    auto silent = ring(res, 24000, 1.0f);
    for (float v : silent) REQUIRE(std::abs(v) < 1.0e-9f);

    // Pedal down + note: rings.
    res.prepare(48000);
    bool held[128] = {};
    res.setPedal(true, held);
    res.noteOn(60);
    auto rung = ring(res, 24000, 1.0f);
    REQUIRE(rmsRange(rung, 100, 24000) > 1.0e-5f);
    for (float v : rung) REQUIRE(std::isfinite(v));
}

TEST_CASE("held notes claim combs when the pedal goes down", "[resonance]")
{
    SympatheticResonance res;
    res.prepare(48000);
    bool held[128] = {};
    held[48] = held[55] = true;
    res.setPedal(true, held);
    auto rung = ring(res, 24000, 1.0f);
    REQUIRE(rmsRange(rung, 100, 24000) > 1.0e-5f);
}

TEST_CASE("resonance decays and fades quickly after pedal release", "[resonance]")
{
    SympatheticResonance res;
    res.prepare(48000);
    bool held[128] = {};
    res.setPedal(true, held);
    res.noteOn(60);

    auto rung = ring(res, 48000, 1.0f);
    const float earlyE = rmsRange(rung, 500, 6000);
    const float lateE = rmsRange(rung, 40000, 48000);
    REQUIRE(earlyE > lateE);  // decaying, not blowing up

    // Release: the tail must die within ~0.2 s.
    res.setPedal(false, nullptr);
    std::vector<float> inL(24000, 0.0f), inR(24000, 0.0f);
    std::vector<float> outL(24000, 0.0f), outR(24000, 0.0f);
    res.process(inL.data(), inR.data(), outL.data(), outR.data(), 24000, 1.0f);
    REQUIRE(rmsRange(outL, 10000, 24000) < 1.0e-6f);
}

TEST_CASE("comb tuning follows the claimed note", "[resonance]")
{
    // Feed white-ish noise; the ringing output should be periodic at ~f0.
    SympatheticResonance res;
    res.prepare(48000);
    bool held[128] = {};
    res.setPedal(true, held);
    res.noteOn(69);  // A4 = 440 Hz → period ≈ 109 samples

    const int n = 48000;
    std::vector<float> inL(size_t(n), 0.0f), inR(size_t(n), 0.0f);
    std::vector<float> outL(size_t(n), 0.0f), outR(size_t(n), 0.0f);
    uint32_t rng = 123;
    for (int i = 0; i < 4800; ++i) {
        rng = rng * 1664525u + 1013904223u;
        inL[size_t(i)] = inR[size_t(i)] = (float(rng >> 9) * (1.0f / 8388608.0f) - 1.0f) * 0.5f;
    }
    res.process(inL.data(), inR.data(), outL.data(), outR.data(), n, 1.0f);

    // Autocorrelation peak near the A4 period (allow the ±5 ct detune).
    auto corr = [&](int lag) {
        double sum = 0.0;
        for (int i = 24000; i < 44000; ++i)
            sum += double(outL[size_t(i)]) * double(outL[size_t(i - lag)]);
        return sum;
    };
    double best = 0.0;
    int bestLag = 0;
    for (int lag = 80; lag <= 140; ++lag) {
        const double c = corr(lag);
        if (c > best) { best = c; bestLag = lag; }
    }
    REQUIRE(bestLag >= 106);
    REQUIRE(bestLag <= 112);
}

TEST_CASE("reseed makes detune deterministic", "[resonance]")
{
    auto renderOnce = [] {
        SympatheticResonance res;
        res.prepare(48000);
        res.reseed(7);
        bool held[128] = {};
        res.setPedal(true, held);
        res.noteOn(60);
        return ring(res, 12000, 1.0f);
    };
    auto a = renderOnce();
    auto b = renderOnce();
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); i += 97) REQUIRE(a[i] == b[i]);
}
