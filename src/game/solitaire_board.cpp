#include "solenshaded/solitaire_board.hpp"
#include <algorithm>
#include <cmath>

namespace solenshaded {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

SolitaireBoard::SolitaireBoard() {
    newDeal(2026u, GameVariant::KlondikeDraw1);
}

uint32_t SolitaireBoard::nextRandom() noexcept {
    // Mulberry32 deterministic PRNG matching Eatsbits & Eatscharted world seeds
    uint32_t z = (rngState_ += 0x6D2B79F5u);
    z = (z ^ (z >> 15)) * (z | 1u);
    z ^= z + (z ^ (z >> 7)) * (z | 61u);
    return z ^ (z >> 14);
}

const char* SolitaireBoard::variantName(GameVariant v) noexcept {
    switch (v) {
        case GameVariant::KlondikeDraw1:    return "KLONDIKE (DRAW 1)";
        case GameVariant::KlondikeDraw3:    return "KLONDIKE (DRAW 3)";
        case GameVariant::FreeCell:         return "FREECELL";
        case GameVariant::SpectresPatience: return "SPECTRE'S PATIENCE";
        default:                            return "KLONDIKE";
    }
}

const char* SolitaireBoard::layoutModeName(ResponsiveLayoutMode m) noexcept {
    switch (m) {
        case ResponsiveLayoutMode::Auto:            return "AUTO (ADAPTIVE)";
        case ResponsiveLayoutMode::DesktopStandard: return "DESKTOP (CLASSIC)";
        case ResponsiveLayoutMode::MobileCompact:   return "MOBILE (COMPACT)";
        default: return "AUTO (ADAPTIVE)";
    }
}

const char* SolitaireBoard::stockPositionName(StockPosition p) noexcept {
    switch (p) {
        case StockPosition::Auto:        return "AUTO (SMART)";
        case StockPosition::TopLeft:     return "TOP-LEFT (CLASSIC)";
        case StockPosition::BottomRight: return "BOT-RIGHT (THUMB)";
        case StockPosition::BottomLeft:  return "BOT-LEFT (LEFTY)";
        default: return "AUTO (SMART)";
    }
}

void SolitaireBoard::setLayoutMode(ResponsiveLayoutMode mode) noexcept {
    layoutMode_ = mode;
    computeLayout(viewportW_, viewportH_);
}

void SolitaireBoard::setStockPosition(StockPosition pos) noexcept {
    stockPosition_ = pos;
    computeLayout(viewportW_, viewportH_);
}

bool SolitaireBoard::isMobileLayout() const noexcept {
    if (layoutMode_ == ResponsiveLayoutMode::MobileCompact) return true;
    if (layoutMode_ == ResponsiveLayoutMode::DesktopStandard) return false;
    const float aspect = viewportW_ / std::max(1.0f, viewportH_);
    return (aspect < 1.05f) || (viewportW_ < 600.0f);
}

const char* SolitaireBoard::themeName(TableThemeId t) noexcept {
    switch (t) {
        case TableThemeId::PastelTan:       return "PASTEL TAN";
        case TableThemeId::EmeraldLawn:     return "GREEN LINEN";
        case TableThemeId::HopsackTweed:    return "HOPSACK TWEED";
        case TableThemeId::DenimChambray:   return "DENIM CHAMBRAY";
        case TableThemeId::StripedJersey:   return "STRIPED JERSEY";
        case TableThemeId::SunkenSanctum:   return "SUNKEN SANCTUM";
        case TableThemeId::PerilousShores:  return "PERILOUS SHORES";
        case TableThemeId::PebbledCowhide:  return "PEBBLED COWHIDE";
        case TableThemeId::LeatherNubuck:   return "DISTRESSED LEATHER";
        case TableThemeId::PaintedWood:     return "PAINTED WOOD";
        case TableThemeId::PixelDungeon:    return "PIXEL DUNGEON";
        case TableThemeId::PixelWater:      return "PIXEL WATER";
        default:                            return "GREEN LINEN";
    }
}

bool SolitaireBoard::restoreSessionState(
    uint32_t seed,
    uint32_t rngState,
    GameVariant variant,
    int32_t score,
    uint32_t moveCount,
    float elapsedSeconds,
    const std::vector<Pile>& piles,
    const std::vector<MoveCommand>& undoStack,
    const std::vector<MoveCommand>& redoStack
) {
    if (piles.empty()) return false;
    constexpr float kLocalPi = 3.14159265358979323846f;
    seed_ = seed;
    rngState_ = rngState;
    variant_ = variant;
    score_ = score;
    moveCount_ = moveCount;
    elapsedSeconds_ = elapsedSeconds;
    victorySeconds_ = 0.0f;
    activeHint_ = {};
    piles_ = piles;
    undoStack_ = undoStack;
    redoStack_ = redoStack;
    computeLayout(viewportW_, viewportH_);
    updateCardTargets();
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.x = c.targetX;
            c.y = c.targetY;
            c.elevation = c.targetElevation;
            c.angle = c.targetAngle;
            c.flipAngle = c.faceUp ? 0.0f : kLocalPi;
            c.vx = 0.0f;
            c.vy = 0.0f;
        }
    }
    return true;
}

