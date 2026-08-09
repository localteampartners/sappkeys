// Output-safety regressions for sapptune#17: a dense burst of unexpected MIDI
// (a re-enumerating controller reaching a track left on "All Ins") must never
// produce a full-scale distorted blast, and must never leave notes sounding.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <sapp/sounds/DiagnosticInstrument.h>

#include "core/KeysEngine.h"

using namespace sapp::keys;
using sapp::sounds::MidiEvent;

namespace {

MidiEvent noteOn(uint32_t frame, uint8_t note, uint8_t vel)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOn;
    e.frame = frame;
    e.note = note;
    e.value = vel;
    return e;
}

MidiEvent noteOff(uint32_t frame, uint8_t note)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOff;
    e.frame = frame;
    e.note = note;
    return e;
}

struct Measured {
    float peak = 0.0f;
    float burstRms = 0.0f;   // first 0.5 s
    float tailRms = 0.0f;    // last 1 s
    bool allFinite = true;
    int voicesAtEnd = 0;
};

// Renders `seconds` of audio, feeding `events` in the block their frame lands
// in. Frames are absolute.
Measured render(KeysEngine& engine, std::vector<MidiEvent> events,
                double seconds, int block = 512)
{
    Measured m;
    std::stable_sort(events.begin(), events.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });
    const int total = int(seconds * 48000.0);
    std::vector<float> l, r;
    l.assign(size_t(block), 0.0f);
    r.assign(size_t(block), 0.0f);
    std::vector<MidiEvent> blockEvents;
    blockEvents.reserve(events.size() + 8);
    size_t next = 0;
    double burstSum = 0.0, tailSum = 0.0;
    size_t burstCount = 0, tailCount = 0;

    for (int start = 0; start < total; start += block) {
        const int frames = std::min(block, total - start);
        blockEvents.clear();
        while (next < events.size() && events[next].frame < uint32_t(start + frames)) {
            MidiEvent e = events[next];
            e.frame -= uint32_t(start);
            blockEvents.push_back(e);
            ++next;
        }
        engine.process(blockEvents.data(), int(blockEvents.size()), l.data(), r.data(), frames);
        for (int f = 0; f < frames; ++f) {
            const float a = l[size_t(f)], b = r[size_t(f)];
            if (!std::isfinite(a) || !std::isfinite(b)) { m.allFinite = false; continue; }
            m.peak = std::max({m.peak, std::abs(a), std::abs(b)});
            const double energy = double(a) * a + double(b) * b;
            if (start + f < 24000) { burstSum += energy; burstCount += 2; }
            if (start + f > total - 48000) { tailSum += energy; tailCount += 2; }
        }
    }
    m.burstRms = float(std::sqrt(burstSum / double(std::max<size_t>(1, burstCount))));
    m.tailRms = float(std::sqrt(tailSum / double(std::max<size_t>(1, tailCount))));
    m.voicesAtEnd = engine.sampler().activeVoiceCount();
    return m;
}

sapp::sounds::InstrumentPtr testInstrument()
{
    return sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.5f, 3});
}

// N simultaneous notes at velocity 127, released two seconds later.
std::vector<MidiEvent> simultaneousBurst(int count, uint32_t releaseFrame = 96000)
{
    std::vector<MidiEvent> events;
    for (int i = 0; i < count; ++i) {
        const uint8_t note = uint8_t(36 + (i % 60));
        events.push_back(noteOn(0, note, 127));
        events.push_back(noteOff(releaseFrame, note));
    }
    return events;
}

} // namespace

TEST_CASE("the safety limiter is on by default", "[safety]")
{
    // The plugin parameter default must match, and it does: PluginProcessor's
    // "limiter" AudioParameterBool is constructed with true. Changing either
    // would silently change what an existing session recalls.
    REQUIRE(KeysParams{}.limiter);
}

TEST_CASE("a dense simultaneous burst stays bounded and undistorted", "[safety]")
{
    auto inst = testInstrument();

    float eightNoteRms = 0.0f;
    for (int count : {1, 4, 8, 16, 32, 64}) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        engine.setParams(KeysParams{});    // defaults: limiter on
        engine.setInstrument(inst);

        const auto m = render(engine, simultaneousBurst(count), 4.0);
        INFO("notes = " << count << "  peak = " << 20.0f * std::log10(m.peak) << " dBFS");
        REQUIRE(m.allFinite);
        // Peak-accurate: the limiter must not let a single sample past.
        REQUIRE(m.peak <= kSafetyCeiling + 1.0e-4f);
        // And it must limit, not soft-clip. The old tanh output stage held the
        // peak at 0 dBFS but pushed the burst RMS to -6.5 dBFS at 64 notes —
        // a near full-scale square wave. -8 dBFS is the line that catches that.
        REQUIRE(m.burstRms < 0.398f);
        if (count == 8) eightNoteRms = m.burstRms;
        // Piling on notes must stop making it louder once the limiter engages.
        if (count > 8) REQUIRE(m.burstRms < eightNoteRms * 2.0f);
    }
}

