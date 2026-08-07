// SappKeysUiShot — renders the plugin editor offscreen and writes a PNG.
// Used to verify UI changes without a screen-recording session.
//   SappKeysUiShot [output.png]
//   SappKeysUiShot --sounds [output.png]   (snapshot with the GET SOUNDS panel open)
//   SappKeysUiShot --cctest     (SappLink CC-in proof through the plugin path)

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

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
        // The diagnostic instrument loads asynchronously on the message
        // thread; give it time on the normal run loop, then measure.
        juce::Timer::callAfterDelay(2500, [this] { finishCcTest(); });
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

    void initialise(const juce::String& commandLine) override
    {
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
};

START_JUCE_APPLICATION(UiShotApp)
