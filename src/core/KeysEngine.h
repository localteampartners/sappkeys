#pragma once
// KeysEngine — SappKeys' product policy wrapped around the generic
// sapp::sounds::PlaybackEngine.
//
// SappSounds owns: SFZ, samples, voices, velocity layers, release samples,
// sustain pedal (CC64 deferred releases), round robin.
// SappKeys owns (here): touch (velocity curve), CC1 gentle dynamics trim,
// CC11 expression, una-corda softening, lid tilt EQ + width, sympathetic
// resonance on pedal-down, mechanical-noise mix (release-sample level via an
// internal reserved CC), tape/EP vintage character (random tune + wow/flutter
// + HF soften), gentle drive, small-room ambience, master output policy.
//
// Framework-independent: no JUCE. The JUCE plugin and the CLI both drive this.

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <sapp/sounds/InstrumentDefinition.h>
#include <sapp/sounds/PlaybackEngine.h>

#include "Resonance.h"
#include "Room.h"

namespace sapp::keys {

// Internal reserved controller: KeysEngine injects CC 102 to scale
// release-sample regions (see KeysInstrument.h policy injection). External
// CC 102 events are dropped so outside MIDI can't fight the mech knob.
inline constexpr int kMechNoiseInternalCc = 102;
inline constexpr float kMechNoiseRangeDb = -60.0f;

// Most MIDI events one process() call acts on. A block carrying more than this
// is a MIDI flood, not a performance. The surplus is dropped AND every note is
// ended (see process()): the dangerous truncation is a lost note-off, which
// leaves notes stuck on at whatever velocity the flood carried.
inline constexpr int kMaxBlockEvents = 1024;

// Slots the engine's per-block event scratch needs in the worst case: the
// incoming events, one released note-off per key at the head of the block, one
// more per re-triggered key, the injected mech-noise CC, and the panic.
inline constexpr int kEventScratchSlots = 2 * kMaxBlockEvents + 130;

// Safety-limiter ceiling, linear (-1 dBFS), and the absolute output bound the
// engine enforces whether or not the limiter is switched on (0 dBFS).
inline constexpr float kSafetyCeiling = 0.891251f;
inline constexpr float kOutputBound = 1.0f;

// (The note-off guard that used to live here is gone: SappSounds now delivers
// note-offs to steal-fading voices itself — PendingStart::releasedBeforeStart
// in PlaybackEngine.cpp — so holding offs back is no longer needed. Verified
// by this repo's same-block burst safety test running against the fixed
// engine with no guard.)

struct KeysParams {
    // Performance
    float touch = 0.5f;        // velocity response: 0 heavy .. 0.5 neutral .. 1 light
    float dynamics = 1.0f;     // 0..1, follows CC1 (gentle level + brightness trim)
    float expression = 1.0f;   // 0..1, follows CC11
    float unaCorda = 0.0f;     // 0..1, soft pedal: velocity softening + darker tilt
    // Instrument body
    float lid = 0.85f;         // 0 closed .. 1 full stick (tilt EQ + width)
    float resonance = 0.5f;    // sympathetic resonance level on pedal-down
    float mechNoise = 1.0f;    // release/mechanical sample mix: 1 as recorded, 0 off
    float width = 1.0f;        // 0 mono .. 2 wide
    // Character
    float vintage = 0.0f;      // tape/EP age: random tune, wow/flutter, HF soften
    float drive = 0.0f;        // gentle saturation (EPs love it)
    // Room
    float roomLevel = 0.30f;
    float roomSize = 1.0f;     // 0.6..1.4
    float roomDecay = 0.9f;    // seconds, 0.2..2.5
    // Output
    float masterGainDb = 0.0f;
    // Safety limiter (default ON). Peak-accurate gain reduction to
    // kSafetyCeiling — it turns loud material DOWN rather than squaring it off.
    // Switching it off does not remove the unconditional kOutputBound clamp.
    bool limiter = true;
    int quality = 1;           // 0 draft (linear), 1 normal (cubic)
};

// The touch/una-corda velocity mapping, shared with the UI curve display.
// Returns the remapped velocity (1..127) for an incoming velocity 1..127.
inline float shapeVelocity(float velocity, float touch, float unaCorda) noexcept
{
    const float softened = velocity * (1.0f - 0.30f * unaCorda);
    const float norm = std::clamp(softened / 127.0f, 0.0f, 1.0f);
    // touch 0 → gamma 3 (heavy action), 0.5 → 1 (as played), 1 → 1/3 (light).
    const float gamma = std::pow(3.0f, 1.0f - 2.0f * std::clamp(touch, 0.0f, 1.0f));
    return std::clamp(std::pow(norm, gamma) * 127.0f, 1.0f, 127.0f);
}

class KeysEngine {
public:
    KeysEngine();

