#include "solenshaded/card_canvas.hpp"
#include "solenshaded/card_texture_cache.hpp"
#include "solenshaded/classic_deck_symbols.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace solenshaded {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string formatColorHex(uint32_t c) {
    uint8_t a = static_cast<uint8_t>((c >> 24) & 0xFF);
    uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(c & 0xFF);
    char buf[32];
    if (a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    } else {
        std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", r, g, b, static_cast<float>(a) / 255.0f);
    }
    return std::string(buf);
}

} // namespace

CardCanvas::CardCanvas(int width, int height)
    : width_(std::max(16, width)), height_(std::max(16, height)) {
    rgba_.assign(static_cast<size_t>(width_ * height_ * 4), 0);
}

CardCanvas::~CardCanvas() = default;

void CardCanvas::clear(uint32_t color) {
    uint8_t a = static_cast<uint8_t>((color >> 24) & 0xFF);
    uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(color & 0xFF);

    const size_t totalPixels = static_cast<size_t>(width_ * height_);
    for (size_t i = 0; i < totalPixels; ++i) {
        rgba_[i * 4 + 0] = r;
        rgba_[i * 4 + 1] = g;
        rgba_[i * 4 + 2] = b;
        rgba_[i * 4 + 3] = a;
    }
}

void CardCanvas::push() {
    stateStack_.push_back(state_);
}

void CardCanvas::pop() {
    if (!stateStack_.empty()) {
        state_ = stateStack_.back();
        stateStack_.pop_back();
    }
}

void CardCanvas::translate(float dx, float dy) {
    state_.tx += state_.a * dx + state_.c * dy;
    state_.ty += state_.b * dx + state_.d * dy;
}

void CardCanvas::rotate(float angleRad) {
    float cosA = std::cos(angleRad);
    float sinA = std::sin(angleRad);

    float a = state_.a * cosA + state_.c * sinA;
    float c = state_.a * (-sinA) + state_.c * cosA;
    float b = state_.b * cosA + state_.d * sinA;
    float d = state_.b * (-sinA) + state_.d * cosA;

    state_.a = a;
    state_.c = c;
    state_.b = b;
    state_.d = d;
}

void CardCanvas::rotateDegrees(float angleDeg) {
    rotate(angleDeg * (kPi / 180.0f));
}

void CardCanvas::scale(float sx, float sy) {
    state_.a *= sx;
    state_.b *= sx;
    state_.c *= sy;
    state_.d *= sy;
}

void CardCanvas::rotate180() {
    scale(-1.0f, -1.0f);
}

void CardCanvas::flipX() {
    scale(-1.0f, 1.0f);
}

void CardCanvas::flipY() {
    scale(1.0f, -1.0f);
}

void CardCanvas::setOpacity(float alpha) {
    state_.opacity = std::clamp(alpha, 0.0f, 1.0f);
}

void CardCanvas::setTint(uint32_t color) {
    state_.tint = color;
}

