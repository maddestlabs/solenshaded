#ifndef SOLENSHADED_CARD_CANVAS_HPP
#define SOLENSHADED_CARD_CANVAS_HPP

#include "solenshaded/solitaire_board.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

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
    void drawLine(float x0, float y0, float x1, float y1, uint32_t color, float lineWidth = 1.0f);
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);

    // SVG & Vector Path Injection
    void drawSvg(const std::string& svgXml, float x = 0.0f, float y = 0.0f, float w = -1.0f, float h = -1.0f);
    void drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color);

    // Typography
    void drawText(const std::string& text, float x, float y, float size, uint32_t color, const std::string& align = "left");

    // Classic Deck High-Level Compositing
    void drawSuit(Suit suit, float cx, float cy, float size, uint32_t color = 0);
    void drawCornerIndices(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color = 0, float fontSize = 28.0f);
    void drawPipLayout(int rank, Suit suit, float cardW, float cardH, uint32_t color = 0, const std::string& variant = "classic");
    void drawCourt(const std::string& rank, Suit suit, float x, float y, float w, float h, float opacity = 1.0f, uint32_t tint = 0);
    void drawCardFace(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color = 0, const std::string& layout = "classic");

    // Static Helpers
    static uint32_t parseColor(const std::string& colorStr);
    static Suit parseSuit(const std::string& suitStr);
    static int parseRank(const std::string& rankStr);

    // Evaluator Bindings
    static void bindToEvaluator(eatsbits::eatscript::Evaluator& eval, CardCanvas* canvas);

private:
    int width_{360};
    int height_{504};
    std::vector<uint8_t> rgba_;
    CanvasTransformState state_;
    std::vector<CanvasTransformState> stateStack_;

    void rasterizeSvgFragment(const std::string& svgXml, float x, float y, float w, float h);
    void compositeBuffer(const uint8_t* srcRgba, int srcW, int srcH, float dstX, float dstY, float dstW, float dstH);
};

} // namespace solenshaded

#endif // SOLENSHADED_CARD_CANVAS_HPP