void SolitaireBoard::restartDeal() {
    newDeal(seed_, variant_);
}

void SolitaireBoard::newDeal(uint32_t seed, GameVariant variant) {
    seed_ = seed;
    rngState_ = seed;
    variant_ = variant;
    score_ = 0;
    moveCount_ = 0;
    elapsedSeconds_ = 0.0f;
    victorySeconds_ = 0.0f;
    undoStack_.clear();
    redoStack_.clear();
    activeHint_ = {};
    piles_.clear();

    // Build standard 52-card deck
    std::vector<Card> deck;
    deck.reserve(52);
    uint8_t idCounter = 0;
    for (uint8_t s = 0; s < 4; ++s) {
        for (uint8_t r = 1; r <= 13; ++r) {
            Card c{};
            c.id = idCounter++;
            c.suit = static_cast<Suit>(s);
            c.rank = static_cast<Rank>(r);
            c.faceUp = false;
            c.flipAngle = 0.0f;
            deck.push_back(c);
        }
    }

    // Fisher-Yates shuffle with deterministic Mulberry32 seed
    for (size_t i = deck.size() - 1; i > 0; --i) {
        size_t j = static_cast<size_t>(nextRandom() % static_cast<uint32_t>(i + 1));
        std::swap(deck[i], deck[j]);
    }

    if (variant_ == GameVariant::FreeCell) {
        // 4 FreeCells (0..3), 4 Foundations (4..7), 8 Tableau columns (8..15)
        for (uint8_t i = 0; i < 4; ++i) {
            Pile p{};
            p.type = PileType::FreeCell;
            p.index = i;
            piles_.push_back(p);
        }
        for (uint8_t i = 0; i < 4; ++i) {
            Pile p{};
            p.type = PileType::Foundation;
            p.index = i;
            piles_.push_back(p);
        }
        for (uint8_t i = 0; i < 8; ++i) {
            Pile p{};
            p.type = PileType::Tableau;
            p.index = i;
            piles_.push_back(p);
        }
        for (size_t i = 0; i < deck.size(); ++i) {
            Card c = deck[i];
            c.faceUp = true;
            c.flipAngle = kPi * 0.65f;
            piles_[8 + (i % 8)].cards.push_back(c);
        }
    } else {
        // Klondike / Spectre's Patience:
        // 0 = Stock, 1 = Waste, 2..5 = Foundations, 6..12 = 7 Tableau columns
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        piles_.push_back(waste);

        for (uint8_t i = 0; i < 4; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < 7; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }

        size_t cursor = 0;
        for (uint8_t col = 0; col < 7; ++col) {
            uint8_t count = col + 1;
            if (variant_ == GameVariant::SpectresPatience && col > 0) {
                count = col + 4; // Yukon-style deep deal
            }
            for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = (row == count - 1) || (variant_ == GameVariant::SpectresPatience && row >= col);
                if (c.faceUp) {
                    c.flipAngle = kPi * 0.75f;
                }
                piles_[6 + col].cards.push_back(c);
            }
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    }

    computeLayout(viewportW_, viewportH_);
    const float originX = piles_[0].x;
    const float originY = piles_[0].y;
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.x = originX;
            c.y = originY;
            c.elevation = 6.0f;
        }
    }
}