void CardCanvas::compositeBuffer(const uint8_t* srcRgba, int srcW, int srcH, float dstX, float dstY, float dstW, float dstH) {
    if (!srcRgba || srcW <= 0 || srcH <= 0 || dstW <= 0.0f || dstH <= 0.0f) return;

    int minX = std::max(0, static_cast<int>(std::floor(dstX)));
    int minY = std::max(0, static_cast<int>(std::floor(dstY)));
    int maxX = std::min(width_, static_cast<int>(std::ceil(dstX + dstW)));
    int maxY = std::min(height_, static_cast<int>(std::ceil(dstY + dstH)));

    float invDstW = 1.0f / dstW;
    float invDstH = 1.0f / dstH;

    const float globalAlpha = state_.opacity;
    bool hasTint = ((state_.tint >> 24) & 0xFF) > 0;
    float tintR = static_cast<float>((state_.tint >> 16) & 0xFF);
    float tintG = static_cast<float>((state_.tint >> 8) & 0xFF);
    float tintB = static_cast<float>(state_.tint & 0xFF);
    float tintA = static_cast<float>((state_.tint >> 24) & 0xFF) / 255.0f;

    for (int y = minY; y < maxY; ++y) {
        float srcYF = (static_cast<float>(y) - dstY) * invDstH * static_cast<float>(srcH);
        int srcY = std::clamp(static_cast<int>(srcYF), 0, srcH - 1);

        uint8_t* dstRow = &rgba_[static_cast<size_t>(y * width_ + minX) * 4];
        for (int x = minX; x < maxX; ++x) {
            float srcXF = (static_cast<float>(x) - dstX) * invDstW * static_cast<float>(srcW);
            int srcX = std::clamp(static_cast<int>(srcXF), 0, srcW - 1);

            const uint8_t* srcPixel = &srcRgba[static_cast<size_t>(srcY * srcW + srcX) * 4];
            float sa = (static_cast<float>(srcPixel[3]) / 255.0f) * globalAlpha;

            if (sa > 0.001f) {
                float sr = static_cast<float>(srcPixel[0]);
                float sg = static_cast<float>(srcPixel[1]);
                float sb = static_cast<float>(srcPixel[2]);

                if (hasTint) {
                    sr = sr * (1.0f - tintA) + tintR * tintA;
                    sg = sg * (1.0f - tintA) + tintG * tintA;
                    sb = sb * (1.0f - tintA) + tintB * tintA;
                }

                float da = static_cast<float>(dstRow[3]) / 255.0f;
                float outA = sa + da * (1.0f - sa);

                if (outA > 0.0001f) {
                    float outR = (sr * sa + static_cast<float>(dstRow[0]) * da * (1.0f - sa)) / outA;
                    float outG = (sg * sa + static_cast<float>(dstRow[1]) * da * (1.0f - sa)) / outA;
                    float outB = (sb * sa + static_cast<float>(dstRow[2]) * da * (1.0f - sa)) / outA;

                    dstRow[0] = static_cast<uint8_t>(std::clamp(outR, 0.0f, 255.0f));
                    dstRow[1] = static_cast<uint8_t>(std::clamp(outG, 0.0f, 255.0f));
                    dstRow[2] = static_cast<uint8_t>(std::clamp(outB, 0.0f, 255.0f));
                    dstRow[3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
                }
            }
            dstRow += 4;
        }
    }
}

void CardCanvas::rasterizeSvgFragment(const std::string& svgXml, float x, float y, float w, float h) {
    if (svgXml.empty()) return;

    std::vector<char> xmlCopy(svgXml.begin(), svgXml.end());
    xmlCopy.push_back('\0');

    NSVGimage* img = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!img) return;

    int renderW = (w > 0.0f) ? static_cast<int>(std::ceil(w)) : static_cast<int>(img->width);
    int renderH = (h > 0.0f) ? static_cast<int>(std::ceil(h)) : static_cast<int>(img->height);
    renderW = std::clamp(renderW, 1, 4096);
    renderH = std::clamp(renderH, 1, 4096);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(img);
        return;
    }

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(renderW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(renderH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    std::vector<uint8_t> tmp(static_cast<size_t>(renderW * renderH * 4), 0);
    nsvgRasterize(rast, img, 0.0f, 0.0f, scale, tmp.data(), renderW, renderH, renderW * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    compositeBuffer(tmp.data(), renderW, renderH, x, y, static_cast<float>(renderW), static_cast<float>(renderH));
}

void CardCanvas::fillRect(float x, float y, float w, float h, uint32_t color) {
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::strokeRect(float x, float y, float w, float h, uint32_t color, float lineWidth) {
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << (lineWidth * 0.5f) << "' y='" << (lineWidth * 0.5f) << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillRoundedRect(float x, float y, float w, float h, float radius, uint32_t color) {
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' rx='" << radius << "' ry='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::strokeRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, float lineWidth) {
    std::string hex = formatColorHex(color);
    float offset = lineWidth * 0.5f;
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << offset << "' y='" << offset << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' rx='" << radius << "' ry='" << radius << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillCircle(float cx, float cy, float radius, uint32_t color) {
    float diam = radius * 2.0f;
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << diam << "' height='" << diam << "' viewBox='0 0 " << diam << " " << diam << "'>"
       << "<circle cx='" << radius << "' cy='" << radius << "' r='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), (state_.tx + state_.a * cx) - radius * std::abs(state_.a), (state_.ty + state_.d * cy) - radius * std::abs(state_.d), diam * std::abs(state_.a), diam * std::abs(state_.d));
}

void CardCanvas::strokeCircle(float cx, float cy, float radius, uint32_t color, float lineWidth) {
    float diam = (radius + lineWidth) * 2.0f;
    float center = radius + lineWidth;
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << diam << "' height='" << diam << "' viewBox='0 0 " << diam << " " << diam << "'>"
       << "<circle cx='" << center << "' cy='" << center << "' r='" << radius << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), (state_.tx + state_.a * cx) - center * std::abs(state_.a), (state_.ty + state_.d * cy) - center * std::abs(state_.d), diam * std::abs(state_.a), diam * std::abs(state_.d));
}

void CardCanvas::drawLine(float x0, float y0, float x1, float y1, uint32_t color, float lineWidth) {
    float minX = std::min(x0, x1) - lineWidth;
    float minY = std::min(y0, y1) - lineWidth;
    float maxX = std::max(x0, x1) + lineWidth;
    float maxY = std::max(y0, y1) + lineWidth;
    float w = std::max(1.0f, maxX - minX);
    float h = std::max(1.0f, maxY - minY);

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='" << minX << " " << minY << " " << w << " " << h << "'>"
       << "<line x1='" << x0 << "' y1='" << y0 << "' x2='" << x1 << "' y2='" << y1 << "' stroke='" << hex << "' stroke-width='" << lineWidth << "' stroke-linecap='round'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * minX, state_.ty + state_.d * minY, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color) {
    float minX = std::min({x0, x1, x2});
    float minY = std::min({y0, y1, y2});
    float maxX = std::max({x0, x1, x2});
    float maxY = std::max({y0, y1, y2});
    float w = std::max(1.0f, maxX - minX);
    float h = std::max(1.0f, maxY - minY);

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='" << minX << " " << minY << " " << w << " " << h << "'>"
       << "<polygon points='" << x0 << "," << y0 << " " << x1 << "," << y1 << " " << x2 << "," << y2 << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * minX, state_.ty + state_.d * minY, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::drawSvg(const std::string& svgXml, float x, float y, float w, float h) {
    rasterizeSvgFragment(svgXml, state_.tx + state_.a * x, state_.ty + state_.d * y, (w > 0.0f) ? (w * std::abs(state_.a)) : -1.0f, (h > 0.0f) ? (h * std::abs(state_.d)) : -1.0f);
}

void CardCanvas::drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color) {
    float size = scale * 2.0f;
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << size << "' height='" << size << "' viewBox='-1 -1 2 2'>"
       << "<path d='" << pathD << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), (state_.tx + state_.a * cx) - scale * std::abs(state_.a), (state_.ty + state_.d * cy) - scale * std::abs(state_.d), size * std::abs(state_.a), size * std::abs(state_.d));
}

