// SappKeysUiShot — renders the plugin editor offscreen and writes a PNG.
// Used to verify UI changes without a screen-recording session.
//   SappKeysUiShot [output.png]
//   SappKeysUiShot --sounds [output.png]   (snapshot with the GET SOUNDS panel open)
//   SappKeysUiShot --cctest      (SappLink CC-in proof through the plugin path)
//   SappKeysUiShot --presettest  (user-preset round-trip proof; point
//                                 SAPPSOUNDS_PRESETS at a temp dir first)

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UserPresets.h"

class UiShotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SappKeysUiShot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    // --cctest: end-to-end SappLink proof through the PLUGIN path — CC 7
    // (masterGain) arrives via processBlock, slews the APVTS parameter exactly
    // like host automation, and must scale the output level.
    void runCcTest()
    {
        processor = std::make_unique<sappkeys::SappKeysProcessor>();
        processor->prepareToPlay(48000.0, 512);
        // The diagnostic instrument loads asynchronously on a worker thread;
        // wait for it rather than guessing a delay (a fixed 2.5 s wait went
        // flaky whenever the machine was busy and measured silence).
        whenIdle([this] { finishCcTest(); });
    }

    // Runs `then` once no processor is still loading an instrument.
    void whenIdle(std::function<void()> then, int attemptsLeft = 600)
    {
        const bool busy = (processor && processor->isLoading())
                          || (procA && procA->isLoading()) || (procB && procB->isLoading())
                          || (procC && procC->isLoading());
        if (!busy || attemptsLeft <= 0) {
            then();
            return;
        }
        juce::Timer::callAfterDelay(
            50, [this, then, attemptsLeft] { whenIdle(then, attemptsLeft - 1); });
    }

    void finishCcTest()
    {
        // Kill room + limiter so the direct path dominates the measurement.
        processor->valueTree().getParameter("roomLevel")->setValueNotifyingHost(0.0f);
        processor->valueTree().getParameter("limiter")->setValueNotifyingHost(0.0f);

        juce::AudioBuffer<float> buffer(2, 512);
        auto measure = [&](int ccValue) {
            double energy = 0.0;
            for (int b = 0; b < 120; ++b) {   // ~1.3 s per side
                juce::MidiBuffer midi;
                if (b == 0) {
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, ccValue), 0);
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 1);
                }
                buffer.clear();
                processor->processBlock(buffer, midi);
                if (b > 40) {  // measure after the slew settles
                    for (int i = 0; i < 512; ++i) {
                        energy += double(buffer.getSample(0, i)) * buffer.getSample(0, i);
                        energy += double(buffer.getSample(1, i)) * buffer.getSample(1, i);
                    }
                }
            }
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            buffer.clear();
            processor->processBlock(buffer, off);
            for (int b = 0; b < 60; ++b) { juce::MidiBuffer none; buffer.clear(); processor->processBlock(buffer, none); }
            return energy;
        };

        const double quiet = measure(0);     // masterGain = -24 dB
        const double loud = measure(127);    // masterGain = +12 dB
        const bool pass = loud > quiet * 100.0;
        std::printf("SappLink CC7 sweep: cc=0 %.3g  cc=127 %.3g  [%s]\n",
                    quiet, loud, pass ? "PASS" : "FAIL");
        editor.reset();
        processor.reset();
        setApplicationReturnValue(pass ? 0 : 1);
        quit();
    }

    // ------------------------------------------------------- --presettest --
    // Headless proof of the user-preset system (sapptune/sapplink/PRESETS.md):
    // capture -> disk -> a FRESH processor -> identical normalised values, plus
    // regressions for the `preset` parameter, MIDI program change, and host
    // state. Everything runs through the real plugin path; steps are chained
    // on the message loop because factory/preset selection is deferred to the
    // processor's 30 Hz timer.
    void runPresetTest()
    {
        const auto dir = sapp::userpresets::presetDir(sappkeys::SappKeysProcessor::kInstrument);
        std::printf("preset dir: %s\n", dir.getFullPathName().toRawUTF8());

        procA = std::make_unique<sappkeys::SappKeysProcessor>();
        procA->prepareToPlay(48000.0, 512);

        steps.push_back({100, [this] { stepApplyFactory(); }});
        steps.push_back({100, [this] { stepCaptureAndSave(); }});
        steps.push_back({100, [this] { stepLoadInFreshProcessor(); }});
        steps.push_back({100, [this] { stepCheckSfzHint(); }});
        steps.push_back({100,  [this] { stepSelectFactoryViaParameter(); }});
        steps.push_back({500,  [this] { stepCheckFactoryViaParameter(); }});
        steps.push_back({500,  [this] { stepCheckProgramChange(); }});
        steps.push_back({500,  [this] { stepStateRoundTrip(); }});
        steps.push_back({500,  [this] { stepEditorList(); }});
        steps.push_back({800,  [this] { stepFinish(); }});
        runNextStep();
    }

    void runNextStep()
    {
        if (stepIndex >= steps.size())
            return;
        const auto step = steps[stepIndex++];
        // Each step waits for any in-flight instrument load to finish, so the
        // proof does not depend on how busy the machine is.
        juce::Timer::callAfterDelay(step.first, [this, action = step.second] {
            whenIdle([this, action] {
                action();
                runNextStep();
            });
        });
    }

    // Every parameter except the `preset` chooser, which is deliberately never
    // stored in a preset (PRESETS.md section 1).
    static std::vector<std::pair<juce::String, float>> soundParams(juce::AudioProcessor& p)
    {
        std::vector<std::pair<juce::String, float>> out;
        for (auto* parameter : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                if (withId->paramID != sapp::userpresets::kPresetParamId)
                    out.push_back({withId->paramID, withId->getValue()});
        return out;
    }

    static float plainValue(juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return state.getRawParameterValue(id)->load();
    }

    void stepApplyFactory()
    {
        // A factory preset first: it also swaps the SFZ library when the one
        // it prefers is installed, and that load is asynchronous.
        procA->applyFactoryPreset(3);   // "EP Mark I"
        std::printf("A: applied factory preset 3 (%s)\n",
                    procA->getProgramName(3).toRawUTF8());
    }

    void stepCaptureAndSave()
    {
        std::printf("A: instrument = %s\n",
                    procA->currentInstrumentPath().isEmpty()
                        ? "(built-in diagnostic)"
                        : procA->currentInstrumentPath().toRawUTF8());

        // Several parameters nudged off the factory preset's values.
        const struct { const char* id; float norm; } nudges[] = {
            {"touch", 0.7734f},  {"lid", 0.2266f},      {"resonance", 0.9101f},
            {"width", 0.4237f},  {"vintage", 0.6613f},  {"drive", 0.3129f},
            {"roomDecay", 0.5804f}, {"masterGain", 0.7211f}, {"mechNoise", 0.1279f},
            {"quality", 0.0f},   {"limiter", 0.0f},
        };
        for (const auto& n : nudges) {
            auto* parameter = procA->valueTree().getParameter(n.id);
            parameter->setValueNotifyingHost(n.norm);
            std::printf("A: %-11s norm=%.9f plain=%.6f\n", n.id,
                        double(parameter->getValue()),
                        double(plainValue(procA->valueTree(), n.id)));
        }

        juce::String error;
        if (!procA->saveUserPreset("RoundTrip Test", "headless round-trip proof", error)) {
            std::printf("FAIL: saveUserPreset: %s\n", error.toRawUTF8());
            pass = false;
            return;
        }
        const auto file = sapp::userpresets::presetDir(sappkeys::SappKeysProcessor::kInstrument)
                              .getChildFile("RoundTrip Test.json");
        if (!file.existsAsFile()) {
            std::printf("FAIL: no file at %s\n", file.getFullPathName().toRawUTF8());
            pass = false;
            return;
        }
        std::printf("--- %s ---\n%s---\n", file.getFullPathName().toRawUTF8(),
                    file.loadFileAsString().toRawUTF8());

        // 2. a FRESH processor, constructed after the file exists so its
        //    `preset` parameter picks the preset up in its choice list.
        procB = std::make_unique<sappkeys::SappKeysProcessor>();
        procB->prepareToPlay(48000.0, 512);
    }

    void stepLoadInFreshProcessor()
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
            procB->valueTree().getParameter(sapp::userpresets::kPresetParamId));
        std::printf("B: `preset` choices=%d factory=%d last=\"%s\"\n",
                    choice->choices.size(), procB->factoryPresetCount(),
                    choice->choices[choice->choices.size() - 1].toRawUTF8());
        if (choice->choices[choice->choices.size() - 1] != "RoundTrip Test (user)") {
            std::printf("FAIL: user preset missing from the choice list\n");
            pass = false;
        }

        juce::String error;
        if (!procB->loadUserPreset("RoundTrip Test", error)) {
            std::printf("FAIL: loadUserPreset: %s\n", error.toRawUTF8());
            pass = false;
            return;
        }

        const auto a = soundParams(*procA), b = soundParams(*procB);
        float worst = 0.0f;
        juce::String worstId;
        for (size_t i = 0; i < a.size(); ++i) {
            const float diff = std::abs(a[i].second - b[i].second);
            if (diff > worst) { worst = diff; worstId = a[i].first; }
        }
        std::printf("round-trip: %d params compared (excluding `preset`), "
                    "max |A-B| = %.9g%s\n",
                    int(a.size()), double(worst),
                    worstId.isEmpty() ? "" : (" (" + worstId + ")").toRawUTF8());
        for (size_t i = 0; i < a.size(); ++i)
            std::printf("   %-11s A=%.9f B=%.9f\n", a[i].first.toRawUTF8(),
                        double(a[i].second), double(b[i].second));
        if (worst != 0.0f) {
            std::printf("FAIL: round-trip is not exact\n");
            pass = false;
        }
    }

    void stepCheckSfzHint()
    {
        // PRESETS.md's optional `sfz` resource hint: the fresh processor should
        // be playing the same library the preset was captured with.
        const auto a = procA->currentInstrumentPath(), b = procB->currentInstrumentPath();
        std::printf("sfz hint: A=%s\n          B=%s\n",
                    a.isEmpty() ? "(built-in diagnostic)" : a.toRawUTF8(),
                    b.isEmpty() ? "(built-in diagnostic)" : b.toRawUTF8());
        if (a.isNotEmpty() && a != b) {
            std::printf("FAIL: the `sfz` hint did not restore the library\n");
            pass = false;
        }
    }

    void stepSelectFactoryViaParameter()
    {
        beforeVintage = plainValue(procB->valueTree(), "vintage");
        beforeDrive = plainValue(procB->valueTree(), "drive");
        std::printf("B: before `preset`=7 (Honky Tonk): vintage=%.4f drive=%.4f\n",
                    double(beforeVintage), double(beforeDrive));
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
            procB->valueTree().getParameter(sapp::userpresets::kPresetParamId));
        choice->setValueNotifyingHost(choice->convertTo0to1(7.0f));
    }

    void stepCheckFactoryViaParameter()
    {
        const float vintage = plainValue(procB->valueTree(), "vintage");
        const float drive = plainValue(procB->valueTree(), "drive");
        std::printf("B: after  `preset`=7 (Honky Tonk): vintage=%.4f drive=%.4f "
                    "(expected 0.8500 / 0.2500), currentProgram=%d\n",
                    double(vintage), double(drive), procB->getCurrentProgram());
        if (std::abs(vintage - 0.85f) > 1.0e-4f || std::abs(drive - 0.25f) > 1.0e-4f
            || vintage == beforeVintage || procB->getCurrentProgram() != 7) {
            std::printf("FAIL: the `preset` parameter did not select the factory preset\n");
            pass = false;
        }

        // Regression: MIDI program change still selects factory presets.
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, 0), 0);   // "Grand Concert"
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        procB->processBlock(buffer, midi);
    }

    void stepCheckProgramChange()
    {
        const float lid = plainValue(procB->valueTree(), "lid");
        const float decay = plainValue(procB->valueTree(), "roomDecay");
        std::printf("B: after MIDI program change 0 (Grand Concert): lid=%.4f "
                    "roomDecay=%.4f (expected 1.0000 / 1.3000), currentProgram=%d\n",
                    double(lid), double(decay), procB->getCurrentProgram());
        if (std::abs(lid - 1.0f) > 1.0e-4f || std::abs(decay - 1.3f) > 1.0e-4f
            || procB->getCurrentProgram() != 0) {
            std::printf("FAIL: MIDI program change regressed\n");
            pass = false;
        }

        // Regression: host state still round-trips.
        procB->getStateInformation(stateBlob);
        procC = std::make_unique<sappkeys::SappKeysProcessor>();
        procC->prepareToPlay(48000.0, 512);
        procC->setStateInformation(stateBlob.getData(), int(stateBlob.getSize()));
    }

    void stepStateRoundTrip()
    {
        const auto b = soundParams(*procB), c = soundParams(*procC);
        float worst = 0.0f;
        juce::String worstId;
        for (size_t i = 0; i < b.size(); ++i) {
            const float diff = std::abs(b[i].second - c[i].second);
            if (diff > worst) { worst = diff; worstId = b[i].first; }
        }
        // The APVTS XML blob keeps ~7 significant digits, so this path has
        // always lost a little; the tolerance is pre-existing behaviour, not
        // something the preset work introduced.
        std::printf("host state: %d bytes, %d params compared, max |B-C| = %.9g%s "
                    "(tolerance 1e-6)\n",
                    int(stateBlob.getSize()), int(b.size()), double(worst),
                    worstId.isEmpty() ? "" : (" (" + worstId + ")").toRawUTF8());
        if (worst > 1.0e-6f) {
            std::printf("FAIL: getStateInformation/setStateInformation regressed\n");
            pass = false;
        }
    }

    // The editor's chooser must offer the preset saved this session, by name
    // and marked as a user preset — it rescans on open, unlike the automation
    // parameter's fixed list.
    void stepEditorList()
    {
        editor.reset(procB->createEditor());
        sappkeys::PresetChooser* chooser = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* found = dynamic_cast<sappkeys::PresetChooser*>(child))
                chooser = found;
        if (chooser == nullptr) {
            std::printf("FAIL: the editor has no preset chooser\n");
            pass = false;
            return;
        }
        juce::StringArray items;
        for (int i = 0; i < chooser->getNumItems(); ++i)
            items.add(chooser->getItemText(i));
        std::printf("editor chooser: %d items [%s]\n", items.size(),
                    items.joinIntoString(" | ").toRawUTF8());
        if (!items.contains("RoundTrip Test (user)")) {
            std::printf("FAIL: the saved user preset is not selectable in the editor\n");
            pass = false;
        }
    }

    void stepFinish()
    {
        std::printf("presettest: %s\n", pass ? "PASS" : "FAIL");
        editor.reset();
        procA.reset();
        procB.reset();
        procC.reset();
        setApplicationReturnValue(pass ? 0 : 1);
        quit();
    }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--presettest")) {
            runPresetTest();
            return;
        }

        if (commandLine.contains("--cctest")) {
            runCcTest();
            return;
        }

        const bool showSounds = commandLine.contains("--sounds");
        const juce::String rest = commandLine.replace("--sounds", "").trim();
        const juce::String outPath = rest.isNotEmpty()
            ? rest.unquoted() : juce::String("/tmp/sappkeys-ui.png");

        processor = std::make_unique<sappkeys::SappKeysProcessor>();
        processor->prepareToPlay(48000.0, 512);
        editor.reset(processor->createEditor());
        if (showSounds)
            if (auto* keysEditor = dynamic_cast<sappkeys::SappKeysEditor*>(editor.get()))
                keysEditor->openSoundsPanel();

        // Give the async diagnostic-instrument load and fonts time to settle,
        // then play a pedaled chord so the meter, velocity dots, and pedal
        // lamp are alive in the shot.
        juce::Timer::callAfterDelay(2500, [this, outPath]
        {
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 43, 0.55f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 59, 0.7f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.62f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 66, 0.8f), 0);
            for (int i = 0; i < 20; ++i) {
                buffer.clear();
                processor->processBlock(buffer, midi);
                midi.clear();
            }

            juce::Timer::callAfterDelay(300, [this, outPath]
            {
                auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
                juce::File file(outPath);
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat png;
                if (stream.openedOk() && png.writeImageToStream(snapshot, stream))
                    std::printf("wrote %s (%dx%d)\n", outPath.toRawUTF8(),
                                snapshot.getWidth(), snapshot.getHeight());
                else
                    std::printf("FAILED to write %s\n", outPath.toRawUTF8());
                editor.reset();
                processor.reset();
                quit();
            });
        });
    }

    void shutdown() override {}

private:
    std::unique_ptr<sappkeys::SappKeysProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    // --presettest state.
    std::unique_ptr<sappkeys::SappKeysProcessor> procA, procB, procC;
    std::vector<std::pair<int, std::function<void()>>> steps;
    size_t stepIndex = 0;
    bool pass = true;
    float beforeVintage = 0.0f, beforeDrive = 0.0f;
    juce::MemoryBlock stateBlob;
};

START_JUCE_APPLICATION(UiShotApp)
