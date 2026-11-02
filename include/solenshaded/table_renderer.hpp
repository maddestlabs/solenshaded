#ifndef SOLENSHADED_TABLE_RENDERER_HPP
#define SOLENSHADED_TABLE_RENDERER_HPP

#include "solenshaded/solitaire_board.hpp"
#include "solenshaded/audio_foley.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <cstdint>
#include <string>

namespace solenshaded {

/**
 * 100% Procedural Hardwood 3.0 & Enshaded Solitaire Renderer.
 * Synthesizes carved hardwood frames, baize/obsidian surfaces, directional drop shadows,
 * parchment cards, 2.5D card flips, vector suit pips, geometric card backs, and interactive HUD chrome.
 */
class TableRenderer {
public:
    TableRenderer() = default;
    ~TableRenderer() = default;

    static void renderBoard(
        eatsbits::ui::BatchRenderer2D& r,
        const SolitaireBoard& board,
        float width,
        float height,
        int draggedPile = -1,
        int draggedCardIdx = -1,
        float dragOffsetX = 0.0f,
        float dragOffsetY = 0.0f,
        int hoveredButtonId = -1
    );

    static bool renderToPng(
        SolitaireBoard& board,
        uint32_t width,
        uint32_t height,
        const std::string& outputPngPath,
        int aaMode = 2
    );

    static int runInteractiveWindow(
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        uint32_t width = 1280,
        uint32_t height = 720,
        int initialAaMode = 2
    );

    static bool writePngFile(
        const std::string& filePath,
        uint32_t width,
        uint32_t height,
        const uint8_t* rgbaPixels
    );
};

} // namespace solenshaded

#endif // SOLENSHADED_TABLE_RENDERER_HPP