void SolitaireBoard::computeLayout(float viewportWidth, float viewportHeight) {
    viewportW_ = std::max(240.0f, viewportWidth);
    viewportH_ = std::max(240.0f, viewportHeight);

    const uint32_t numCols = (variant_ == GameVariant::FreeCell) ? 8u : 7u;
    const bool isMobile = isMobileLayout();

    StockPosition effectiveStock = stockPosition_;
    if (effectiveStock == StockPosition::Auto) {
        effectiveStock = isMobile ? StockPosition::BottomRight : StockPosition::TopLeft;
    }

    // Maximized Full-Felt Responsive Framing:
    // With header cleared and single-line telemetry docked at bottom edge,
    // we use lean modern margins (4-12px on mobile, 10-28px on desktop) to maximize card scale.
    const float marginX = isMobile
        ? std::clamp(viewportW_ * 0.015f, 4.0f, 12.0f)
        : std::clamp(viewportW_ * 0.018f, 10.0f, 28.0f);
    const float marginY = isMobile
        ? std::clamp(viewportH_ * 0.012f, 6.0f, 12.0f)
        : std::clamp(viewportH_ * 0.014f, 8.0f, 18.0f);
    constexpr float bottomReserved = 24.0f; // Minimal reservation for bottom telemetry watermark

    const float availW = viewportW_ - 2.0f * marginX;
    const float playfieldH = viewportH_ - bottomReserved;
    const float availH = playfieldH - marginY;

    // Canonical proportions in units of Card Width (w):
    // Card aspect ratio: 1.40 (height = 1.40 * width)
    constexpr float kCardAspect = 1.40f;
    // On mobile portrait, squeeze column gap ratio down to 4%..8% so cards stay as large as possible
    const float kGapXRatio = isMobile
        ? std::clamp(viewportW_ * 0.00015f, 0.04f, 0.08f)
        : 0.15f;
    const float kGapYRatio = isMobile ? 0.10f : 0.16f;

    // Nominal cascade depth budget (in card heights) below the top tableau card:
    float cascadeBudgetRatio = 1.48f;
    if (variant_ == GameVariant::FreeCell) {
        cascadeBudgetRatio = 1.38f;
    } else if (variant_ == GameVariant::SpectresPatience) {
        cascadeBudgetRatio = 1.68f;
    }

    // Solve for maximum card width constrained by both width and height:
    // Horizontal span = numCols * w + (numCols - 1) * (kGapXRatio * w)
    const float spanWPerCard = static_cast<float>(numCols) + static_cast<float>(numCols - 1) * kGapXRatio;
    const float maxW_fromWidth = availW / spanWPerCard;

    // In bottom-stock mode, bottom rail needs a 1.0*h card row
    const bool hasBottomStock = (effectiveStock != StockPosition::TopLeft) &&
        (variant_ == GameVariant::KlondikeDraw1 || variant_ == GameVariant::KlondikeDraw3);
    const float spanHPerCard = hasBottomStock
        ? (kCardAspect * (3.0f + 2.0f * kGapYRatio + cascadeBudgetRatio))
        : (kCardAspect * (2.0f + kGapYRatio + cascadeBudgetRatio));
    const float maxW_fromHeight = availH / spanHPerCard;

    // Optimal card scale: perfectly fits in center of display maximizing card size
    // High-res monitors get cards up to 320px; small mobile viewports scale down smoothly without clipping
    const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
    const float minCardW = std::min(36.0f, maxW_fromWidth);
    cardW_ = std::clamp(optimalCardW, minCardW, 320.0f);
    cardH_ = std::round(cardW_ * kCardAspect);

    const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
    const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));

    // 2D Centering: Center the entire board both horizontally and vertically
    const float totalGridW = static_cast<float>(numCols) * cardW_ + static_cast<float>(numCols - 1) * colGap;
    const float startX = std::round((viewportW_ - totalGridW) * 0.5f);

    const float nominalGridH = cardH_ * (hasBottomStock ? 3.0f : 2.0f) + rowGap * (hasBottomStock ? 2.0f : 1.0f) + std::round(cardH_ * cascadeBudgetRatio);
    float topRowY = marginY;
    if (playfieldH > nominalGridH) {
        const float slackY = playfieldH - nominalGridH;
        topRowY = std::round(marginY + slackY * (hasBottomStock ? 0.15f : 0.28f));
    }
    const float tableauY = topRowY + cardH_ + rowGap;

    if (variant_ == GameVariant::FreeCell) {
        for (uint8_t i = 0; i < 4; ++i) {
            piles_[i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[i].y = topRowY;
        }
        for (uint8_t i = 0; i < 4; ++i) {
            piles_[4 + i].x = startX + static_cast<float>(4 + i) * (cardW_ + colGap);
            piles_[4 + i].y = topRowY;
        }
        for (uint8_t i = 0; i < 8; ++i) {
            piles_[8 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[8 + i].y = tableauY;
        }
    } else if (variant_ == GameVariant::SpectresPatience) {
        // Spectre's Patience: 4 Foundations at top, 7 tableau columns
        const float foundStartX = isMobile ? (startX + 1.5f * (cardW_ + colGap)) : (startX + 3.0f * (cardW_ + colGap));
        for (uint8_t i = 0; i < 4; ++i) {
            piles_[i].x = std::round(foundStartX + static_cast<float>(i) * (cardW_ + colGap));
            piles_[i].y = topRowY;
        }
        for (uint8_t i = 0; i < 7; ++i) {
            piles_[4 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[4 + i].y = tableauY;
        }
    } else {
        // Klondike Draw 1 & Draw 3
        if (hasBottomStock) {
            // Thumb-zone layout: 4 Foundations centered at top, Stock and Waste at bottom rail
            const float foundStartX = startX + 1.5f * (cardW_ + colGap);
            for (uint8_t i = 0; i < 4; ++i) {
                piles_[2 + i].x = std::round(foundStartX + static_cast<float>(i) * (cardW_ + colGap));
                piles_[2 + i].y = topRowY;
            }
            for (uint8_t i = 0; i < 7; ++i) {
                piles_[6 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[6 + i].y = tableauY;
            }

            const float bottomRailY = std::round(viewportH_ - bottomReserved - cardH_ - 4.0f);
            if (effectiveStock == StockPosition::BottomRight) {
                // Stock at column 6 (rightmost), Waste at column 5
                piles_[0].x = startX + 6.0f * (cardW_ + colGap);
                piles_[0].y = bottomRailY;
                piles_[1].x = startX + 5.0f * (cardW_ + colGap);
                piles_[1].y = bottomRailY;
            } else {
                // BottomLeft: Stock at column 0 (leftmost), Waste at column 1
                piles_[0].x = startX;
                piles_[0].y = bottomRailY;
                piles_[1].x = startX + (cardW_ + colGap);
                piles_[1].y = bottomRailY;
            }
        } else {
            // Traditional Top-Left stock layout
            piles_[0].x = startX;
            piles_[0].y = topRowY;
            piles_[1].x = startX + (cardW_ + colGap);
            piles_[1].y = topRowY;

            for (uint8_t i = 0; i < 4; ++i) {
                piles_[2 + i].x = startX + static_cast<float>(3 + i) * (cardW_ + colGap);
                piles_[2 + i].y = topRowY;
            }
            for (uint8_t i = 0; i < 7; ++i) {
                piles_[6 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[6 + i].y = tableauY;
            }
        }
    }

    updateCardTargets();
}

void SolitaireBoard::setCardDeviation(float dev) noexcept {
    cardDeviation_ = std::clamp(dev, 0.0f, 1.0f);
    updateCardTargets();
}

void SolitaireBoard::updateCardTargets() {
    bool bottomStockActive = false;
    float bottomRailStockY = viewportH_ - 56.0f;
    if (variant_ == GameVariant::KlondikeDraw1 || variant_ == GameVariant::KlondikeDraw3) {
        if (!piles_.empty() && piles_[0].y > (viewportH_ * 0.5f)) {
            bottomStockActive = true;
            bottomRailStockY = piles_[0].y - 8.0f;
        }
    }
    // 14px safety clearance above bottom molding bar (or clearance above bottom stock)
    const float maxTableauBottom = bottomStockActive ? bottomRailStockY : (viewportH_ - 56.0f);

    // Vector-proportional deviation: 0% = 0px, 0 deg; 100% = up to +-10px, +-5 deg
    const float cardScale = cardW_ / 96.0f;
    const float maxPosDev = 10.0f * cardScale * cardDeviation_;
    const float maxAngleRad = (5.0f * (kPi / 180.0f)) * cardDeviation_;

    for (auto& p : piles_) {
        if (p.type == PileType::Tableau) {
            float downStep = std::round(cardH_ * 0.14f);
            float upStep = std::round(cardH_ * 0.24f);

            size_t downCount = 0;
            size_t upCount = 0;
            for (const auto& c : p.cards) {
                if (c.faceUp) ++upCount;
                else ++downCount;
            }
            const float projectedH = p.y + cardH_ + static_cast<float>(downCount) * downStep +
                                     (upCount > 0 ? static_cast<float>(upCount - 1) * upStep : 0.0f);
            if (projectedH > maxTableauBottom && (downCount + upCount) > 1) {
                const float availableSpan = maxTableauBottom - p.y - cardH_;
                const float neededSpan = static_cast<float>(downCount) * downStep +
                                         static_cast<float>(upCount > 0 ? upCount - 1 : 0) * upStep;
                const float scale = availableSpan / std::max(1.0f, neededSpan);

                // Minimum step bounds proportional to card scale so indices/ranks are always clearly legible
                const float minDownStep = std::max(6.0f, std::round(cardH_ * 0.06f));
                const float minUpStep = std::max(14.0f, std::round(cardH_ * 0.12f));

                downStep = std::max(minDownStep, std::floor(downStep * scale));
                upStep = std::max(minUpStep, std::floor(upStep * scale));
            }
            p.fanOffsetDown = downStep;
            p.fanOffsetUp = upStep;

            float curY = p.y;
            for (size_t i = 0; i < p.cards.size(); ++i) {
                // Subtle organic tactile micro-jitter for hand-dealt tableau feel
                const float jx = cardHashNoise(seed_, p.cards[i].id, 101) * maxPosDev;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 102) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 103) * maxAngleRad;

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = curY + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
                curY += p.cards[i].faceUp ? upStep : downStep;
            }
        } else if (p.type == PileType::Waste) {
            const size_t n = p.cards.size();
            for (size_t i = 0; i < n; ++i) {
                float fanOffset = 0.0f;
                if (variant_ == GameVariant::KlondikeDraw3 && n > 1) {
                    size_t visibleStart = (n > 3) ? (n - 3) : 0;
                    size_t visibleIdx = (i >= visibleStart) ? (i - visibleStart) : 0;
                    fanOffset = static_cast<float>(visibleIdx) * std::round(cardW_ * 0.22f);
                }
                const float jx = cardHashNoise(seed_, p.cards[i].id, 201) * (maxPosDev * 0.80f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 202) * (maxPosDev * 0.70f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 203) * (maxAngleRad * 0.85f);

                p.cards[i].targetX = p.x + fanOffset + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Stock) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                // Stratified physical deck edge thickness & organic stacking
                const float jx = cardHashNoise(seed_, p.cards[i].id, 301) * (maxPosDev * 0.75f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 302) * (maxPosDev * 0.75f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 303) * (maxAngleRad * 0.75f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Foundation) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                const float jx = cardHashNoise(seed_, p.cards[i].id, 401) * (maxPosDev * 0.60f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 402) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 403) * (maxAngleRad * 0.65f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                const float jx = cardHashNoise(seed_, p.cards[i].id, 501) * (maxPosDev * 0.60f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 502) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 503) * (maxAngleRad * 0.65f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        }
    }
}

void SolitaireBoard::updateAnimations(float dt) {
    const float clampedDt = std::min(dt, 0.05f);
    if (!isVictory()) {
        elapsedSeconds_ += clampedDt;
        victorySeconds_ = 0.0f;
    } else {
        victorySeconds_ += clampedDt;
    }

    const float springLerp = 1.0f - std::exp(-18.0f * clampedDt);
    const float flipLerp = 1.0f - std::exp(-10.0f * clampedDt);
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            float dx = c.targetX - c.x;
            float dy = c.targetY - c.y;
            float dist = std::hypot(dx, dy);
            c.x += dx * springLerp;
            c.y += dy * springLerp;

            float flightLift = std::clamp(dist * 0.09f, 0.0f, 14.0f);
            float desiredElev = std::max(c.targetElevation, flightLift);
            c.elevation += (desiredElev - c.elevation) * springLerp;

            // Aerodynamic flight banking proportional to horizontal motion + spring settling to resting angle
            const float flightBank = std::clamp(dx * 0.0016f, -0.065f, 0.065f);
            const float desiredAngle = (dist > 3.0f) ? (c.targetAngle + flightBank) : c.targetAngle;
            c.angle += (desiredAngle - c.angle) * springLerp;

            if (c.flipAngle > 0.001f) {
                c.flipAngle *= (1.0f - flipLerp);
                if (c.flipAngle < 0.01f) {
                    c.flipAngle = 0.0f;
                }
            }
        }
    }
}

