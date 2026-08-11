// sappkeys-headless — the station harness.
//
// Drives SappKeysProcessor the way the sappradio station host does: no editor
// is ever created, the host writes a program / preset and then SETTLES on the
// `libraryReady` parameter before it renders a note.
//
// Unlike its siblings, SappKeys still installs instruments through
// MessageManager::callAsync() and a juce::Timer, so this harness DOES pump the
// dispatch loop — that is the real station condition (the flag was measured
// going true at 1.55 s, which is the plugin's own 30 Hz timer arming the
// fresh-insert grace window, so the loop was demonstrably running). What the
// station does NOT do is keep waiting once the flag says ready, which is the
// whole of sappkeys #4.
//
//   sappkeys-headless selftest [--fixture DIR]
//       Regression suite for sappkeys #4 (readiness must not lie). Exit 0 =
//       all pass.
//
//   sappkeys-headless render [--program N] [--out F.wav] [--root DIR]
//                            [--settle MS] [--param ID=VALUE]
//       One station-style render: select a program, settle on libraryReady,
//       then render two hands from 0.00 s.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "SoundsPanel.h"

namespace {

void setEnv(const char* name, const juce::String& value)
{
#if JUCE_WINDOWS
    _putenv_s(name, value.toRawUTF8());
#else
    if (value.isEmpty()) ::unsetenv(name);
    else ::setenv(name, value.toRawUTF8(), 1);
#endif
}

double toDb(double linear)
{
    return linear > 1.0e-12 ? 20.0 * std::log10(linear) : -200.0;
}

constexpr int kBlock = 512;
constexpr double kSampleRate = 48000.0;

// How the host asked for the sound. Every one of these starts an instrument
// load, so every one of them owns a readiness window.
enum class Select { None, HostProgram, PresetParameter, MidiProgramChange };

struct Settle {
    double seconds = 0.0;       // when libraryReady first read 1
    bool ready = false;         // did it ever?
    bool lied = false;          // ready while the load was still in flight
    juce::String pathAtReady;   // what was installed at that instant
};

struct RenderResult {
    std::vector<float> left, right;
    Settle settle;
    juce::String instrumentPath, instrumentName, status;
    double rms = 0.0, peak = 0.0;
    double headPeak = 0.0;      // peak over the first kHeadMs of the render
    double firstSoundSeconds = -1.0;
    bool readyAfter = false;
};

// The station's shape for a piano chain: both hands from 0.00 s. The measured
// failure carried 934 note events starting at 0.00 s, so the very first block
// must sound.
struct Note { int block; int note; float velocity; };

std::vector<Note> stationScore(int blocks)
{
    // ~8 notes/second across two hands, first note at block 0.
    std::vector<Note> score;
    static const int rh[] = {72, 76, 79, 77, 74, 76, 81, 79};
    static const int lh[] = {36, 43, 40, 45, 38, 45, 41, 43};
    for (int i = 0, b = 0; b < blocks; ++i, b += 11) {   // 11 blocks ~ 117 ms
        score.push_back({b, rh[i % 8], 0.80f});
        score.push_back({b, lh[i % 8], 0.70f});
    }
    return score;
}

// The head of the render: if the host trusted the flag and the library was not
// actually in, THIS is the window that comes out as digital silence.
constexpr double kHeadMs = 120.0;

struct RenderOptions {
    Select select = Select::None;
    int program = 0;
    int settleMs = 20000;
    juce::StringPairArray params;
    // Pump the loop briefly once the flag reads ready, exactly as a host does
    // between "the plugin says it is ready" and its first block of audio.
    bool pumpAfterReady = true;
};

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Select the sound the way the host asked for it. Returns the moment the
// request was made (for the "did the flag drop synchronously?" checks).
void applySelection(sappkeys::SappKeysProcessor& processor, const RenderOptions& options)
{
    switch (options.select) {
    case Select::None:
        break;
    case Select::HostProgram:
        processor.setCurrentProgram(options.program);
        break;
    case Select::PresetParameter: {
        auto* parameter = processor.valueTree().getParameter("preset");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(float(options.program)));
        break;
    }
    case Select::MidiProgramChange: {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, options.program), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        break;
    }
    }
}

// The station's settle: poll libraryReady, pumping the loop, up to a cap.
Settle settleOnFlag(sappkeys::SappKeysProcessor& processor, int capMs)
{
    Settle out;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - start < double(capMs)) {
        if (processor.libraryReady()) {
            out.ready = true;
            break;
        }
        pump(5);
    }
    out.seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    out.pathAtReady = processor.currentInstrumentPath();
    // The flag's whole contract: ready means the library the host asked for is
    // installed, not that a load for it is somewhere in flight.
    out.lied = out.ready && processor.isLoading();
    return out;
}

