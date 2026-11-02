#ifndef SOLENSHADED_AUDIO_FOLEY_HPP
#define SOLENSHADED_AUDIO_FOLEY_HPP

#include <atomic>
#include <cstdint>

namespace solenshaded {

namespace FoleySound {
    constexpr uint16_t CardSlide       = 0; // Soft parchment brush across felt
    constexpr uint16_t CardSnap        = 1; // Crisp card placement snap
    constexpr uint16_t DeckShuffle     = 2; // Multi-card deal riffle
    constexpr uint16_t WoodClick       = 3; // Hardwood UI button click
    constexpr uint16_t FoundationChime = 4; // Warm harmonic interval when promoting to Foundation
    constexpr uint16_t SpectreWhisper  = 5; // Ethereal ZAngband Spectre Nether-Sight shimmer
} // namespace FoleySound

/**
 * Real-Time Procedural Solitaire & Enshaded Foley Synthesizer built on miniaudio.
 * Zero sample bloat: synthesizes parchment slides, wooden taps, and spectral chimes mathematically.
 */
class AudioFoleyEngine {
public:
    AudioFoleyEngine() = default;
    ~AudioFoleyEngine();

    bool init(bool enableHardwareDevice = true);
    void shutdown();

    void triggerSound(uint16_t soundId, float intensity = 1.0f);
    void mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate = 48000) noexcept;

private:
    struct Voice {
        std::atomic<bool> active{false};
        uint16_t soundId{0};
        float phase1{0.0f};
        float phase2{0.0f};
        float freq1{440.0f};
        float freq2{660.0f};
        float env{0.0f};
        float decay{0.999f};
        float noiseMix{0.25f};
        float filterState{0.0f};
    };

    static constexpr int kMaxVoices = 8;
    Voice voices_[kMaxVoices]{};
    std::atomic_flag voiceLock_ = ATOMIC_FLAG_INIT;
    uint32_t rngState_{0x9E3779B9u};
    bool initialized_{false};
    void* maDevicePtr_{nullptr};
};

} // namespace solenshaded

#endif // SOLENSHADED_AUDIO_FOLEY_HPP
