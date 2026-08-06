#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/Room.h"

using namespace sapp::keys;

namespace {

template <typename Fn>
std::vector<float> impulseResponse(Fn&& process, int frames)
{
    std::vector<float> inL(size_t(frames), 0.0f), inR(size_t(frames), 0.0f);
    std::vector<float> outL(size_t(frames), 0.0f), outR(size_t(frames), 0.0f);
    inL[0] = inR[0] = 1.0f;
    process(inL.data(), inR.data(), outL.data(), outR.data(), frames);
    return outL;
}

float rmsRange(const std::vector<float>& x, size_t a, size_t b)
{
    double sum = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) sum += double(x[i]) * x[i];
    return float(std::sqrt(sum / double(b - a)));
}

} // namespace

TEST_CASE("early reflections arrive fast and decay", "[room]")
{
    RoomEarly er;
    er.prepare(48000);

    auto ir = impulseResponse(
        [&](const float* a, const float* b, float* c, float* d, int n) { er.process(a, b, c, d, n); },
        24000);

    for (float v : ir) REQUIRE(std::isfinite(v));
    // First taps inside 25 ms; the buffer is essentially quiet by 60 ms.
    REQUIRE(rmsRange(ir, 0, 1200) > 0.001f);
    REQUIRE(rmsRange(ir, 4800, 24000) < rmsRange(ir, 0, 1200) * 0.05f);
}

TEST_CASE("small room tail is dense, decaying, and finite", "[room]")
{
    SmallRoom room;
    room.prepare(48000);
    room.setParams(1.0f, 0.9f);

    auto ir = impulseResponse(
        [&](const float* a, const float* b, float* c, float* d, int n) { room.process(a, b, c, d, n); },
        96000);

    for (float v : ir) REQUIRE(std::isfinite(v));
    const float early = rmsRange(ir, 1000, 12000);
    const float mid = rmsRange(ir, 24000, 48000);
    const float late = rmsRange(ir, 72000, 96000);
    REQUIRE(early > 1.0e-4f);
    REQUIRE(mid < early);
    REQUIRE(late < mid);
    REQUIRE(late < early * 0.05f);  // this is a room, not a hall
}

TEST_CASE("decay parameter controls the tail length", "[room]")
{
    auto energyAt = [](float decaySeconds, size_t a, size_t b) {
        SmallRoom room;
        room.prepare(48000);
        room.setParams(1.0f, decaySeconds);
        auto ir = impulseResponse(
            [&](const float* p, const float* q, float* r, float* s, int n) {
                room.process(p, q, r, s, n);
            },
            96000);
        return rmsRange(ir, a, b);
    };

    const float shortTail = energyAt(0.25f, 24000, 48000);
    const float longTail = energyAt(2.5f, 24000, 48000);
    REQUIRE(longTail > shortTail * 3.0f);
}

TEST_CASE("size parameter scales the delay pattern without instability", "[room]")
{
    for (float size : {0.6f, 1.0f, 1.4f}) {
        SmallRoom room;
        room.prepare(48000);
        room.setParams(size, 1.5f);
        auto ir = impulseResponse(
            [&](const float* p, const float* q, float* r, float* s, int n) {
                room.process(p, q, r, s, n);
            },
            96000);
        for (float v : ir) REQUIRE(std::isfinite(v));
        REQUIRE(rmsRange(ir, 72000, 96000) < rmsRange(ir, 1000, 24000));
    }
}