RenderResult stationRender(const RenderOptions& options, double seconds = 4.0)
{
    RenderResult out;
    auto processor = std::make_unique<sappkeys::SappKeysProcessor>();
    processor->prepareToPlay(kSampleRate, kBlock);

    applySelection(*processor, options);
    for (const auto& id : options.params.getAllKeys())
        if (auto* parameter = processor->valueTree().getParameter(id))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(options.params[id].getFloatValue()));

    out.settle = settleOnFlag(*processor, options.settleMs);

    // A host does not go from "ready" to its first block instantaneously.
    if (options.pumpAfterReady) pump(20);

    const int blocks = int(seconds * kSampleRate / kBlock);
    const auto score = stationScore(blocks - 40);
    juce::AudioBuffer<float> buffer(2, kBlock);
    const int headBlocks = int(kHeadMs * 0.001 * kSampleRate / kBlock);

    for (int b = 0; b < blocks; ++b) {
        juce::MidiBuffer midi;
        for (const auto& n : score)
            if (n.block == b) {
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, n.velocity), 0);
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), 300);
            }
        buffer.clear();
        processor->processBlock(buffer, midi);

        double blockPeak = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i)
                blockPeak = std::max(blockPeak, double(std::abs(buffer.getReadPointer(c)[i])));
        if (b < headBlocks) out.headPeak = std::max(out.headPeak, blockPeak);
        if (out.firstSoundSeconds < 0.0 && blockPeak > 1.0e-6)
            out.firstSoundSeconds = double(b) * kBlock / kSampleRate;

        out.left.insert(out.left.end(), buffer.getReadPointer(0),
                        buffer.getReadPointer(0) + kBlock);
        out.right.insert(out.right.end(), buffer.getReadPointer(1),
                         buffer.getReadPointer(1) + kBlock);

        // A JUCE host keeps its message thread alive while audio runs; this is
        // what lets a load that was still in flight finish mid-render (and is
        // exactly how the station heard the piano arrive 40-60 s in).
        if (b % 8 == 0) pump(1);
    }

    double sum = 0.0;
    for (size_t i = 0; i < out.left.size(); ++i) {
        sum += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
        out.peak = std::max(out.peak, double(std::abs(out.left[i])));
        out.peak = std::max(out.peak, double(std::abs(out.right[i])));
    }
    out.rms = std::sqrt(sum / double(out.left.size() * 2));
    out.instrumentPath = processor->currentInstrumentPath();
    out.instrumentName = processor->currentInstrumentName();
    out.status = processor->loadStatus();
    out.readyAfter = processor->libraryReady();
    processor.reset();
    return out;
}

void report(const char* label, const RenderResult& r)
{
    std::printf("        %-12s ready@%.2fs head %7.2f dBFS  first sound %6.2fs  "
                "rms %7.2f dBFS  \"%s\"\n",
                label, r.settle.seconds, toDb(r.headPeak),
                r.firstSoundSeconds < 0.0 ? 999.0 : r.firstSoundSeconds,
                toDb(r.rms),
                r.instrumentPath.isEmpty() ? "(diagnostic)"
                                           : juce::File(r.instrumentPath).getFileName().toRawUTF8());
    std::fflush(stdout);
}

// --------------------------------------------------------------- selftest --

int fails = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush(stdout);
    if (!ok) ++fails;
}

// Fixture programs (FactoryPresets.cpp order): 1 "Intimate Grand" is the
// program wanderer-piano selected, and resolves to the salamander fixture;
// 3 "EP Mark I" resolves to the deliberately quieter fm-piano1 fixture.
constexpr int kGrandProgram = 1;
constexpr int kEpProgram = 3;

juce::File fixtureSfz(const juce::File& root, const char* dir, const char* file)
{
    return root.getChildFile(dir).getChildFile(file);
}

