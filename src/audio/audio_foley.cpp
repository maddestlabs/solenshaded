#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "solenshaded/audio_foley.hpp"
#include <algorithm>
#include <cmath>

namespace solenshaded {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

inline AudioFoleyEngine::BiquadCoeffs makeBandpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float a0 = 1.0f + alpha;
    return {
        alpha / a0,
        0.0f,
        -alpha / a0,
        (-2.0f * std::cos(w0)) / a0,
        (1.0f - alpha) / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeHighpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float cosw = std::cos(w0);
    float a0 = 1.0f + alpha;
    float b0 = (1.0f + cosw) * 0.5f;
    return {
        b0 / a0,
        -(1.0f + cosw) / a0,
        b0 / a0,
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeLowpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float cosw = std::cos(w0);
    float a0 = 1.0f + alpha;
    float b1 = 1.0f - cosw;
    float b0 = b1 * 0.5f;
    return {
        b0 / a0,
        b1 / a0,
        b0 / a0,
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    };
}

void miniaudioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioFoleyEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixSamples(static_cast<float*>(pOutput), frameCount, pDevice->sampleRate);
    }
}
} // namespace

AudioFoleyEngine::AudioFoleyEngine() = default;

AudioFoleyEngine::~AudioFoleyEngine() {
    shutdown();
}

bool AudioFoleyEngine::init(bool enableHardwareDevice) {
    if (initialized_) return true;
    if (!enableHardwareDevice) {
        initialized_ = true;
        return true;
    }

    auto* device = new ma_device{};
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = miniaudioCallback;
    config.pUserData         = this;

    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        delete device;
        initialized_ = true;
        return false;
    }

    ma_device_start(device);
    maDevicePtr_ = device;
    initialized_ = true;
    return true;
}

void AudioFoleyEngine::shutdown() {
    if (maDevicePtr_) {
        auto* device = static_cast<ma_device*>(maDevicePtr_);
        ma_device_uninit(device);
        delete device;
        maDevicePtr_ = nullptr;
    }
    initialized_ = false;
}

float AudioFoleyEngine::nextNoise() noexcept {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return static_cast<float>(static_cast<int32_t>(rngState_)) * (1.0f / 2147483648.0f);
}

void AudioFoleyEngine::triggerSound(uint16_t soundId, float intensity) {
    uint32_t w = writeIdx_.load(std::memory_order_relaxed);
    uint32_t r = readIdx_.load(std::memory_order_acquire);
    if (w - r < kQueueCapacity) {
        cmdQueue_[w % kQueueCapacity] = {soundId, intensity};
        writeIdx_.store(w + 1, std::memory_order_release);
    }
}

void AudioFoleyEngine::allocateVoice(const Voice& v) noexcept {
    for (auto& slot : voices_) {
        if (!slot.active) {
            slot = v;
            return;
        }
    }
    // All voices busy: steal the voice closest to finishing its duration
    size_t oldestIdx = 0;
    float maxNormAge = -1.0f;
    for (size_t i = 0; i < voices_.size(); ++i) {
        float normAge = voices_[i].ageSec / std::max(0.001f, voices_[i].durationSec);
        if (normAge > maxNormAge) {
            maxNormAge = normAge;
            oldestIdx = i;
        }
    }
    voices_[oldestIdx] = v;
}