bool SolitaireBoard::drawFromStock() {
    if (piles_.size() < 2 || piles_[0].type != PileType::Stock) {
        return false;
    }
    clearHint();
    Pile& stock = piles_[0];
    Pile& waste = piles_[1];

    if (!stock.cards.empty()) {
        const uint8_t drawCount = (variant_ == GameVariant::KlondikeDraw3)
            ? static_cast<uint8_t>(std::min<size_t>(3, stock.cards.size()))
            : 1u;
        for (uint8_t i = 0; i < drawCount; ++i) {
            Card c = stock.cards.back();
            stock.cards.pop_back();
            c.faceUp = true;
            c.flipAngle = kPi; // 2.5D turn-over animation
            c.elevation = 9.0f;
            waste.cards.push_back(c);
        }
        MoveCommand cmd{};
        cmd.fromPile = 0;
        cmd.toPile = 1;
        cmd.cardCount = drawCount;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        updateCardTargets();
        return true;
    } else if (!waste.cards.empty()) {
        const uint8_t recycledCount = static_cast<uint8_t>(waste.cards.size());
        while (!waste.cards.empty()) {
            Card c = waste.cards.back();
            waste.cards.pop_back();
            c.faceUp = false;
            c.flipAngle = 0.0f;
            c.elevation = 6.0f;
            stock.cards.push_back(c);
        }
        MoveCommand cmd{};
        cmd.fromPile = 1;
        cmd.toPile = 0;
        cmd.cardCount = recycledCount;
        cmd.recycledWaste = true;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        updateCardTargets();
        return true;
    }
    return false;
}

