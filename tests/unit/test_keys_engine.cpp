#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <sapp/sounds/DiagnosticInstrument.h>

#include "core/KeysEngine.h"
#include "core/KeysInstrument.h"

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

MidiEvent cc(uint32_t frame, uint8_t num, uint8_t value)
{
    MidiEvent e;
    e.type = MidiEvent::Type::Controller;
    e.frame = frame;
    e.note = num;
    e.value = value;
    return e;
}

struct Rendered {
    std::vector<float> left, right;
    float peak = 0.0f;
    float rms = 0.0f;
};

Rendered run(KeysEngine& engine, std::vector<MidiEvent> events, int totalFrames,
             int block = 512)
{
    Rendered out;
    out.left.assign(size_t(totalFrames), 0.0f);
    out.right.assign(size_t(totalFrames), 0.0f);
    size_t next = 0;
    double sumSq = 0.0;
    for (int start = 0; start < totalFrames; start += block) {
        const int frames = std::min(block, totalFrames - start);
        std::vector<MidiEvent> blockEvents;
        while (next < events.size() && events[next].frame < uint32_t(start + frames)) {
            MidiEvent e = events[next];
            e.frame = e.frame >= uint32_t(start) ? e.frame - uint32_t(start) : 0;
            blockEvents.push_back(e);
            ++next;
        }
        engine.process(blockEvents.data(), int(blockEvents.size()),
                       out.left.data() + start, out.right.data() + start, frames);
    }
    for (size_t i = 0; i < out.left.size(); ++i) {
        out.peak = std::max({out.peak, std::abs(out.left[i]), std::abs(out.right[i])});
        sumSq += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
    }
    out.rms = float(std::sqrt(sumSq / double(out.left.size() * 2)));
    return out;
}

float rmsRange(const std::vector<float>& x, size_t a, size_t b)
{
    double sum = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) sum += double(x[i]) * x[i];
    return float(std::sqrt(sum / double(b - a)));
}

// A tiny in-memory instrument with one attack region and one release region,
// for testing the mechanical-noise policy without disk samples.
sapp::sounds::InstrumentPtr makeReleaseTestInstrument()
{
    using namespace sapp::sounds;
    auto inst = std::make_shared<LoadedInstrument>();

    auto makeSample = [](float freq, float seconds) {
        SampleData s;
        s.sampleRate = 48000;
        s.channels = 1;
        s.frames = uint64_t(seconds * 48000);
        s.data.assign(1, std::vector<float>(size_t(s.frames), 0.0f));
        for (uint64_t i = 0; i < s.frames; ++i) {
            const float env = std::exp(-3.0f * float(i) / float(s.frames));
            s.data[0][size_t(i)] =
                0.5f * env * std::sin(2.0f * 3.14159265f * freq * float(i) / 48000.0f);
        }
        s.peak = 0.5f;
        return s;
    };
    inst->samples.push_back(makeSample(261.6f, 1.0f));   // attack tone
    inst->samples.push_back(makeSample(1200.0f, 0.3f));  // "thump" on release

    RegionDefinition attack;
    attack.sample = 0;
    attack.samplePath = "attack";
    attack.loKey = 0; attack.hiKey = 127; attack.rootKey = 60;
    inst->definition.regions.push_back(attack);

    RegionDefinition release;
    release.sample = 1;
    release.samplePath = "release";
    release.loKey = 0; release.hiKey = 127; release.rootKey = 60;
    release.trigger = TriggerMode::Release;
    inst->definition.regions.push_back(release);

    inst->definition.name = "ReleaseTest";
    inst->definition.loKeyUsed = 0;
    inst->definition.hiKeyUsed = 127;
    applyKeysPolicy(inst->definition);
    return inst;
}

} // namespace

TEST_CASE("touch curve reshapes velocity response", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto renderMezzo = [&](float touch) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.touch = touch;
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(100, 60, 64), noteOff(40000, 60)};
        return run(engine, events, 60000);
    };

    const float heavy = renderMezzo(0.0f).rms;
    const float neutral = renderMezzo(0.5f).rms;
    const float light = renderMezzo(1.0f).rms;
    // A mezzo note gets quieter on a heavy action and louder on a light one.
    REQUIRE(heavy < neutral * 0.8f);
    REQUIRE(light > neutral * 1.1f);
}