void CardCanvas::drawText(const std::string& text, float x, float y, float size, uint32_t color, const std::string& align) {
    if (text.empty()) return;
    std::string hex = formatColorHex(color);
    std::string anchor = (align == "center") ? "middle" : ((align == "right") ? "end" : "start");
    float estW = size * static_cast<float>(text.size()) * 1.2f;
    float estH = size * 1.5f;

    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << estW << "' height='" << estH << "' viewBox='0 0 " << estW << " " << estH << "'>"
       << "<text x='" << (align == "center" ? (estW * 0.5f) : (align == "right" ? estW : 0.0f)) << "' y='" << (size * 0.9f)
       << "' font-family='serif, Times New Roman, sans-serif' font-weight='bold' font-size='" << size
       << "' text-anchor='" << anchor << "' fill='" << hex << "'>"
       << text << "</text></svg>";

    float targetX = state_.tx + state_.a * x;
    float targetY = state_.ty + state_.d * y;
    if (align == "center") targetX -= (estW * 0.5f) * std::abs(state_.a);
    else if (align == "right") targetX -= estW * std::abs(state_.a);

    rasterizeSvgFragment(ss.str(), targetX, targetY, estW * std::abs(state_.a), estH * std::abs(state_.d));
}

void CardCanvas::drawSuit(Suit suit, float cx, float cy, float size, uint32_t color) {
    std::string_view path;
    switch (suit) {
        case Suit::Spades:   path = kClassicSuitPathSpades; break;
        case Suit::Hearts:   path = kClassicSuitPathHearts; break;
        case Suit::Clubs:    path = kClassicSuitPathClubs; break;
        case Suit::Diamonds: path = kClassicSuitPathDiamonds; break;
    }
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }
    drawSvgPath(std::string(path), cx, cy, size * 0.5f, color);
}

