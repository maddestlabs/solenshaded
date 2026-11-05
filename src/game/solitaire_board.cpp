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

const char* SolitaireBoard::themeName(TableThemeId t) noexcept {
    switch (t) {
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
    viewportW_ = std::max(480.0f, viewportWidth);
    viewportH_ = std::max(360.0f, viewportHeight);

    const uint32_t numCols = (variant_ == GameVariant::FreeCell) ? 8u : 7u;

    // Maximized Full-Felt Responsive Framing:
    // With header cleared and single-line telemetry docked at bottom edge,
    // we use lean modern margins (10-28px horizontal, 8-18px vertical) to maximize card scale.
    const float marginX = std::clamp(viewportW_ * 0.018f, 10.0f, 28.0f);
    const float marginY = std::clamp(viewportH_ * 0.014f, 8.0f, 18.0f);
    constexpr float bottomReserved = 24.0f; // Minimal reservation for bottom telemetry watermark

    const float availW = viewportW_ - 2.0f * marginX;
    const float playfieldH = viewportH_ - bottomReserved;
    const float availH = playfieldH - marginY;

    // Canonical proportions in units of Card Width (w):
    // Card aspect ratio: 1.40 (height = 1.40 * width)
    constexpr float kCardAspect = 1.40f;
    // Column gap ratio: 15% of card width
    constexpr float kGapXRatio = 0.15f;
    // Row gap ratio between top piles (Stock/Foundations) and Tableau
    constexpr float kGapYRatio = 0.16f;

    // Nominal cascade depth budget (in card heights) below the top tableau card:
    // Klondike: ~6-7 cards visible initially. 1.48 provides generous uncompressed spacing.
    // FreeCell: 8 columns with 6-7 cards deal. 1.38 gives optimal initial fit.
    // Spectre's Patience: Column 7 starts with 11 cards (Yukon deep deal), so 1.68 budget.
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

    // Vertical span = TopRow (1.0*h) + Gap (kGapYRatio*h) + TableauHead (1.0*h) + Cascade (cascadeBudgetRatio*h)
    // where h = kCardAspect * w
    const float spanHPerCard = kCardAspect * (2.0f + kGapYRatio + cascadeBudgetRatio);
    const float maxW_fromHeight = availH / spanHPerCard;

    // Optimal card scale: perfectly fits in center of display maximizing card size
    // No artificial 118px cap! High-res monitors (1080p, 1440p, 4K) get huge, gorgeous cards up to 320px
    const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
    cardW_ = std::clamp(optimalCardW, 52.0f, 320.0f);
    cardH_ = std::round(cardW_ * kCardAspect);

    const float colGap = std::round(cardW_ * kGapXRatio);
    const float rowGap = std::round(cardH_ * kGapYRatio);

    // 2D Centering: Center the entire board both horizontally and vertically
    const float totalGridW = static_cast<float>(numCols) * cardW_ + static_cast<float>(numCols - 1) * colGap;
    const float startX = std::round((viewportW_ - totalGridW) * 0.5f);

    const float nominalGridH = cardH_ * 2.0f + rowGap + std::round(cardH_ * cascadeBudgetRatio);
    float topRowY = marginY;
    if (playfieldH > nominalGridH) {
        // Center vertically within the felt table area, with an optimal upward bias (0.28)
        // to leave abundant natural room for downward tableau cascades
        const float slackY = playfieldH - nominalGridH;
        topRowY = std::round(marginY + slackY * 0.28f);
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
    } else {
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

    updateCardTargets();
}

void SolitaireBoard::updateCardTargets() {
    // 14px safety clearance above bottom molding bar (botBarH = 42px)
    const float maxTableauBottom = viewportH_ - 56.0f;

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
                // Hardwood 3.0 subtle organic micro-jitter for hand-dealt tableau feel
                const float jx = cardHashNoise(seed_, p.cards[i].id, 101) * 0.85f;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 102) * 0.40f;
                const float ja = cardHashNoise(seed_, p.cards[i].id, 103) * 0.0075f; // ~0.43 degrees

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
                const float jx = cardHashNoise(seed_, p.cards[i].id, 201) * 0.70f;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 202) * 0.50f;
                const float ja = cardHashNoise(seed_, p.cards[i].id, 203) * 0.0065f;

                p.cards[i].targetX = p.x + fanOffset + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Stock) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                // Stratified physical deck edge thickness
                const float jx = cardHashNoise(seed_, p.cards[i].id, 301) * 0.80f;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 302) * 0.80f;
                const float ja = cardHashNoise(seed_, p.cards[i].id, 303) * 0.0070f;

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Foundation) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                const float jx = cardHashNoise(seed_, p.cards[i].id, 401) * 0.60f;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 402) * 0.60f;
                const float ja = cardHashNoise(seed_, p.cards[i].id, 403) * 0.0055f;

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else {
            for (auto& c : p.cards) {
                c.targetX = p.x;
                c.targetY = p.y;
                c.targetAngle = 0.0f;
                c.targetElevation = 0.0f;
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

    // Test piles back-to-front, and cards top-to-bottom within each pile
    for (int pIdx = static_cast<int>(piles_.size()) - 1; pIdx >= 0; --pIdx) {
        const Pile& p = piles_[static_cast<size_t>(pIdx)];
        if (p.type == PileType::Stock) {
            if (px >= p.x && px <= p.x + cardW_ && py >= p.y && py <= p.y + cardH_) {
                outPileIdx = pIdx;
                outCardIdx = p.cards.empty() ? -1 : static_cast<int>(p.cards.size()) - 1;
                return true;
            }
            continue;
        }

        for (int cIdx = static_cast<int>(p.cards.size()) - 1; cIdx >= 0; --cIdx) {
            const Card& c = p.cards[static_cast<size_t>(cIdx)];
            if (px >= c.x && px <= c.x + cardW_ && py >= c.y && py <= c.y + cardH_) {
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
    float bestDist = cardW_ * 1.15f; // Generous Hardwood 3.0 magnetic snap radius

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
