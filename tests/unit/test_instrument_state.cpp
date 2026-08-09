// Instrument-state regressions for sapptune#21: the idle MIDI blast played
// "an altogether default sound", not the loaded SFZ. Two contracts close that
// class of fault:
//
//   1. StartupGate — note-ons are suppressed until the instrument knows what
//      it is: a host state restore has installed its SFZ, or the fresh-insert
//      grace window passed with no restore. Stray MIDI arriving between
//      plugin instantiation and setStateInformation()'s async load finishing
//      must produce SILENCE, never the construction-default diagnostic patch.
//
//   2. Instrument swap under sounding notes — voices from the outgoing
//      snapshot fade out at the swap and can never retrigger, hold a region
//      of, or produce audio from the old instrument afterwards.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <sapp/sounds/DiagnosticInstrument.h>

#include "core/KeysEngine.h"
#include "core/StartupGate.h"

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

// Dry-only params: no room/resonance tails, so post-swap silence is testable.
KeysParams dryParams()
{
    KeysParams p;
    p.roomLevel = 0.0f;
    p.resonance = 0.0f;
    p.vintage = 0.0f;
    return p;
}

struct Measured {
    float peak = 0.0f;
    float rms = 0.0f;
    bool allFinite = true;
};

// Renders `totalFrames`, feeding each event in the block its (absolute) frame
// lands in. Returns peak/rms over the whole render.
Measured render(KeysEngine& engine, std::vector<MidiEvent> events, int totalFrames,
                int block = 512)
{
    Measured m;
    std::stable_sort(events.begin(), events.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });
    std::vector<float> l(size_t(block), 0.0f), r(size_t(block), 0.0f);
    std::vector<MidiEvent> blockEvents;
    blockEvents.reserve(events.size() + 1);
    size_t next = 0;
    double sumSq = 0.0;
    for (int start = 0; start < totalFrames; start += block) {
        const int frames = std::min(block, totalFrames - start);
        blockEvents.clear();
        while (next < events.size() && events[next].frame < uint32_t(start + frames)) {
            MidiEvent e = events[next++];
            e.frame -= uint32_t(start);
            blockEvents.push_back(e);
        }
        engine.process(blockEvents.data(), int(blockEvents.size()), l.data(), r.data(), frames);
        for (int f = 0; f < frames; ++f) {
            for (float v : {l[size_t(f)], r[size_t(f)]}) {
                if (!std::isfinite(v)) m.allFinite = false;
                m.peak = std::max(m.peak, std::abs(v));
                sumSq += double(v) * v;
            }
        }
    }
    if (totalFrames > 0) m.rms = float(std::sqrt(sumSq / double(totalFrames * 2)));
    return m;
}

// A full-range instrument whose only sample is digital silence. Any audio
// after swapping to it proves a voice still referenced the OLD instrument.
sapp::sounds::InstrumentPtr makeSilentInstrument()
{
    auto inst = std::make_shared<sapp::sounds::LoadedInstrument>();
    inst->definition.name = "Silent";

    sapp::sounds::SampleData s;
    s.sampleRate = 48000;
    s.channels = 2;
    s.frames = 4800;
    s.data.assign(2, std::vector<float>(4800, 0.0f));
    inst->samples.push_back(std::move(s));

    sapp::sounds::RegionDefinition region;
    region.sample = 0;
    region.loKey = 0;
    region.hiKey = 127;
    region.rootKey = 60;
    inst->definition.regions.push_back(region);
    inst->definition.loKeyUsed = 0;
    inst->definition.hiKeyUsed = 127;
    return inst;
}

} // namespace

// --------------------------------------------------------- StartupGate policy

TEST_CASE("startup gate: fresh instance arms only after the grace window")
{
    StartupGate gate;
    REQUIRE_FALSE(gate.armed());

    gate.tick(StartupGate::kGraceMs - 1.0);
    REQUIRE_FALSE(gate.armed());

    gate.tick(StartupGate::kGraceMs);
    REQUIRE(gate.armed());
}

