#include "KeysEngine.h"

#include <algorithm>
#include <cmath>

namespace sapp::keys {

using sapp::sounds::MidiEvent;

namespace {
inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

// One-pole smoothing coefficient for ~t milliseconds.
inline float smoothCoef(double sampleRate, float ms) noexcept
{
    return 1.0f - std::exp(-1.0f / (float(sampleRate) * ms * 0.001f));
}
} // namespace

KeysEngine::KeysEngine() = default;

void KeysEngine::prepare(double sampleRate, int maxBlockFrames)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockFrames;
    sampler_.prepare(sampleRate, maxBlockFrames);
    resonance_.prepare(sampleRate);
    early_.prepare(sampleRate);
    room_.prepare(sampleRate);

    const size_t n = size_t(maxBlockFrames);
    dryL_.assign(n, 0.0f); dryR_.assign(n, 0.0f);
    sendL_.assign(n, 0.0f); sendR_.assign(n, 0.0f);
    earlyL_.assign(n, 0.0f); earlyR_.assign(n, 0.0f);
    tailL_.assign(n, 0.0f); tailR_.assign(n, 0.0f);
    blockEvents_.assign(size_t(kEventScratchSlots), MidiEvent{});


    limGain_ = 1.0f;
    limiterGainUi_.store(1.0f, std::memory_order_relaxed);
    limReleaseCoef_ = 1.0f - std::exp(-1.0f / (float(sampleRate) * 0.15f));

    lpL_ = lpR_ = lidLpL_ = lidLpR_ = 0.0f;
    wowPhase_ = flutterPhase_ = 0.0f;
    liveDynamics_ = liveExpression_ = -1.0f;
    std::fill(std::begin(heldNotes_), std::end(heldNotes_), false);
    pedalDown_ = false;
    pedalDownUi_.store(false, std::memory_order_relaxed);
    lastQuality_ = -1;
    lastVintageCents_ = -1.0f;
    lastMechCc_ = -1;
    lastRoomSize_ = lastRoomDecay_ = -1.0f;
}

void KeysEngine::setInstrument(sapp::sounds::InstrumentPtr instrument)
{
    sampler_.setInstrument(std::move(instrument));
}
void KeysEngine::collectRetired() { sampler_.collectRetired(); }
sapp::sounds::InstrumentPtr KeysEngine::currentInstrument() const
{
    return sampler_.currentInstrument();
}

void KeysEngine::setParams(const KeysParams& params)
{
    const int inactive = 1 - paramIndex_.load(std::memory_order_acquire);
    paramSlots_[inactive] = params;
    paramIndex_.store(inactive, std::memory_order_release);
}

KeysParams KeysEngine::params() const
{
    return paramSlots_[paramIndex_.load(std::memory_order_acquire)];
}

void KeysEngine::resetSequences() { sampler_.resetSequences(); }

void KeysEngine::reseed(uint32_t seed)
{
    sampler_.reseed(seed);
    resonance_.reseed(seed);
}

int KeysEngine::velocityHistory(VelSample out[kVelHistory]) const noexcept
{
    const uint32_t write = velWrite_.load(std::memory_order_acquire);
    const int count = int(std::min<uint32_t>(write, kVelHistory));
    for (int i = 0; i < count; ++i) {
        const uint32_t slot = (write - uint32_t(count) + uint32_t(i)) % kVelHistory;
        const uint32_t packed = velRing_[slot].load(std::memory_order_relaxed);
        out[i].in = uint8_t(packed >> 8);
        out[i].out = uint8_t(packed & 0xFF);
    }
    return count;
}

void KeysEngine::applyQuality(const KeysParams& p) noexcept
{
    if (p.quality != lastQuality_) {
        lastQuality_ = p.quality;
        sampler_.setInterpolationQuality(p.quality == 0 ? 0 : 1);
    }
    // Vintage character: per-note random detune via the sampler hook.
    const float cents = 4.5f * p.vintage;
    if (cents != lastVintageCents_) {
        lastVintageCents_ = cents;
        sampler_.setRandomTuneCents(cents);
    }
    if (p.roomSize != lastRoomSize_ || p.roomDecay != lastRoomDecay_) {
        lastRoomSize_ = p.roomSize;
        lastRoomDecay_ = p.roomDecay;
        room_.setParams(p.roomSize, p.roomDecay);
    }
}