    // --- control thread -----------------------------------------------------
    void prepare(double sampleRate, int maxBlockFrames);
    void setInstrument(sapp::sounds::InstrumentPtr instrument);
    void collectRetired();
    sapp::sounds::InstrumentPtr currentInstrument() const;

    void setParams(const KeysParams& params);   // copied atomically
    KeysParams params() const;

    void resetSequences();
    void reseed(uint32_t seed);

    const sapp::sounds::PlaybackEngine& sampler() const { return sampler_; }
    sapp::sounds::PlaybackEngine& sampler() { return sampler_; }

    // --- UI feed (any thread) ----------------------------------------------
    bool sustainPedalDown() const noexcept
    {
        return pedalDownUi_.load(std::memory_order_relaxed);
    }
    struct VelSample { uint8_t in = 0, out = 0; };
    static constexpr int kVelHistory = 8;
    // Copies the most recent note-on velocity pairs, newest last. Returns count.
    int velocityHistory(VelSample out[kVelHistory]) const noexcept;

    // --- audio thread -------------------------------------------------------
    // Replaces buffer contents (not additive). Events sorted by frame.
    void process(const sapp::sounds::MidiEvent* events, int eventCount,
                 float* outL, float* outR, int frames) noexcept;

    // Gain reduction currently applied by the safety limiter, linear (1 = none).
    // UI/diagnostic feed, any thread.
    float limiterGain() const noexcept
    {
        return limiterGainUi_.load(std::memory_order_relaxed);
    }

private:
    void applyQuality(const KeysParams& p) noexcept;
    // Output stage: peak-accurate limiting (when enabled) followed by the
    // unconditional finite/range guard. Operates in place on the block.
    void limitAndGuard(float* outL, float* outR, int frames, bool enabled) noexcept;
    // Zero every filter/delay state. Called when a non-finite sample is seen so
    // one bad sample cannot park a NaN inside an IIR forever. No allocation.
    void scrubState() noexcept;

    sapp::sounds::PlaybackEngine sampler_;
    SympatheticResonance resonance_;
    RoomEarly early_;
    SmallRoom room_;

    // Double-buffered params: control writes inactive slot then flips.
    KeysParams paramSlots_[2];
    std::atomic<int> paramIndex_{0};

    // Live controller state (audio thread): CC1/CC11 override the params once
    // received; -1 = no CC received yet → the parameter value applies.
    float liveDynamics_ = -1.0f, liveExpression_ = -1.0f;

    bool heldNotes_[128] = {};
    bool pedalDown_ = false;
    std::atomic<bool> pedalDownUi_{false};

    // Velocity display feed: packed (in<<8)|out, lock-free ring.
    std::atomic<uint32_t> velRing_[kVelHistory] = {};
    std::atomic<uint32_t> velWrite_{0};

    // Smoothed audio-thread state.
    float smDynGain_ = 1.0f, smExprGain_ = 1.0f, smCutoffCoef_ = 1.0f;
    float smLidHi_ = 1.0f, smWidth_ = 1.0f, smRoom_ = 0.3f, smMaster_ = 1.0f;
    float smDriveMix_ = 0.0f;
    float lpL_ = 0.0f, lpR_ = 0.0f;        // dynamics/una-corda/vintage filter
    float lidLpL_ = 0.0f, lidLpR_ = 0.0f;  // lid shelf crossover
    float wowPhase_ = 0.0f, flutterPhase_ = 0.0f;

    // Safety limiter state. Attack is instantaneous at the block boundary
    // (the gain is chosen from the whole block's peak, so no sample can slip
    // through above the ceiling); release is exponential, ~150 ms.
    float limGain_ = 1.0f;
    float limReleaseCoef_ = 0.0f;
    std::atomic<float> limiterGainUi_{1.0f};

    // Scratch buffers (allocated in prepare).
    std::vector<float> dryL_, dryR_, sendL_, sendR_, earlyL_, earlyR_, tailL_, tailR_;
    // Per-block event scratch (allocated in prepare): kMaxBlockEvents plus room
    // for the injected mech-noise CC and the flood-panic AllNotesOff.
    std::vector<sapp::sounds::MidiEvent> blockEvents_;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    int lastQuality_ = -1;
    float lastVintageCents_ = -1.0f;
    int lastMechCc_ = -1;
    float lastRoomSize_ = -1.0f, lastRoomDecay_ = -1.0f;
};

} // namespace sapp::keys
