#ifndef SOLENSHADED_SETTINGS_DIALOG_HPP
#define SOLENSHADED_SETTINGS_DIALOG_HPP

#include "solenshaded/settings_manager.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <functional>
#include <string>

namespace solenshaded {

class SolitaireBoard;
class AudioFoleyEngine;
struct ThemePalette;

/**
 * General-purpose responsive modal dialog overlay with backdrop darkening,
 * tabbed settings navigation, seed editing/randomization, deck & theme customizer,
 * and audio controls. Leans into SolEnshaded's SVG icon system.
 */
class SettingsDialog {
public:
    SettingsDialog();
    ~SettingsDialog() = default;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal
    );

    // Input routing (returns true if event was absorbed by the modal)
    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleMouseMove(
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleKey(
        int key,
        int action,
        int mods,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleChar(unsigned int codepoint);

    std::function<void()> onSettingsChanged;

private:
    void randomizeSeed();

    bool isOpen_{false};
    int currentTab_{0}; // 0: Game & Seed, 1: Aesthetics, 2: Audio & Display
    AppSettings tempSettings_{};
    std::string seedText_{"2026"};
    bool editingSeed_{false};

    // Cached dialog bounds for hit testing
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{640.0f};
    float dialogH_{440.0f};

    // Slider dragging states
    bool draggingVolume_{false};
    bool draggingDeviation_{false};
    float volTrackX_{0.0f};
    float volTrackY_{0.0f};
    float volTrackW_{280.0f};
    float volTrackH_{26.0f};
    float devTrackX_{0.0f};
    float devTrackY_{0.0f};
    float devTrackW_{280.0f};
    float devTrackH_{26.0f};
};

} // namespace solenshaded

#endif // SOLENSHADED_SETTINGS_DIALOG_HPP