TEST_CASE("velocity history feed reports the shaped velocities", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});
    KeysEngine engine;
    engine.prepare(48000, 512);
    KeysParams p;
    p.touch = 0.0f;  // heavy: mids drop
    engine.setParams(p);
    engine.setInstrument(inst);

    std::vector<MidiEvent> events{noteOn(0, 60, 64)};
    run(engine, events, 1024);

    KeysEngine::VelSample history[KeysEngine::kVelHistory];
    const int count = engine.velocityHistory(history);
    REQUIRE(count == 1);
    REQUIRE(history[0].in == 64);
    REQUIRE(int(history[0].out) < 30);  // 64^3 curve lands well below half
    REQUIRE(int(history[0].out) == int(shapeVelocity(64.0f, 0.0f, 0.0f) + 0.5f));
}

TEST_CASE("una corda softens and darkens", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto render = [&](float unaCorda) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.unaCorda = unaCorda;
        p.roomLevel = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(100, 72, 110), noteOff(40000, 72)};
        return run(engine, events, 60000);
    };

    const float open = render(0.0f).rms;
    const float soft = render(1.0f).rms;
    REQUIRE(soft < open * 0.75f);
    REQUIRE(soft > 0.0f);
}

TEST_CASE("lid closing darkens the sound", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto highFreqEnergy = [&](float lid) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.lid = lid;
        p.roomLevel = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(100, 84, 120), noteOff(40000, 84)};
        auto out = run(engine, events, 48000);
        // Crude HF proxy: energy of the first difference.
        double sum = 0.0;
        for (size_t i = 1; i < out.left.size(); ++i) {
            const double d = double(out.left[i]) - double(out.left[i - 1]);
            sum += d * d;
        }
        return sum;
    };

    const double open = highFreqEnergy(1.0f);
    const double closed = highFreqEnergy(0.0f);
    REQUIRE(closed < open * 0.75);
}

TEST_CASE("sympathetic resonance adds energy while the pedal is down", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto render = [&](float resonance) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        engine.reseed(42);
        KeysParams p;
        p.resonance = resonance;
        p.roomLevel = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{
            cc(0, 64, 127),            // pedal down
            noteOn(100, 48, 110), noteOn(120, 60, 110), noteOn(140, 64, 110),
            noteOff(24000, 48), noteOff(24000, 60), noteOff(24000, 64),
        };
        return run(engine, events, 96000);
    };

    const float dry = render(0.0f).rms;
    const float resonant = render(1.0f).rms;
    REQUIRE(resonant > dry * 1.01f);
}

TEST_CASE("resonance layer dies after pedal release", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});
    KeysEngine engine;
    engine.prepare(48000, 512);
    KeysParams p;
    p.resonance = 1.0f;
    p.roomLevel = 0.0f;
    engine.setParams(p);
    engine.setInstrument(inst);

    std::vector<MidiEvent> events{
        cc(0, 64, 127),
        noteOn(100, 60, 120),
        noteOff(12000, 60),
        cc(24000, 64, 0),          // pedal up: notes + resonance released
    };
    auto out = run(engine, events, 96000);
    const float whileDown = rmsRange(out.left, 6000, 12000);
    const float longAfter = rmsRange(out.left, 80000, 96000);
    REQUIRE(whileDown > 0.001f);
    REQUIRE(longAfter < whileDown * 0.05f);
}

TEST_CASE("mech-noise policy tags release regions and the knob scales them", "[engine]")
{
    auto inst = makeReleaseTestInstrument();

    // Policy: the release region carries the internal gain CC, attack doesn't.
    REQUIRE(inst->definition.regions[1].gainCc.size() == 1);
    REQUIRE(int(inst->definition.regions[1].gainCc[0].cc) == kMechNoiseInternalCc);
    REQUIRE(inst->definition.regions[0].gainCc.empty());
    // Idempotent.
    auto def = inst->definition;
    REQUIRE(applyKeysPolicy(def) == 1);
    REQUIRE(def.regions[1].gainCc.size() == 1);

    auto render = [&](float mech) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.mechNoise = mech;
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(0, 60, 100), noteOff(24000, 60)};
        return run(engine, events, 48000);
    };

    // The release thump sounds after the note-off at 24000.
    const float full = rmsRange(render(1.0f).left, 24500, 34000);
    const float off = rmsRange(render(0.0f).left, 24500, 34000);
    REQUIRE(full > 0.0005f);
    REQUIRE(off < full * 0.05f);
}

