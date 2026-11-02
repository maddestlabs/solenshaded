#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "solenshaded/audio_foley.hpp"
#include <algorithm>
#include <cmath>

namespace solenshaded {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

void miniaudioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioFoleyEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixSamples(static_cast<float*>(pOutput), frameCount, pDevice->sampleRate);
    }
}
} // namespace

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

void AudioFoleyEngine::triggerSound(uint16_t soundId, float intensity) {
    while (voiceLock_.test_and_set(std::memory_order_acquire)) {}

    int slot = 0;
    float minEnv = 999.0f;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active.load(std::memory_order_relaxed)) {
            slot = i;
            break;
        }
        if (voices_[i].env < minEnv) {
            minEnv = voices_[i].env;
            slot = i;
        }
    }

    Voice& v = voices_[slot];
    v.soundId = soundId;
    v.phase1 = 0.0f;
    v.phase2 = 0.0f;
    v.filterState = 0.0f;
    v.env = std::clamp(intensity, 0.1f, 1.0f);

    switch (soundId) {
        case FoleySound::CardSlide:
            v.freq1 = 310.0f;
            v.freq2 = 620.0f;
            v.decay = 0.9986f;
            v.noiseMix = 0.85f;
            break;
        case FoleySound::CardSnap:
            v.freq1 = 185.0f;
            v.freq2 = 540.0f;
            v.decay = 0.9972f;
            v.noiseMix = 0.55f;
            break;
        case FoleySound::DeckShuffle:
            v.freq1 = 240.0f;
            v.freq2 = 480.0f;
            v.decay = 0.9991f;
            v.noiseMix = 0.78f;
            break;
        case FoleySound::WoodClick:
            v.freq1 = 140.0f;
            v.freq2 = 420.0f;
            v.decay = 0.9965f;
            v.noiseMix = 0.25f;
            break;
        case FoleySound::FoundationChime:
            v.freq1 = 528.0f;
            v.freq2 = 792.0f; // Perfect fifth harmonic chime
            v.decay = 0.9995f;
            v.noiseMix = 0.05f;
            break;
        case FoleySound::SpectreWhisper:
        default:
            v.freq1 = 396.0f;
            v.freq2 = 594.0f;
            v.decay = 0.9996f;
            v.noiseMix = 0.35f;
            break;
    }

    v.active.store(true, std::memory_order_release);
    voiceLock_.clear(std::memory_order_release);
}

void AudioFoleyEngine::mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate) noexcept {
    const float invSr = 1.0f / static_cast<float>(std::max(1u, sampleRate));
    for (uint32_t f = 0; f < frameCount; ++f) {
        float mix = 0.0f;
        for (int i = 0; i < kMaxVoices; ++i) {
            Voice& v = voices_[i];
            if (!v.active.load(std::memory_order_relaxed)) continue;

            rngState_ ^= rngState_ << 13;
            rngState_ ^= rngState_ >> 17;
            rngState_ ^= rngState_ << 5;
            float white = static_cast<float>(static_cast<int32_t>(rngState_)) * (1.0f / 2147483648.0f);
            v.filterState += 0.18f * (white - v.filterState);

            v.phase1 += v.freq1 * invSr;
            if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
            v.phase2 += v.freq2 * invSr;
            if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

            float tone = 0.65f * std::sin(v.phase1 * kTwoPi) + 0.35f * std::sin(v.phase2 * kTwoPi);
            float sample = ((1.0f - v.noiseMix) * tone + v.noiseMix * v.filterState) * v.env * 0.22f;
            mix += sample;

            v.env *= v.decay;
            if (v.env < 0.0005f) {
                v.active.store(false, std::memory_order_relaxed);
            }
        }
        mix = std::clamp(mix, -0.95f, 0.95f);
        outInterleavedStereo[f * 2 + 0] = mix;
        outInterleavedStereo[f * 2 + 1] = mix;
    }
}

} // namespace solenshaded