void AudioFoleyEngine::handleTrigger(uint16_t soundId, float intensity, float sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;

    // Resolve high-level actions to physical profiles
    if (soundId == FoleySound::DeckShuffle) {
        soundId = (shuffleStyle_.load(std::memory_order_relaxed) == ShuffleStyle::Loud)
                      ? FoleySound::ShuffleLoud
                      : FoleySound::ShuffleSoft;
    } else if (soundId == FoleySound::CardSlide) {
        // Natural human dealing alternates between crisp flicks and soft swishes
        soundId = ((dealAlternator_++ % 3) == 0) ? FoleySound::CardDealAlt : FoleySound::CardDealSingle;
    } else if (soundId == FoleySound::CardSnap) {
        soundId = FoleySound::CardPlace;
    }

    float jitter = 1.0f + (nextNoise() * 0.025f);

    if (soundId == FoleySound::ShuffleSoft) {
        // Choreographed 5-stage shuffle sequence directly reproducing shuffle-soft.wav / shuffle-part-1..5:
        // Stage 1 (Part 1): Light airy parchment brush lift
        Voice v1{};
        v1.active = true;
        v1.soundId = FoleySound::CardDealAlt;
        v1.delaySec = 0.040f;
        v1.durationSec = 0.035f;
        v1.intensity = intensity * 0.38f;
        v1.jitter = 1.0f + (nextNoise() * 0.025f);
        v1.snapFilter.c = makeHighpass(5200.0f * v1.jitter, 1.0f, sr);
        v1.slideFilter1.c = makeBandpass(2200.0f * v1.jitter, 1.0f, sr);
        v1.slideFilter2.c = makeBandpass(3600.0f * v1.jitter, 1.4f, sr);
        allocateVoice(v1);

        // Stage 2 (Part 2): Soft card body drop / felt contact
        Voice v2{};
        v2.active = true;
        v2.soundId = FoleySound::CardDealSingle;
        v2.delaySec = 0.082f;
        v2.durationSec = 0.040f;
        v2.intensity = intensity * 0.30f;
        v2.jitter = 1.0f + (nextNoise() * 0.025f);
        v2.fBody = 260.0f * v2.jitter;
        v2.thudDelaySec = 0.005f;
        v2.snapFilter.c = makeHighpass(4800.0f * v2.jitter, 1.0f, sr);
        v2.slideFilter1.c = makeBandpass(1200.0f * v2.jitter, 0.9f, sr);
        allocateVoice(v2);

        // Stage 3 (Part 3): Crisp deal flick / parchment swish
        Voice v3{};
        v3.active = true;
        v3.soundId = FoleySound::CardDealSingle;
        v3.delaySec = 0.128f;
        v3.durationSec = 0.038f;
        v3.intensity = intensity * 0.68f;
        v3.jitter = 1.0f + (nextNoise() * 0.025f);
        v3.fBody = 790.0f * v3.jitter;
        v3.thudDelaySec = 0.012f;
        v3.snapFilter.c = makeHighpass(4500.0f * v3.jitter, 1.2f, sr);
        v3.slideFilter1.c = makeBandpass(3100.0f * v3.jitter, 1.1f, sr);
        allocateVoice(v3);

        // Stage 4 (Part 4): Gentle interleaf gather
        Voice v4{};
        v4.active = true;
        v4.soundId = FoleySound::CardDealAlt;
        v4.delaySec = 0.230f;
        v4.durationSec = 0.036f;
        v4.intensity = intensity * 0.42f;
        v4.jitter = 1.0f + (nextNoise() * 0.025f);
        v4.fBody = 790.0f * v4.jitter;
        v4.slideFilter1.c = makeBandpass(1800.0f * v4.jitter, 0.9f, sr);
        v4.slideFilter2.c = makeBandpass(2800.0f * v4.jitter, 1.2f, sr);
        allocateVoice(v4);

        // Stage 5 (Part 5): Final resolving placement / square-up tap
        Voice v5{};
        v5.active = true;
        v5.soundId = FoleySound::CardPlace;
        v5.delaySec = 0.325f;
        v5.durationSec = 0.072f;
        v5.intensity = intensity * 0.95f;
        v5.jitter = 1.0f + (nextNoise() * 0.025f);
        v5.fBody = 775.0f * v5.jitter;
        v5.thudDelaySec = 0.0f;
        v5.snapFilter.c = makeHighpass(4200.0f * v5.jitter, 1.3f, sr);
        v5.bodyFilter.c = makeBandpass(1200.0f * v5.jitter, 1.2f, sr);
        allocateVoice(v5);
        return;
    }

    if (soundId == FoleySound::ShuffleLoud) {
        // Choreographed energetic riffle with sharp edge whip and loud resolving snap
        Voice v1{};
        v1.active = true;
        v1.soundId = FoleySound::CardDealAlt;
        v1.delaySec = 0.035f;
        v1.durationSec = 0.035f;
        v1.intensity = intensity * 0.50f;
        v1.jitter = 1.0f + (nextNoise() * 0.025f);
        v1.snapFilter.c = makeHighpass(5500.0f * v1.jitter, 1.1f, sr);
        v1.slideFilter1.c = makeBandpass(2400.0f * v1.jitter, 1.0f, sr);
        allocateVoice(v1);

        Voice v2{};
        v2.active = true;
        v2.soundId = FoleySound::CardDealSingle;
        v2.delaySec = 0.075f;
        v2.durationSec = 0.038f;
        v2.intensity = intensity * 0.45f;
        v2.jitter = 1.0f + (nextNoise() * 0.025f);
        v2.fBody = 280.0f * v2.jitter;
        v2.snapFilter.c = makeHighpass(4500.0f * v2.jitter, 1.2f, sr);
        v2.slideFilter1.c = makeBandpass(1400.0f * v2.jitter, 1.0f, sr);
        allocateVoice(v2);

        Voice v3{};
        v3.active = true;
        v3.soundId = FoleySound::CardDealSingle;
        v3.delaySec = 0.120f;
        v3.durationSec = 0.040f;
        v3.intensity = intensity * 0.88f;
        v3.jitter = 1.0f + (nextNoise() * 0.025f);
        v3.fBody = 820.0f * v3.jitter;
        v3.snapFilter.c = makeHighpass(4200.0f * v3.jitter, 1.4f, sr);
        v3.slideFilter1.c = makeBandpass(2800.0f * v3.jitter, 1.1f, sr);
        allocateVoice(v3);

        Voice v4{};
        v4.active = true;
        v4.soundId = FoleySound::CardDealAlt;
        v4.delaySec = 0.210f;
        v4.durationSec = 0.038f;
        v4.intensity = intensity * 0.65f;
        v4.jitter = 1.0f + (nextNoise() * 0.025f);
        v4.slideFilter1.c = makeBandpass(2000.0f * v4.jitter, 1.0f, sr);
        v4.slideFilter2.c = makeBandpass(3200.0f * v4.jitter, 1.3f, sr);
        allocateVoice(v4);

        Voice v5{};
        v5.active = true;
        v5.soundId = FoleySound::CardPlace;
        v5.delaySec = 0.295f;
        v5.durationSec = 0.075f;
        v5.intensity = intensity * 1.05f;
        v5.jitter = 1.0f + (nextNoise() * 0.025f);
        v5.fBody = 820.0f * v5.jitter;
        v5.snapFilter.c = makeHighpass(4500.0f * v5.jitter, 1.4f, sr);
        v5.bodyFilter.c = makeBandpass(1400.0f * v5.jitter, 1.3f, sr);
        allocateVoice(v5);
        return;
    }

    if (soundId == FoleySound::DealSeries) {
        // Rapid rhythmic deal sequence matching deal-series.wav
        constexpr int kStrokeCount = 8;
        for (int i = 0; i < kStrokeCount; ++i) {
            float tDelay = static_cast<float>(i) * 0.130f + (nextNoise() * 0.010f);
            tDelay = std::max(0.01f, tDelay);
            float strokeAmp = intensity * (0.65f + (nextNoise() * 0.15f));
            uint16_t subType = (i % 2 == 0) ? FoleySound::CardDealSingle : FoleySound::CardDealAlt;

            Voice v{};
            v.active = true;
            v.soundId = subType;
            v.delaySec = tDelay;
            v.intensity = strokeAmp;
            v.jitter = 1.0f + (nextNoise() * 0.025f);

            if (subType == FoleySound::CardDealSingle) {
                v.durationSec = 0.046f;
                v.fBody = 320.0f * v.jitter;
                v.thudDelaySec = 0.009f;
                v.snapFilter.c = makeHighpass(5500.0f * v.jitter, 1.1f, sr);
                v.slideFilter1.c = makeBandpass(2200.0f * v.jitter, 1.0f, sr);
            } else {
                v.durationSec = 0.042f;
                v.fBody = 300.0f * v.jitter;
                v.thudDelaySec = 0.012f;
                v.snapFilter.c = makeHighpass(5200.0f * v.jitter, 1.0f, sr);
                v.slideFilter1.c = makeBandpass(1800.0f * v.jitter, 0.85f, sr);
                v.slideFilter2.c = makeBandpass(3200.0f * v.jitter, 1.25f, sr);
            }
            allocateVoice(v);
        }
        return;
    }

    Voice v{};
    v.active = true;
    v.soundId = soundId;
    v.intensity = intensity;
    v.jitter = jitter;

    switch (soundId) {
        case FoleySound::CardDealSingle:
            // single.wav: Crisp edge flick followed after ~9ms by low table arrival
            v.durationSec = 0.046f;
            v.fBody = 320.0f * jitter;
            v.thudDelaySec = 0.009f;
            v.snapFilter.c = makeHighpass(5500.0f * jitter, 1.1f, sr);
            v.slideFilter1.c = makeBandpass(2200.0f * jitter, 1.0f, sr);
            break;

        case FoleySound::CardDealAlt:
            // single-alternative.wav: Airy swish/brush deal with dominant mid friction
            v.durationSec = 0.042f;
            v.fBody = 300.0f * jitter;
            v.thudDelaySec = 0.012f;
            v.snapFilter.c = makeHighpass(5200.0f * jitter, 1.0f, sr);
            v.slideFilter1.c = makeBandpass(1800.0f * jitter, 0.85f, sr);
            v.slideFilter2.c = makeBandpass(3200.0f * jitter, 1.25f, sr);
            break;

        case FoleySound::CardPlace:
            // place.wav: Flat tabletop placement with coincident edge snap and felt thud
            v.durationSec = 0.048f;
            v.thudDelaySec = 0.0f; // Immediate impact
            v.snapFilter.c = makeHighpass(5200.0f * jitter, 1.3f, sr);
            v.bodyFilter.c = makeBandpass(1800.0f * jitter, 1.2f, sr);
            break;

        case FoleySound::WoodClick:
            // Hardwood UI button tap
            v.durationSec = 0.038f;
            v.fBody = 140.0f * jitter;
            v.snapFilter.c = makeHighpass(1600.0f * jitter, 1.2f, sr);
            v.bodyFilter.c = makeBandpass(420.0f * jitter, 1.8f, sr);
            break;

        case FoleySound::FoundationChime:
            // Pure harmonic chime triad: 528Hz root (Solfeggio MI) + 792Hz (5th) + 1056Hz (octave)
            v.durationSec = 0.420f;
            v.fBody = 528.0f * jitter;
            break;

        case FoleySound::SpectreWhisper:
        default:
            // Ethereal ZAngband Nether-Sight shimmer
            v.durationSec = 0.350f;
            v.fBody = 396.0f * jitter;
            v.slideFilter1.c = makeBandpass(880.0f * jitter, 2.5f, sr);
            break;
    }

    allocateVoice(v);
}

