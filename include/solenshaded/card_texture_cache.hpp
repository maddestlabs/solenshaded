#ifndef SOLENSHADED_CARD_TEXTURE_CACHE_HPP
#define SOLENSHADED_CARD_TEXTURE_CACHE_HPP

#include "solenshaded/solitaire_types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace eatsbits::ui {
    class BatchRenderer2D;
}

namespace solenshaded {

struct CardAtlasSlot {
    float u0{0.0f};
    float v0{0.0f};
    float u1{1.0f};
    float v1{1.0f};
    int width{0};
    int height{0};
};

struct CardTexture {
    int width{0};
    int height{0};
    std::vector<uint8_t> rgba;    // 32-bit RGBA (byte 0=R, 1=G, 2=B, 3=A)
    std::vector<uint32_t> argb32; // Mip 0: Pre-packed 0xAARRGGBB for 1-cycle direct blitting
    std::vector<uint32_t> mip1;   // Mip 1: (W/2 x H/2) 2x2 box-supersampled
    std::vector<uint32_t> mip2;   // Mip 2: (W/4 x H/4) 4x4 box-supersampled

    // Select the optimal pre-filtered mipmap level based on target screen width
    [[nodiscard]] const uint32_t* getMipData(float screenW, int& outW, int& outH) const noexcept {
        if (!mip2.empty() && screenW <= 115.0f && width >= 360) {
            outW = width / 4;
            outH = height / 4;
            return mip2.data();
        }
        if (!mip1.empty() && screenW <= 230.0f && width >= 180) {
            outW = width / 2;
            outH = height / 2;
            return mip1.data();
        }
        outW = width;
        outH = height;
        return argb32.empty() ? nullptr : argb32.data();
    }
};

enum class DeckThemeId : uint8_t {
    ClassicSvg  = 0,
    SteampunkAi = 1,
    Anime       = 2,
    Cyberpunk   = 3,
    Custom      = 4,
    Count
};

class CardTextureCache {
public:
    static CardTextureCache& instance();

    static constexpr int kDefaultCardW = 360;
    static constexpr int kDefaultCardH = 504;

    bool initialize(int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadEatsDeck(const std::string& eatsFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadDeckFromMemory(
        const uint8_t* data,
        size_t size,
        const std::string& format = "svgz",
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        DeckThemeId theme = DeckThemeId::Custom,
        const std::string& cacheKey = ""
    );
    bool loadSvgDeck(const std::string& svgFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadBitmapDeck(const std::string& bitmapFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadDeckTheme(DeckThemeId theme, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    void bakeProceduralDeck(int cardW = kDefaultCardW, int cardH = kDefaultCardH);

    [[nodiscard]] const CardTexture* getCardTexture(Suit s, Rank r) const noexcept;
    [[nodiscard]] const CardTexture* getCardBackTexture() const noexcept;
    [[nodiscard]] const CardTexture* getTextureByIndex(size_t index) const noexcept;

    bool buildGpuAtlas(eatsbits::ui::BatchRenderer2D& renderer);
    [[nodiscard]] const CardAtlasSlot& getAtlasSlot(Suit s, Rank r) const noexcept;
    [[nodiscard]] const CardAtlasSlot& getBackAtlasSlot() const noexcept;
    [[nodiscard]] const CardAtlasSlot& getAtlasSlotByIndex(size_t index) const noexcept;
    [[nodiscard]] bool hasGpuAtlas() const noexcept { return hasGpuAtlas_; }

    [[nodiscard]] bool isLoaded() const noexcept { return isLoaded_; }
    [[nodiscard]] int cardWidth() const noexcept { return cardW_; }
    [[nodiscard]] int cardHeight() const noexcept { return cardH_; }
    [[nodiscard]] DeckThemeId currentDeckTheme() const noexcept { return currentTheme_; }
    static const char* deckThemeName(DeckThemeId theme) noexcept;

    static constexpr size_t kCardBackIndex = 52;
    static constexpr size_t kTotalTextures = 53;

    static inline size_t cardIndex(Suit s, Rank r) noexcept {
        size_t suitIdx = static_cast<size_t>(s);
        size_t rankIdx = (static_cast<size_t>(r) >= 1 && static_cast<size_t>(r) <= 13)
                         ? (static_cast<size_t>(r) - 1) : 0;
        return suitIdx * 13 + rankIdx;
    }

private:
    CardTextureCache() = default;
    int cardW_{200};
    int cardH_{280};
    bool isLoaded_{false};
    bool hasGpuAtlas_{false};
    DeckThemeId currentTheme_{DeckThemeId::ClassicSvg};
    std::array<CardTexture, kTotalTextures> textures_{};
    std::array<CardAtlasSlot, kTotalTextures> atlasSlots_{};
};

} // namespace solenshaded

#endif // SOLENSHADED_CARD_TEXTURE_CACHE_HPP

