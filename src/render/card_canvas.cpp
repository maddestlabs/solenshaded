#include "solenshaded/card_canvas.hpp"
#include "solenshaded/card_texture_cache.hpp"
#include "solenshaded/classic_deck_symbols.hpp"
#include "eatstextures/field_ops.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/monospace_font_8x16.hpp"
#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

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

    const float globalAlpha = state_.opacity;
    bool hasTint = ((state_.tint >> 24) & 0xFF) > 0;
    float tintR = static_cast<float>((state_.tint >> 16) & 0xFF);
    float tintG = static_cast<float>((state_.tint >> 8) & 0xFF);
    float tintB = static_cast<float>(state_.tint & 0xFF);
    float tintA = static_cast<float>((state_.tint >> 24) & 0xFF) / 255.0f;

    // Fast direct 1:1 composite for full-size fragments
    if (srcW == width_ && srcH == height_ && dstX == 0.0f && dstY == 0.0f &&
        dstW == static_cast<float>(width_) && dstH == static_cast<float>(height_) &&
        globalAlpha >= 0.999f && !hasTint) {
        const size_t totalPixels = static_cast<size_t>(width_ * height_);
        const uint8_t* s = srcRgba;
        uint8_t* d = rgba_.data();
        for (size_t i = 0; i < totalPixels; ++i, s += 4, d += 4) {
            uint8_t sa = s[3];
            if (sa == 0) continue;
            if (sa == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            } else {
                float a = static_cast<float>(sa) / 255.0f;
                float invA = 1.0f - a;
                d[0] = static_cast<uint8_t>(s[0] * a + d[0] * invA);
                d[1] = static_cast<uint8_t>(s[1] * a + d[1] * invA);
                d[2] = static_cast<uint8_t>(s[2] * a + d[2] * invA);
                d[3] = static_cast<uint8_t>(std::clamp(static_cast<float>(d[3]) + static_cast<float>(sa) * invA, 0.0f, 255.0f));
            }
        }
        return;
    }

    int minX = std::max(0, static_cast<int>(std::floor(dstX)));
    int minY = std::max(0, static_cast<int>(std::floor(dstY)));
    int maxX = std::min(width_, static_cast<int>(std::ceil(dstX + dstW)));
    int maxY = std::min(height_, static_cast<int>(std::ceil(dstY + dstH)));

    float invDstW = 1.0f / dstW;
    float invDstH = 1.0f / dstH;

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

    thread_local static NSVGrasterizer* sRast = nullptr;
    if (!sRast) {
        sRast = nsvgCreateRasterizer();
    }
    if (!sRast) {
        nsvgDelete(img);
        return;
    }

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(renderW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(renderH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    thread_local static std::vector<uint8_t> sTmpBuffer;
    size_t needed = static_cast<size_t>(renderW * renderH * 4);
    if (sTmpBuffer.size() < needed) {
        sTmpBuffer.resize(needed);
    }
    std::fill(sTmpBuffer.begin(), sTmpBuffer.begin() + needed, 0);

    nsvgRasterize(sRast, img, 0.0f, 0.0f, scale, sTmpBuffer.data(), renderW, renderH, renderW * 4);
    nsvgDelete(img);

    compositeBuffer(sTmpBuffer.data(), renderW, renderH, x, y, static_cast<float>(renderW), static_cast<float>(renderH));
}

void CardCanvas::fillRect(float x, float y, float w, float h, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, color]() { fillRect(x, y, w, h, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::strokeRect(float x, float y, float w, float h, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, color, lineWidth]() { strokeRect(x, y, w, h, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << (lineWidth * 0.5f) << "' y='" << (lineWidth * 0.5f) << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillRoundedRect(float x, float y, float w, float h, float radius, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, radius, color]() { fillRoundedRect(x, y, w, h, radius, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' rx='" << radius << "' ry='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::strokeRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, radius, color, lineWidth]() { strokeRoundedRect(x, y, w, h, radius, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << (lineWidth * 0.5f) << "' y='" << (lineWidth * 0.5f) << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' rx='" << radius << "' ry='" << radius << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillCircle(float cx, float cy, float radius, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, color]() { fillCircle(cx, cy, radius, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    float d = radius * 2.0f;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << d << "' height='" << d << "' viewBox='0 0 " << d << " " << d << "'>"
       << "<circle cx='" << radius << "' cy='" << radius << "' r='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * (cx - radius), state_.ty + state_.d * (cy - radius), d * std::abs(state_.a), d * std::abs(state_.d));
}

void CardCanvas::strokeCircle(float cx, float cy, float radius, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, color, lineWidth]() { strokeCircle(cx, cy, radius, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    float d = radius * 2.0f;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << d << "' height='" << d << "' viewBox='0 0 " << d << " " << d << "'>"
       << "<circle cx='" << radius << "' cy='" << radius << "' r='" << (radius - lineWidth * 0.5f) << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * (cx - radius), state_.ty + state_.d * (cy - radius), d * std::abs(state_.a), d * std::abs(state_.d));
}

void CardCanvas::fillOval(float cx, float cy, float rx, float ry, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, color]() { fillOval(cx, cy, rx, ry, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<ellipse cx='" << cx << "' cy='" << cy << "' rx='" << rx << "' ry='" << ry << "' fill='" << hex << "'/>";
    ss << "</g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::strokeOval(float cx, float cy, float rx, float ry, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, color, lineWidth]() { strokeOval(cx, cy, rx, ry, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<ellipse cx='" << cx << "' cy='" << cy << "' rx='" << rx << "' ry='" << ry
       << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>";
    ss << "</g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawLine(float x0, float y0, float x1, float y1, uint32_t color, float width) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x0, y0, x1, y1, color, width]() { drawLine(x0, y0, x1, y1, color, width); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_ << "' viewBox='0 0 " << width_ << " " << height_ << "'>"
       << "<line x1='" << (state_.tx + state_.a * x0) << "' y1='" << (state_.ty + state_.d * y0)
       << "' x2='" << (state_.tx + state_.a * x1) << "' y2='" << (state_.ty + state_.d * y1)
       << "' stroke='" << hex << "' stroke-width='" << (width * std::abs(state_.a)) << "' stroke-linecap='round'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawSvg(const std::string& svgXml, float x, float y, float w, float h) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, svgXml, x, y, w, h]() { drawSvg(svgXml, x, y, w, h); });
    }
    rasterizeSvgFragment(svgXml, state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, pathD, cx, cy, scale, color]() { drawSvgPath(pathD, cx, cy, scale, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_ << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << ")'>";
    ss << "<path d='" << pathD << "' fill='" << hex << "'/>";
    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawText(const std::string& text, float x, float y, float size, uint32_t color, const std::string& align) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, text, x, y, size, color, align]() { drawText(text, x, y, size, color, align); });
    }
    if (text.empty() || size <= 0.0f) return;
    if (color == 0) color = 0xFF1A1A1A;

    const float charH = 16.0f;
    const float charAdvance = 8.5f;
    const float scale = size / charH;
    const float totalW = static_cast<float>(text.size()) * charAdvance * scale;

    float startX = x;
    if (align == "center") {
        startX = x - totalW * 0.5f;
    } else if (align == "right") {
        startX = x - totalW;
    }

    std::string hex = formatColorHex(color);
    std::ostringstream pathD;

    for (size_t ci = 0; ci < text.size(); ++ci) {
        uint8_t ch = static_cast<uint8_t>(text[ci]);
        const uint8_t* glyph = eatsbits::ui::getGlyph8x16Bitmap(ch);
        float gx = startX + static_cast<float>(ci) * charAdvance * scale;

        for (int row = 0; row < 16; ++row) {
            uint8_t rowBits = glyph[row];
            if (rowBits == 0) continue;
            for (int col = 0; col < 8; ++col) {
                if (rowBits & (1 << (7 - col))) {
                    float px = gx + static_cast<float>(col) * scale;
                    float py = y + static_cast<float>(row) * scale;
                    pathD << "M " << px << " " << py
                          << " h " << scale << " v " << scale << " h " << -scale << " Z ";
                }
            }
        }
    }

    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<path d='" << pathD.str() << "' fill='" << hex << "'/>";
    ss << "</g></svg>";

    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawRank(const std::string& rank, float x, float y, float size, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, rank, x, y, size, color]() { drawRank(rank, x, y, size, color); });
    }
    if (rank.empty()) return;
    auto glyph = getClassicRankGlyph(rank);
    if (glyph.path.empty()) return;

    if (color == 0) color = 0xFF1A1A1A;

    float aspect = glyph.width / glyph.height;
    float drawH = size;
    float drawW = size * aspect;

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << x << " " << y << ") scale(" << (drawW / glyph.width) << " " << (drawH / glyph.height)
       << ") translate(" << (-glyph.minX - glyph.width * 0.5f) << " " << (-glyph.minY - glyph.height * 0.5f) << ")'>";
    ss << "<path d='" << glyph.path << "' fill='" << hex << "'/>";
    ss << "</g></g></svg>";

    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawSuit(Suit suit, float cx, float cy, float size, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, suit, cx, cy, size, color]() { drawSuit(suit, cx, cy, size, color); });
    }
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

void CardCanvas::beginSymmetry(bool mirrorX, bool mirrorY) {
    symmetryActive_ = true;
    symmetryMirrorX_ = mirrorX;
    symmetryMirrorY_ = mirrorY;
    symmetryQueue_.clear();
}

void CardCanvas::endSymmetry() {
    if (!symmetryActive_) return;
    symmetryActive_ = false;

    auto ops = std::move(symmetryQueue_);
    symmetryQueue_.clear();

    for (const auto& op : ops) op();

    if (symmetryMirrorX_) {
        push();
        translate(static_cast<float>(width_), 0.0f);
        flipX();
        for (const auto& op : ops) op();
        pop();
    }
    if (symmetryMirrorY_) {
        push();
        translate(0.0f, static_cast<float>(height_));
        flipY();
        for (const auto& op : ops) op();
        pop();
    }
    if (symmetryMirrorX_ && symmetryMirrorY_) {
        push();
        translate(static_cast<float>(width_), static_cast<float>(height_));
        flipX();
        flipY();
        for (const auto& op : ops) op();
        pop();
    }
}

void CardCanvas::drawVolute(float cx, float cy, float radius, float turns, float angleStart,
                            uint32_t color, float width, const std::string& profile) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, turns, angleStart, color, width, profile]() {
            drawVolute(cx, cy, radius, turns, angleStart, color, width, profile);
        });
    }

    if (radius <= 1.0f || turns <= 0.0f) return;

    const int steps = std::clamp(static_cast<int>(turns * 48.0f), 24, 256);
    const float totalAngle = turns * 2.0f * kPi;
    const float rMin = std::max(1.0f, radius * 0.12f);
    const float growthRate = std::log(radius / rMin);

    std::vector<std::pair<float, float>> leftEdge;
    std::vector<std::pair<float, float>> rightEdge;
    leftEdge.reserve(steps + 1);
    rightEdge.reserve(steps + 1);

    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float theta = angleStart + t * totalAngle;
        float r = rMin * std::exp(growthRate * t);

        float w = width * (0.2f + 0.8f * std::pow(t, 0.7f));

        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        float px = cx + r * cosT;
        float py = cy + r * sinT;

        float dr = r * (growthRate / totalAngle);
        float tx = dr * cosT - r * sinT;
        float ty = dr * sinT + r * cosT;
        float tlen = std::hypot(tx, ty);
        if (tlen > 1e-4f) {
            tx /= tlen;
            ty /= tlen;
        } else {
            tx = -sinT;
            ty = cosT;
        }

        float nx = -ty;
        float ny = tx;

        leftEdge.push_back({px + nx * (w * 0.5f), py + ny * (w * 0.5f)});
        rightEdge.push_back({px - nx * (w * 0.5f), py - ny * (w * 0.5f)});
    }

    std::ostringstream path;
    path << "M " << leftEdge[0].first << " " << leftEdge[0].second;
    for (size_t i = 1; i < leftEdge.size(); ++i) {
        path << " L " << leftEdge[i].first << " " << leftEdge[i].second;
    }
    for (int i = static_cast<int>(rightEdge.size()) - 1; i >= 0; --i) {
        path << " L " << rightEdge[i].first << " " << rightEdge[i].second;
    }
    path << " Z";

    std::string hex = formatColorHex(color);
    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='" << hex << "' stroke='" << hex << "' stroke-width='0.5'/>";

    if (profile == "fluted_ridge") {
        uint8_t a = (color >> 24) & 0xFF;
        uint8_t r = std::min(255, static_cast<int>(((color >> 16) & 0xFF) * 1.35f));
        uint8_t g = std::min(255, static_cast<int>(((color >> 8) & 0xFF) * 1.35f));
        uint8_t b = std::min(255, static_cast<int>((color & 0xFF) * 1.35f));
        uint32_t spineCol = (a << 24) | (r << 16) | (g << 8) | b;
        std::string spineHex = formatColorHex(spineCol);

        svg << "<path d='";
        for (size_t i = 0; i < leftEdge.size(); ++i) {
            float mx = (leftEdge[i].first + rightEdge[i].first) * 0.5f;
            float my = (leftEdge[i].second + rightEdge[i].second) * 0.5f;
            if (i == 0) svg << "M " << mx << " " << my;
            else svg << " L " << mx << " " << my;
        }
        svg << "' fill='none' stroke='" << spineHex << "' stroke-width='" << std::max(0.7f, width * 0.28f) << "' stroke-linecap='round'/>";
    }

    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawAcanthus(float x1, float y1, float x2, float y2, int lobes,
                              const std::string& curlDir, uint32_t color,
                              uint32_t strokeColor, float strokeWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x1, y1, x2, y2, lobes, curlDir, color, strokeColor, strokeWidth]() {
            drawAcanthus(x1, y1, x2, y2, lobes, curlDir, color, strokeColor, strokeWidth);
        });
    }

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::hypot(dx, dy);
    if (len < 4.0f) return;

    float ux = dx / len;
    float uy = dy / len;
    float nx = -uy;
    float ny = ux;
    if (curlDir == "right") {
        nx = -nx;
        ny = -ny;
    }

    lobes = std::clamp(lobes, 2, 9);
    float bow = len * 0.18f;

    const int spineSteps = lobes * 6;
    std::vector<std::pair<float, float>> spine(spineSteps + 1);
    for (int i = 0; i <= spineSteps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(spineSteps);
        float arch = std::sin(t * kPi) * bow;
        spine[i] = {x1 + ux * (len * t) + nx * arch,
                    y1 + uy * (len * t) + ny * arch};
    }

    std::ostringstream path;
    path << "M " << spine[0].first << " " << spine[0].second;

    for (int l = 0; l < lobes; ++l) {
        float t0 = static_cast<float>(l) / static_cast<float>(lobes);
        float t1 = static_cast<float>(l + 1) / static_cast<float>(lobes);
        float tMid = (t0 + t1) * 0.5f;

        float lobeLen = len * (0.16f + 0.14f * std::sin(tMid * kPi));
        int idx0 = std::clamp(static_cast<int>(t0 * spineSteps), 0, spineSteps);
        int idx1 = std::clamp(static_cast<int>(t1 * spineSteps), 0, spineSteps);

        float baseX = spine[idx0].first;
        float baseY = spine[idx0].second;
        float nextX = spine[idx1].first;
        float nextY = spine[idx1].second;

        // Classical curved acanthus lobe with smooth swelling & hooked tip
        float tipX = baseX + ux * (len / static_cast<float>(lobes) * 0.9f) + nx * lobeLen;
        float tipY = baseY + uy * (len / static_cast<float>(lobes) * 0.9f) + ny * lobeLen;

        float cp1X = baseX + nx * (lobeLen * 0.85f) - ux * (lobeLen * 0.15f);
        float cp1Y = baseY + ny * (lobeLen * 0.85f) - uy * (lobeLen * 0.15f);
        path << " Q " << cp1X << " " << cp1Y << " " << tipX << " " << tipY;

        float cp2X = tipX - nx * (lobeLen * 0.4f) + ux * (lobeLen * 0.25f);
        float cp2Y = tipY - ny * (lobeLen * 0.4f) + uy * (lobeLen * 0.25f);
        path << " Q " << cp2X << " " << cp2Y << " " << nextX << " " << nextY;
    }

    path << " L " << spine.back().first << " " << spine.back().second;
    for (int i = spineSteps - 1; i >= 0; --i) {
        float t = static_cast<float>(i) / static_cast<float>(spineSteps);
        float innerOffset = len * 0.045f * std::sin(t * kPi);
        path << " L " << (spine[i].first - nx * innerOffset) << " " << (spine[i].second - ny * innerOffset);
    }
    path << " Z";

    std::string fillHex = formatColorHex(color);
    std::string strokeHex = strokeColor ? formatColorHex(strokeColor) : fillHex;

    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='" << fillHex << "' stroke='" << strokeHex << "' stroke-width='" << strokeWidth << "' stroke-linejoin='round'/>";

    svg << "<path d='";
    for (size_t i = 0; i < spine.size(); ++i) {
        if (i == 0) svg << "M " << spine[i].first << " " << spine[i].second;
        else svg << " L " << spine[i].first << " " << spine[i].second;
    }
    svg << "' fill='none' stroke='" << strokeHex << "' stroke-width='" << std::max(0.8f, strokeWidth * 0.8f) << "' stroke-linecap='round'/>";

    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::repeatBeads(float inset, float radius, float spacing, uint32_t color) {
    if (radius <= 0.0f || spacing <= 1.0f) return;

    float x0 = inset;
    float x1 = static_cast<float>(width_) - inset;
    float y0 = inset;
    float y1 = static_cast<float>(height_) - inset;
    if (x1 <= x0 || y1 <= y0) return;

    float bw = x1 - x0;
    float bh = y1 - y0;
    int nx = std::max(2, static_cast<int>(std::round(bw / spacing)));
    int ny = std::max(2, static_cast<int>(std::round(bh / spacing)));
    float dx = bw / static_cast<float>(nx);
    float dy = bh / static_cast<float>(ny);

    uint8_t wa = static_cast<uint8_t>(((color >> 24) & 0xFF) * 0.45f);
    uint32_t wireCol = (wa << 24) | (color & 0x00FFFFFF);
    strokeRect(x0, y0, bw, bh, wireCol, 0.8f);

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";

    float glintR = radius * 0.35f;
    float glintOff = radius * 0.3f;

    auto addBead = [&](float bx, float by) {
        ss << "<circle cx='" << bx << "' cy='" << by << "' r='" << radius << "' fill='" << hex << "'/>";
        ss << "<circle cx='" << (bx - glintOff) << "' cy='" << (by - glintOff) << "' r='" << glintR << "' fill='#FFFFFF' opacity='0.55'/>";
    };

    for (int i = 0; i <= nx; ++i) {
        float bx = x0 + static_cast<float>(i) * dx;
        addBead(bx, y0);
        addBead(bx, y1);
    }
    for (int j = 1; j < ny; ++j) {
        float by = y0 + static_cast<float>(j) * dy;
        addBead(x0, by);
        addBead(x1, by);
    }

    ss << "</g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::strokeCartouche(float cx, float cy, float rx, float ry,
                                 const std::string& style, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, style, color, lineWidth]() {
            strokeCartouche(cx, cy, rx, ry, style, color, lineWidth);
        });
    }

    if (rx <= 2.0f || ry <= 2.0f) return;

    std::string hex = formatColorHex(color);
    std::ostringstream path;

    if (style == "baroque_oval") {
        float tx = cx, ty = cy - ry;
        float bx = cx, by = cy + ry;
        float rxR = cx + rx, ryR = cy;
        float rxL = cx - rx, ryL = cy;
        float sX = rx * 0.72f;
        float sY = ry * 0.70f;

        path << "M " << tx << " " << ty
             << " C " << (cx + sX * 0.5f) << " " << ty << " " << (cx + sX) << " " << (cy - sY * 0.85f) << " " << rxR << " " << ryR
             << " C " << (cx + sX) << " " << (cy + sY * 0.85f) << " " << (cx + sX * 0.5f) << " " << by << " " << bx << " " << by
             << " C " << (cx - sX * 0.5f) << " " << by << " " << (cx - sX) << " " << (cy + sY * 0.85f) << " " << rxL << " " << ryL
             << " C " << (cx - sX) << " " << (cy - sY * 0.85f) << " " << (cx - sX * 0.5f) << " " << ty << " " << tx << " " << ty << " Z";
    } else {
        path << "M " << (cx - rx) << " " << (cy - ry + ry * 0.2f)
             << " Q " << cx << " " << (cy - ry * 1.15f) << " " << (cx + rx) << " " << (cy - ry + ry * 0.2f)
             << " Q " << (cx + rx * 1.15f) << " " << cy << " " << (cx + rx) << " " << (cy + ry - ry * 0.2f)
             << " Q " << cx << " " << (cy + ry * 1.15f) << " " << (cx - rx) << " " << (cy + ry - ry * 0.2f)
             << " Q " << (cx - rx * 1.15f) << " " << cy << " " << (cx - rx) << " " << (cy - ry + ry * 0.2f)
             << " Z";
    }

    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "' stroke-linejoin='round'/>";
    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawOrnateSuit(Suit suit, float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, suit, cx, cy, size, bodyColor, filigreeColor]() {
            drawOrnateSuit(suit, cx, cy, size, bodyColor, filigreeColor);
        });
    }

    if (bodyColor == 0) {
        bodyColor = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }
    if (filigreeColor == 0) {
        filigreeColor = 0xFFD4AF37;
    }

    drawSuit(suit, cx, cy, size, bodyColor);

    std::string filigreeHex = formatColorHex(filigreeColor);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << (size * 0.5f) << ")'>";

    if (suit == Suit::Spades) {
        ss << "<path d='M 0 0.35 L 0 -0.55' stroke='" << filigreeHex << "' stroke-width='0.05' stroke-linecap='round'/>";
        ss << "<path d='M -0.12 -0.15 C -0.35 -0.3 -0.45 0.1 -0.18 0.25 C 0.0 0.35 -0.05 0.15 -0.12 0.05' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<path d='M 0.12 -0.15 C 0.35 -0.3 0.45 0.1 0.18 0.25 C 0.0 0.35 0.05 0.15 0.12 0.05' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='-0.12' r='0.06' fill='" << filigreeHex << "'/>";
    } else if (suit == Suit::Hearts) {
        ss << "<path d='M 0 0.45 C -0.2 0.2 -0.4 0.0 -0.25 -0.25 C -0.15 -0.38 -0.02 -0.28 -0.08 -0.15' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<path d='M 0 0.45 C 0.2 0.2 0.4 0.0 0.25 -0.25 C 0.15 -0.38 0.02 -0.28 0.08 -0.15' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='0.1' r='0.055' fill='" << filigreeHex << "'/>";
    } else if (suit == Suit::Diamonds) {
        ss << "<path d='M 0 -0.45 L 0.25 0 L 0 0.45 L -0.25 0 Z' fill='none' stroke='" << filigreeHex << "' stroke-width='0.045'/>";
        ss << "<circle cx='0' cy='0' r='0.08' fill='" << filigreeHex << "'/>";
        ss << "<path d='M 0 -0.25 L 0.12 0 L 0 0.25 L -0.12 0 Z' fill='none' stroke='" << filigreeHex << "' opacity='0.75'/>";
    } else if (suit == Suit::Clubs) {
        ss << "<circle cx='0' cy='-0.3' r='0.12' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<circle cx='-0.3' cy='0.15' r='0.11' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<circle cx='0.3' cy='0.15' r='0.11' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<path d='M -0.12 0.55 L 0 0.25 L 0.12 0.55' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='0.05' r='0.06' fill='" << filigreeHex << "'/>";
    }

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawBaroqueCorner(float x, float y, float size, uint32_t goldColor, uint32_t accentColor) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, size, goldColor, accentColor]() {
            drawBaroqueCorner(x, y, size, goldColor, accentColor);
        });
    }

    if (size <= 4.0f) return;
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex = formatColorHex(goldColor);
    std::string accHex = formatColorHex(accentColor);
    std::string deepHex = formatColorHex((goldColor & 0xFF000000) | (((goldColor >> 16) & 0xFF) * 2 / 3 << 16) | (((goldColor >> 8) & 0xFF) * 2 / 3 << 8) | ((goldColor & 0xFF) * 2 / 3));

    float scale = size / 100.0f;
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << x << " " << y << ") scale(" << scale << ")'>";

    // 1. Primary Fluted C-Scroll Volute (curving from outer corner into an ornate eye)
    ss << "<path d='M 10 92 C 10 52 24 22 54 14 C 80 6 96 18 96 38 C 96 56 80 68 62 66 C 48 64 42 50 48 40 C 52 34 60 35 60 41 C 60 45 56 46 54 44' "
       << "fill='none' stroke='" << goldHex << "' stroke-width='4.2' stroke-linecap='round' stroke-linejoin='round'/>";
    ss << "<path d='M 10 92 C 10 52 24 22 54 14 C 80 6 96 18 96 38 C 96 56 80 68 62 66 C 48 64 42 50 48 40' "
       << "fill='none' stroke='" << accHex << "' stroke-width='1.2' stroke-linecap='round'/>";

    // 2. Cascading Vertical Acanthus Plumes (Scalloped curling leaf fronds along vertical border)
    ss << "<path d='M 12 36 C 4 46 2 60 14 66 C 18 64 20 54 17 44 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 11 40 Q 8 52 14 62' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    ss << "<path d='M 13 60 C 3 72 2 88 12 96 C 17 93 19 82 16 70 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 12 66 Q 8 80 13 92' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    ss << "<path d='M 12 90 C 4 104 4 120 16 128 C 20 122 21 110 16 100 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 12 96 Q 9 110 15 122' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    // 3. Cascading Horizontal Acanthus Plumes (Scalloped curling fronds along top border)
    ss << "<path d='M 45 15 C 54 5 70 4 76 15 C 73 18 62 20 52 17 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 48 14 Q 60 10 72 15' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    ss << "<path d='M 72 15 C 84 4 102 3 110 14 C 106 18 94 20 82 17 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 76 14 Q 90 9 105 14' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    // 4. Center Rosette Button at the eye
    ss << "<circle cx='62' cy='46' r='4.5' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.0'/>";
    ss << "<circle cx='62' cy='46' r='2.2' fill='" << accHex << "'/>";

    // 5. Inward sweeping tendril flourish
    ss << "<path d='M 66 64 C 76 78 90 88 104 92 C 98 84 92 76 86 68 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 70 68 Q 85 80 100 88' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawBaroqueCrest(float cx, float cy, float scale, uint32_t goldColor, uint32_t accentColor, bool flipY) {
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex = formatColorHex(goldColor);
    std::string accHex = formatColorHex(accentColor);
    std::string deepHex = formatColorHex((goldColor & 0xFF000000) | (((goldColor >> 16) & 0xFF) * 2 / 3 << 16) | (((goldColor >> 8) & 0xFF) * 2 / 3 << 8) | ((goldColor & 0xFF) * 2 / 3));

    float ySign = flipY ? -1.0f : 1.0f;
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << " " << (scale * ySign) << ")'>";

    // Center Fleur-de-lis / Palmette Spear Finial
    ss << "<path d='M 0 -24 C -5 -16 -4 -6 0 0 C 4 -6 5 -16 0 -24 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<path d='M 0 -22 L 0 -2' stroke='" << accHex << "' stroke-width='0.8'/>";

    // Outward flaring palmette fronds
    ss << "<path d='M 0 -12 C -8 -18 -18 -12 -16 -4 C -12 -2 -4 -6 0 -8 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.7'/>";
    ss << "<path d='M 0 -12 C 8 -18 18 -12 16 -4 C 12 -2 4 -6 0 -8 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.7'/>";

    // Dual Outward-Curling Fluted Volute Scrolls
    ss << "<path d='M 0 -4 C -14 -12 -34 -14 -42 -2 C -48 8 -42 22 -30 22 C -20 22 -16 12 -22 6 C -26 2 -32 5 -30 10' "
       << "fill='none' stroke='" << goldHex << "' stroke-width='3.2' stroke-linecap='round'/>";
    ss << "<path d='M 0 -4 C 14 -12 34 -14 42 -2 C 48 8 42 22 30 22 C 20 22 16 12 22 6 C 26 2 32 5 30 10' "
       << "fill='none' stroke='" << goldHex << "' stroke-width='3.2' stroke-linecap='round'/>";

    ss << "<path d='M 0 -4 C -14 -12 -34 -14 -42 -2 C -46 6 -42 18 -32 18' fill='none' stroke='" << accHex << "' stroke-width='1.0'/>";
    ss << "<path d='M 0 -4 C 14 -12 34 -14 42 -2 C 46 6 42 18 32 18' fill='none' stroke='" << accHex << "' stroke-width='1.0'/>";

    // Drop Pearl Pendants
    ss << "<circle cx='0' cy='6' r='3.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<circle cx='0' cy='6' r='1.4' fill='" << accHex << "'/>";
    ss << "<circle cx='-30' cy='12' r='2.4' fill='" << goldHex << "'/>";
    ss << "<circle cx='30' cy='12' r='2.4' fill='" << goldHex << "'/>";

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawBaroqueCartouche(float cx, float cy, float rx, float ry, uint32_t goldColor, uint32_t accentColor, float lineWidth) {
    if (rx <= 4.0f || ry <= 4.0f) return;
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex = formatColorHex(goldColor);
    std::string accHex = formatColorHex(accentColor);
    std::string deepHex = formatColorHex((goldColor & 0xFF000000) | (((goldColor >> 16) & 0xFF) * 2 / 3 << 16) | (((goldColor >> 8) & 0xFF) * 2 / 3 << 8) | ((goldColor & 0xFF) * 2 / 3));

    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ")'>";

    // 1. Classical Diamond-Oval Molded Arch
    float tx = 0.0f, ty = -ry;
    float bx = 0.0f, by = ry;
    float rxR = rx, ryR = 0.0f;
    float rxL = -rx, ryL = 0.0f;

    float sX = rx * 0.72f;
    float sY = ry * 0.70f;

    ss << "<path d='M " << tx << " " << ty
       << " C " << (sX * 0.5f) << " " << ty << " " << sX << " " << (-sY * 0.85f) << " " << rxR << " " << ryR
       << " C " << sX << " " << (sY * 0.85f) << " " << (sX * 0.5f) << " " << by << " " << bx << " " << by
       << " C " << (-sX * 0.5f) << " " << by << " " << -sX << " " << (sY * 0.85f) << " " << rxL << " " << ryL
       << " C " << -sX << " " << (-sY * 0.85f) << " " << (-sX * 0.5f) << " " << ty << " " << tx << " " << ty << " Z' "
       << "fill='none' stroke='" << goldHex << "' stroke-width='" << lineWidth << "' stroke-linejoin='round'/>";

    // Inner parallel molded frame with highlight
    float inRx = rx * 0.88f;
    float inRy = ry * 0.88f;
    float inSx = inRx * 0.72f;
    float inSy = inRy * 0.70f;
    ss << "<path d='M 0 " << -inRy
       << " C " << (inSx * 0.5f) << " " << -inRy << " " << inSx << " " << (-inSy * 0.85f) << " " << inRx << " 0"
       << " C " << inSx << " " << (inSy * 0.85f) << " " << (inSx * 0.5f) << " " << inRy << " 0 " << inRy
       << " C " << (-inSx * 0.5f) << " " << inRy << " " << -inSx << " " << (inSy * 0.85f) << " " << -inRx << " 0"
       << " C " << -inSx << " " << (-inSy * 0.85f) << " " << (-inSx * 0.5f) << " " << -inRy << " 0 " << -inRy << " Z' "
       << "fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.45f) << "' stroke-linejoin='round'/>";

    // 2. Beaded pearl trim along the molded frame (20 pearls)
    constexpr int kPearlCount = 20;
    for (int p = 0; p < kPearlCount; ++p) {
        float angle = static_cast<float>(p) / static_cast<float>(kPearlCount) * 2.0f * kPi;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float px = cosA * (rx * 0.94f);
        float py = sinA * (ry * 0.94f);
        ss << "<circle cx='" << px << "' cy='" << py << "' r='1.8' fill='" << goldHex << "'/>";
        ss << "<circle cx='" << (px - 0.5f) << "' cy='" << (py - 0.5f) << "' r='0.7' fill='#FFFFFF' opacity='0.7'/>";
    }

    // 3. Lateral Waist Shell / Scroll Clasps at left and right
    auto drawClasp = [&](float xOff, float dir) {
        ss << "<g transform='translate(" << xOff << " 0) scale(" << dir << " 1)'>";
        ss << "<path d='M 0 -18 C 12 -12 14 -4 14 0 C 14 4 12 12 0 18 C 4 8 4 -8 0 -18 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
        ss << "<path d='M 0 -10 Q 8 0 0 10' fill='none' stroke='" << accHex << "' stroke-width='0.8'/>";
        ss << "<circle cx='5' cy='0' r='2.8' fill='" << accHex << "'/>";
        ss << "</g>";
    };
    drawClasp(rx, 1.0f);
    drawClasp(-rx, -1.0f);

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));

    // 4. Top and bottom Crown Pediments
    drawBaroqueCrest(cx, cy - ry, std::min(rx, ry) * 0.42f, goldColor, accentColor, false);
    drawBaroqueCrest(cx, cy + ry, std::min(rx, ry) * 0.42f, goldColor, accentColor, true);
}