void AudioFoleyEngine::mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? static_cast<float>(sampleRate) : 48000.0f;
    const float dt = 1.0f / sr;

    // Process queued triggers from the main thread
    uint32_t r = readIdx_.load(std::memory_order_relaxed);
    uint32_t w = writeIdx_.load(std::memory_order_acquire);
    while (r != w) {
        const auto& cmd = cmdQueue_[r % kQueueCapacity];
        handleTrigger(cmd.soundId, cmd.intensity, sr);
        ++r;
    }
    readIdx_.store(r, std::memory_order_release);

    if (muted_.load(std::memory_order_relaxed)) {
        std::fill(outInterleavedStereo, outInterleavedStereo + frameCount * 2, 0.0f);
        return;
    }
    const float masterVol = masterVolume_.load(std::memory_order_relaxed);

    for (uint32_t f = 0; f < frameCount; ++f) {
        float mix = 0.0f;

        for (auto& v : voices_) {
            if (!v.active) continue;

            if (v.delaySec > 0.0f) {
                v.delaySec -= dt;
                continue;
            }

            v.ageSec += dt;
            if (v.ageSec >= v.durationSec) {
                v.active = false;
                continue;
            }

            const float normAge = v.ageSec / v.durationSec;
            float sample = 0.0f;
            const float noise = nextNoise();

            switch (v.soundId) {
                case FoleySound::CardDealSingle: {
                    // Edge snap flick (fast exponential spike)
                    float snapEnv = std::exp(-110.0f * normAge) * (normAge * 30.0f);
                    float snap = v.snapFilter.process(noise) * snapEnv * 0.40f;

                    // Parchment friction brush
                    float slideEnv = std::pow(normAge, 1.1f) * std::exp(-22.0f * normAge) * 15.0f;
                    float slide = v.slideFilter1.process(noise) * slideEnv * 0.28f;

                    // Cardboard modal bending (320 Hz / 790 Hz)
                    v.phaseBody += v.fBody * dt;
                    if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;
                    float cardEnv = std::exp(-16.0f * normAge) * (normAge * 8.0f);
                    float card = std::sin(v.phaseBody * kTwoPi) * cardEnv * 0.15f;

                    // Damped table arrival landing (~9ms flight delay)
                    float thud = 0.0f;
                    if (v.ageSec >= v.thudDelaySec) {
                        float normThud = (v.ageSec - v.thudDelaySec) / (v.durationSec - v.thudDelaySec);
                        float thudEnv = std::exp(-14.0f * normThud) * (normThud * 10.0f);
                        float thudFreq = (95.0f * std::exp(-8.0f * normThud) + 45.0f) * v.jitter;
                        v.phaseThud += thudFreq * dt;
                        if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;
                        thud = std::sin(v.phaseThud * kTwoPi) * thudEnv * 0.58f;
                    }

                    sample = (snap + slide + card + thud) * v.intensity * 0.48f;
                    break;
                }

                case FoleySound::CardDealAlt: {
                    // Airy swish deal
                    float slideEnv = std::pow(normAge, 0.8f) * std::exp(-16.0f * normAge) * 9.0f;
                    float s1 = v.slideFilter1.process(noise);
                    float s2 = v.slideFilter2.process(noise);
                    float slide = (s1 * 0.55f + s2 * 0.45f) * slideEnv * 0.65f;

                    float snapEnv = std::exp(-130.0f * normAge) * (normAge * 22.0f);
                    float snap = v.snapFilter.process(noise) * snapEnv * 0.18f;

                    v.phaseBody += v.fBody * dt;
                    if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;
                    float card = std::sin(v.phaseBody * kTwoPi) * std::exp(-22.0f * normAge) * (normAge * 6.0f) * 0.12f;

                    float thud = 0.0f;
                    if (v.ageSec >= v.thudDelaySec) {
                        float normThud = (v.ageSec - v.thudDelaySec) / (v.durationSec - v.thudDelaySec);
                        float thudEnv = std::exp(-18.0f * normThud) * (normThud * 5.0f);
                        v.phaseThud += 85.0f * v.jitter * dt;
                        if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;
                        thud = std::sin(v.phaseThud * kTwoPi) * thudEnv * 0.15f;
                    }

                    sample = (snap + slide + card + thud) * v.intensity * 0.44f;
                    break;
                }

                case FoleySound::CardPlace: {
                    // Coincident crisp placement snap + tabletop felt slap
                    float snapEnv = std::exp(-95.0f * normAge) * (normAge * 30.0f);
                    float snap = v.snapFilter.process(noise) * snapEnv * 0.70f;

                    float bodyEnv = std::exp(-45.0f * normAge) * (normAge * 15.0f);
                    float body = v.bodyFilter.process(noise) * bodyEnv * 0.45f;

                    float thudEnv = std::exp(-18.0f * normAge) * (normAge * 8.0f);
                    float thudFreq = (110.0f * std::exp(-14.0f * normAge) + 60.0f) * v.jitter;
                    v.phaseThud += thudFreq * dt;
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;
                    float thud = std::sin(v.phaseThud * kTwoPi) * thudEnv * 0.22f;

                    sample = (snap + body + thud) * v.intensity * 0.46f;
                    break;
                }

                case FoleySound::WoodClick: {
                    float clickEnv = std::exp(-120.0f * normAge) * (normAge * 25.0f);
                    float click = v.snapFilter.process(noise) * clickEnv * 0.60f;

                    float woodBodyEnv = std::exp(-35.0f * normAge) * (normAge * 12.0f);
                    float woodCavity = v.bodyFilter.process(noise) * woodBodyEnv * 0.35f;

                    v.phaseThud += ((140.0f * std::exp(-16.0f * normAge) + 60.0f) * v.jitter) * dt;
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;
                    float woodThump = std::sin(v.phaseThud * kTwoPi) * std::exp(-18.0f * normAge) * 0.30f;

                    sample = (click + woodCavity + woodThump) * v.intensity * 0.45f;
                    break;
                }

                case FoleySound::FoundationChime: {
                    v.phaseBody += v.fBody * dt;
                    if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;

                    v.phaseThud += (v.fBody * 1.5f) * dt; // Perfect 5th
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;

                    v.phaseHarmonic += (v.fBody * 2.0f) * dt; // Octave
                    if (v.phaseHarmonic >= 1.0f) v.phaseHarmonic -= 1.0f;

                    float env = std::exp(-4.5f * normAge) * (1.0f - normAge);
                    float tone = 0.55f * std::sin(v.phaseBody * kTwoPi)
                               + 0.30f * std::sin(v.phaseThud * kTwoPi)
                               + 0.15f * std::sin(v.phaseHarmonic * kTwoPi);
                    sample = tone * env * v.intensity * 0.35f;
                    break;
                }

                case FoleySound::SpectreWhisper:
                default: {
                    v.phaseBody += v.fBody * dt;
                    if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;

                    v.phaseThud += (v.fBody * 1.503f) * dt; // Detuned Nether interval
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;

                    float whisper = v.slideFilter1.process(noise) * 0.25f;
                    float env = std::sin(normAge * 3.14159265f) * std::exp(-2.2f * normAge);
                    float tone = 0.55f * std::sin(v.phaseBody * kTwoPi) + 0.45f * std::sin(v.phaseThud * kTwoPi);
                    sample = (tone * 0.75f + whisper * 0.25f) * env * v.intensity * 0.30f;
                    break;
                }
            }

            mix += sample;
        }

        // Soft saturation limiter to preserve dynamics without harsh digital clipping
        mix = std::clamp(mix * masterVol, -0.92f, 0.92f);
        outInterleavedStereo[f * 2 + 0] = mix;
        outInterleavedStereo[f * 2 + 1] = mix;
    }
}

} // namespace solenshaded