TEST_CASE("startup gate: construction-default install never arms")
{
    StartupGate gate;
    gate.loadCompleted(true);   // the constructor's diagnostic load
    REQUIRE_FALSE(gate.armed());
    gate.tick(0.0);
    REQUIRE_FALSE(gate.armed());
}

TEST_CASE("startup gate: state restore holds fire until the restored load completes")
{
    StartupGate gate;
    gate.beginStateRestore();

    // The grace path is disabled once a restore is seen — no amount of time
    // arms the gate while the restored instrument is still loading.
    gate.tick(StartupGate::kGraceMs * 10.0);
    REQUIRE_FALSE(gate.armed());

    // A construction-default install completing late still never arms.
    gate.loadCompleted(true);
    REQUIRE_FALSE(gate.armed());

    // The restore's own install (SFZ or declared fallback) arms.
    gate.loadCompleted(false);
    REQUIRE(gate.armed());
}

TEST_CASE("startup gate: a late state restore disarms a grace-armed instance")
{
    StartupGate gate;
    gate.tick(StartupGate::kGraceMs);
    REQUIRE(gate.armed());

    // Slow session load: state arrives after the grace window. The instrument
    // is about to become something else — hold fire again.
    gate.beginStateRestore();
    REQUIRE_FALSE(gate.armed());

    gate.loadCompleted(false);
    REQUIRE(gate.armed());
}

// ---------------------------------------- mid-session loads (sappkeys #2)

TEST_CASE("gate: a mid-session load disarms until its instrument is installed")
{
    StartupGate gate;
    gate.loadCompleted(false);   // real instrument playing
    REQUIRE(gate.armed());

    // Program change / preset / SFZ pick begins its own async load: notes
    // arriving in this window must NOT sound the outgoing instrument.
    gate.beginLoad();
    REQUIRE_FALSE(gate.armed());

    // No amount of time re-arms while the load is in flight — the grace path
    // must not hand the window back to the old instrument.
    gate.tick(StartupGate::kGraceMs * 10.0);
    REQUIRE_FALSE(gate.armed());

    gate.loadCompleted(false);
    REQUIRE(gate.armed());
}

TEST_CASE("gate: superseding loads keep the window closed until the last lands")
{
    StartupGate gate;
    gate.loadCompleted(false);
    gate.beginLoad();            // pick A
    gate.beginLoad();            // pick B supersedes A before it finishes
    REQUIRE_FALSE(gate.armed());
    // Only the newest load reports back (the processor's generation guard).
    gate.loadCompleted(false);
    REQUIRE(gate.armed());
}

TEST_CASE("gate: a failed mid-session load re-arms onto the installed real instrument")
{
    StartupGate gate;
    gate.loadCompleted(false);   // a real instrument is installed
    gate.beginLoad();            // corrupt SFZ pick
    gate.loadFailed();
    // The previous, user-chosen instrument is still installed — a bad pick
    // must not brick the session.
    REQUIRE(gate.armed());
}

TEST_CASE("gate: a failed restore load over the construction diagnostic stays silent")
{
    StartupGate gate;
    gate.loadCompleted(true);    // only the construction default exists
    gate.beginStateRestore();
    gate.beginLoad();
    gate.loadFailed();
    REQUIRE_FALSE(gate.armed());
    // And the grace path stays disabled after a restore was seen.
    gate.tick(StartupGate::kGraceMs * 10.0);
    REQUIRE_FALSE(gate.armed());
}

TEST_CASE("gate: an SFZ pick inside the grace window blocks grace arming")
{
    StartupGate gate;
    gate.loadCompleted(true);    // construction default installed
    gate.beginLoad();            // user picks an SFZ at t < kGraceMs
    gate.tick(StartupGate::kGraceMs * 2.0);
    // The diagnostic must not become playable under the pick's load window.
    REQUIRE_FALSE(gate.armed());
    gate.loadCompleted(false);
    REQUIRE(gate.armed());
}

