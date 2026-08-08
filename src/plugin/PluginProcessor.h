#pragma once
// SappKeys plugin processor: JUCE wrapper around KeysEngine. Owns parameters
// (APVTS), host state, MIDI conversion, and async instrument loading. All
// sampler/keys DSP lives below in sappkeys_core / SappSounds.

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include <sapp/sounds/InstrumentLoader.h>

#include "../core/KeysEngine.h"
#include "UserPresets.h"

namespace sappkeys {

class SappKeysProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::Timer
{
public:
    // The SappLink instrument name: names the user-preset folder and must
    // match sapplink/manifests/sappkeys.json.
    static constexpr const char* kInstrument = "sappkeys";

    SappKeysProcessor();
    ~SappKeysProcessor() override;

    // --- AudioProcessor -----------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SappKeys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    // Factory-preset programs (see FactoryPresets.h): program N applies
    // preset N — parameter starting points plus an instrument hint when the
    // matching library is installed. Reachable from the host program API and
    // via MIDI program change (SappLink set_patches). CCs keep working on
    // top after a program change.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // Apply factory preset N now. Message thread only.
    void applyFactoryPreset(int index);

    // ---------------------------------------------------------- user presets --
    // Saved sounds, shared format across the suite (sapplink/PRESETS.md).
    // Factory presets stay addressed by program index; user presets are
    // addressed by NAME, so the two can never collide.

    // Capture the current parameter state (plus the loaded SFZ library path)
    // to <Documents>/SappSounds/presets/sappkeys/<name>.json. Message thread.
    bool saveUserPreset(const juce::String& name, const juce::String& notes,
                        juce::String& error);

    // Load a user preset by name (case-insensitive). Message thread.
    bool loadUserPreset(const juce::String& name, juce::String& error);

    // Fresh scan of the user preset folder.
    std::vector<sapp::userpresets::UserPreset> userPresets() const;

    // Choice-list geometry of the `preset` parameter: [0, factoryPresetCount)
    // are factory programs, the rest are the user presets discovered when this
    // instance was constructed.
    int factoryPresetCount() const;

    // Apply choice N of the `preset` parameter. Message thread.
    void applyPresetChoice(int index);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- SappKeys -----------------------------------------------------------
    juce::AudioProcessorValueTreeState& valueTree() { return apvts_; }
    sapp::keys::KeysEngine& engine() { return engine_; }

    // Async instrument management (message thread).
    void loadSfzInstrument(const juce::File& sfzFile);
    void loadDiagnosticInstrument();
    juce::String currentInstrumentName() const;
    juce::String currentInstrumentPath() const { return sfzPath_; }
    juce::String loadStatus() const;
    bool isLoading() const { return loading_.load(); }

    juce::MidiKeyboardState keyboardState;

    std::function<void()> onInstrumentChanged;  // editor hook (message thread)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void pushParamsToEngine();
    void finishLoad(sapp::sounds::LoadResult result, const juce::String& path,
                    uint64_t generation);

    juce::AudioProcessorValueTreeState apvts_;
    sapp::keys::KeysEngine engine_;

    // Cached raw parameter pointers (audio thread reads).
    std::atomic<float>* pTouch_ = nullptr;
    std::atomic<float>* pDynamics_ = nullptr;
    std::atomic<float>* pExpression_ = nullptr;
    std::atomic<float>* pUnaCorda_ = nullptr;
    std::atomic<float>* pLid_ = nullptr;
    std::atomic<float>* pResonance_ = nullptr;
    std::atomic<float>* pMechNoise_ = nullptr;
    std::atomic<float>* pWidth_ = nullptr;
    std::atomic<float>* pVintage_ = nullptr;
    std::atomic<float>* pDrive_ = nullptr;
    std::atomic<float>* pRoomLevel_ = nullptr;
    std::atomic<float>* pRoomSize_ = nullptr;
    std::atomic<float>* pRoomDecay_ = nullptr;
    std::atomic<float>* pMaster_ = nullptr;
    std::atomic<float>* pLimiter_ = nullptr;
    std::atomic<float>* pQuality_ = nullptr;

    // Knob→CC bridging: moving Dynamics/Expression injects the matching CC.
    float lastDynParam_ = -1.0f, lastExprParam_ = -1.0f;

    // SappLink CC-in (see src/core/SappLinkCCMap.h): mapped controllers land
    // as slew targets; each block moves the APVTS parameter a fraction of the
    // way — the same normalized path host automation uses — so 7-bit CC steps
    // don't zipper. CC 1/11/64 are engine-native and never appear here.
    struct CcSlew {
        juce::RangedAudioParameter* parameter = nullptr;
        float target = 0.0f, current = 0.0f;
        bool active = false;
    };
    std::array<CcSlew, 12> ccSlews_;
    void handleSappLinkCc(int ccNumber, int ccValue);
    void advanceCcSlews(int numSamples);

    std::vector<sapp::sounds::MidiEvent> eventScratch_;

    // MIDI program change lands on the audio thread; the preset itself is
    // applied on the message thread (timer), sappstep-style.
    void timerCallback() override;
    std::atomic<int> pendingProgram_{-1};
    std::atomic<int> currentProgram_{0};

    // The `preset` parameter can be moved from the audio thread (host
    // automation), so its listener only stores an index — the same timer that
    // already defers program changes does the loading.
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    std::atomic<int> pendingPresetChoice_{-1};
    // Set while WE are moving the `preset` parameter, so syncing it after a
    // program change never re-enters the load.
    bool applyingPreset_ = false;
    void syncPresetParameter(int choiceIndex);

    juce::String sfzPath_;                 // "" = diagnostic instrument
    juce::String instrumentName_{"(loading)"};
    juce::String loadStatus_{"starting"};
    std::atomic<bool> loading_{false};
    std::atomic<uint64_t> loadGeneration_{0};
    juce::CriticalSection loadLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKeysProcessor)
};

} // namespace sappkeys
