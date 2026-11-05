#ifndef SOLENSHADED_SOLITAIRE_TYPES_HPP
#define SOLENSHADED_SOLITAIRE_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace solenshaded {

enum class Suit : uint8_t {
    Spades   = 0, // Iron & Grave-Blades (Black / Obsidian)
    Hearts   = 1, // Ichors & Soul-Vessels (Red / Crimson)
    Diamonds = 2, // Aether & Waystones (Red / Amber Crimson)
    Clubs    = 3  // Wraithwood & Elder Boughs (Black / Deep Moss-Charcoal)
};

[[nodiscard]] constexpr bool isRedSuit(Suit s) noexcept {
    return s == Suit::Hearts || s == Suit::Diamonds;
}

enum class Rank : uint8_t {
    Ace   = 1,
    Two   = 2,
    Three = 3,
    Four  = 4,
    Five  = 5,
    Six   = 6,
    Seven = 7,
    Eight = 8,
    Nine  = 9,
    Ten   = 10,
    Jack  = 11,
    Queen = 12,
    King  = 13
};

enum class PileType : uint8_t {
    Stock      = 0,
    Waste      = 1,
    Foundation = 2,
    Tableau    = 3,
    FreeCell   = 4
};

enum class GameVariant : uint8_t {
    KlondikeDraw1    = 0,
    KlondikeDraw3    = 1,
    FreeCell         = 2,
    SpectresPatience = 3 // Enshaded Yukon-style variant where buried sub-stacks move freely
};

enum class TableThemeId : uint8_t {
    EmeraldLawn   = 0, // Green linen canvas weave (ambientCG Fabric035 / classic felt)
    HopsackTweed  = 1, // Basketweave hopsack tweed with multicolor neps (ambientCG Fabric063)
    DenimChambray = 2, // Indigo chambray denim with horizontal weft slubs (ambientCG Fabric061)
    StripedJersey = 3, // Horizontal striped ribbed knit jersey (ambientCG Fabric079)
    SunkenSanctum = 4, // ZAngband Spectre vault: polished obsidian + cyan aether runes
    PerilousShores= 5, // Cartographer's cabin + weathered coastal moss (ambientCG Moss002)
    PebbledCowhide= 6, // Embossed pebble grain cowhide leather
    LeatherNubuck = 7, // Distressed aniline nubuck cowhide (ambientCG Leather006)
    PaintedWood   = 8, // Weathered painted barnwood planks (ambientCG PaintedWood008C)
    PixelDungeon  = 9, // Retro 16x16 dungeon brick wall quantized to Endesga32
    PixelWater    = 10,// Retro 32x32 aquatic shimmering water tile
    Count         = 11
};

struct Card {
    uint8_t id{0};
    Suit suit{Suit::Spades};
    Rank rank{Rank::Ace};
    bool faceUp{false};
    bool enshadedGlow{false};

    // Spring-damper visual state (Hardwood 3.0 smooth card flight & 2.5D flip)
    float x{0.0f};
    float y{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float targetX{0.0f};
    float targetY{0.0f};
    float elevation{0.0f};       // 0 = resting on pile, >0 = airborne/dragged drop-shadow lift
    float targetElevation{0.0f};
    float flipAngle{0.0f};       // 0.0 = faceUp state, pi = flipped
    float angle{0.0f};           // Current visual rotation angle in radians (Hardwood 3.0 tactile realism)
    float targetAngle{0.0f};     // Target resting angle in radians
};

/**
 * Fast stateless Thomas Wang integer hash yielding deterministic uniform float in [-1.0f, +1.0f].
 * Zero memory overhead, zero state mutation, zero heap allocation.
 */
[[nodiscard]] inline float cardHashNoise(uint32_t seed, uint8_t cardId, uint32_t channel) noexcept {
    uint32_t key = seed ^ (static_cast<uint32_t>(cardId) << 8) ^ (channel * 0x9E3779B9u);
    key = (key ^ 61u) ^ (key >> 16);
    key += (key << 3);
    key ^= (key >> 4);
    key *= 0x27D4EB2Du;
    key ^= (key >> 15);
    return (static_cast<float>(key & 0xFFFFu) / 32767.5f) - 1.0f;
}

struct Pile {
    PileType type{PileType::Tableau};
    uint8_t index{0};
    float x{0.0f};
    float y{0.0f};
    float fanOffsetDown{18.0f};
    float fanOffsetUp{28.0f};
    std::vector<Card> cards;
};

struct MoveCommand {
    uint8_t fromPile{0};
    uint8_t toPile{0};
    uint8_t cardCount{1};
    bool flippedSourceTop{false};
    bool recycledWaste{false};
    int32_t scoreDelta{0};
};

struct HintMove {
    bool valid{false};
    uint8_t fromPile{0};
    uint8_t cardIndex{0};
    uint8_t toPile{0};
};

} // namespace solenshaded

#endif // SOLENSHADED_SOLITAIRE_TYPES_HPP
