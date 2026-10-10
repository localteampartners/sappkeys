#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// ---------------------------------------------------------------- v0.15.1 --
// The sapplisten "steady partial cluster": the FDN's fractional read wrapped
// in single precision, so a position a hair below zero rounded up to exactly
// `size` and the read landed one element past the line. What lives there is
// whatever the heap put next to it — 0 in most renders, 1.17e13 in about one
// in four, and that one sample was a full-scale impulse into the room.

TEST_CASE("delay taps never leave the line, even when float rounding would", "[room][v0.15.1]")
{
    // The exact read the station hit: line 1 at roomSize 0.75, 48 kHz.
    const int size = 1438, write = 643;
    const float delay = 643.000061f;

    // What the old code computed: the wrap lands ON size.
    float old = float(write) - delay;
    while (old < 0.0f) old += float(size);
    REQUIRE(int(old) == size);

    const DelayTap tap = delayTap(write, delay, size);
    REQUIRE(tap.i0 >= 0);
    REQUIRE(tap.i0 < size);
    REQUIRE(tap.i1 >= 0);
    REQUIRE(tap.i1 < size);
    REQUIRE(tap.frac >= 0.0f);
    REQUIRE(tap.frac <= 1.0f);
    // It is the read just behind the write head, nearly all on the newest
    // sample before it — not a jump to the far end of the line.
    REQUIRE(tap.i0 == size - 1);
    REQUIRE(tap.i1 == 0);

    // Every write position against delays that sit within a few ulps of it,
    // across the line lengths both the room and the comb bank use.
    int outside = 0;
    for (const int n : {64, 1438, 1928, 2298, 3407}) {
        for (int w = 0; w < n; ++w) {
            for (const float d0 : {float(w), float(w) + 1.0f, float(n - 1)}) {
                float d = d0;
                for (int step = 0; step < 6; ++step) {
                    const DelayTap t = delayTap(w, d, n);
                    if (t.i0 < 0 || t.i0 >= n || t.i1 < 0 || t.i1 >= n ||
                        !(t.frac >= 0.0f && t.frac <= 1.0f))
                        ++outside;
                    d = std::nextafter(d, 1.0e9f);
                }
            }
        }
    }
    REQUIRE(outside == 0);
}

TEST_CASE("the Jazz Grand room runs two minutes without an over-read or a blow-up",
          "[room][v0.15.1]")
{
    // Same settings and the same sample clock the plugin runs (the room is
    // processed every block from prepare(), so the read positions that went
    // past the line in the station take recur here). Under AddressSanitizer
    // (see CHANGELOG 0.15.1) the old code fails this with a heap over-read;
    // in a plain build the bound below is the audible half of the contract.
    SmallRoom room;
    room.prepare(48000);
    room.setParams(0.75f, 0.55f);
    RoomEarly early;
    early.prepare(48000);

    constexpr int kBlock = 512;
    std::vector<float> inL(kBlock), inR(kBlock), eL(kBlock), eR(kBlock), oL(kBlock), oR(kBlock);
    uint32_t rng = 12345u;
    float peak = 0.0f;
    bool finite = true;
    for (int b = 0; b < 120 * 48000 / kBlock; ++b) {
        for (int f = 0; f < kBlock; ++f) {
            rng = rng * 1664525u + 1013904223u;
            const float noise = (float(rng >> 8) / 16777216.0f - 0.5f) * 0.2f;
            inL[size_t(f)] = noise;
            inR[size_t(f)] = -noise;
        }
        early.process(inL.data(), inR.data(), eL.data(), eR.data(), kBlock);
        room.process(eL.data(), eR.data(), oL.data(), oR.data(), kBlock);
        for (int f = 0; f < kBlock; ++f) {
            finite = finite && std::isfinite(oL[size_t(f)]) && std::isfinite(oR[size_t(f)]);
            peak = std::max({peak, std::abs(oL[size_t(f)]), std::abs(oR[size_t(f)])});
        }
    }
    REQUIRE(finite);
    // Noise at ±0.1 into a lossy room: the tail stays well under full scale.
    REQUIRE(peak < 1.0f);
}