// ------------------------------------------- pre-state note-ons stay silent

TEST_CASE("note-ons before state restore produce silence, not the default sound")
{
    KeysEngine engine;
    engine.prepare(48000.0, 512);
    engine.setParams(dryParams());
    // The construction default is installed and would sound if asked.
    engine.setInstrument(sapp::sounds::makeDiagnosticInstrument());

    StartupGate gate;   // unarmed: no restore completed, grace not passed

    // The processor's gate contract: unarmed → note-ons are dropped before
    // they reach the engine (everything else passes).
    const auto gateEvents = [&gate](std::vector<MidiEvent> events) {
        std::vector<MidiEvent> passed;
        for (const auto& e : events)
            if (gate.armed() || e.type != MidiEvent::Type::NoteOn)
                passed.push_back(e);
        return passed;
    };

    // A stray idle burst: note-ons with no transport running.
    std::vector<MidiEvent> burst;
    for (int i = 0; i < 16; ++i)
        burst.push_back(noteOn(uint32_t(i * 400), uint8_t(48 + i), 112));

    const auto silent = render(engine, gateEvents(burst), 48000 / 2);
    REQUIRE(silent.allFinite);
    REQUIRE(silent.peak == 0.0f);
    REQUIRE(engine.sampler().activeVoiceCount() == 0);

    // Once the restored instrument is installed the gate arms and the same
    // events sound normally.
    gate.loadCompleted(false);
    const auto sounding = render(engine, gateEvents({noteOn(0, 60, 100)}), 48000 / 2);
    REQUIRE(sounding.allFinite);
    REQUIRE(sounding.peak > 0.01f);
}

// ------------------------------- mid-session load window stays silent (#2)

TEST_CASE("notes during a mid-session program-change load never sound the outgoing instrument")
{
    KeysEngine engine;
    engine.prepare(48000.0, 512);
    engine.setParams(dryParams());

    // A real session in flight: instrument installed, gate armed, playable.
    engine.setInstrument(sapp::sounds::makeDiagnosticInstrument());
    StartupGate gate;
    gate.loadCompleted(false);
    REQUIRE(gate.armed());

    const auto gateEvents = [&gate](std::vector<MidiEvent> events) {
        std::vector<MidiEvent> passed;
        for (const auto& e : events)
            if (gate.armed() || e.type != MidiEvent::Type::NoteOn)
                passed.push_back(e);
        return passed;
    };

    // A program change / preset pick begins its async load. Notes arriving in
    // the seconds the load takes used to play the OUTGOING instrument; the
    // documented behavior is now suppression — silence — until the new
    // instrument is installed.
    gate.beginLoad();
    std::vector<MidiEvent> burst;
    for (int i = 0; i < 12; ++i)
        burst.push_back(noteOn(uint32_t(i * 500), uint8_t(50 + i), 110));

    const auto duringLoad = render(engine, gateEvents(burst), 48000 / 2);
    REQUIRE(duringLoad.allFinite);
    REQUIRE(duringLoad.peak == 0.0f);                       // NOT the old sound
    REQUIRE(engine.sampler().activeVoiceCount() == 0);

    // The new instrument lands (here: the silent one, so any audio would be
    // the old instrument leaking through). The gate re-arms; the same notes
    // now trigger — and bind to — the NEW instrument only.
    engine.setInstrument(makeSilentInstrument());
    gate.loadCompleted(false);
    REQUIRE(gate.armed());

    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    auto passed = gateEvents({noteOn(0, 60, 120)});
    REQUIRE(passed.size() == 1);
    engine.process(passed.data(), int(passed.size()), l.data(), r.data(), 512);
    REQUIRE(engine.sampler().activeVoiceCount() > 0);       // note did trigger

    const auto afterInstall = render(engine, {}, 48000 / 4);
    REQUIRE(afterInstall.allFinite);
    REQUIRE(afterInstall.peak < 1.0e-4f);                   // ...silently
}

// -------------------------------------------------- instrument swap safety