void CardCanvas::drawBaroqueSpade(float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor, uint32_t accentColor) {
    if (size <= 4.0f) return;
    if (bodyColor == 0) bodyColor = 0xFF14151A;
    if (filigreeColor == 0) filigreeColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string bodyHex = formatColorHex(bodyColor);
    std::string filigreeHex = formatColorHex(filigreeColor);
    std::string accHex = formatColorHex(accentColor);
    std::string deepHex = formatColorHex((filigreeColor & 0xFF000000) | (((filigreeColor >> 16) & 0xFF) * 2 / 3 << 16) | (((filigreeColor >> 8) & 0xFF) * 2 / 3 << 8) | ((filigreeColor & 0xFF) * 2 / 3));

    float scale = size / 200.0f;
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << ")'>";

    // 1. Monumental Baroque Spade Silhouette
    std::string spadePath =
        "M 0 -100 "
        "C 22 -62 65 -36 78 4 "
        "C 86 30 76 56 50 62 "
        "C 34 66 22 56 16 42 "
        "C 14 58 18 80 32 94 "
        "L -32 94 "
        "C -18 80 -14 58 -16 42 "
        "C -22 56 -34 66 -50 62 "
        "C -76 56 -86 30 -78 4 "
        "C -65 -36 -22 -62 0 -100 Z";

    // Outer silhouette with carved gold/silver stroke
    ss << "<path d='" << spadePath << "' fill='" << bodyHex << "' stroke='" << filigreeHex << "' stroke-width='3.2' stroke-linejoin='round'/>";

    // Inner relief bevel line
    ss << "<path d='"
        "M 0 -90 "
        "C 18 -55 56 -32 68 4 "
        "C 74 26 66 48 44 54 "
        "C 30 58 20 48 14 36 "
        "C 12 52 16 74 26 86 "
        "L -26 86 "
        "C -16 74 -12 52 -14 36 "
        "C -20 48 -30 58 -44 54 "
        "C -66 48 -74 26 -68 4 "
        "C -56 -32 -18 -55 0 -90 Z' "
       << "fill='none' stroke='" << filigreeHex << "' stroke-width='1.2' stroke-linejoin='round' opacity='0.75'/>";

    // 2. Central Tree of Life / Palmette Spine
    ss << "<path d='M 0 -84 L 0 28' stroke='" << filigreeHex << "' stroke-width='2.6' stroke-linecap='round'/>";
    ss << "<path d='M 0 -84 L 0 28' stroke='" << accHex << "' stroke-width='1.0' stroke-linecap='round'/>";
    ss << "<path d='M 0 -84 L -8 -64 Q 0 -68 8 -64 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='0.6'/>";
    ss << "<path d='M 0 -82 L 0 -66' stroke='" << accHex << "' stroke-width='0.8'/>";

    // 3. Symmetrical Dual Baroque Acanthus Volutes Inside Spade Lobes
    // Right lobe
    ss << "<path d='M 0 -36 C 24 -46 54 -24 50 8 C 46 32 24 38 12 24 C 6 16 12 6 22 10 C 28 12 28 20 22 22 C 18 24 16 20 18 16' "
       << "fill='none' stroke='" << filigreeHex << "' stroke-width='2.8' stroke-linecap='round'/>";
    ss << "<path d='M 0 -36 C 24 -46 54 -24 50 8 C 46 32 24 38 12 24' fill='none' stroke='" << accHex << "' stroke-width='1.0' stroke-linecap='round'/>";
    ss << "<path d='M 24 -44 C 36 -46 48 -38 52 -28 C 46 -26 36 -32 26 -38 Z' fill='" << filigreeHex << "'/>";
    ss << "<path d='M 52 -24 C 60 -14 58 4 52 14 C 48 4 48 -8 46 -18 Z' fill='" << filigreeHex << "'/>";

    // Left lobe (mirrored)
    ss << "<path d='M 0 -36 C -24 -46 -54 -24 -50 8 C -46 32 -24 38 -12 24 C -6 16 -12 6 -22 10 C -28 12 -28 20 -22 22 C -18 24 -16 20 -18 16' "
       << "fill='none' stroke='" << filigreeHex << "' stroke-width='2.8' stroke-linecap='round'/>";
    ss << "<path d='M 0 -36 C -24 -46 -54 -24 -50 8 C -46 32 -24 38 -12 24' fill='none' stroke='" << accHex << "' stroke-width='1.0' stroke-linecap='round'/>";
    ss << "<path d='M -24 -44 C -36 -46 -48 -38 -52 -28 C -46 -26 -36 -32 -26 -38 Z' fill='" << filigreeHex << "'/>";
    ss << "<path d='M -52 -24 C -60 -14 -58 4 -52 14 C -48 4 -48 -8 -46 -18 Z' fill='" << filigreeHex << "'/>";

    // 4. Center Drops
    ss << "<path d='M 0 -50 C -6 -42 -6 -32 0 -24 C 6 -32 6 -42 0 -50 Z' fill='" << accHex << "'/>";
    ss << "<circle cx='0' cy='-12' r='4.0' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";
    ss << "<circle cx='0' cy='-12' r='2.0' fill='" << accHex << "'/>";
    ss << "<circle cx='0' cy='8' r='3.2' fill='" << filigreeHex << "'/>";

    // 5. Pedestal Curled Bracket Feet at base
    ss << "<path d='M -16 86 C -24 86 -32 88 -34 94 C -34 98 -28 100 -24 98 C -18 96 -18 90 -22 88' fill='none' stroke='" << filigreeHex << "' stroke-width='2.0'/>";
    ss << "<path d='M 16 86 C 24 86 32 88 34 94 C 34 98 28 100 24 98 C 18 96 18 90 22 88' fill='none' stroke='" << filigreeHex << "' stroke-width='2.0'/>";
    ss << "<line x1='-32' y1='94' x2='32' y2='94' stroke='" << filigreeHex << "' stroke-width='2.4'/>";

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawAcanthusScroll(float cx, float cy, float radius, float angleStart, float angleEnd, uint32_t color, uint32_t strokeColor) {
    if (radius <= 2.0f) return;
    if (color == 0) color = 0xFFD4AF37;
    drawVolute(cx, cy, radius, std::abs(angleEnd - angleStart) / (2.0f * kPi), angleStart, color, 3.2f, "fluted_ridge");
}

void CardCanvas::applyFinish(const std::string& style, float lightAzimuth, float lightElevation,
                             float aoIntensity, float reliefDepth, uint32_t patinaColor,
                             uint32_t highlightColor, float paperGrain, float inkBleed) {
    if (rgba_.empty() || width_ <= 0 || height_ <= 0) return;

    if (style == "flat" || style == "none" || style.empty()) {
        return;
    }

    if (style == "hand_sketched") {
        const size_t totalPixels = static_cast<size_t>(width_ * height_);
        for (size_t i = 0; i < totalPixels; ++i) {
            uint8_t a = rgba_[i * 4 + 3];
            if (a == 0) continue;

            int px = static_cast<int>(i % width_);
            int py = static_cast<int>(i / width_);

            uint32_t h = static_cast<uint32_t>(px * 374761393 + py * 668265263);
            h = (h ^ (h >> 13)) * 1274126177;
            float n = static_cast<float>(h & 0xFFFF) / 65535.0f;

            float grainMod = 1.0f + (n - 0.5f) * paperGrain;
            rgba_[i * 4 + 0] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 0]) * grainMod, 0.0f, 255.0f));
            rgba_[i * 4 + 1] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 1]) * grainMod, 0.0f, 255.0f));
            rgba_[i * 4 + 2] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 2]) * grainMod, 0.0f, 255.0f));

            if (inkBleed > 0.0f && a > 10 && a < 245) {
                float bleedOffset = (n - 0.5f) * inkBleed * 40.0f;
                rgba_[i * 4 + 3] = static_cast<uint8_t>(std::clamp(static_cast<float>(a) + bleedOffset, 0.0f, 255.0f));
            }
        }
        return;
    }

    if (style == "relief") {
        const int w = width_;
        const int h = height_;
        const size_t totalPixels = static_cast<size_t>(w * h);

        thread_local static std::vector<float> height;
        if (height.size() < totalPixels) height.resize(totalPixels);

        for (size_t i = 0; i < totalPixels; ++i) {
            float r = static_cast<float>(rgba_[i * 4 + 0]);
            float g = static_cast<float>(rgba_[i * 4 + 1]);
            float b = static_cast<float>(rgba_[i * 4 + 2]);
            float a = static_cast<float>(rgba_[i * 4 + 3]) / 255.0f;
            float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
            height[i] = a * lum;
        }

        const float azRad = lightAzimuth * (kPi / 180.0f);
        const float elRad = std::clamp(lightElevation, 10.0f, 85.0f) * (kPi / 180.0f);
        const float lx = std::cos(elRad) * std::cos(azRad);
        const float ly = std::cos(elRad) * std::sin(azRad);
        const float lz = std::sin(elRad);

        float hx = lx, hy = ly, hz = lz + 1.0f;
        float hLen = std::sqrt(hx * hx + hy * hy + hz * hz);
        hx /= hLen; hy /= hLen; hz /= hLen;

        uint8_t patR = (patinaColor != 0) ? ((patinaColor >> 16) & 0xFF) : 10;
        uint8_t patG = (patinaColor != 0) ? ((patinaColor >> 8) & 0xFF) : 12;
        uint8_t patB = (patinaColor != 0) ? (patinaColor & 0xFF) : 16;

        uint8_t hlR = (highlightColor != 0) ? ((highlightColor >> 16) & 0xFF) : 255;
        uint8_t hlG = (highlightColor != 0) ? ((highlightColor >> 8) & 0xFF) : 248;
        uint8_t hlB = (highlightColor != 0) ? (highlightColor & 0xFF) : 220;

        for (int y = 0; y < h; ++y) {
            int ym = std::max(0, y - 1);
            int yp = std::min(h - 1, y + 1);
            size_t rowIdx = static_cast<size_t>(y * w);
            size_t rowM = static_cast<size_t>(ym * w);
            size_t rowP = static_cast<size_t>(yp * w);

            for (int x = 0; x < w; ++x) {
                size_t idx = rowIdx + x;
                uint8_t curA = rgba_[idx * 4 + 3];
                if (curA <= 2) continue;

                int xm = std::max(0, x - 1);
                int xp = std::min(w - 1, x + 1);

                float hCenter = height[idx];
                float hLeft   = height[rowIdx + xm];
                float hRight  = height[rowIdx + xp];
                float hUp     = height[rowM + x];
                float hDown   = height[rowP + x];

                float dzdx = (hRight - hLeft) * 0.5f * reliefDepth;
                float dzdy = (hDown - hUp) * 0.5f * reliefDepth;

                float nx = -dzdx;
                float ny = -dzdy;
                float nz = 1.0f;
                float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen; ny *= invLen; nz *= invLen;

                float nDotL = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                float nDotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);
                float spec = (nDotH > 0.85f) ? std::pow(nDotH, 18.0f) * 0.75f : 0.0f;

                float laplacian = (hLeft + hRight + hUp + hDown) * 0.25f - hCenter;
                float crevice = std::max(0.0f, laplacian) * aoIntensity * 4.0f;
                float aoVal = std::clamp(1.0f - crevice, 0.25f, 1.0f);

                float relLight = (nDotL - lz) * 0.6f;
                float lightMod = std::clamp(1.0f + relLight, 0.4f, 1.6f) * aoVal;

                float r = static_cast<float>(rgba_[idx * 4 + 0]) * lightMod;
                float g = static_cast<float>(rgba_[idx * 4 + 1]) * lightMod;
                float b = static_cast<float>(rgba_[idx * 4 + 2]) * lightMod;

                if (aoVal < 0.75f) {
                    float pw = (0.75f - aoVal) / 0.75f * 0.5f;
                    r = r * (1.0f - pw) + static_cast<float>(patR) * pw;
                    g = g * (1.0f - pw) + static_cast<float>(patG) * pw;
                    b = b * (1.0f - pw) + static_cast<float>(patB) * pw;
                }

                if (spec > 0.02f) {
                    float hw = std::min(1.0f, spec);
                    r = r * (1.0f - hw) + static_cast<float>(hlR) * hw;
                    g = g * (1.0f - hw) + static_cast<float>(hlG) * hw;
                    b = b * (1.0f - hw) + static_cast<float>(hlB) * hw;
                }

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
            }
        }
        return;
    }
}