TEST_CASE("output is bounded even with the safety limiter switched off", "[safety]")
{
    auto inst = testInstrument();
    KeysEngine engine;
    engine.prepare(48000, 512);
    KeysParams p;
    p.limiter = false;
    p.masterGainDb = 12.0f;            // worst case the parameter allows
    engine.setParams(p);
    engine.setInstrument(inst);

    const auto m = render(engine, simultaneousBurst(64), 4.0);
    REQUIRE(m.allFinite);
    REQUIRE(m.peak <= kOutputBound);   // unconditional, not the limiter's job
}

TEST_CASE("non-finite audio never leaves the engine", "[safety]")
{
    auto inst = testInstrument();
    KeysEngine engine;
    engine.prepare(48000, 512);
    KeysParams p;
    p.masterGainDb = std::numeric_limits<float>::infinity();
    engine.setParams(p);
    engine.setInstrument(inst);

    const auto m = render(engine, simultaneousBurst(8), 1.5);
    REQUIRE(m.allFinite);
    REQUIRE(m.peak <= kOutputBound);

    // And the engine recovers: normal parameters produce audio again.
    engine.setParams(KeysParams{});
    const auto after = render(engine, simultaneousBurst(4, 24000), 2.0);
    REQUIRE(after.allFinite);
    REQUIRE(after.peak > 0.0f);
}

TEST_CASE("a burst whose note-offs land in the same block leaves nothing sounding",
          "[safety]")
{
    // The failure this covers: SappSounds starts a stolen voice only after its
    // steal fade, so a note-off arriving inside that window found no active
    // voice and was dropped — the note then sounded forever. Enough notes to
    // force stealing, with every note-off in the same 512-frame block.
    auto inst = testInstrument();
    for (int count : {50, 96, 200}) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        engine.setParams(KeysParams{});
        engine.setInstrument(inst);

        std::vector<MidiEvent> events;
        for (int i = 0; i < count; ++i) {
            const uint8_t note = uint8_t(24 + (i % 84));
            events.push_back(noteOn(0, note, 127));
        }
        for (int i = 0; i < count; ++i)
            events.push_back(noteOff(96, uint8_t(24 + (i % 84))));

        const auto m = render(engine, events, 8.0);
        INFO("notes = " << count << "  voices at 8 s = " << m.voicesAtEnd);
        REQUIRE(m.voicesAtEnd == 0);
        REQUIRE(m.tailRms < 1.0e-4f);
    }
}

TEST_CASE("a MIDI flood ends every voice instead of losing note-offs", "[safety]")
{
    auto inst = testInstrument();
    KeysEngine engine;
    engine.prepare(48000, 512);
    engine.setParams(KeysParams{});
    engine.setInstrument(inst);

    // More events in one block than the engine will act on: the surplus is
    // dropped, so the engine must panic rather than keep the note-ons.
    std::vector<MidiEvent> events;
    for (int i = 0; i < kMaxBlockEvents; ++i)
        events.push_back(noteOn(0, uint8_t(24 + (i % 84)), 127));
    for (int i = 0; i < kMaxBlockEvents; ++i)
        events.push_back(noteOff(0, uint8_t(24 + (i % 84))));

    const auto m = render(engine, events, 8.0);
    REQUIRE(m.allFinite);
    REQUIRE(m.peak <= kSafetyCeiling + 1.0e-4f);
    REQUIRE(m.voicesAtEnd == 0);
    REQUIRE(m.tailRms < 1.0e-4f);
}

TEST_CASE("the note-off guard does not swallow ordinary note-offs", "[safety]")
{
    auto inst = testInstrument();
    KeysEngine engine;
    engine.prepare(48000, 512);
    KeysParams p;
    p.roomLevel = 0.0f;
    p.resonance = 0.0f;
    engine.setParams(p);
    engine.setInstrument(inst);

    // A short staccato chord: 40 ms, well clear of the 8 ms guard.
    std::vector<MidiEvent> events;
    for (uint8_t note : {uint8_t(48), uint8_t(55), uint8_t(64)}) {
        events.push_back(noteOn(0, note, 100));
        events.push_back(noteOff(1920, note));
    }
    const auto m = render(engine, events, 12.0);
    REQUIRE(m.burstRms > 0.0f);        // it sounded
    REQUIRE(m.voicesAtEnd == 0);       // and it stopped
    REQUIRE(m.tailRms < 1.0e-4f);
}