// --------------------------------------------------------------- clean (#3) --
// `clean` (SappLink CC 3) is the suite-wide imperfection master: every modeled
// imperfection scaled by (1 − clean). The contract is (a) clean 0 changes
// nothing, ever, and (b) clean 1 leaves no modeled noise, wear or jitter.

TEST_CASE("Mechanics defaults low enough for unattended playback", "[engine][clean]")
{
    // sappkeys #3: the default used to be 1.0 — full-scale hammer/key/pedal
    // noise out of the box, which stacked into audible grain on the station.
    REQUIRE(KeysParams{}.mechNoise == kMechNoiseDefault);
    REQUIRE(kMechNoiseDefault == 0.18f);
    REQUIRE(kMechNoiseDefault < 0.25f);   // "nearer 0.1–0.2 than 1.0"
    REQUIRE(kMechNoiseDefault > 0.0f);    // character stays on
    REQUIRE(KeysParams{}.clean == 0.0f);  // and nothing is scaled by default
}

TEST_CASE("clean 0 leaves every parameter untouched", "[engine][clean]")
{
    KeysParams p;
    p.mechNoise = 0.73f;
    p.vintage = 0.41f;
    p.drive = 0.6f;
    p.resonance = 0.9f;
    const KeysParams scaled = applyClean(p);
    // Exact equality, not approximate: a multiply by 1.0f is exact, so the DSP
    // sees bit-for-bit what it saw before `clean` existed.
    REQUIRE(scaled.mechNoise == p.mechNoise);
    REQUIRE(scaled.vintage == p.vintage);
    REQUIRE(scaled.drive == p.drive);
    REQUIRE(scaled.resonance == p.resonance);
}

TEST_CASE("clean scales the imperfection sources proportionally", "[engine][clean]")
{
    KeysParams p;
    p.mechNoise = 0.8f;
    p.vintage = 0.5f;
    p.clean = 0.25f;
    const KeysParams scaled = applyClean(p);
    REQUIRE(std::abs(scaled.mechNoise - 0.6f) < 1e-6f);
    REQUIRE(std::abs(scaled.vintage - 0.375f) < 1e-6f);
    // Not an imperfection: the room, the resonance layer and drive are the
    // instrument, and clean must not touch them.
    REQUIRE(scaled.roomLevel == p.roomLevel);
    REQUIRE(scaled.resonance == p.resonance);
    REQUIRE(scaled.drive == p.drive);

    p.clean = 1.0f;
    const KeysParams none = applyClean(p);
    REQUIRE(none.mechNoise == 0.0f);
    REQUIRE(none.vintage == 0.0f);
    // Idempotent: the flag is consumed, so a second pass cannot compound it.
    REQUIRE(applyClean(none).mechNoise == 0.0f);
    REQUIRE(applyClean(scaled).mechNoise == scaled.mechNoise);
}

TEST_CASE("clean 1 silences the modeled mechanical noise", "[engine][clean]")
{
    auto inst = makeReleaseTestInstrument();

    auto render = [&](float mech, float clean) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.mechNoise = mech;
        p.clean = clean;
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(0, 60, 100), noteOff(24000, 60)};
        return run(engine, events, 48000);
    };

    // The release thump — the mechanical noise — sounds after the note-off.
    const float noisy = rmsRange(render(1.0f, 0.0f).left, 24500, 34000);
    const float cleaned = rmsRange(render(1.0f, 1.0f).left, 24500, 34000);
    const float mechOff = rmsRange(render(0.0f, 0.0f).left, 24500, 34000);

    INFO("release-window RMS: clean 0 " << noisy << ", clean 1 " << cleaned
         << ", mechNoise 0 " << mechOff);
    REQUIRE(noisy > 0.0005f);
    // clean 1 lands exactly where Mechanics 0 lands: the mechanical layer is
    // gone and only the note's own decay tail is left in the window.
    REQUIRE(cleaned <= mechOff * 1.0001f);
    REQUIRE(cleaned < noisy * 0.05f);   // ≥ 26 dB down (measured: ~36 dB)
    // Said as a level drop, which is how the station hears it.
    const float dropDb = 20.0f * std::log10(noisy / cleaned);
    REQUIRE(dropDb > 20.0f);
}