bool SolitaireBoard::canMoveCards(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx) const {
    if (fromPileIdx >= piles_.size() || toPileIdx >= piles_.size() || fromPileIdx == toPileIdx) {
        return false;
    }
    const Pile& src = piles_[fromPileIdx];
    const Pile& dst = piles_[toPileIdx];
    if (startCardIdx >= src.cards.size()) {
        return false;
    }

    const Card& lead = src.cards[startCardIdx];
    if (!lead.faceUp) {
        return false;
    }
    const size_t moveCount = src.cards.size() - startCardIdx;

    if (moveCount > 1 && src.type != PileType::Tableau) {
        return false;
    }

    if (moveCount > 1 && variant_ != GameVariant::SpectresPatience) {
        for (size_t i = startCardIdx; i + 1 < src.cards.size(); ++i) {
            const Card& upper = src.cards[i];
            const Card& lower = src.cards[i + 1];
            if (isRedSuit(upper.suit) == isRedSuit(lower.suit) ||
                static_cast<int>(upper.rank) != static_cast<int>(lower.rank) + 1) {
                return false;
            }
        }
    }

    if (dst.type == PileType::Foundation) {
        if (moveCount != 1) return false;
        if (dst.cards.empty()) {
            return lead.rank == Rank::Ace;
        }
        const Card& top = dst.cards.back();
        return (lead.suit == top.suit) &&
               (static_cast<int>(lead.rank) == static_cast<int>(top.rank) + 1);
    }

    if (dst.type == PileType::FreeCell) {
        return moveCount == 1 && dst.cards.empty();
    }

    if (dst.type == PileType::Tableau) {
        if (dst.cards.empty()) {
            if (variant_ == GameVariant::FreeCell) {
                return true;
            }
            return lead.rank == Rank::King;
        }
        const Card& top = dst.cards.back();
        if (!top.faceUp) return false;
        return (isRedSuit(lead.suit) != isRedSuit(top.suit)) &&
               (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
    }

    return false;
}

bool SolitaireBoard::executeMove(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, bool recordUndo) {
    if (!canMoveCards(fromPileIdx, startCardIdx, toPileIdx)) {
        return false;
    }
    clearHint();

    Pile& src = piles_[fromPileIdx];
    Pile& dst = piles_[toPileIdx];
    const uint8_t count = static_cast<uint8_t>(src.cards.size() - startCardIdx);

    for (size_t i = startCardIdx; i < src.cards.size(); ++i) {
        Card c = src.cards[i];
        c.elevation = std::max(c.elevation, 11.0f);
        dst.cards.push_back(c);
    }
    src.cards.erase(src.cards.begin() + static_cast<std::ptrdiff_t>(startCardIdx), src.cards.end());

    bool flippedTop = false;
    if (src.type == PileType::Tableau && !src.cards.empty() && !src.cards.back().faceUp) {
        src.cards.back().faceUp = true;
        src.cards.back().flipAngle = kPi; // 2.5D card flip animation
        flippedTop = true;
    }

    int32_t scoreDelta = 0;
    if (dst.type == PileType::Foundation) scoreDelta += 10;
    if (src.type == PileType::Waste && dst.type == PileType::Tableau) scoreDelta += 5;
    if (flippedTop) scoreDelta += 5;
    score_ += scoreDelta;
    ++moveCount_;

    if (recordUndo) {
        MoveCommand cmd{};
        cmd.fromPile = fromPileIdx;
        cmd.toPile = toPileIdx;
        cmd.cardCount = count;
        cmd.flippedSourceTop = flippedTop;
        cmd.scoreDelta = scoreDelta;
        undoStack_.push_back(cmd);
        redoStack_.clear();
    }

    updateCardTargets();
    return true;
}

bool SolitaireBoard::smartMove(uint8_t fromPileIdx, size_t startCardIdx) {
    if (fromPileIdx >= piles_.size()) return false;
    const Pile& src = piles_[fromPileIdx];
    if (startCardIdx >= src.cards.size()) return false;

    // 1. If single top card, try Foundation first (Hardwood 3.0 priority)
    if (startCardIdx + 1 == src.cards.size()) {
        const Card& lead = src.cards[startCardIdx];
        // If Ace, prioritize the matching foundation suit slot (Spades=0, Hearts=1, Diamonds=2, Clubs=3)
        if (lead.rank == Rank::Ace) {
            for (size_t i = 0; i < piles_.size(); ++i) {
                if (piles_[i].type == PileType::Foundation &&
                    (piles_[i].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                    return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
                }
            }
        }
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Foundation &&
                canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
            }
        }
    }

    // 2. Try non-empty Tableau columns first (preferring building on existing cards)
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::Tableau && !piles_[i].cards.empty() &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    // 3. Try empty Tableau column
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::Tableau && piles_[i].cards.empty() &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    // 4. Try FreeCell slot if single card
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::FreeCell &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    return false;
}

