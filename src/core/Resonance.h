#pragma once
// Sympathetic resonance — the quiet shimmer of undamped strings when the
// sustain pedal is down. Lightweight model: a small bank of tuned feedback
// combs, one per resonating string, fed by the dry signal at low gain.
//
// A comb is claimed for a note when the pedal goes down (for keys already
// held) or on note-on while the pedal is down. Pedal-up releases every comb
// with a fast fade. Slight per-assignment detune (deterministic under the
// seed) keeps the bank from sounding like a static filter.
//
// Framework-independent, realtime-safe after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sapp::keys {

class SympatheticResonance {
public:
    static constexpr int kCombs = 12;

    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        // Longest string modelled: A0 (27.5 Hz) minus detune headroom.
        const size_t cap = size_t(sampleRate / 25.0) + 8;
        for (auto& c : combs_) {
            c.line.assign(cap, 0.0f);
            c.reset();
        }
        reseed(seed_);
    }

    void reseed(uint32_t seed)
    {
        seed_ = seed;
        rng_ = seed ^ 0x9E3779B9u;
        for (auto& c : combs_) c.reset();
        pedalDown_ = false;
        nextComb_ = 0;
    }

    void setPedal(bool down, const bool* heldNotes /*128 flags, may be null*/)
    {
        if (down == pedalDown_) return;
        pedalDown_ = down;
        if (down) {
            if (heldNotes != nullptr)
                for (int n = 0; n < 128; ++n)
                    if (heldNotes[n]) claim(n);
        } else {
            for (auto& c : combs_) c.releasing = true;
        }
    }

    void noteOn(int note)
    {
        if (pedalDown_) claim(note);
    }

    bool pedalDown() const noexcept { return pedalDown_; }

    // Adds the resonance layer into outL/outR. `level` 0..1 is the product
    // resonance parameter; inputs are the dry stereo bus.
    void process(const float* inL, const float* inR,
                 float* outL, float* outR, int frames, float level) noexcept
    {
        if (level <= 0.0001f) {
            // Keep lines decaying so a later level increase is seamless.
            level = 0.0f;
        }
        const float inGain = 0.055f;
        for (int f = 0; f < frames; ++f) {
            const float feed = 0.5f * (inL[f] + inR[f]) * inGain;
            float accL = 0.0f, accR = 0.0f;
            for (int i = 0; i < kCombs; ++i) {
                Comb& c = combs_[i];
                if (!c.active) continue;
                // Fractional delay read (linear — fine for a quiet layer).
                const int size = int(c.line.size());
                float pos = float(c.write) - c.delay;
                if (pos < 0.0f) pos += float(size);
                const int i0 = int(pos);
                const float frac = pos - float(i0);
                const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
                const float read = c.line[size_t(i0)] * (1.0f - frac) +
                                   c.line[size_t(i1)] * frac;

                // Feedback with in-loop damping (strings lose highs fast).
                c.damp += c.dampCoef * (read * c.feedback - c.damp);
                float v = c.damp + feed;
                // In-loop DC blocker: a feedback comb also resonates at 0 Hz,
                // and the damping lowpass would make that mode decay slowest.
                c.dc += 0.002f * (v - c.dc);
                v -= c.dc;

                // Release fade after pedal-up (~35 ms) then free the comb.
                if (c.releasing) {
                    c.gain *= releaseCoef_;
                    if (c.gain < 1.0e-4f) { c.free(); continue; }
                }
                c.line[size_t(c.write)] = v;
                if (++c.write >= size) c.write = 0;

                const float out = read * c.gain;
                accL += out * c.panL;
                accR += out * c.panR;
            }
            outL[f] += accL * level;
            outR[f] += accR * level;
        }
    }

private:
    struct Comb {
        std::vector<float> line;
        int write = 0;
        float delay = 100.0f;
        float feedback = 0.0f;
        float damp = 0.0f;
        float dc = 0.0f;
        float dampCoef = 0.3f;
        float gain = 0.0f;
        float panL = 1.0f, panR = 1.0f;
        int note = -1;
        bool active = false;
        bool releasing = false;

        void reset()
        {
            std::fill(line.begin(), line.end(), 0.0f);
            write = 0;
            damp = 0.0f;
            dc = 0.0f;
            free();
        }
        void free()
        {
            active = false;
            releasing = false;
            gain = 0.0f;
            note = -1;
        }
    };

    float nextRand01() noexcept
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return float(rng_ >> 8) * (1.0f / 16777216.0f);
    }

    void claim(int note)
    {
        // Real strings only: A0..C7 (above that, dampers don't exist but the
        // strings barely feed back either — skip to keep the bank musical).
        if (note < 21 || note > 96) return;
        for (const auto& c : combs_)
            if (c.active && c.note == note && !c.releasing) return;

        // Round-robin steal of the next slot.
        Comb& c = combs_[size_t(nextComb_)];
        nextComb_ = (nextComb_ + 1) % kCombs;

        const float detuneCents = (nextRand01() - 0.5f) * 5.0f;
        const double freq = 440.0 * std::pow(2.0, (double(note) - 69.0 +
                                                   double(detuneCents) * 0.01) / 12.0);
        c.delay = std::min(float(sampleRate_ / freq),
                           float(c.line.size()) - 4.0f);
        // Uniform T60 ≈ 2.2 s across the bank.
        c.feedback = std::pow(10.0f, -3.0f * c.delay / (2.2f * float(sampleRate_)));
        // Higher strings are darker in resonance (less feedback brightness).
        const float t = std::clamp((float(note) - 21.0f) / 75.0f, 0.0f, 1.0f);
        c.dampCoef = 0.55f - 0.30f * t;
        c.damp = 0.0f;
        c.dc = 0.0f;
        c.gain = 1.0f;
        // Gentle stereo spread by pitch class.
        const float pan = ((float(note % 12) / 11.0f) - 0.5f) * 0.7f;
        c.panL = std::cos((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f * 0.5f + 0.5f;
        c.panR = std::sin((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f * 0.5f + 0.5f;
        c.note = note;
        c.active = true;
        c.releasing = false;
    }

    std::array<Comb, kCombs> combs_;
    double sampleRate_ = 48000.0;
    uint32_t seed_ = 0x5A9F00D5, rng_ = 0;
    int nextComb_ = 0;
    bool pedalDown_ = false;
    // ~35 ms release at 48 kHz; recomputed cheaply enough to leave fixed.
    static constexpr float releaseCoef_ = 0.9994f;
};

} // namespace sapp::keys