TEST_CASE("clean is exactly a (1 - clean) scale of the imperfection params",
          "[engine][clean]")
{
    // The strongest statement of backwards compatibility: rendering with
    // `clean` is sample-identical to rendering the pre-scaled parameters with
    // no clean at all — so clean 0 IS the pre-change behavior, exactly.
    auto inst = makeReleaseTestInstrument();

    auto render = [&](float mech, float vintage, float clean) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        engine.reseed(7);
        KeysParams p;
        p.mechNoise = mech;
        p.vintage = vintage;
        p.clean = clean;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(0, 60, 100), noteOn(500, 67, 90),
                                      noteOff(24000, 60), noteOff(26000, 67)};
        return run(engine, events, 48000);
    };

    auto identical = [](const Rendered& a, const Rendered& b) {
        REQUIRE(a.left.size() == b.left.size());
        for (size_t i = 0; i < a.left.size(); ++i) {
            if (a.left[i] != b.left[i] || a.right[i] != b.right[i]) {
                INFO("first difference at frame " << i);
                REQUIRE(a.left[i] == b.left[i]);
                REQUIRE(a.right[i] == b.right[i]);
            }
        }
    };

    identical(render(0.8f, 0.5f, 0.25f), render(0.6f, 0.375f, 0.0f));
    identical(render(0.8f, 0.5f, 1.0f), render(0.0f, 0.0f, 0.0f));
    // And clean 0 is a no-op on top of any parameter set.
    identical(render(0.8f, 0.5f, 0.0f), render(0.8f, 0.5f, 0.0f));
}

TEST_CASE("clean 1 removes the vintage wear and jitter too", "[engine][clean]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto highFreqEnergy = [&](float vintage, float clean) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        engine.reseed(11);
        KeysParams p;
        p.vintage = vintage;
        p.clean = clean;
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(100, 84, 120), noteOff(40000, 84)};
        auto out = run(engine, events, 48000);
        double sum = 0.0;
        for (size_t i = 1; i < out.left.size(); ++i) {
            const double d = double(out.left[i]) - double(out.left[i - 1]);
            sum += d * d;
        }
        return sum;
    };

    const double aged = highFreqEnergy(1.0f, 0.0f);
    const double cleaned = highFreqEnergy(1.0f, 1.0f);
    const double never = highFreqEnergy(0.0f, 0.0f);
    // Vintage colours the signal (detune jitter, wow/flutter, HF wear)…
    REQUIRE(std::abs(aged - never) > never * 1.0e-3);
    // …and clean 1 puts it back exactly where a vintage-0 render leaves it.
    REQUIRE(cleaned == never);
}

TEST_CASE("drive saturates loud material and is transparent at zero", "[engine]")
{
    auto inst = sapp::sounds::makeDiagnosticInstrument({48000, 1.0f, 0.4f, 5});

    auto render = [&](float drive) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.drive = drive;
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        p.limiter = false;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events{noteOn(100, 60, 127), noteOn(100, 64, 127),
                                      noteOn(100, 67, 127)};
        return run(engine, events, 48000);
    };

    const auto clean = render(0.0f);
    const auto driven = render(1.0f);
    // Drive compresses the crest factor: peak/rms drops.
    REQUIRE(driven.peak / driven.rms < clean.peak / clean.rms);
}

TEST_CASE("external CC 102 is dropped (reserved for mech policy)", "[engine]")
{
    auto inst = makeReleaseTestInstrument();

    auto render = [&](bool sendExternal) {
        KeysEngine engine;
        engine.prepare(48000, 512);
        KeysParams p;
        p.mechNoise = 1.0f;   // engine wants CC102 = 0 (full level)
        p.roomLevel = 0.0f;
        p.resonance = 0.0f;
        engine.setParams(p);
        engine.setInstrument(inst);
        std::vector<MidiEvent> events;
        if (sendExternal)
            events.push_back(cc(0, uint8_t(kMechNoiseInternalCc), 127));  // "mute releases"
        events.push_back(noteOn(200, 60, 100));
        events.push_back(noteOff(24000, 60));
        return run(engine, events, 48000);
    };

    const float withAttack = rmsRange(render(true).left, 24500, 34000);
    const float withoutAttack = rmsRange(render(false).left, 24500, 34000);
    // The hostile external CC must not silence the release samples.
    REQUIRE(withAttack > withoutAttack * 0.9f);
}