void CardCanvas::drawCornerIndices(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, float fontSize) {
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    float pipSize = fontSize * 0.85f;
    float marginX = cardW * 0.09f;
    float marginY = cardH * 0.05f;

    // Top-left index
    drawText(rank, marginX, marginY + fontSize * 0.2f, fontSize, color, "center");
    drawSuit(suit, marginX, marginY + fontSize * 1.05f, pipSize, color);

    // Bottom-right inverted index (180 degree rotation)
    push();
    translate(cardW, cardH);
    rotate180();
    drawText(rank, marginX, marginY + fontSize * 0.2f, fontSize, color, "center");
    drawSuit(suit, marginX, marginY + fontSize * 1.05f, pipSize, color);
    pop();
}

void CardCanvas::drawPipLayout(int rank, Suit suit, float cardW, float cardH, uint32_t color, const std::string& /*variant*/) {
    if (rank < 1 || rank > 10) return;
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    const auto& layout = getStandardPipLayout(rank);
    float basePipSize = cardW * 0.17f;

    for (const auto& item : layout) {
        float px = item.x * cardW;
        float py = item.y * cardH;
        float pSize = basePipSize * item.scale;

        if (item.flipY) {
            push();
            translate(px, py);
            rotate180();
            drawSuit(suit, 0, 0, pSize, color);
            pop();
        } else {
            drawSuit(suit, px, py, pSize, color);
        }
    }
}