int runSelftest(const juce::String& fixtureRoot)
{
    const juce::File root(fixtureRoot);
    if (!root.getChildFile("salamander").isDirectory()) {
        std::printf("FAIL: %s is not a fixture samples root\n", fixtureRoot.toRawUTF8());
        return 1;
    }
    setEnv(sappkeys::kSamplesRootEnvVar, root.getFullPathName());
    const auto grandSfz = fixtureSfz(root, "salamander", "SalamanderGrandPiano-V3.sfz");
    const auto epSfz = fixtureSfz(root, "fm-piano1", "FM-Piano1.sfz");

    std::printf("sappkeys #4 — libraryReady must never report a library that is not in\n");

    // ---- 1. a reprogrammed instance, settled the way the station settles ---
    for (const auto select : {Select::HostProgram, Select::PresetParameter,
                              Select::MidiProgramChange}) {
        const char* label = select == Select::HostProgram      ? "program API"
                            : select == Select::PresetParameter ? "preset param"
                                                                : "MIDI PC";
        RenderOptions options;
        options.select = select;
        options.program = kGrandProgram;
        const auto r = stationRender(options);
        report(label, r);
        check(r.settle.ready, juce::String(label) + ": libraryReady eventually reads 1");
        check(r.settle.pathAtReady == grandSfz.getFullPathName(),
              juce::String(label) + ": the selected library is installed at the instant "
              "libraryReady first reads 1 (got \""
              + (r.settle.pathAtReady.isEmpty() ? juce::String("(diagnostic)")
                                                : juce::File(r.settle.pathAtReady).getFileName())
              + "\")");
        check(!r.settle.lied,
              juce::String(label) + ": no load is still in flight when the flag reads 1");
        check(r.headPeak > 1.0e-5,
              juce::String(label) + ": a render started when the flag reads 1 is NOT silent "
              "at the head (" + juce::String(toDb(r.headPeak), 1) + " dBFS over the first "
              + juce::String(int(kHeadMs)) + " ms)");
        check(r.firstSoundSeconds >= 0.0 && r.firstSoundSeconds < 0.05,
              juce::String(label) + ": first sound arrives with the first notes, not "
              "seconds later (" + juce::String(r.firstSoundSeconds, 3) + " s)");
    }

    // ---- 2. the flag must drop SYNCHRONOUSLY, on the calling thread --------
    // No pumping at all between the request and the read: a host that changes
    // the program and immediately polls must never see the OUTGOING library's
    // "ready". This is the check that has no timing in it.
    {
        auto processor = std::make_unique<sappkeys::SappKeysProcessor>();
        processor->prepareToPlay(kSampleRate, kBlock);
        processor->setCurrentProgram(kGrandProgram);
        const auto first = settleOnFlag(*processor, 20000);
        check(first.ready && processor->currentInstrumentPath() == grandSfz.getFullPathName(),
              "mid-session: the first program settled on its library");

        processor->setCurrentProgram(kEpProgram);
        check(!processor->libraryReady(),
              "mid-session: libraryReady reads 0 the instant setCurrentProgram() returns");

        const auto second = settleOnFlag(*processor, 20000);
        check(second.ready && second.pathAtReady == epSfz.getFullPathName(),
              "mid-session: the flag comes back only with the NEW library installed");
        processor.reset();
    }

    // ---- 3. same, through the `preset` parameter and a MIDI program change --
    {
        auto processor = std::make_unique<sappkeys::SappKeysProcessor>();
        processor->prepareToPlay(kSampleRate, kBlock);
        processor->setCurrentProgram(kEpProgram);
        settleOnFlag(*processor, 20000);

        auto* parameter = processor->valueTree().getParameter("preset");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(float(kGrandProgram)));
        check(!processor->libraryReady(),
              "mid-session: libraryReady reads 0 the instant the `preset` parameter moves");
        const auto viaParam = settleOnFlag(*processor, 20000);
        check(viaParam.ready && viaParam.pathAtReady == grandSfz.getFullPathName(),
              "mid-session: the `preset` parameter's library is in when the flag returns");

        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, kEpProgram), 0);
        buffer.clear();
        processor->processBlock(buffer, midi);
        check(!processor->libraryReady(),
              "mid-session: libraryReady reads 0 the instant a MIDI program change lands");
        const auto viaMidi = settleOnFlag(*processor, 20000);
        check(viaMidi.ready && viaMidi.pathAtReady == epSfz.getFullPathName(),
              "mid-session: the MIDI program change's library is in when the flag returns");
        processor.reset();
    }

    // ---- 4. a state restore owns a readiness window too --------------------
    {
        auto saved = std::make_unique<sappkeys::SappKeysProcessor>();
        saved->prepareToPlay(kSampleRate, kBlock);
        saved->loadSfzInstrument(grandSfz);
        settleOnFlag(*saved, 20000);
        juce::MemoryBlock state;
        saved->getStateInformation(state);
        saved.reset();

        auto restored = std::make_unique<sappkeys::SappKeysProcessor>();
        restored->prepareToPlay(kSampleRate, kBlock);
        restored->setStateInformation(state.getData(), int(state.getSize()));
        check(!restored->libraryReady(),
              "restore: libraryReady reads 0 the instant setStateInformation() returns");
        const auto settled = settleOnFlag(*restored, 20000);
        check(settled.ready && settled.pathAtReady == grandSfz.getFullPathName(),
              "restore: the flag returns with the restored library installed");
        restored.reset();
    }

    // ---- 5. the selected library is the one that sounded -------------------
    {
        RenderOptions grand;
        grand.select = Select::HostProgram;
        grand.program = kGrandProgram;
        RenderOptions ep = grand;
        ep.program = kEpProgram;
        const auto a = stationRender(grand);
        const auto b = stationRender(ep);
        report("grand", a);
        report("ep", b);
        check(b.rms < a.rms * 0.5,
              "the quiet fixture library really is what sounded when its program was "
              "selected (" + juce::String(toDb(b.rms), 1) + " vs "
              + juce::String(toDb(a.rms), 1) + " dBFS)");
    }

    std::printf("selftest: %s\n", fails == 0 ? "ALL PASS" : "FAILURES");
    return fails == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String command = argc > 1 ? juce::String(argv[1]) : juce::String();
    juce::String root, out, fixture;
    RenderOptions options;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg(argv[i]);
        auto next = [&]() -> juce::String {
            return i + 1 < argc ? juce::String(argv[++i]) : juce::String();
        };
        if (arg == "--root") root = next();
        else if (arg == "--fixture") fixture = next();
        else if (arg == "--out") out = next();
        else if (arg == "--settle") options.settleMs = next().getIntValue();
        else if (arg == "--program") {
            options.select = Select::HostProgram;
            options.program = next().getIntValue();
        } else if (arg == "--param") {
            const auto kv = next();
            options.params.set(kv.upToFirstOccurrenceOf("=", false, false),
                               kv.fromFirstOccurrenceOf("=", false, false));
        }
    }

    if (command == "selftest") {
        if (fixture.isEmpty()) fixture = root;
#ifdef SAPPKEYS_TEST_DATA_DIR
        if (fixture.isEmpty()) fixture = juce::String(SAPPKEYS_TEST_DATA_DIR) + "/keys-headless";
#endif
        return runSelftest(fixture);
    }

    if (command == "render") {
        if (root.isNotEmpty())
            setEnv(sappkeys::kSamplesRootEnvVar, root);
        const auto result = stationRender(options);
        std::printf("instrument:  %s\n", result.instrumentPath.toRawUTF8());
        std::printf("name:        %s\n", result.instrumentName.toRawUTF8());
        std::printf("status:      %s\n", result.status.toRawUTF8());
        std::printf("ready at:    %.2f s (%s)\n", result.settle.seconds,
                    result.settle.ready ? "flag" : "TIMED OUT");
        std::printf("at ready:    %s%s\n",
                    result.settle.pathAtReady.isEmpty()
                        ? "(diagnostic - NOTHING the host asked for was installed)"
                        : result.settle.pathAtReady.toRawUTF8(),
                    result.settle.lied ? "  [LOAD STILL IN FLIGHT]" : "");
        std::printf("head %d ms:  %.2f dBFS%s\n", int(kHeadMs), toDb(result.headPeak),
                    result.headPeak > 1.0e-5 ? "" : "   <- DIGITAL SILENCE");
        std::printf("first sound: %.3f s\n", result.firstSoundSeconds);
        std::printf("rms:         %.8f  (%.2f dBFS)\n", result.rms, toDb(result.rms));
        std::printf("peak:        %.8f  (%.2f dBFS)\n", result.peak, toDb(result.peak));
        if (out.isNotEmpty()) {
            juce::File file(out);
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
            if (stream != nullptr) {
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wav.createWriterFor(stream.get(), kSampleRate, 2, 24, {}, 0));
                if (writer != nullptr) {
                    stream.release();
                    const float* channels[2] = {result.left.data(), result.right.data()};
                    writer->writeFromFloatArrays(channels, 2, int(result.left.size()));
                }
            }
            std::printf("wrote:       %s\n", out.toRawUTF8());
        }
        return 0;
    }

    std::fprintf(stderr,
                 "sappkeys-headless — station harness (no GUI)\n"
                 "  sappkeys-headless selftest [--fixture DIR]\n"
                 "  sappkeys-headless render   [--program N] [--out F.wav]\n"
                 "                             [--root DIR] [--settle MS] [--param ID=V]\n");
    return 2;
}