void KeysEngine::process(const MidiEvent* events, int eventCount,
                         float* outL, float* outR, int frames) noexcept
{
    // Every modeled imperfection is scaled by (1 − clean) HERE, once, before
    // anything reads a parameter — mech noise, vintage detune, wow/flutter and
    // the HF wear colour all come off the scaled block (sappkeys #3).
    const KeysParams p = applyClean(paramSlots_[paramIndex_.load(std::memory_order_acquire)]);
    applyQuality(p);

    // --- event policy -------------------------------------------------------
    // Rewrite note-on velocities through the touch/una-corda curve, track the
    // sustain pedal + held keys for the resonance bank, and drive the internal
    // mech-noise controller. CC1/CC11 ride the dynamics/expression trims.
    if (blockEvents_.size() < size_t(kEventScratchSlots)) {  // prepare() not run
        for (int f = 0; f < frames; ++f) { outL[f] = 0.0f; outR[f] = 0.0f; }
        return;
    }
    MidiEvent* const localEvents = blockEvents_.data();
    int localCount = 0;
    // One slot always stays free for the panic event below.
    const int capacity = kEventScratchSlots - 1;
    const int n = std::min(frames, maxBlock_);

    const int mechCc = int((1.0f - std::clamp(p.mechNoise, 0.0f, 1.0f)) * 127.0f + 0.5f);
    if (mechCc != lastMechCc_) {
        lastMechCc_ = mechCc;
        MidiEvent e;
        e.type = MidiEvent::Type::Controller;
        e.frame = 0;
        e.note = uint8_t(kMechNoiseInternalCc);
        e.value = uint8_t(mechCc);
        localEvents[localCount++] = e;
    }

    const int usableEvents = std::min(eventCount, kMaxBlockEvents);
    int consumed = 0;
    for (int i = 0; i < usableEvents; ++i) {
        if (localCount + 2 > capacity) break;   // cannot happen; never overrun
        ++consumed;
        MidiEvent e = events[i];
        switch (e.type) {
            case MidiEvent::Type::NoteOn:
                if (e.value > 0) {
                    const float shaped = shapeVelocity(float(e.value), p.touch, p.unaCorda);
                    const uint8_t outVel = uint8_t(std::clamp(int(shaped + 0.5f), 1, 127));
                    const uint32_t idx = velWrite_.load(std::memory_order_relaxed);
                    velRing_[idx % kVelHistory].store(uint32_t(e.value) << 8 | outVel,
                                                      std::memory_order_relaxed);
                    velWrite_.store(idx + 1, std::memory_order_release);
                    e.value = outVel;
                    if (e.note < 128)
                        heldNotes_[e.note] = true;
                    resonance_.noteOn(e.note);
                    break;
                }
                [[fallthrough]];  // note-on with velocity 0 is a note-off
            case MidiEvent::Type::NoteOff:
                if (e.note < 128)
                    heldNotes_[e.note] = false;
                break;
            case MidiEvent::Type::Controller:
                if (e.note == 1) liveDynamics_ = float(e.value) / 127.0f;
                else if (e.note == 11) liveExpression_ = float(e.value) / 127.0f;
                else if (e.note == 64) {
                    const bool down = e.value >= 64;
                    if (down != pedalDown_) {
                        pedalDown_ = down;
                        pedalDownUi_.store(down, std::memory_order_relaxed);
                        resonance_.setPedal(down, heldNotes_);
                    }
                } else if (e.note == kMechNoiseInternalCc) {
                    continue;  // reserved for the mech-noise policy above
                }
                break;
            case MidiEvent::Type::AllNotesOff:
            case MidiEvent::Type::AllSoundOff:
                std::fill(std::begin(heldNotes_), std::end(heldNotes_), false);
                break;
            default:
                break;
        }
        localEvents[localCount++] = e;
    }

    // MIDI flood: more events in one block than any performance produces (a
    // controller re-enumerating, a host feedback loop). Silently truncating
    // drops the note-OFFs — they sort after the note-ons — and leaves notes
    // stuck on forever. Fail toward silence instead: end everything.
    if (eventCount > consumed) {
        MidiEvent panic;
        panic.type = MidiEvent::Type::AllSoundOff;
        panic.frame = uint32_t(std::max(0, frames - 1));
        localEvents[localCount++] = panic;
        std::fill(std::begin(heldNotes_), std::end(heldNotes_), false);
    }

    const float dynamics = liveDynamics_ >= 0.0f ? liveDynamics_ : p.dynamics;
    const float expression = liveExpression_ >= 0.0f ? liveExpression_ : p.expression;

    // --- dry sampler render -------------------------------------------------
    std::fill(dryL_.begin(), dryL_.begin() + n, 0.0f);
    std::fill(dryR_.begin(), dryR_.begin() + n, 0.0f);
    sampler_.process(localEvents, localCount, dryL_.data(), dryR_.data(), n);
    framesRendered_.fetch_add(uint64_t(n), std::memory_order_release);

    // --- target gains -------------------------------------------------------
    // CC1 dynamics trim (gentle for keys): level −10 dB..0, brightness with it.
    const float dynGainTarget = dbToGain(-10.0f * (1.0f - dynamics));
    const float exprGainTarget = dbToGain(-40.0f * (1.0f - expression));

    // Tone filter: dynamics brightness × una-corda felt × vintage tape HF.
    const float bright = 0.35f + 0.65f * dynamics;
    float cutoffHz = 900.0f * std::pow(18000.0f / 900.0f, std::pow(bright, 0.85f));
    cutoffHz *= std::pow(0.50f, p.unaCorda);
    cutoffHz *= std::pow(0.60f, p.vintage);
    cutoffHz = std::clamp(cutoffHz, 200.0f, 19000.0f);
    const float cutoffCoefTarget =
        1.0f - std::exp(-6.2831853f * cutoffHz / float(sampleRate_));

    // Lid: closing pulls highs down (shelf) and narrows the image.
    const float lid = std::clamp(p.lid, 0.0f, 1.0f);
    const float lidHiTarget = 0.32f + 0.68f * lid;
    const float widthTarget = std::clamp(p.width, 0.0f, 2.0f) * (0.55f + 0.45f * lid);

    const float roomTarget = std::clamp(p.roomLevel, 0.0f, 1.0f);
    const float masterTarget = dbToGain(p.masterGainDb);

    // Drive: transparent at 0 (full bypass through the mix), gentle tanh above.
    const float drive = std::clamp(p.drive, 0.0f, 1.0f);
    const float driveMixTarget = std::min(1.0f, drive * 1.4f);
    const float drivePre = 1.0f + 6.0f * drive;
    const float driveMakeup = 1.0f + 1.2f * drive;

    // Lid shelf crossover (fixed frequency).
    const float lidCoef = 1.0f - std::exp(-6.2831853f * 2400.0f / float(sampleRate_));

    // Vintage wow & flutter (deterministic: phases reset at prepare).
    const float wowInc = float(2.0 * 3.14159265 * 0.9 / sampleRate_);
    const float flutterInc = float(2.0 * 3.14159265 * 6.1 / sampleRate_);
    const float wowDepth = 0.010f * p.vintage;
    const float flutterDepth = 0.004f * p.vintage;

    const float smFast = smoothCoef(sampleRate_, 12.0f);
    const float smSlow = smoothCoef(sampleRate_, 40.0f);

    // --- per-sample dry chain ----------------------------------------------
    for (int f = 0; f < n; ++f) {
        smDynGain_ += smFast * (dynGainTarget - smDynGain_);
        smExprGain_ += smFast * (exprGainTarget - smExprGain_);
        smCutoffCoef_ += smSlow * (cutoffCoefTarget - smCutoffCoef_);
        smLidHi_ += smSlow * (lidHiTarget - smLidHi_);
        smWidth_ += smSlow * (widthTarget - smWidth_);
        smRoom_ += smSlow * (roomTarget - smRoom_);
        smMaster_ += smSlow * (masterTarget - smMaster_);
        smDriveMix_ += smSlow * (driveMixTarget - smDriveMix_);

        float l = dryL_[size_t(f)];
        float r = dryR_[size_t(f)];

        // Tone filter (dynamics brightness, una corda, vintage HF).
        lpL_ += smCutoffCoef_ * (l - lpL_);
        lpR_ += smCutoffCoef_ * (r - lpR_);
        l = lpL_;
        r = lpR_;

        // Lid shelf: keep lows, scale highs.
        lidLpL_ += lidCoef * (l - lidLpL_);
        lidLpR_ += lidCoef * (r - lidLpR_);
        l = lidLpL_ + (l - lidLpL_) * smLidHi_;
        r = lidLpR_ + (r - lidLpR_) * smLidHi_;

        // Width (mid/side).
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * smWidth_;
        l = mid + side;
        r = mid - side;

        float gain = smDynGain_ * smExprGain_;
        if (wowDepth > 0.0f) {
            wowPhase_ += wowInc;
            if (wowPhase_ > 6.2831853f) wowPhase_ -= 6.2831853f;
            flutterPhase_ += flutterInc;
            if (flutterPhase_ > 6.2831853f) flutterPhase_ -= 6.2831853f;
            gain *= 1.0f + wowDepth * std::sin(wowPhase_) +
                    flutterDepth * std::sin(flutterPhase_);
        }
        l *= gain;
        r *= gain;

        // Gentle drive (EPs): blended tanh, bypassed at drive 0.
        if (smDriveMix_ > 0.0005f) {
            const float satL = std::tanh(drivePre * l) / drivePre * driveMakeup;
            const float satR = std::tanh(drivePre * r) / drivePre * driveMakeup;
            l += smDriveMix_ * (satL - l);
            r += smDriveMix_ * (satR - r);
        }

        dryL_[size_t(f)] = l;
        dryR_[size_t(f)] = r;
    }

    // --- sympathetic resonance (pedal-down string shimmer) ------------------
    resonance_.process(dryL_.data(), dryR_.data(), dryL_.data(), dryR_.data(), n,
                       std::clamp(p.resonance, 0.0f, 1.0f));

    // --- room ---------------------------------------------------------------
    for (int f = 0; f < n; ++f) {
        sendL_[size_t(f)] = dryL_[size_t(f)];
        sendR_[size_t(f)] = dryR_[size_t(f)];
    }
    early_.process(sendL_.data(), sendR_.data(), earlyL_.data(), earlyR_.data(), n);
    // The tail is fed by direct + early reflections (coherent small space).
    for (int f = 0; f < n; ++f) {
        sendL_[size_t(f)] = sendL_[size_t(f)] * 0.85f + earlyL_[size_t(f)] * 0.5f;
        sendR_[size_t(f)] = sendR_[size_t(f)] * 0.85f + earlyR_[size_t(f)] * 0.5f;
    }
    room_.process(sendL_.data(), sendR_.data(), tailL_.data(), tailR_.data(), n);

    for (int f = 0; f < n; ++f) {
        outL[f] = (dryL_[size_t(f)] +
                   (earlyL_[size_t(f)] * 0.8f + tailL_[size_t(f)]) * smRoom_) * smMaster_;
        outR[f] = (dryR_[size_t(f)] +
                   (earlyR_[size_t(f)] * 0.8f + tailR_[size_t(f)]) * smRoom_) * smMaster_;
    }
    for (int f = n; f < frames; ++f) { outL[f] = 0.0f; outR[f] = 0.0f; }

    limitAndGuard(outL, outR, frames, p.limiter);
}

