#ifndef SOLENSHADED_SETTINGS_MANAGER_HPP
#define SOLENSHADED_SETTINGS_MANAGER_HPP

#include "solenshaded/solitaire_types.hpp"
#include "solenshaded/card_texture_cache.hpp"
#include "solenshaded/table_renderer.hpp"
#include "solenshaded/dynamic_light.hpp"
#include "solenshaded/audio_foley.hpp"
#include <string>
#include <cstdint>

namespace solenshaded {

class SolitaireBoard;

struct AppSettings {
    GameVariant variant{GameVariant::KlondikeDraw1};
    TableThemeId tableTheme{TableThemeId::PastelTan};
    DeckThemeId deckTheme{DeckThemeId::SolEnshade};
    CardWearLevel cardWear{CardWearLevel::Pristine};
    CardStock cardStock{CardStock::SmoothParchment};
    CardCornerStyle cardCorners{CardCornerStyle::Auto};
    float cardDeviation{0.25f}; // 0.0 = perfectly neat (0px, 0 deg), 0.25 = Subtle (+-2.5px, +-1.25 deg), 1.0 = maximum organic skew (+-10px, +-5 deg)
    ResponsiveLayoutMode layoutMode{ResponsiveLayoutMode::Auto};
    StockPosition stockPosition{StockPosition::Auto};
    MenuAnchor menuAnchor{MenuAnchor::BottomLeft};
    MenuDisplayMode menuDisplayMode{MenuDisplayMode::Auto};
    int aaMode{2}; // 1 = 1x, 2 = 2x RGSS, 4 = 4x SSAA
    LightingPreset lightingPreset{LightingPreset::StudioSteady}; // Animated lighting (default StudioSteady = off)
    float masterVolume{0.85f};
    bool muted{false};
    ShuffleStyle shuffleStyle{ShuffleStyle::Soft};
    bool randomizeOnNewDeal{true};
    uint32_t lastSeed{2026u};
    bool autoFinish{true};
    bool showTableMarkings{true}; // true = guides/runes/wells, false = pure bare tabletop (cards only)
};

class SettingsManager {
public:
    static SettingsManager& instance();

    bool load();
    bool save();

    [[nodiscard]] const AppSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] AppSettings& settings() noexcept { return settings_; }

    void applyToGame(SolitaireBoard& board, AudioFoleyEngine& foley, bool applyTextures = true);
    void syncFromGame(const SolitaireBoard& board, const AudioFoleyEngine& foley);

    [[nodiscard]] std::string getSettingsFilePath() const;

private:
    SettingsManager() = default;
    AppSettings settings_{};
};

} // namespace solenshaded

#endif // SOLENSHADED_SETTINGS_MANAGER_HPP

