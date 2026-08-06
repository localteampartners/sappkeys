#pragma once
// SappKeys room: a small, intimate space — short early reflections plus a
// compact 6-line FDN tail. Deliberately NOT a concert hall: short delays,
// fast decay range (0.2–2.5 s), light modulation to avoid metallic ring.
// Framework-independent, realtime-safe after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sapp::keys {

// ------------------------------------------------------------ early taps ---
// Close-mic room cues: first reflections inside ~20 ms, mild HF absorption.
class RoomEarly {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        buffer_.assign(size_t(sampleRate * 0.06) + 4, 0.0f);
        writePos_ = 0;
        lpL_ = lpR_ = 0.0f;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames)
    {
        static constexpr float tapMs[6] = {3.1f, 5.9f, 8.3f, 11.7f, 15.1f, 19.7f};
        static constexpr float tapGain[6] = {0.68f, 0.55f, 0.44f, 0.34f, 0.26f, 0.19f};
        constexpr float dampCoef = 0.55f;

        const int size = int(buffer_.size());
        for (int f = 0; f < frames; ++f) {
            buffer_[size_t(writePos_)] = 0.5f * (inL[f] + inR[f]);

            float l = 0.0f, r = 0.0f;
            for (int t = 0; t < 6; ++t) {
                const float delay = float(tapMs[t] * 0.001 * sampleRate_);
                int idx = writePos_ - int(delay);
                while (idx < 0) idx += size;
                const float v = buffer_[size_t(idx)] * tapGain[t];
                if (t % 2 == 0) { l += v; r += v * 0.62f; }
                else            { r += v; l += v * 0.62f; }
            }
            lpL_ += dampCoef * (l - lpL_);
            lpR_ += dampCoef * (r - lpR_);
            outL[f] = lpL_ * 0.5f;
            outR[f] = lpR_ * 0.5f;

            if (++writePos_ >= size) writePos_ = 0;
        }
    }

private:
    std::vector<float> buffer_;
    int writePos_ = 0;
    double sampleRate_ = 48000.0;
    float lpL_ = 0.0f, lpR_ = 0.0f;
};

// ------------------------------------------------------------ small room ---
class SmallRoom {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        // Mutually prime short delays (ms) — dense but small-sounding.
        static constexpr float baseMs[kLines] = {13.7f, 17.9f, 23.3f, 29.1f, 35.9f, 43.7f};
        for (int i = 0; i < kLines; ++i) {
            baseSamples_[i] = float(baseMs[i] * 0.001 * sampleRate);
            const size_t cap = size_t(baseSamples_[i] * 1.6f) + 64;
            lines_[i].assign(cap, 0.0f);
            writePos_[i] = 0;
            damp_[i] = 0.0f;
            lfoPhase_[i] = float(i) * 1.047f;
        }
        inDiffL_.prepare(sampleRate, 2.9f, 0.55f);
        inDiffR_.prepare(sampleRate, 2.1f, 0.55f);
        update();
    }

    void setParams(float size, float decaySeconds)
    {
        size_ = std::clamp(size, 0.6f, 1.4f);
        decay_ = std::clamp(decaySeconds, 0.2f, 2.5f);
        update();
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames)
    {
        for (int f = 0; f < frames; ++f) {
            const float inMono = 0.4f * (inDiffL_.tick(inL[f]) + inDiffR_.tick(inR[f]));

            float read[kLines];
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i) {
                lfoPhase_[i] += lfoInc_[i];
                if (lfoPhase_[i] > 6.2831853f) lfoPhase_[i] -= 6.2831853f;
                const float mod = std::sin(lfoPhase_[i]) * 1.4f;
                const float delay = delaySamples_[i] + mod;
                const int size = int(lines_[i].size());
                float pos = float(writePos_[i]) - delay;
                while (pos < 0.0f) pos += float(size);
                const int i0 = int(pos);
                const float frac = pos - float(i0);
                const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
                read[i] = lines_[i][size_t(i0)] * (1.0f - frac) + lines_[i][size_t(i1)] * frac;
                sum += read[i];
            }

            // Householder feedback keeps the loop lossless before decay gain.
            const float k = 2.0f / float(kLines);
            for (int i = 0; i < kLines; ++i) {
                float v = feedback_[i] * (read[i] - k * sum) + inMono;
                damp_[i] += kDampCoef * (v - damp_[i]);
                v = damp_[i];
                lines_[i][size_t(writePos_[i])] = v;
                if (++writePos_[i] >= int(lines_[i].size())) writePos_[i] = 0;
            }

            outL[f] = (read[0] - read[2] + read[4]) * 0.42f;
            outR[f] = (read[1] - read[3] + read[5]) * 0.42f;
        }
    }

private:
    static constexpr int kLines = 6;
    static constexpr float kDampCoef = 0.62f;  // fixed HF absorption

    struct Allpass {
        void prepare(double sampleRate, float ms, float g)
        {
            buffer.assign(size_t(ms * 0.001 * sampleRate) + 2, 0.0f);
            pos = 0;
            gain = g;
        }
        float tick(float x)
        {
            const float d = buffer[size_t(pos)];
            const float y = -gain * x + d;
            buffer[size_t(pos)] = x + gain * y;
            if (++pos >= int(buffer.size())) pos = 0;
            return y;
        }
        std::vector<float> buffer;
        int pos = 0;
        float gain = 0.55f;
    };

    void update()
    {
        for (int i = 0; i < kLines; ++i) {
            delaySamples_[i] = std::min(baseSamples_[i] * size_,
                                        float(lines_[i].size()) - 8.0f);
            feedback_[i] = std::pow(10.0f, -3.0f * delaySamples_[i] /
                                                (decay_ * float(sampleRate_)));
            lfoInc_[i] = float((0.41 + 0.17 * i) * 2.0 * 3.14159265 / sampleRate_);
        }
    }

    double sampleRate_ = 48000.0;
    std::array<std::vector<float>, kLines> lines_;
    float baseSamples_[kLines] = {};
    float delaySamples_[kLines] = {};
    float feedback_[kLines] = {};
    float damp_[kLines] = {};
    float lfoPhase_[kLines] = {};
    float lfoInc_[kLines] = {};
    int writePos_[kLines] = {};
    Allpass inDiffL_, inDiffR_;

    float size_ = 1.0f, decay_ = 0.9f;
};

} // namespace sapp::keys