void CardCanvas::drawCornerIndices(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, float fontSize) {
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    float pipSize = fontSize * 0.85f;
    float marginX = cardW * 0.09f;
    float marginY = cardH * 0.05f;

    // Top-left index with 3px vertical padding between rank and suit pip
    float rankY = marginY + fontSize * 0.5f;
    float suitY = rankY + (fontSize * 0.5f) + (pipSize * 0.5f) + 3.0f;
    drawRank(rank, marginX, rankY, fontSize, color);
    drawSuit(suit, marginX, suitY, pipSize, color);

    // Bottom-right inverted index (180 degree rotation)
    push();
    translate(cardW, cardH);
    rotate180();
    drawRank(rank, marginX, rankY, fontSize, color);
    drawSuit(suit, marginX, suitY, pipSize, color);
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
            drawSuit(suit, 0.0f, 0.0f, pSize, color);
            pop();
        } else {
            drawSuit(suit, px, py, pSize, color);
        }
    }
}

void CardCanvas::drawCourt(const std::string& rank, Suit suit, float x, float y, float w, float h, float /*opacity*/, uint32_t tint) {
    push();
    translate(x, y);

    float cx = w * 0.5f;
    uint32_t gold = (tint != 0) ? tint : 0xFFD4AF37;
    uint32_t suitColor = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFB81424 : 0xFF14151A;

    // 1. Ornate Arch Frame
    strokeRoundedRect(4.0f, 4.0f, w - 8.0f, h - 8.0f, 6.0f, gold, 1.8f);
    strokeRoundedRect(8.0f, 8.0f, w - 16.0f, h - 16.0f, 4.0f, gold, 0.8f);

    // 2. Royal Crest Crown
    float crownW = w * 0.44f;
    float crownH = crownW * 0.55f;
    float crownY = h * 0.14f;

    std::string goldHex = formatColorHex(gold);
    std::ostringstream crownSvg;
    crownSvg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << crownW << "' height='" << crownH << "' viewBox='0 0 100 55'>"
             << "<path d='M 10 50 L 5 15 L 30 32 L 50 8 L 70 32 L 95 15 L 90 50 Z' fill='" << goldHex << "' stroke='#8C7220' stroke-width='2'/>"
             << "<circle cx='5' cy='15' r='4' fill='#FFF8DC'/>"
             << "<circle cx='50' cy='8' r='5' fill='#FFF8DC'/>"
             << "<circle cx='95' cy='15' r='4' fill='#FFF8DC'/>"
             << "<circle cx='30' cy='32' r='3.5' fill='#FFF8DC'/>"
             << "<circle cx='70' cy='32' r='3.5' fill='#FFF8DC'/>"
             << "</svg>";
    rasterizeSvgFragment(crownSvg.str(), state_.tx + state_.a * (cx - crownW * 0.5f), state_.ty + state_.d * crownY, crownW * std::abs(state_.a), crownH * std::abs(state_.d));

    // 3. Central Ornate Pip
    float pipSize = std::min(w, h) * 0.38f;
    drawOrnateSuit(suit, cx, h * 0.52f, pipSize, suitColor, gold);

    // 4. Bold Royal Rank Letter below crown
    drawRank(rank, cx, h * 0.86f, std::min(w, h) * 0.28f, gold);

    pop();
}

