#ifndef SOLENSHADED_AUDIO_FOLEY_HPP
#define SOLENSHADED_AUDIO_FOLEY_HPP

#include <atomic>
#include <cstdint>
#include <array>

namespace solenshaded {

namespace FoleySound {
    // Core game Foley actions
    constexpr uint16_t CardSlide       = 0;  // Soft parchment brush / deal across felt (organic alternating flick/swish)
    constexpr uint16_t CardSnap        = 1;  // Crisp card placement snap onto pile/table
    constexpr uint16_t DeckShuffle     = 2;  // Multi-card deal riffle (uses configured shuffle style)
    constexpr uint16_t WoodClick       = 3;  // Hardwood UI button click
    constexpr uint16_t FoundationChime = 4;  // Warm harmonic interval when promoting to Foundation
    constexpr uint16_t SpectreWhisper  = 5;  // Ethereal ZAngband Spectre Nether-Sight shimmer

    // Explicit procedural sound modes matching acoustic analysis of /cardsounds/:
    constexpr uint16_t CardDealSingle  = 6;  // single.wav: Crisp deal flick + delayed table tap
    constexpr uint16_t CardDealAlt     = 7;  // single-alternative.wav: Airy parchment swish / brush deal
    constexpr uint16_t CardPlace       = 8;  // place.wav: Flat placement snap + immediate table thud
    constexpr uint16_t ShuffleSoft     = 9;  // shuffle-soft.wav: Gentle 4-stroke cascading brush/gather
    constexpr uint16_t ShuffleLoud     = 10; // shuffle-loud.wav: Energetic 4-stroke riffle release
    constexpr uint16_t DealSeries      = 11; // deal-series.wav: Rapid rhythmic deal sequence (tableau deals)
} // namespace FoleySound

enum class ShuffleStyle : uint8_t {
    Soft = 0, // Gentle cascading brush / gather (matches shuffle-soft.wav)
    Loud = 1  // Crisp energetic riffle snaps (matches shuffle-loud.wav)
};

/**
 * Real-Time Multi-Modal Procedural Card & Solitaire Foley Synthesizer.
 * Built on miniaudio with zero sample dependencies: mathematically synthesizes
 * parchment friction, edge snaps, cardboard flex resonances, and table landings.
 */
class AudioFoleyEngine {
public:
    AudioFoleyEngine();
    ~AudioFoleyEngine();

    bool init(bool enableHardwareDevice = true);
    void shutdown();

    void triggerSound(uint16_t soundId, float intensity = 1.0f);
    void mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate = 48000) noexcept;

    void setShufflePreference(ShuffleStyle style) noexcept { shuffleStyle_.store(style, std::memory_order_relaxed); }
    ShuffleStyle getShufflePreference() const noexcept { return shuffleStyle_.load(std::memory_order_relaxed); }

    struct BiquadCoeffs {
        float b0{1.0f}, b1{0.0f}, b2{0.0f};
        float a1{0.0f}, a2{0.0f};
    };

    struct BiquadFilter {
        BiquadCoeffs c;
        float x1{0.0f}, x2{0.0f};
        float y1{0.0f}, y2{0.0f};

        inline float process(float in) noexcept {
            float out = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        void reset() noexcept {
            x1 = x2 = y1 = y2 = 0.0f;
        }
    };

private:

    struct Voice {
        bool active{false};
        uint16_t soundId{0};
        float delaySec{0.0f};
        float ageSec{0.0f};
        float durationSec{0.045f};
        float intensity{1.0f};
        float jitter{1.0f};

        // Synthesis parameters
        float fBody{320.0f};
        float phaseBody{0.0f};
        float phaseThud{0.0f};
        float phaseHarmonic{0.0f};
        float phaseLfo{0.0f};
        float thudDelaySec{0.0f};

        // Multi-modal filters
        BiquadFilter snapFilter;
        BiquadFilter slideFilter1;
        BiquadFilter slideFilter2;
        BiquadFilter bodyFilter;

        void reset() noexcept {
            active = false;
            delaySec = 0.0f;
            ageSec = 0.0f;
            phaseBody = phaseThud = phaseHarmonic = phaseLfo = 0.0f;
            snapFilter.reset();
            slideFilter1.reset();
            slideFilter2.reset();
            bodyFilter.reset();
        }
    };

    struct TriggerCmd {
        uint16_t soundId{0};
        float intensity{1.0f};
    };

    void allocateVoice(const Voice& v) noexcept;
    void handleTrigger(uint16_t soundId, float intensity, float sampleRate) noexcept;
    float nextNoise() noexcept;

    static constexpr int kMaxVoices = 24;
    std::array<Voice, kMaxVoices> voices_{};

    // Lock-free single-producer single-consumer ring buffer for triggers
    static constexpr size_t kQueueCapacity = 32;
    std::array<TriggerCmd, kQueueCapacity> cmdQueue_{};
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};

    std::atomic<ShuffleStyle> shuffleStyle_{ShuffleStyle::Soft};
    uint32_t rngState_{0x9E3779B9u};
    uint32_t dealAlternator_{0};
    bool initialized_{false};
    void* maDevicePtr_{nullptr};
};

} // namespace solenshaded

#endif // SOLENSHADED_AUDIO_FOLEY_HPP