bool SolitaireBoard::stepAutoFoundation() {
    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) {
            continue;
        }
        const size_t topIdx = src.cards.size() - 1;
        const Card& lead = src.cards[topIdx];
        if (lead.rank == Rank::Ace) {
            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Foundation &&
                    (piles_[d].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                    return executeMove(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d));
                }
            }
        }
        for (size_t d = 0; d < piles_.size(); ++d) {
            if (piles_[d].type == PileType::Foundation &&
                canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                return executeMove(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d));
            }
        }
    }
    return false;
}

bool SolitaireBoard::canAutoFinish() const noexcept {
    if (isVictory()) return false;
    for (const auto& p : piles_) {
        if (p.type == PileType::Stock || p.type == PileType::Waste) {
            if (!p.cards.empty()) return false;
        } else if (p.type == PileType::Tableau) {
            for (const auto& c : p.cards) {
                if (!c.faceUp) return false;
            }
        }
    }
    return true;
}

HintMove SolitaireBoard::findBestHint() {
    clearHint();

    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) continue;
        size_t topIdx = src.cards.size() - 1;
        const Card& lead = src.cards[topIdx];
        if (lead.rank == Rank::Ace) {
            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Foundation &&
                    (piles_[d].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                    activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                    piles_[s].cards[topIdx].enshadedGlow = true;
                    return activeHint_;
                }
            }
        }
        for (size_t d = 0; d < piles_.size(); ++d) {
            if (piles_[d].type == PileType::Foundation &&
                canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                piles_[s].cards[topIdx].enshadedGlow = true;
                return activeHint_;
            }
        }
    }

    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type != PileType::Tableau && src.type != PileType::Waste && src.type != PileType::FreeCell) continue;
        for (size_t cIdx = 0; cIdx < src.cards.size(); ++cIdx) {
            if (!src.cards[cIdx].faceUp) continue;
            if (src.type == PileType::Tableau && cIdx == 0 && src.cards[cIdx].rank == Rank::King) continue;

            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Tableau &&
                    canMoveCards(static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d))) {
                    activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                    piles_[s].cards[cIdx].enshadedGlow = true;
                    return activeHint_;
                }
            }
        }
    }

    return activeHint_;
}