void CardCanvas::drawCourt(const std::string& rank, Suit suit, float x, float y, float w, float h, float opacity, uint32_t tint) {
    int rInt = parseRank(rank);
    if (rInt < 11 || rInt > 13) return;

    Rank r = static_cast<Rank>(rInt);
    const CardTexture* tex = CardTextureCache::instance().getCardTexture(suit, r);
    if (!tex || tex->rgba.empty() || tex->width <= 0 || tex->height <= 0) return;

    push();
    if (opacity < 1.0f) setOpacity(opacity);
    if (tint != 0) setTint(tint);

    compositeBuffer(tex->rgba.data(), tex->width, tex->height, state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
    pop();
}

void CardCanvas::drawCardFace(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, const std::string& layout) {
    // 1. Crisp rounded card plate
    fillRoundedRect(0, 0, cardW, cardH, 8.0f, 0xFFFFFFFF);
    strokeRoundedRect(0, 0, cardW, cardH, 8.0f, 0xFFE0DACF, 1.0f);

    int rInt = parseRank(rank);
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    // 2. Corner indices
    drawCornerIndices(rank, suit, cardW, cardH, color, cardW * 0.095f);

    // 3. Central element (Court illustration or Pip Matrix)
    if (rInt >= 11 && rInt <= 13) {
        // Court card: draw centered court illustration with a frame
        float marginX = cardW * 0.16f;
        float marginY = cardH * 0.12f;
        float innerW = cardW - marginX * 2.0f;
        float innerH = cardH - marginY * 2.0f;
        drawCourt(rank, suit, marginX, marginY, innerW, innerH);
        strokeRect(marginX, marginY, innerW, innerH, color, 1.2f);
    } else {
        // Number card: draw layout
        drawPipLayout(rInt, suit, cardW, cardH, color, layout);
    }
}

uint32_t CardCanvas::parseColor(const std::string& colorStr) {
    if (colorStr.empty()) return 0xFFFFFFFF;
    if (colorStr[0] == '#') {
        std::string hex = colorStr.substr(1);
        if (hex.size() == 3) {
            uint32_t r = std::stoul(hex.substr(0, 1) + hex.substr(0, 1), nullptr, 16);
            uint32_t g = std::stoul(hex.substr(1, 1) + hex.substr(1, 1), nullptr, 16);
            uint32_t b = std::stoul(hex.substr(2, 1) + hex.substr(2, 1), nullptr, 16);
            return 0xFF000000 | (r << 16) | (g << 8) | b;
        } else if (hex.size() == 6) {
            uint32_t rgb = std::stoul(hex, nullptr, 16);
            return 0xFF000000 | rgb;
        } else if (hex.size() == 8) {
            return std::stoul(hex, nullptr, 16);
        }
    }
    if (colorStr == "red") return 0xFFCC0000;
    if (colorStr == "black") return 0xFF1A1A1A;
    if (colorStr == "white") return 0xFFFFFFFF;
    if (colorStr == "gold") return 0xFFD4AF37;
    return 0xFFFFFFFF;
}

Suit CardCanvas::parseSuit(const std::string& suitStr) {
    if (suitStr == "hearts" || suitStr == "Hearts") return Suit::Hearts;
    if (suitStr == "diamonds" || suitStr == "Diamonds") return Suit::Diamonds;
    if (suitStr == "clubs" || suitStr == "Clubs") return Suit::Clubs;
    return Suit::Spades;
}

int CardCanvas::parseRank(const std::string& rankStr) {
    if (rankStr == "A" || rankStr == "a" || rankStr == "1") return 1;
    if (rankStr == "J" || rankStr == "j" || rankStr == "11") return 11;
    if (rankStr == "Q" || rankStr == "q" || rankStr == "12") return 12;
    if (rankStr == "K" || rankStr == "k" || rankStr == "13") return 13;
    try {
        return std::stoi(rankStr);
    } catch (...) {
        return 1;
    }
}

void CardCanvas::bindToEvaluator(eatsbits::eatscript::Evaluator& eval, CardCanvas* canvas) {
    // 1. ctx.fill_rect(x, y, w, h, color)
    eval.registerMemberFunction("ctx", "fill_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        canvas->fillRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 2. ctx.stroke_rect(x, y, w, h, color, width)
    eval.registerMemberFunction("ctx", "stroke_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        float lw = (args.size() > 5) ? args[5].asFloat(1.0f) : 1.0f;
        canvas->strokeRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 3. ctx.fill_rounded_rect(x, y, w, h, radius, color)
    eval.registerMemberFunction("ctx", "fill_rounded_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 6) return eatsbits::eatscript::Value(false);
        uint32_t col = args[5].isString() ? CardCanvas::parseColor(args[5].asString()) : static_cast<uint32_t>(args[5].asInt());
        canvas->fillRoundedRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), args[4].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 4. ctx.stroke_rounded_rect(x, y, w, h, radius, color, width)
    eval.registerMemberFunction("ctx", "stroke_rounded_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 6) return eatsbits::eatscript::Value(false);
        uint32_t col = args[5].isString() ? CardCanvas::parseColor(args[5].asString()) : static_cast<uint32_t>(args[5].asInt());
        float lw = (args.size() > 6) ? args[6].asFloat(1.0f) : 1.0f;
        canvas->strokeRoundedRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), args[4].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 5. ctx.fill_circle(cx, cy, radius, color)
    eval.registerMemberFunction("ctx", "fill_circle", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        uint32_t col = args[3].isString() ? CardCanvas::parseColor(args[3].asString()) : static_cast<uint32_t>(args[3].asInt());
        canvas->fillCircle(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 6. ctx.stroke_circle(cx, cy, radius, color, width)
    eval.registerMemberFunction("ctx", "stroke_circle", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        uint32_t col = args[3].isString() ? CardCanvas::parseColor(args[3].asString()) : static_cast<uint32_t>(args[3].asInt());
        float lw = (args.size() > 4) ? args[4].asFloat(1.0f) : 1.0f;
        canvas->strokeCircle(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 7. ctx.draw_line(x0, y0, x1, y1, color, width)
    eval.registerMemberFunction("ctx", "draw_line", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        float lw = (args.size() > 5) ? args[5].asFloat(1.0f) : 1.0f;
        canvas->drawLine(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 8. ctx.draw_svg(svg_xml, x, y, w, h)
    eval.registerMemberFunction("ctx", "draw_svg", [canvas](const auto& args, const auto&) {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        float x = (args.size() > 1) ? args[1].asFloat(0.0f) : 0.0f;
        float y = (args.size() > 2) ? args[2].asFloat(0.0f) : 0.0f;
        float w = (args.size() > 3) ? args[3].asFloat(-1.0f) : -1.0f;
        float h = (args.size() > 4) ? args[4].asFloat(-1.0f) : -1.0f;
        canvas->drawSvg(args[0].asString(), x, y, w, h);
        return eatsbits::eatscript::Value(true);
    });

    // 9. ctx.draw_svg_path(path_d, cx, cy, scale, color)
    eval.registerMemberFunction("ctx", "draw_svg_path", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        canvas->drawSvgPath(args[0].asString(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 10. Transforms: push, pop, translate, rotate, scale, rotate_180, flip_x, flip_y, set_opacity, set_tint
    eval.registerMemberFunction("ctx", "push", [canvas](const auto&, const auto&) {
        if (canvas) canvas->push();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "pop", [canvas](const auto&, const auto&) {
        if (canvas) canvas->pop();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "translate", [canvas](const auto& args, const auto&) {
        if (canvas && args.size() >= 2) canvas->translate(args[0].asFloat(), args[1].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "rotate", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) canvas->rotateDegrees(args[0].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "scale", [canvas](const auto& args, const auto&) {
        if (canvas && args.size() >= 2) canvas->scale(args[0].asFloat(), args[1].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "rotate_180", [canvas](const auto&, const auto&) {
        if (canvas) canvas->rotate180();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "flip_x", [canvas](const auto&, const auto&) {
        if (canvas) canvas->flipX();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "flip_y", [canvas](const auto&, const auto&) {
        if (canvas) canvas->flipY();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "set_opacity", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) canvas->setOpacity(args[0].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "set_tint", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) {
            uint32_t col = args[0].isString() ? CardCanvas::parseColor(args[0].asString()) : static_cast<uint32_t>(args[0].asInt());
            canvas->setTint(col);
        }
        return eatsbits::eatscript::Value(true);
    });

    // 11. classic_deck Module
    auto drawSuitFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[0].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        canvas->drawSuit(s, args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCornerIndicesFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[1].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        float fSize = (args.size() > 5) ? args[5].asFloat(28.0f) : 28.0f;
        canvas->drawCornerIndices(args[0].asString(), s, args[2].asFloat(), args[3].asFloat(), col, fSize);
        return eatsbits::eatscript::Value(true);
    };

    auto drawPipLayoutFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        int r = args[0].isString() ? CardCanvas::parseRank(args[0].asString()) : args[0].asInt();
        Suit s = CardCanvas::parseSuit(args[1].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        std::string variant = (args.size() > 5) ? args[5].asString() : "classic";
        canvas->drawPipLayout(r, s, args[2].asFloat(), args[3].asFloat(), col, variant);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCourtFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 6) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[1].asString());
        float op = (args.size() > 6) ? args[6].asFloat(1.0f) : 1.0f;
        uint32_t tint = (args.size() > 7) ? (args[7].isString() ? CardCanvas::parseColor(args[7].asString()) : static_cast<uint32_t>(args[7].asInt())) : 0;
        canvas->drawCourt(args[0].asString(), s, args[2].asFloat(), args[3].asFloat(), args[4].asFloat(), args[5].asFloat(), op, tint);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCardFaceFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[1].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        std::string layout = (args.size() > 5) ? args[5].asString() : "classic";
        canvas->drawCardFace(args[0].asString(), s, args[2].asFloat(), args[3].asFloat(), col, layout);
        return eatsbits::eatscript::Value(true);
    };

    // Register on both classic_deck and ctx for convenience
    eval.setVariable("classic_deck", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    eval.registerMemberFunction("classic_deck", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("classic_deck", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("classic_deck", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("classic_deck", "draw_court", drawCourtFn);
    eval.registerMemberFunction("classic_deck", "draw_card_face", drawCardFaceFn);

    eval.registerMemberFunction("ctx", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("ctx", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("ctx", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("ctx", "draw_court", drawCourtFn);
    eval.registerMemberFunction("ctx", "draw_card_face", drawCardFaceFn);
}

} // namespace solenshaded