void CardCanvas::drawCardFace(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, const std::string& layout) {
    float r = cornerRadius_;
    fillRoundedRect(0, 0, cardW, cardH, r, 0xFFFFFFFF);
    strokeRoundedRect(0, 0, cardW, cardH, r, 0xFFE0DACF, 1.0f);

    int rInt = parseRank(rank);
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    drawCornerIndices(rank, suit, cardW, cardH, color, cardW * 0.095f);

    if (rInt >= 11 && rInt <= 13) {
        float marginX = cardW * 0.16f;
        float marginY = cardH * 0.12f;
        float innerW = cardW - marginX * 2.0f;
        float innerH = cardH - marginY * 2.0f;
        drawCourt(rank, suit, marginX, marginY, innerW, innerH);
        strokeRect(marginX, marginY, innerW, innerH, color, 1.2f);
    } else {
        drawPipLayout(rInt, suit, cardW, cardH, color, layout);
    }
}

void CardCanvas::drawPixelSprite(
    const std::vector<std::string>& spriteLines,
    float cx, float cy,
    float pixelSize,
    uint32_t color,
    char onChar,
    bool flipY,
    bool flipX
) {
    if (spriteLines.empty() || pixelSize <= 0.0f) return;
    int h = static_cast<int>(spriteLines.size());
    int w = 0;
    for (const auto& line : spriteLines) {
        w = std::max(w, static_cast<int>(line.size()));
    }
    if (w <= 0 || h <= 0) return;

    float totalW = static_cast<float>(w) * pixelSize;
    float totalH = static_cast<float>(h) * pixelSize;
    float startX = cx - totalW * 0.5f;
    float startY = cy - totalH * 0.5f;

    uint8_t cr = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t cg = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t cb = static_cast<uint8_t>(color & 0xFF);
    uint8_t ca = static_cast<uint8_t>((color >> 24) & 0xFF);
    if (ca == 0 && (cr != 0 || cg != 0 || cb != 0)) ca = 255;

    for (int y = 0; y < h; ++y) {
        int srcY = flipY ? (h - 1 - y) : y;
        const auto& line = spriteLines[srcY];
        int lineLen = static_cast<int>(line.size());

        for (int x = 0; x < w; ++x) {
            int srcX = flipX ? (w - 1 - x) : x;
            if (srcX < lineLen && line[srcX] == onChar) {
                int px0 = static_cast<int>(std::round(startX + static_cast<float>(x) * pixelSize));
                int py0 = static_cast<int>(std::round(startY + static_cast<float>(y) * pixelSize));
                int px1 = static_cast<int>(std::round(startX + static_cast<float>(x + 1) * pixelSize));
                int py1 = static_cast<int>(std::round(startY + static_cast<float>(y + 1) * pixelSize));

                px0 = std::clamp(px0, 0, width_);
                px1 = std::clamp(px1, 0, width_);
                py0 = std::clamp(py0, 0, height_);
                py1 = std::clamp(py1, 0, height_);

                for (int py = py0; py < py1; ++py) {
                    uint8_t* row = &rgba_[static_cast<size_t>(py * width_ + px0) * 4];
                    for (int px = px0; px < px1; ++px) {
                        row[0] = cr;
                        row[1] = cg;
                        row[2] = cb;
                        row[3] = ca;
                        row += 4;
                    }
                }
            }
        }
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
    eval.registerMemberFunction("ctx", "fill_rounded_rect", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        float defR = canvas ? canvas->cornerRadius() : 8.0f;
        float rad = eatsbits::eatscript::resolveArg(args, kwargs, 4, "radius", eatsbits::eatscript::Value(defR)).asFloat(defR);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->fillRoundedRect(x, y, w, h, rad, col);
        return eatsbits::eatscript::Value(true);
    });

    // 4. ctx.stroke_rounded_rect(x, y, w, h, radius, color, width)
    eval.registerMemberFunction("ctx", "stroke_rounded_rect", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        float defR = canvas ? canvas->cornerRadius() : 8.0f;
        float rad = eatsbits::eatscript::resolveArg(args, kwargs, 4, "radius", eatsbits::eatscript::Value(defR)).asFloat(defR);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        canvas->strokeRoundedRect(x, y, w, h, rad, col, lw);
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

    // 10. Transforms
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
    eval.registerMemberFunction("ctx", "set_corner_radius", [canvas](const auto& args, const auto& kwargs) {
        if (canvas) {
            float r = eatsbits::eatscript::resolveArg(args, kwargs, 0, "radius", eatsbits::eatscript::Value(24.0)).asFloat(24.0f);
            canvas->setCornerRadius(r);
            CardTextureCache::instance().setDeckCornerRadius(r);
        }
        return eatsbits::eatscript::Value(true);
    });

    // 11. High-Level Classic Deck Functions
    auto drawSuitFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[0].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        canvas->drawSuit(s, args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCornerIndicesFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        float fSize = eatsbits::eatscript::resolveArg(args, kwargs, 5, "font_size", eatsbits::eatscript::Value(28.0)).asFloat(28.0f);
        canvas->drawCornerIndices(rankVal.asString(), CardCanvas::parseSuit(suitVal.asString()), w, h, col, fSize);
        return eatsbits::eatscript::Value(true);
    };

    auto drawPipLayoutFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("1"));
        int r = rankVal.isString() ? CardCanvas::parseRank(rankVal.asString()) : rankVal.asInt();
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        std::string variant = eatsbits::eatscript::resolveArg(args, kwargs, 5, "variant", eatsbits::eatscript::Value("classic")).asString();
        canvas->drawPipLayout(r, s, w, h, col, variant);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCourtFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("K"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 2, "x", eatsbits::eatscript::Value(0.0)).asFloat();
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 3, "y", eatsbits::eatscript::Value(0.0)).asFloat();
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 4, "w", eatsbits::eatscript::Value(200.0)).asFloat(200.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 5, "h", eatsbits::eatscript::Value(300.0)).asFloat(300.0f);
        float op = eatsbits::eatscript::resolveArg(args, kwargs, 6, "opacity", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        auto tintVal = eatsbits::eatscript::resolveArg(args, kwargs, 7, "tint", eatsbits::eatscript::Value());
        uint32_t tint = tintVal.isString() ? CardCanvas::parseColor(tintVal.asString()) : static_cast<uint32_t>(tintVal.asInt(0));
        canvas->drawCourt(rankVal.asString(), s, x, y, w, h, op, tint);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCardFaceFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        std::string layout = eatsbits::eatscript::resolveArg(args, kwargs, 5, "layout", eatsbits::eatscript::Value("classic")).asString();
        canvas->drawCardFace(rankVal.asString(), s, w, h, col, layout);
        return eatsbits::eatscript::Value(true);
    };

    auto drawRankFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 1, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 2, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float size = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(24.0)).asFloat(24.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->drawRank(rankVal.asString(), x, y, size, col);
        return eatsbits::eatscript::Value(true);
    };

    auto drawTextFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto textVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "text", eatsbits::eatscript::Value(""));
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 1, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 2, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float size = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(16.0)).asFloat(16.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        std::string align = eatsbits::eatscript::resolveArg(args, kwargs, 5, "align", eatsbits::eatscript::Value("left")).asString();
        canvas->drawText(textVal.asString(), x, y, size, col, align);
        return eatsbits::eatscript::Value(true);
    };

    auto drawPixelSpriteFn = [canvas](const std::vector<eatsbits::eatscript::Value>& args,
                                      const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        std::vector<std::string> lines;
        const auto& spriteVal = args[0];
        if (spriteVal.isList()) {
            for (const auto& item : spriteVal.listVal) {
                lines.push_back(item.asString());
            }
        } else if (spriteVal.isString()) {
            std::stringstream ss(spriteVal.asString());
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float pSize = eatsbits::eatscript::resolveArg(args, kwargs, 3, "pixel_size", eatsbits::eatscript::Value(4.0)).asFloat(4.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#000000"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFF000000));
        auto onVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "on_char", eatsbits::eatscript::Value("#"));
        std::string onCharStr = onVal.asString();
        char onChar = onCharStr.empty() ? '#' : onCharStr[0];
        bool flipY = eatsbits::eatscript::resolveArg(args, kwargs, 6, "flip_y", eatsbits::eatscript::Value(false)).asBoolean(false);
        bool flipX = eatsbits::eatscript::resolveArg(args, kwargs, 7, "flip_x", eatsbits::eatscript::Value(false)).asBoolean(false);

        canvas->drawPixelSprite(lines, cx, cy, pSize, col, onChar, flipY, flipX);
        return eatsbits::eatscript::Value(true);
    };

    eval.setVariable("classic_deck", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    eval.registerMemberFunction("classic_deck", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("classic_deck", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("classic_deck", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("classic_deck", "draw_court", drawCourtFn);
    eval.registerMemberFunction("classic_deck", "draw_card_face", drawCardFaceFn);
    eval.registerMemberFunction("classic_deck", "draw_rank", drawRankFn);
    eval.registerMemberFunction("classic_deck", "draw_text", drawTextFn);
    eval.registerMemberFunction("classic_deck", "draw_pixel_sprite", drawPixelSpriteFn);

    eval.registerMemberFunction("ctx", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("ctx", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("ctx", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("ctx", "draw_court", drawCourtFn);
    eval.registerMemberFunction("ctx", "draw_card_face", drawCardFaceFn);
    eval.registerMemberFunction("ctx", "draw_rank", drawRankFn);
    eval.registerMemberFunction("ctx", "draw_text", drawTextFn);
    eval.registerMemberFunction("ctx", "draw_pixel_sprite", drawPixelSpriteFn);

    // 12. Ovals
    eval.registerMemberFunction("ctx", "fill_oval", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->fillOval(cx, cy, rx, ry, col);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "stroke_oval", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 5, "line_width", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        canvas->strokeOval(cx, cy, rx, ry, col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 13. Symmetry Scope
    eval.registerMemberFunction("ctx", "begin_symmetry", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        bool mx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "mirror_x", eatsbits::eatscript::Value(true)).asBoolean(true);
        bool my = eatsbits::eatscript::resolveArg(args, kwargs, 1, "mirror_y", eatsbits::eatscript::Value(true)).asBoolean(true);
        canvas->beginSymmetry(mx, my);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "end_symmetry", [canvas](const auto&, const auto&) {
        if (canvas) canvas->endSymmetry();
        return eatsbits::eatscript::Value(true);
    });

    // 14. Baroque Architectural Primitives
    eval.registerMemberFunction("ctx", "draw_volute", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float r = eatsbits::eatscript::resolveArg(args, kwargs, 2, "radius", eatsbits::eatscript::Value(30.0)).asFloat(30.0f);
        float turns = eatsbits::eatscript::resolveArg(args, kwargs, 3, "turns", eatsbits::eatscript::Value(1.25)).asFloat(1.25f);
        float aStart = eatsbits::eatscript::resolveArg(args, kwargs, 4, "angle_start", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));
        float width = eatsbits::eatscript::resolveArg(args, kwargs, 6, "width", eatsbits::eatscript::Value(3.5)).asFloat(3.5f);
        std::string prof = eatsbits::eatscript::resolveArg(args, kwargs, 7, "profile", eatsbits::eatscript::Value("fluted_ridge")).asString();

        canvas->drawVolute(cx, cy, r, turns, aStart, col, width, prof);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_acanthus", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x1 = 0.0f, y1 = 0.0f, x2 = 100.0f, y2 = 100.0f;
        if (kwargs.find("from") != kwargs.end()) {
            auto fVal = kwargs.at("from");
            if (fVal.isList() && fVal.listVal.size() >= 2) {
                x1 = fVal.listVal[0].asFloat();
                y1 = fVal.listVal[1].asFloat();
            }
        }
        if (kwargs.find("to") != kwargs.end()) {
            auto tVal = kwargs.at("to");
            if (tVal.isList() && tVal.listVal.size() >= 2) {
                x2 = tVal.listVal[0].asFloat();
                y2 = tVal.listVal[1].asFloat();
            }
        }
        int lobes = eatsbits::eatscript::resolveArg(args, kwargs, 4, "lobes", eatsbits::eatscript::Value(4)).asInt(4);
        std::string curlDir = eatsbits::eatscript::resolveArg(args, kwargs, 5, "curl_dir", eatsbits::eatscript::Value("left")).asString();
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "color", eatsbits::eatscript::Value("#EDE6D6"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFEDE6D6));
        auto sColVal = eatsbits::eatscript::resolveArg(args, kwargs, 7, "stroke", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t sCol = sColVal.isString() ? CardCanvas::parseColor(sColVal.asString()) : static_cast<uint32_t>(sColVal.asInt(0xFFD4AF37));
        float sWidth = eatsbits::eatscript::resolveArg(args, kwargs, 8, "stroke_width", eatsbits::eatscript::Value(1.2)).asFloat(1.2f);

        canvas->drawAcanthus(x1, y1, x2, y2, lobes, curlDir, col, sCol, sWidth);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "repeat_beads", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float inset = eatsbits::eatscript::resolveArg(args, kwargs, 0, "inset", eatsbits::eatscript::Value(12.0)).asFloat(12.0f);
        float radius = eatsbits::eatscript::resolveArg(args, kwargs, 1, "radius", eatsbits::eatscript::Value(3.5)).asFloat(3.5f);
        float spacing = eatsbits::eatscript::resolveArg(args, kwargs, 2, "spacing", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));

        canvas->repeatBeads(inset, radius, spacing, col);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "stroke_cartouche", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(100.0)).asFloat(100.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(140.0)).asFloat(140.0f);
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 4, "style", eatsbits::eatscript::Value("baroque_oval")).asString();
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(2.5)).asFloat(2.5f);

        canvas->strokeCartouche(cx, cy, rx, ry, style, col, lw);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_ornate_suit", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(sVal.asString());
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(100.0)).asFloat(100.0f);
        auto bodyVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "body_color", eatsbits::eatscript::Value());
        uint32_t bodyCol = bodyVal.isString() ? CardCanvas::parseColor(bodyVal.asString()) : static_cast<uint32_t>(bodyVal.asInt(0));
        auto filigreeVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "filigree_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t filigreeCol = filigreeVal.isString() ? CardCanvas::parseColor(filigreeVal.asString()) : static_cast<uint32_t>(filigreeVal.asInt(0xFFD4AF37));

        canvas->drawOrnateSuit(s, cx, cy, sz, bodyCol, filigreeCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_corner", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat();
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "size", eatsbits::eatscript::Value(80.0)).asFloat(80.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));

        canvas->drawBaroqueCorner(x, y, sz, gCol, aCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_cartouche", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(110.0)).asFloat(110.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(140.0)).asFloat(140.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(2.8)).asFloat(2.8f);

        canvas->drawBaroqueCartouche(cx, cy, rx, ry, gCol, aCol, lw);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_crest", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "scale", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));
        bool flipY = eatsbits::eatscript::resolveArg(args, kwargs, 5, "flip_y", eatsbits::eatscript::Value(false)).asBoolean(false);

        canvas->drawBaroqueCrest(cx, cy, sz, gCol, aCol, flipY);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_spade", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "size", eatsbits::eatscript::Value(160.0)).asFloat(160.0f);
        auto bVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "body_color", eatsbits::eatscript::Value("#14151A"));
        uint32_t bCol = bVal.isString() ? CardCanvas::parseColor(bVal.asString()) : static_cast<uint32_t>(bVal.asInt(0xFF14151A));
        auto fVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "filigree_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t fCol = fVal.isString() ? CardCanvas::parseColor(fVal.asString()) : static_cast<uint32_t>(fVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));

        canvas->drawBaroqueSpade(cx, cy, sz, bCol, fCol, aCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "apply_finish", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 0, "style", eatsbits::eatscript::Value("relief")).asString();
        float az = eatsbits::eatscript::resolveArg(args, kwargs, 1, "light_azimuth", eatsbits::eatscript::Value(135.0)).asFloat(135.0f);
        float el = eatsbits::eatscript::resolveArg(args, kwargs, 2, "light_elevation", eatsbits::eatscript::Value(45.0)).asFloat(45.0f);
        float ao = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ao_intensity", eatsbits::eatscript::Value(1.6)).asFloat(1.6f);
        float depth = eatsbits::eatscript::resolveArg(args, kwargs, 4, "relief_depth", eatsbits::eatscript::Value(4.0)).asFloat(4.0f);
        auto patVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "patina_color", eatsbits::eatscript::Value());
        uint32_t patCol = patVal.isString() ? CardCanvas::parseColor(patVal.asString()) : static_cast<uint32_t>(patVal.asInt(0));
        auto hlVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "highlight_color", eatsbits::eatscript::Value());
        uint32_t hlCol = hlVal.isString() ? CardCanvas::parseColor(hlVal.asString()) : static_cast<uint32_t>(hlVal.asInt(0));
        float grain = eatsbits::eatscript::resolveArg(args, kwargs, 7, "paper_grain", eatsbits::eatscript::Value(0.15)).asFloat(0.15f);
        float bleed = eatsbits::eatscript::resolveArg(args, kwargs, 8, "ink_bleed", eatsbits::eatscript::Value(0.20)).asFloat(0.20f);

        canvas->applyFinish(style, az, el, ao, depth, patCol, hlCol, grain, bleed);
        return eatsbits::eatscript::Value(true);
    });
}

} // namespace solenshaded