void SolitaireBoard::clearHint() noexcept {
    activeHint_ = {};
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.enshadedGlow = false;
        }
    }
}

bool SolitaireBoard::undo() {
    if (undoStack_.empty()) return false;
    clearHint();

    MoveCommand cmd = undoStack_.back();
    undoStack_.pop_back();

    Pile& src = piles_[cmd.fromPile];
    Pile& dst = piles_[cmd.toPile];

    if (cmd.recycledWaste) {
        while (!dst.cards.empty()) {
            Card c = dst.cards.back();
            dst.cards.pop_back();
            c.faceUp = true;
            c.elevation = 6.0f;
            src.cards.push_back(c);
        }
    } else {
        if (cmd.flippedSourceTop && !src.cards.empty()) {
            src.cards.back().faceUp = false;
            src.cards.back().flipAngle = 0.0f;
        }
        const size_t startIdx = dst.cards.size() - cmd.cardCount;
        for (size_t i = startIdx; i < dst.cards.size(); ++i) {
            Card c = dst.cards[i];
            if (src.type == PileType::Stock) {
                c.faceUp = false;
            }
            c.elevation = 8.0f;
            src.cards.push_back(c);
        }
        dst.cards.erase(dst.cards.begin() + static_cast<std::ptrdiff_t>(startIdx), dst.cards.end());
    }

    score_ = std::max(0, score_ - cmd.scoreDelta);
    redoStack_.push_back(cmd);
    updateCardTargets();
    return true;
}