void KeysEngine::limitAndGuard(float* outL, float* outR, int frames, bool enabled) noexcept
{
    if (enabled) {
        // Peak-accurate limiting. The whole block is already rendered, so the
        // gain is chosen from the block's own peak and applied from its first
        // sample: no sample can slip through above the ceiling, and there is
        // no added latency. Loud material is turned DOWN, not squared off.
        float peak = 0.0f;
        for (int f = 0; f < frames; ++f) {
            const float l = std::abs(outL[f]);
            const float r = std::abs(outR[f]);
            if (std::isfinite(l) && l > peak) peak = l;
            if (std::isfinite(r) && r > peak) peak = r;
        }
        const float target = peak > kSafetyCeiling ? kSafetyCeiling / peak : 1.0f;
        if (target < limGain_) limGain_ = target;   // instant attack
        for (int f = 0; f < frames; ++f) {
            limGain_ += limReleaseCoef_ * (target - limGain_);
            if (limGain_ > target) limGain_ = target;   // never above the block's
            outL[f] *= limGain_;
            outR[f] *= limGain_;
        }
        limiterGainUi_.store(limGain_, std::memory_order_relaxed);
    } else {
        limGain_ = 1.0f;
        limiterGainUi_.store(1.0f, std::memory_order_relaxed);
    }

    // Unconditional guard. Non-finite audio never leaves the plugin, and the
    // output is bounded even with the limiter switched off — a host, a rack or
    // an interface would clip it anyway, and full-scale is the safety line.
    bool sawNonFinite = false;
    for (int f = 0; f < frames; ++f) {
        float l = outL[f], r = outR[f];
        if (!std::isfinite(l)) { l = 0.0f; sawNonFinite = true; }
        if (!std::isfinite(r)) { r = 0.0f; sawNonFinite = true; }
        outL[f] = std::clamp(l, -kOutputBound, kOutputBound);
        outR[f] = std::clamp(r, -kOutputBound, kOutputBound);
    }
    if (sawNonFinite) scrubState();
}

void KeysEngine::scrubState() noexcept
{
    // A NaN parked in a feedback path would otherwise poison every later block.
    // Everything here is std::fill / scalar stores on already-sized buffers.
    lpL_ = lpR_ = lidLpL_ = lidLpR_ = 0.0f;
    smDynGain_ = smExprGain_ = 1.0f;
    smMaster_ = 1.0f;
    limGain_ = 1.0f;
    std::fill(dryL_.begin(), dryL_.end(), 0.0f);
    std::fill(dryR_.begin(), dryR_.end(), 0.0f);
    std::fill(sendL_.begin(), sendL_.end(), 0.0f);
    std::fill(sendR_.begin(), sendR_.end(), 0.0f);
    std::fill(earlyL_.begin(), earlyL_.end(), 0.0f);
    std::fill(earlyR_.begin(), earlyR_.end(), 0.0f);
    std::fill(tailL_.begin(), tailL_.end(), 0.0f);
    std::fill(tailR_.begin(), tailR_.end(), 0.0f);
    resonance_.clear();
    early_.clear();
    room_.clear();
}

} // namespace sapp::keys
