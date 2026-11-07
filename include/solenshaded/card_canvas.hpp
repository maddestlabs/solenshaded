#ifndef SOLENSHADED_CARD_CANVAS_HPP
#define SOLENSHADED_CARD_CANVAS_HPP

#include "solenshaded/solitaire_board.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace eatsbits::eatscript {
class Evaluator;
}

namespace solenshaded {

/**
 * 2D Canvas State for transform and compositing stack.
 */
struct CanvasTransformState {
    float a{1.0f};  // [a  c  tx]
    float b{0.0f};  // [b  d  ty]
    float c{0.0f};  // [0  0  1 ]
    float d{1.0f};
    float tx{0.0f};
    float ty{0.0f};
    float opacity{1.0f};
    uint32_t tint{0x00000000}; // 0 = no tint, otherwise 0xAARRGGBB
};

/**
 * High-performance, anti-aliased 2D card drawing canvas.
 * Implements drawing primitives, SVG path injection, layer transforms,
 * and high-level card compositing for Eatscript modders.
 */
class CardCanvas {
public:
    CardCanvas(int width, int height);
    ~CardCanvas();

    // Dimensions
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<uint8_t>& rgba() const noexcept { return rgba_; }
    [[nodiscard]] std::vector<uint8_t>& rgba() noexcept { return rgba_; }

    // Clearing & Background
    void clear(uint32_t color = 0x00000000);

    // Transform Stack
    void push();
    void pop();
    void translate(float dx, float dy);
    void rotate(float angleRad);
    void rotateDegrees(float angleDeg);
    void scale(float sx, float sy);
    void rotate180();
    void flipX();
    void flipY();
    void setOpacity(float alpha);
    void setTint(uint32_t color);

    // Basic Drawing Primitives
    void fillRect(float x, float y, float w, float h, uint32_t color);
    void strokeRect(float x, float y, float w, float h, uint32_t color, float lineWidth = 1.0f);
    void fillRoundedRect(float x, float y, float w, float h, float radius, uint32_t color);
    void strokeRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, float lineWidth = 1.0f);
    void fillCircle(float cx, float cy, float radius, uint32_t color);
    void strokeCircle(float cx, float cy, float radius, uint32_t color, float lineWidth = 1.0f);
    void fillOval(float cx, float cy, float rx, float ry, uint32_t color);
    void strokeOval(float cx, float cy, float rx, float ry, uint32_t color, float lineWidth = 1.0f);
    void drawLine(float x0, float y0, float x1, float y1, uint32_t color, float lineWidth = 1.0f);
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);

    // Symmetry & Procedural Baroque Primitives
    void beginSymmetry(bool mirrorX = true, bool mirrorY = true);
    void endSymmetry();
    [[nodiscard]] bool isSymmetryActive() const noexcept { return symmetryActive_; }

    void drawVolute(float cx, float cy, float radius, float turns = 1.25f, float angleStart = 0.0f,
                    uint32_t color = 0xFFD4AF37, float width = 3.5f, const std::string& profile = "fluted_ridge");
    void drawAcanthus(float x1, float y1, float x2, float y2, int lobes = 5,
                      const std::string& curlDir = "left", uint32_t color = 0xFFEDE6D6,
                      uint32_t strokeColor = 0xFFD4AF37, float strokeWidth = 1.2f);
    void repeatBeads(float inset, float radius = 3.5f, float spacing = 10.0f, uint32_t color = 0xFFD4AF37);
    void strokeCartouche(float cx, float cy, float rx, float ry, const std::string& style = "baroque_oval",
                         uint32_t color = 0xFFD4AF37, float lineWidth = 2.5f);
    void drawOrnateSuit(Suit suit, float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor = 0);

    // Baroque Architectural & Masterpiece Primitives (Matching Luxury Museum Decks)
    void drawBaroqueCorner(float x, float y, float size, uint32_t goldColor, uint32_t accentColor = 0);
    void drawBaroqueCartouche(float cx, float cy, float rx, float ry, uint32_t goldColor, uint32_t accentColor = 0, float lineWidth = 2.8f);
    void drawBaroqueCrest(float cx, float cy, float scale, uint32_t goldColor, uint32_t accentColor = 0, bool flipY = false);
    void drawBaroqueSpade(float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor, uint32_t accentColor = 0);
    void drawAcanthusScroll(float cx, float cy, float radius, float angleStart, float angleEnd, uint32_t color, uint32_t strokeColor = 0);

    void setCornerRadius(float r) noexcept { cornerRadius_ = std::max(0.0f, r); }
    [[nodiscard]] float cornerRadius() const noexcept { return cornerRadius_; }

    // Artistic Finishing Pass (Flat, Hand-Sketched, or 3D Relief)
    void applyFinish(const std::string& style, float lightAzimuth = 135.0f, float lightElevation = 45.0f,
                     float aoIntensity = 1.6f, float reliefDepth = 4.0f, uint32_t patinaColor = 0,
                     uint32_t highlightColor = 0, float paperGrain = 0.15f, float inkBleed = 0.2f);

    // SVG & Vector Path Injection
    void drawSvg(const std::string& svgXml, float x = 0.0f, float y = 0.0f, float w = -1.0f, float h = -1.0f);
    void drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color);

    // Typography & Rank Glyphs
    void drawText(const std::string& text, float x, float y, float size, uint32_t color, const std::string& align = "left");
    void drawRank(const std::string& rank, float x, float y, float size, uint32_t color = 0);

    // Classic Deck High-Level Compositing
    void drawSuit(Suit suit, float cx, float cy, float size, uint32_t color = 0);
    void drawCornerIndices(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color = 0, float fontSize = 28.0f);
    void drawPipLayout(int rank, Suit suit, float cardW, float cardH, uint32_t color = 0, const std::string& variant = "classic");
    void drawCourt(const std::string& rank, Suit suit, float x, float y, float w, float h, float opacity = 1.0f, uint32_t tint = 0);
    void drawCardFace(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color = 0, const std::string& layout = "classic");
    void drawPixelSprite(const std::vector<std::string>& spriteLines, float cx, float cy, float pixelSize,
                         uint32_t color, char onChar = '#', bool flipY = false, bool flipX = false);

    // Static Helpers
    static uint32_t parseColor(const std::string& colorStr);
    static Suit parseSuit(const std::string& suitStr);
    static int parseRank(const std::string& rankStr);

    // Evaluator Bindings
    static void bindToEvaluator(eatsbits::eatscript::Evaluator& eval, CardCanvas* canvas);

private:
    int width_{360};
    int height_{504};
    float cornerRadius_{24.0f};
    std::vector<uint8_t> rgba_;
    CanvasTransformState state_;
    std::vector<CanvasTransformState> stateStack_;

    bool symmetryActive_{false};
    bool symmetryMirrorX_{true};
    bool symmetryMirrorY_{true};
    std::vector<std::function<void()>> symmetryQueue_;

    void rasterizeSvgFragment(const std::string& svgXml, float x, float y, float w, float h);
    void compositeBuffer(const uint8_t* srcRgba, int srcW, int srcH, float dstX, float dstY, float dstW, float dstH);
};

} // namespace solenshaded

#endif // SOLENSHADED_CARD_CANVAS_HPP

