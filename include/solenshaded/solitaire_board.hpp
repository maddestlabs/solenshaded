#ifndef SOLENSHADED_SOLITAIRE_BOARD_HPP
#define SOLENSHADED_SOLITAIRE_BOARD_HPP

#include "solenshaded/solitaire_types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace solenshaded {

/**
 * Headless Solitaire Rule Engine, Smart-Tap Solver, Undo Stack & Spring Animator.
 * Models Hardwood Solitaire 3.0 ergonomics and Enshaded (Spectre) mechanics.
 */
class SolitaireBoard {
public:
    SolitaireBoard();

    void newDeal(uint32_t seed, GameVariant variant = GameVariant::KlondikeDraw1);
    void restartDeal();
    void setTableTheme(TableThemeId theme) noexcept { theme_ = theme; }
    [[nodiscard]] TableThemeId tableTheme() const noexcept { return theme_; }

    [[nodiscard]] GameVariant variant() const noexcept { return variant_; }
    [[nodiscard]] uint32_t seed() const noexcept { return seed_; }
    [[nodiscard]] int32_t score() const noexcept { return score_; }
    [[nodiscard]] uint32_t moveCount() const noexcept { return moveCount_; }
    [[nodiscard]] float elapsedSeconds() const noexcept { return elapsedSeconds_; }
    [[nodiscard]] float victorySeconds() const noexcept { return victorySeconds_; }
    [[nodiscard]] bool isVictory() const noexcept;
    [[nodiscard]] bool canAutoFinish() const noexcept;

    // Layout calculation for responsive desktop & mobile aspect ratios
    void computeLayout(float viewportWidth, float viewportHeight);

    // Advance spring-damper card flight trajectories, 2.5D flips, and elevations
    void updateAnimations(float dt);

    // Core interactions
    bool drawFromStock();
    bool canMoveCards(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx) const;
    bool executeMove(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, bool recordUndo = true);

    // Hardwood 3.0 "Smart-Tap" / Right-Click automatic best legal destination
    bool smartMove(uint8_t fromPileIdx, size_t startCardIdx);

    // Auto-promote eligible top cards to Foundations
    bool stepAutoFoundation();

    // Nether Sight (Hint): finds highest-priority legal move and marks spectral glow
    HintMove findBestHint();

    // Unlimited Undo / Redo
    bool undo();
    bool redo();

    // Hit testing helpers for pointer/touch input
    bool hitTestCard(float px, float py, int& outPileIdx, int& outCardIdx) const noexcept;
    int findBestDropPile(uint8_t fromPileIdx, size_t startCardIdx, float dropCenterX, float dropCenterY) const noexcept;

    [[nodiscard]] const std::vector<Pile>& piles() const noexcept { return piles_; }
    [[nodiscard]] std::vector<Pile>& piles() noexcept { return piles_; }
    [[nodiscard]] const HintMove& activeHint() const noexcept { return activeHint_; }
    void clearHint() noexcept;

    [[nodiscard]] static const char* variantName(GameVariant v) noexcept;
    [[nodiscard]] static const char* themeName(TableThemeId t) noexcept;

private:
    uint32_t nextRandom() noexcept;
    void updateCardTargets();

    uint32_t seed_{2026u};
    uint32_t rngState_{2026u};
    GameVariant variant_{GameVariant::KlondikeDraw1};
    TableThemeId theme_{TableThemeId::RosewoodParlor};

    int32_t score_{0};
    uint32_t moveCount_{0};
    float elapsedSeconds_{0.0f};
    float victorySeconds_{0.0f};
    float viewportW_{1280.0f};
    float viewportH_{720.0f};
    float cardW_{96.0f};
    float cardH_{136.0f};

    std::vector<Pile> piles_;
    std::vector<MoveCommand> undoStack_;
    std::vector<MoveCommand> redoStack_;
    HintMove activeHint_{};

public:
    [[nodiscard]] float cardWidth() const noexcept { return cardW_; }
    [[nodiscard]] float cardHeight() const noexcept { return cardH_; }
};

} // namespace solenshaded

#endif // SOLENSHADED_SOLITAIRE_BOARD_HPP