TEST_CASE("instrument swap under sounding notes: old voices fade and never return")
{
    KeysEngine engine;
    engine.prepare(48000.0, 512);
    engine.setParams(dryParams());

    engine.setInstrument(sapp::sounds::makeDiagnosticInstrument());

    // Sustained note sounding on the old (diagnostic) instrument.
    const auto before = render(engine, {noteOn(0, 60, 110)}, 48000 / 2);
    REQUIRE(before.allFinite);
    REQUIRE(before.peak > 0.01f);
    REQUIRE(engine.sampler().activeVoiceCount() > 0);

    // Swap to a silent instrument MID-NOTE, no note-off ever sent.
    engine.setInstrument(makeSilentInstrument());

    // Adoption + steal fade happen inside the first blocks; after 100 ms the
    // old voice must be gone entirely — a sustained looped diagnostic voice
    // surviving the swap would still be sounding here.
    const auto fadeWindow = render(engine, {}, 4800);
    REQUIRE(fadeWindow.allFinite);

    const auto after = render(engine, {}, 48000 / 4);
    REQUIRE(after.allFinite);
    REQUIRE(after.peak < 1.0e-4f);
    REQUIRE(engine.sampler().activeVoiceCount() == 0);

    // Retiring the old snapshot after the fade window has rendered (the
    // processor defers collectRetired() exactly this way) must leave the
    // engine sound and finite.
    engine.collectRetired();

    // Retriggering the same held key now binds to the NEW instrument only:
    // the note starts a voice, and that voice is silent.
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    MidiEvent on = noteOn(0, 60, 120);
    engine.process(&on, 1, l.data(), r.data(), 512);
    REQUIRE(engine.sampler().activeVoiceCount() > 0);   // the note did trigger

    const auto retriggered = render(engine, {}, 48000 / 4);
    REQUIRE(retriggered.allFinite);
    REQUIRE(retriggered.peak < 1.0e-4f);   // ...from the silent instrument

    // And back: a swap to a sounding instrument retriggers audibly (the
    // engine is alive; the silence above was the new instrument, not a wedge).
    engine.setInstrument(sapp::sounds::makeDiagnosticInstrument());
    render(engine, {noteOff(0, 60)}, 2400);   // adopt + clear the held key
    engine.collectRetired();
    const auto back = render(engine, {noteOn(0, 64, 110)}, 48000 / 4);
    REQUIRE(back.allFinite);
    REQUIRE(back.peak > 0.01f);
}

TEST_CASE("rapid successive swaps while notes sound stay finite and end silent")
{
    KeysEngine engine;
    engine.prepare(48000.0, 512);
    engine.setParams(dryParams());

    engine.setInstrument(sapp::sounds::makeDiagnosticInstrument());
    auto m = render(engine, {noteOn(0, 55, 100), noteOn(64, 62, 100), noteOn(128, 67, 100)},
                    48000 / 4);
    REQUIRE(m.allFinite);
    REQUIRE(m.peak > 0.01f);

    // Swap repeatedly mid-note, rendering a little between swaps — the worst
    // realistic churn a preset-hopping user produces. Retire only after audio
    // has rendered past each swap, as the processor does.
    for (int i = 0; i < 4; ++i) {
        engine.setInstrument(i % 2 == 0 ? makeSilentInstrument()
                                        : sapp::sounds::makeDiagnosticInstrument());
        m = render(engine, {noteOn(0, uint8_t(60 + i), 96)}, 4800);
        REQUIRE(m.allFinite);
        engine.collectRetired();
    }

    // End everything; output must decay to silence (no stuck or orphaned voice).
    render(engine, {{[] { MidiEvent e; e.type = MidiEvent::Type::AllSoundOff; return e; }()}},
           2400);
    const auto tail = render(engine, {}, 48000 / 2);
    REQUIRE(tail.allFinite);
    REQUIRE(tail.peak < 1.0e-4f);
    REQUIRE(engine.sampler().activeVoiceCount() == 0);
}