bool SolitaireBoard::redo() {
    if (redoStack_.empty()) return false;
    MoveCommand cmd = redoStack_.back();
    redoStack_.pop_back();

    if (cmd.fromPile == 0 && cmd.toPile == 1 && piles_[0].type == PileType::Stock) {
        return drawFromStock();
    }
    if (cmd.recycledWaste) {
        return drawFromStock();
    }
    const Pile& src = piles_[cmd.fromPile];
    if (src.cards.size() >= cmd.cardCount) {
        return executeMove(cmd.fromPile, src.cards.size() - cmd.cardCount, cmd.toPile, true);
    }
    return false;
}

bool SolitaireBoard::hitTestCard(float px, float py, int& outPileIdx, int& outCardIdx) const noexcept {
    outPileIdx = -1;
    outCardIdx = -1;

    // Responsive touch halo: expand hit targets on narrow/small cards for finger tapping
    const float padX = (cardW_ < 64.0f) ? std::min(10.0f, (64.0f - cardW_) * 0.5f) : 0.0f;
    const float padY = (cardH_ < 90.0f) ? std::min(10.0f, (90.0f - cardH_) * 0.5f) : 0.0f;

    // Test piles back-to-front, and cards top-to-bottom within each pile
    for (int pIdx = static_cast<int>(piles_.size()) - 1; pIdx >= 0; --pIdx) {
        const Pile& p = piles_[static_cast<size_t>(pIdx)];
        if (p.type == PileType::Stock) {
            if (px >= p.x - padX && px <= p.x + cardW_ + padX && py >= p.y - padY && py <= p.y + cardH_ + padY) {
                outPileIdx = pIdx;
                outCardIdx = p.cards.empty() ? -1 : static_cast<int>(p.cards.size()) - 1;
                return true;
            }
            continue;
        }

        for (int cIdx = static_cast<int>(p.cards.size()) - 1; cIdx >= 0; --cIdx) {
            const Card& c = p.cards[static_cast<size_t>(cIdx)];
            if (px >= c.x - padX && px <= c.x + cardW_ + padX && py >= c.y - padY && py <= c.y + cardH_ + padY) {
                // In Waste/Foundation/FreeCell, only the top card is interactive
                if (p.type != PileType::Tableau && cIdx != static_cast<int>(p.cards.size()) - 1) {
                    continue;
                }
                if (!c.faceUp) {
                    return false;
                }
                outPileIdx = pIdx;
                outCardIdx = cIdx;
                return true;
            }
        }
    }
    return false;
}

int SolitaireBoard::findBestDropPile(uint8_t fromPileIdx, size_t startCardIdx, float dropCenterX, float dropCenterY) const noexcept {
    int bestPile = -1;
    float bestDist = std::max(cardW_ * 1.15f, 54.0f); // Generous Hardwood 3.0 magnetic snap radius

    for (size_t d = 0; d < piles_.size(); ++d) {
        if (d == fromPileIdx) continue;
        if (!canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(d))) continue;

        const Pile& dst = piles_[d];
        float targetX = dst.x + cardW_ * 0.5f;
        float targetY = dst.y + cardH_ * 0.5f;
        if (!dst.cards.empty()) {
            targetX = dst.cards.back().targetX + cardW_ * 0.5f;
            targetY = dst.cards.back().targetY + cardH_ * 0.5f;
        }

        float dist = std::hypot(dropCenterX - targetX, dropCenterY - targetY);
        if (dist < bestDist) {
            bestDist = dist;
            bestPile = static_cast<int>(d);
        }
    }
    return bestPile;
}

bool SolitaireBoard::isVictory() const noexcept {
    uint32_t foundationCards = 0;
    for (const auto& p : piles_) {
        if (p.type == PileType::Foundation) {
            foundationCards += static_cast<uint32_t>(p.cards.size());
        }
    }
    return foundationCards == 52u;
}

} // namespace solenshaded
