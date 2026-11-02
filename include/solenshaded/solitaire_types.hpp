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
    TavernHeartwood = 0, // Classic Hardwood 3.0 warm walnut border + deep forest baize felt
    SunkenSanctum   = 1, // ZAngband Spectre vault: carved obsidian + spectral slate + cyan runes
    PerilousShores  = 2, // Cartographer's teak cabin + antique vellum playmat + brass inlays
    RosewoodParlor  = 3, // Deep exotic rosewood grain + satin polyurethane sheen
    CarvedStone     = 4, // Weathered gothic slate / stone slabs with mortar crevice AO
    ReflectiveTile  = 5, // Glazed ceramic subway/marble tile with specular highlight spot
    EmeraldLawn     = 6, // Green linen canvas weave (ambientCG Fabric035)
    HopsackTweed    = 7, // Basketweave hopsack tweed with multicolor neps (ambientCG Fabric054)
    DenimChambray   = 8, // Indigo chambray denim with horizontal weft slubs (ambientCG Fabric073)
    StripedJersey   = 9, // Horizontal striped ribbed knit jersey (ambientCG Fabric018)
    Count           = 10
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
