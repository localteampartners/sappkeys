#pragma once
// SappKeys plugin processor: JUCE wrapper around KeysEngine. Owns parameters
// (APVTS), host state, MIDI conversion, and async instrument loading. All
// sampler/keys DSP lives below in sappkeys_core / SappSounds.

#include <array>
#include <atomic>
#include <memory>
#include <thread>

#include <juce_audio_utils/juce_audio_utils.h>

#include <sapp/sounds/InstrumentLoader.h>

#include "../core/KeysEngine.h"

namespace sappkeys {

class SappKeysProcessor : public juce::AudioProcessor
{
public:
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

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

    juce::String sfzPath_;                 // "" = diagnostic instrument
    juce::String instrumentName_{"(loading)"};
    juce::String loadStatus_{"starting"};
    std::atomic<bool> loading_{false};
    std::atomic<uint64_t> loadGeneration_{0};
    juce::CriticalSection loadLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKeysProcessor)
};

} // namespace sappkeys
