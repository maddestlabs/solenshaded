#include "solenshaded/table_renderer.hpp"
#include "solenshaded/card_texture_cache.hpp"
#include "solenshaded/procedural_background.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLFW/glfw3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace solenshaded {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.5707963267948966f;
using eatsbits::ui::Point2D;

enum HudButtonId : int {
    BtnNone       = -1,
    BtnNewDeal    = 0,
    BtnRestart    = 1,
    BtnVariant    = 2,
    BtnTheme      = 3,
    BtnDeck       = 4,
    BtnUndo       = 5,
    BtnRedo       = 6,
    BtnHint       = 7,
    BtnAutoFinish = 8,
    BtnAA         = 9
};

struct HudButtonRect {
    int id;
    float x;
    float y;
    float w;
    float h;
    const char* label;
};

static const char* deckThemeButtonLabel(DeckThemeId theme) noexcept {
    switch (theme) {
        case DeckThemeId::ClassicSvg:  return "DECK: CLASSIC [D]";
        case DeckThemeId::SteampunkAi: return "DECK: STEAMPUNK [D]";
        case DeckThemeId::Anime:       return "DECK: ANIME [D]";
        case DeckThemeId::Cyberpunk:   return "DECK: CYBERPUNK [D]";
        case DeckThemeId::Custom:      return "DECK: CUSTOM [D]";
        default: return "DECK [D]";
    }
}

std::vector<HudButtonRect> buildHudButtons(float width, float height, const SolitaireBoard& board, int aaMode = 2) {
    const float botY = height - 35.0f;
    const char* aaLabel = "AA: 2X FAST [X]";
    if (aaMode == 0) aaLabel = "AA: OFF [X]";
    else if (aaMode == 2) aaLabel = "AA: 4X RGSS [X]";

    const char* deckLabel = deckThemeButtonLabel(CardTextureCache::instance().currentDeckTheme());

    // Dynamically proportion top HUD buttons to fit screen width without overlapping live telemetry
    float btnGap = 8.0f;
    float startX = 16.0f;
    float newDealW = 108.0f;
    float restartW = 98.0f;
    float variantW = 180.0f;
    float themeW = 180.0f;
    float deckW = 180.0f;
    float aaW = 128.0f;

    if (width < 1320.0f) {
        btnGap = 6.0f;
        startX = 12.0f;
        newDealW = 96.0f;
        restartW = 88.0f;
        variantW = 150.0f;
        themeW = 150.0f;
        deckW = 145.0f;
        aaW = 110.0f;
    }
    if (width < 1120.0f) {
        btnGap = 4.0f;
        startX = 8.0f;
        newDealW = 88.0f;
        restartW = 80.0f;
        variantW = 135.0f;
        themeW = 135.0f;
        deckW = 130.0f;
        aaW = 98.0f;
    }

    float x = startX;
    auto makeBtn = [&](HudButtonId id, float w, const char* label) -> HudButtonRect {
        HudButtonRect r{id, x, 8.0f, w, 30.0f, label};
        x += w + btnGap;
        return r;
    };

    HudButtonRect bNewDeal = makeBtn(BtnNewDeal, newDealW, "NEW DEAL [N]");
    HudButtonRect bRestart = makeBtn(BtnRestart, restartW, "RESTART [R]");
    HudButtonRect bVariant = makeBtn(BtnVariant, variantW, SolitaireBoard::variantName(board.variant()));
    HudButtonRect bTheme   = makeBtn(BtnTheme,   themeW,   SolitaireBoard::themeName(board.tableTheme()));
    HudButtonRect bDeck    = makeBtn(BtnDeck,    deckW,    deckLabel);
    HudButtonRect bAA      = makeBtn(BtnAA,      aaW,      aaLabel);

    // Bottom action buttons
    const float botGap = (width < 1120.0f) ? 6.0f : 8.0f;
    const float botStartX = (width < 1120.0f) ? 10.0f : 16.0f;
    const float undoW = (width < 1120.0f) ? 88.0f : 96.0f;
    const float redoW = (width < 1120.0f) ? 88.0f : 96.0f;
    const float hintW = (width < 1120.0f) ? 150.0f : 168.0f;
    const float finishW = (width < 1120.0f) ? 150.0f : 168.0f;

    float bx = botStartX;
    auto makeBotBtn = [&](HudButtonId id, float w, const char* label) -> HudButtonRect {
        HudButtonRect r{id, bx, botY, w, 28.0f, label};
        bx += w + botGap;
        return r;
    };

    HudButtonRect bUndo   = makeBotBtn(BtnUndo,   undoW,   "UNDO [Z]");
    HudButtonRect bRedo   = makeBotBtn(BtnRedo,   redoW,   "REDO [Y]");
    HudButtonRect bHint   = makeBotBtn(BtnHint,   hintW,   "NETHER SIGHT [H]");
    HudButtonRect bFinish = makeBotBtn(BtnAutoFinish, finishW, board.canAutoFinish() ? "AUTO-FINISH [A] *" : "AUTO-STEP [A]");

    return { bNewDeal, bRestart, bVariant, bTheme, bDeck, bAA, bUndo, bRedo, bHint, bFinish };
}

int hitTestHudButtons(float mx, float my, float width, float height, const SolitaireBoard& board, int aaMode = 2) {
    auto btns = buildHudButtons(width, height, board, aaMode);
    for (const auto& b : btns) {
        if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
            return b.id;
        }
    }
    return BtnNone;
}

struct ThemePalette {
    float woodR0, woodG0, woodB0;
    float woodR1, woodG1, woodB1;
    float trimR, trimG, trimB;
    float feltR0, feltG0, feltB0;
    float feltR1, feltG1, feltB1;
    float cardBackR0, cardBackG0, cardBackB0;
    float cardBackR1, cardBackG1, cardBackB1;
    float accentR, accentG, accentB;
};

ThemePalette getPalette(TableThemeId theme) noexcept {
    switch (theme) {
        case TableThemeId::SunkenSanctum:
            return {
                0.11f, 0.12f, 0.16f,
                0.06f, 0.07f, 0.10f,
                0.36f, 0.68f, 0.78f,
                0.10f, 0.14f, 0.20f,
                0.05f, 0.08f, 0.13f,
                0.12f, 0.18f, 0.28f,
                0.06f, 0.10f, 0.18f,
                0.35f, 0.85f, 0.95f
            };
        case TableThemeId::PerilousShores:
            return {
                0.28f, 0.17f, 0.10f,
                0.17f, 0.10f, 0.06f,
                0.78f, 0.62f, 0.30f,
                0.24f, 0.20f, 0.15f,
                0.16f, 0.13f, 0.09f,
                0.42f, 0.16f, 0.14f,
                0.24f, 0.08f, 0.07f,
                0.88f, 0.70f, 0.34f
            };
        case TableThemeId::RosewoodParlor:
            return {
                0.24f, 0.08f, 0.05f,
                0.14f, 0.04f, 0.02f,
                0.84f, 0.68f, 0.32f,
                0.16f, 0.06f, 0.04f,
                0.09f, 0.03f, 0.02f,
                0.35f, 0.08f, 0.08f,
                0.18f, 0.03f, 0.03f,
                0.95f, 0.78f, 0.38f
            };
        case TableThemeId::CarvedStone:
            return {
                0.18f, 0.20f, 0.23f,
                0.10f, 0.11f, 0.13f,
                0.58f, 0.62f, 0.68f,
                0.12f, 0.14f, 0.17f,
                0.07f, 0.08f, 0.10f,
                0.14f, 0.16f, 0.20f,
                0.08f, 0.09f, 0.11f,
                0.65f, 0.85f, 0.95f
            };
        case TableThemeId::ReflectiveTile:
            return {
                0.12f, 0.16f, 0.22f,
                0.06f, 0.08f, 0.12f,
                0.78f, 0.84f, 0.90f,
                0.10f, 0.15f, 0.22f,
                0.05f, 0.08f, 0.13f,
                0.10f, 0.20f, 0.36f,
                0.05f, 0.10f, 0.20f,
                0.35f, 0.75f, 0.98f
            };
        case TableThemeId::EmeraldLawn:
            return {
                0.30f, 0.16f, 0.08f,
                0.16f, 0.08f, 0.04f,
                0.85f, 0.72f, 0.30f,
                0.07f, 0.25f, 0.13f,
                0.03f, 0.14f, 0.07f,
                0.08f, 0.22f, 0.12f,
                0.04f, 0.12f, 0.06f,
                0.40f, 0.90f, 0.50f
            };
        case TableThemeId::HopsackTweed:
            return {
                0.28f, 0.26f, 0.24f,
                0.16f, 0.15f, 0.14f,
                0.85f, 0.84f, 0.80f,
                0.42f, 0.42f, 0.40f,
                0.22f, 0.22f, 0.20f,
                0.20f, 0.20f, 0.22f,
                0.12f, 0.12f, 0.14f,
                0.88f, 0.45f, 0.55f
            };
        case TableThemeId::DenimChambray:
            return {
                0.26f, 0.14f, 0.08f,
                0.15f, 0.08f, 0.04f,
                0.86f, 0.55f, 0.28f,
                0.14f, 0.24f, 0.38f,
                0.07f, 0.12f, 0.22f,
                0.18f, 0.30f, 0.46f,
                0.10f, 0.18f, 0.28f,
                0.95f, 0.65f, 0.35f
            };
        case TableThemeId::StripedJersey:
            return {
                0.18f, 0.19f, 0.21f,
                0.10f, 0.11f, 0.12f,
                0.75f, 0.78f, 0.82f,
                0.16f, 0.17f, 0.19f,
                0.09f, 0.10f, 0.11f,
                0.30f, 0.32f, 0.35f,
                0.16f, 0.17f, 0.19f,
                0.92f, 0.28f, 0.28f
            };
        case TableThemeId::TavernHeartwood:
        default:
            return {
                0.32f, 0.18f, 0.09f,
                0.18f, 0.09f, 0.04f,
                0.76f, 0.58f, 0.26f,
                0.09f, 0.26f, 0.16f,
                0.04f, 0.15f, 0.09f,
                0.16f, 0.26f, 0.44f,
                0.08f, 0.14f, 0.26f,
                0.92f, 0.74f, 0.34f
            };
    }
}

const char* rankString(Rank r) noexcept {
    switch (r) {
        case Rank::Ace:   return "A";
        case Rank::Two:   return "2";
        case Rank::Three: return "3";
        case Rank::Four:  return "4";
        case Rank::Five:  return "5";
        case Rank::Six:   return "6";
        case Rank::Seven: return "7";
        case Rank::Eight: return "8";
        case Rank::Nine:  return "9";
        case Rank::Ten:   return "10";
        case Rank::Jack:  return "J";
        case Rank::Queen: return "Q";
        case Rank::King:  return "K";
        default:          return "?";
    }
}

void drawSuitPip(
    eatsbits::ui::BatchRenderer2D& r,
    Suit suit,
    float cx,
    float cy,
    float radiusX,
    float radiusY,
    float red,
    float green,
    float blue,
    float alpha = 1.0f
) {
    if (radiusX < 1.0f || radiusY < 1.0f) return;
    switch (suit) {
        case Suit::Diamonds: {
            r.drawTriangle(cx, cy - radiusY, cx + radiusX * 0.78f, cy, cx, cy + radiusY, red, green, blue, alpha);
            r.drawTriangle(cx, cy - radiusY, cx, cy + radiusY, cx - radiusX * 0.78f, cy, red, green, blue, alpha);
            break;
        }
        case Suit::Hearts: {
            float lobeR = std::min(radiusX, radiusY) * 0.46f;
            r.drawCircle(cx - radiusX * 0.36f, cy - radiusY * 0.22f, lobeR, red, green, blue, alpha, 10);
            r.drawCircle(cx + radiusX * 0.36f, cy - radiusY * 0.22f, lobeR, red, green, blue, alpha, 10);
            r.drawTriangle(cx - radiusX * 0.80f, cy - radiusY * 0.08f,
                           cx + radiusX * 0.80f, cy - radiusY * 0.08f,
                           cx, cy + radiusY * 0.95f, red, green, blue, alpha);
            break;
        }
        case Suit::Spades: {
            float lobeR = std::min(radiusX, radiusY) * 0.42f;
            r.drawTriangle(cx, cy - radiusY * 0.95f,
                           cx + radiusX * 0.78f, cy + radiusY * 0.12f,
                           cx - radiusX * 0.78f, cy + radiusY * 0.12f, red, green, blue, alpha);
            r.drawCircle(cx - radiusX * 0.34f, cy + radiusY * 0.18f, lobeR, red, green, blue, alpha, 10);
            r.drawCircle(cx + radiusX * 0.34f, cy + radiusY * 0.18f, lobeR, red, green, blue, alpha, 10);
            r.drawTriangle(cx, cy + radiusY * 0.25f,
                           cx + radiusX * 0.26f, cy + radiusY * 0.95f,
                           cx - radiusX * 0.26f, cy + radiusY * 0.95f, red, green, blue, alpha);
            break;
        }
        case Suit::Clubs: {
            float lobeR = std::min(radiusX, radiusY) * 0.38f;
            r.drawCircle(cx, cy - radiusY * 0.36f, lobeR, red, green, blue, alpha, 10);
            r.drawCircle(cx - radiusX * 0.38f, cy + radiusY * 0.14f, lobeR, red, green, blue, alpha, 10);
            r.drawCircle(cx + radiusX * 0.38f, cy + radiusY * 0.14f, lobeR, red, green, blue, alpha, 10);
            r.drawTriangle(cx, cy + radiusY * 0.15f,
                           cx + radiusX * 0.25f, cy + radiusY * 0.95f,
                           cx - radiusX * 0.25f, cy + radiusY * 0.95f, red, green, blue, alpha);
            break;
        }
    }
}

struct Card3DTransform {
    float cx{0.0f};
    float cy{0.0f};
    float cz{0.0f};
    float baseW{0.0f};
    float baseH{0.0f};
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    float thickness{2.4f};
    float fovDist{720.0f};

    Point2D projectLocal(float lx, float ly, float lz) const {
        // 1. Roll around Z
        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        const float rx = lx * cosR - ly * sinR;
        const float ry = lx * sinR + ly * cosR;
        const float rz = lz;

        // 2. Yaw around Y
        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yx = rx * cosY + rz * sinY;
        const float yy = ry;
        const float yz = -rx * sinY + rz * cosY;

        // 3. Pitch around X
        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float px = yx;
        const float py = yy * cosP - yz * sinP;
        const float pz = yy * sinP + yz * cosP;

        // 4. World position with elevation
        const float wx = px;
        const float wy = py;
        const float wz = cz + pz;

        // 5. Perspective projection
        const float dist = std::max(60.0f, fovDist - wz);
        const float s = fovDist / dist;

        return Point2D{
            cx + wx * s,
            cy + wy * s - cz * 0.35f
        };
    }

    Point2D projectUV(float u, float v, float zSign = 1.0f) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = zSign * (thickness * 0.5f);
        return projectLocal(lx, ly, lz);
    }

    float getScaleUV(float u, float v, float zSign = 1.0f) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = zSign * (thickness * 0.5f);

        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        const float rx = lx * cosR - ly * sinR;
        const float ry = lx * sinR + ly * cosR;
        const float rz = lz;

        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yz = -rx * sinY + rz * cosY;

        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float pz = ry * sinP + yz * cosP;

        const float wz = cz + pz;
        const float dist = std::max(60.0f, fovDist - wz);
        return fovDist / dist;
    }

    Point2D projectShadow(float u, float v, const eatsbits::ui::LightSource2D& light) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = 0.0f;

        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        const float rx = lx * cosR - ly * sinR;
        const float ry = lx * sinR + ly * cosR;

        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yx = rx * cosY + lz * sinY;
        const float yy = ry;
        const float yz = -rx * sinY + lz * cosY;

        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float px = yx;
        const float py = yy * cosP - yz * sinP;
        const float pz = yy * sinP + yz * cosP;

        const float vx = cx + px;
        const float vy = cy + py;
        // Total elevation off table plane: base elevation cz + 3D tilt pz + ground clearance (3.0px)
        const float wz = std::max(0.0f, cz + pz) + 3.0f;

        // Perspective shadow projection from light (light.x, light.y, light.z) to table plane (z=0)
        const float lightZ = std::max(100.0f, light.z);
        const float t = wz / std::max(50.0f, lightZ - wz);
        const float sOffX = (vx - light.x) * t;
        const float sOffY = (vy - light.y) * t;

        return Point2D{
            vx + sOffX,
            vy + sOffY
        };
    }

    float frontArea() const {
        Point2D p0 = projectUV(0.0f, 0.0f, 1.0f);
        Point2D p1 = projectUV(1.0f, 0.0f, 1.0f);
        Point2D p2 = projectUV(1.0f, 1.0f, 1.0f);
        return (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
    }

    bool isFrontFacing() const {
        return frontArea() > 0.0f;
    }
};

inline std::array<Point2D, 16> getCardPerimeterUV(float ru = 0.065f, float rv = 0.046f, float marginU = 0.0f, float marginV = 0.0f) {
    constexpr float kC30 = 0.8660254f;
    constexpr float kS30 = 0.5000000f;

    const float l = marginU;
    const float r = 1.0f - marginU;
    const float t = marginV;
    const float b = 1.0f - marginV;

    return {{
        Point2D{r - ru, t},
        Point2D{r - ru + ru * kS30, t + rv - rv * kC30},
        Point2D{r - ru + ru * kC30, t + rv - rv * kS30},
        Point2D{r, t + rv},

        Point2D{r, b - rv},
        Point2D{r - ru + ru * kC30, b - rv + rv * kS30},
        Point2D{r - ru + ru * kS30, b - rv + rv * kC30},
        Point2D{r - ru, b},

        Point2D{l + ru, b},
        Point2D{l + ru - ru * kS30, b - rv + rv * kC30},
        Point2D{l + ru - ru * kC30, b - rv + rv * kS30},
        Point2D{l, b - rv},

        Point2D{l, t + rv},
        Point2D{l + ru - ru * kC30, t + rv - rv * kS30},
        Point2D{l + ru - ru * kS30, t + rv - rv * kC30},
        Point2D{l + ru, t}
    }};
}

inline uint32_t packColorU32(float r, float g, float b, float a = 1.0f) {
    uint32_t ur = static_cast<uint32_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ug = static_cast<uint32_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ub = static_cast<uint32_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ua = static_cast<uint32_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
    return ur | (ug << 8) | (ub << 16) | (ua << 24);
}

void draw3DRoundedSurface(
    eatsbits::ui::BatchRenderer2D& r,
    const Card3DTransform& xf,
    const std::array<Point2D, 16>& uvs,
    float zSign,
    float r0, float g0, float b0,
    float r1, float g1, float b1,
    float alpha = 1.0f
) {
    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(uvs[i].x, uvs[i].y, zSign);
    }

    if (std::abs(r0 - r1) < 0.005f && std::abs(g0 - g1) < 0.005f && std::abs(b0 - b1) < 0.005f) {
        r.drawConvexPolygon(pts.data(), 16, r0, g0, b0, alpha);
    } else {
        Point2D gp0 = xf.projectUV(0.5f, uvs[0].y, zSign);
        Point2D gp1 = xf.projectUV(0.5f, uvs[7].y, zSign);
        r.drawConvexPolygonGradient(pts.data(), 16, r0, g0, b0, r1, g1, b1, gp0, gp1, alpha);
    }
}

void draw3DRoundedOutline(
    eatsbits::ui::BatchRenderer2D& r,
    const Card3DTransform& xf,
    const std::array<Point2D, 16>& uvs,
    float zSign,
    float cr, float cg, float cb, float ca = 1.0f, float lineWidth = 1.0f
) {
    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(uvs[i].x, uvs[i].y, zSign);
    }
    for (size_t i = 0; i < 16; ++i) {
        size_t next = (i + 1) % 16;
        r.drawLine(pts[i].x, pts[i].y, pts[next].x, pts[next].y, cr, cg, cb, ca, lineWidth);
    }
}

void render3DCard(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& c,
    const Card3DTransform& xf,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    // 16-point perimeter polygons with smooth circular rounded corners
    const auto outerUV = getCardPerimeterUV(0.065f, 0.046f, 0.0f, 0.0f);

    // 1. Dynamic Detached 3D Shadow with Rounded Edges
    {
        std::array<Point2D, 16> pts;
        for (size_t i = 0; i < 16; ++i) {
            pts[i] = xf.projectShadow(outerUV[i].x, outerUV[i].y, light);
        }

        const float sAlpha = std::clamp(0.38f - xf.cz * 0.003f, 0.12f, 0.38f);
        for (size_t i = 1; i + 1 < 16; ++i) {
            r.drawTriangle(pts[0].x, pts[0].y, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y,
                           0.0f, 0.0f, 0.0f, sAlpha);
        }

        // Soft outer penumbra with slight expansion scaling with elevation
        std::array<Point2D, 16> extPts;
        const float penumbraScale = 1.05f + std::min(0.12f, xf.cz * 0.006f);
        for (size_t i = 0; i < 16; ++i) {
            float exU = 0.5f + (outerUV[i].x - 0.5f) * penumbraScale;
            float exV = 0.5f + (outerUV[i].y - 0.5f) * penumbraScale;
            extPts[i] = xf.projectShadow(exU, exV, light);
        }
        const float penumbraAlpha = sAlpha * 0.40f;
        for (size_t i = 1; i + 1 < 16; ++i) {
            r.drawTriangle(extPts[0].x, extPts[0].y, extPts[i].x, extPts[i].y, extPts[i + 1].x, extPts[i + 1].y,
                           0.0f, 0.0f, 0.0f, penumbraAlpha);
        }
    }

    // 2. Physical Cardstock Rim along the rounded boundary
    {
        std::array<Point2D, 16> frontPts;
        std::array<Point2D, 16> backPts;
        for (size_t i = 0; i < 16; ++i) {
            frontPts[i] = xf.projectUV(outerUV[i].x, outerUV[i].y, 1.0f);
            backPts[i]  = xf.projectUV(outerUV[i].x, outerUV[i].y, -1.0f);
        }

        for (size_t i = 0; i < 16; ++i) {
            size_t next = (i + 1) % 16;
            Point2D f0 = frontPts[i];
            Point2D f1 = frontPts[next];
            Point2D b1 = backPts[next];
            Point2D b0 = backPts[i];

            float area = (f1.x - f0.x) * (b1.y - f0.y) - (b1.x - f0.x) * (f1.y - f0.y);
            if (area > 0.0f) {
                float lightShade = 0.88f + (outerUV[i].x - 0.5f) * 0.08f;
                float rimR = 0.91f * lightShade;
                float rimG = 0.88f * lightShade;
                float rimB = 0.82f * lightShade;
                r.drawTriangle(f0.x, f0.y, f1.x, f1.y, b1.x, b1.y, rimR, rimG, rimB, 1.0f);
                r.drawTriangle(f0.x, f0.y, b1.x, b1.y, b0.x, b0.y, rimR, rimG, rimB, 1.0f);
            }
        }
    }

    const bool front = xf.isFrontFacing();
    const float zSign = front ? 1.0f : -1.0f;

    // Smooth directional lighting from light source based on 3D surface normal
    const float cosY = std::cos(xf.yaw);
    const float sinY = std::sin(xf.yaw);
    const float cosP = std::cos(xf.pitch);
    const float sinP = std::sin(xf.pitch);
    const float nx = zSign * sinY;
    const float ny = -zSign * cosY * sinP;
    const float nz = zSign * cosY * cosP;

    const float toLightX = light.x - xf.cx;
    const float toLightY = light.y - xf.cy;
    const float toLightZ = light.z - xf.cz;
    const float invLen = 1.0f / std::max(1.0f, std::sqrt(toLightX * toLightX + toLightY * toLightY + toLightZ * toLightZ));
    const float lx = toLightX * invLen;
    const float ly = toLightY * invLen;
    const float lz = toLightZ * invLen;

    const float nDotL = std::clamp(nx * lx + ny * ly + nz * lz, -0.25f, 1.0f);
    const float cardLight = std::clamp(0.86f + 0.14f * nDotL, 0.72f, 1.04f);

    // 3. Card Surface with Texture Mapping
    const CardTexture* tex = front
        ? CardTextureCache::instance().getCardTexture(c.suit, c.rank)
        : CardTextureCache::instance().getCardBackTexture();

    Point2D p0 = xf.projectUV(0.0f, 0.0f, zSign);
    Point2D p1 = xf.projectUV(1.0f, 0.0f, zSign);
    Point2D p2 = xf.projectUV(1.0f, 1.0f, zSign);
    Point2D p3 = xf.projectUV(0.0f, 1.0f, zSign);

    float w0 = xf.getScaleUV(0.0f, 0.0f, zSign);
    float w1 = xf.getScaleUV(1.0f, 0.0f, zSign);
    float w2 = xf.getScaleUV(1.0f, 1.0f, zSign);
    float w3 = xf.getScaleUV(0.0f, 1.0f, zSign);

    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(outerUV[i].x, outerUV[i].y, zSign);
    }

    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
        const auto& slot = front
            ? CardTextureCache::instance().getAtlasSlot(c.suit, c.rank)
            : CardTextureCache::instance().getBackAtlasSlot();

        std::array<Point2D, 16> atlasUVs;
        for (size_t i = 0; i < 16; ++i) {
            atlasUVs[i].x = slot.u0 + outerUV[i].x * (slot.u1 - slot.u0);
            atlasUVs[i].y = slot.v0 + outerUV[i].y * (slot.v1 - slot.v0);
        }
        r.drawTexturedPolygon(pts.data(), atlasUVs.data(), 16, cardLight, cardLight, cardLight, 1.0f);
    } else if (tex && (!tex->argb32.empty() || !tex->rgba.empty())) {
        int mipW = tex->width;
        int mipH = tex->height;
        const uint32_t* mipData = tex->getMipData(xf.baseW, mipW, mipH);
        if (mipData) {
            r.drawTexturedConvexPolygon(pts.data(), 16, mipData, mipW, mipH,
                                        p0, p1, p2, p3, w0, w1, w2, w3, cardLight);
        } else if (!tex->rgba.empty()) {
            r.drawTexturedConvexPolygon(pts.data(), 16, tex->rgba.data(), tex->width, tex->height,
                                        p0, p1, p2, p3, w0, w1, w2, w3, cardLight);
        }
    } else {
        // Fallback to solid surface
        draw3DRoundedSurface(r, xf, outerUV, zSign,
                             0.98f * cardLight, 0.96f * cardLight, 0.90f * cardLight,
                             0.92f * cardLight, 0.88f * cardLight, 0.79f * cardLight, 1.0f);
    }

    if (c.enshadedGlow) {
        draw3DRoundedOutline(r, xf, outerUV, zSign, pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f);
    } else {
        draw3DRoundedOutline(r, xf, outerUV, zSign, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f);
    }
}

void drawProceduralCard(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& c,
    float baseX,
    float baseY,
    float baseW,
    float baseH,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    if (c.flipAngle >= 0.005f) {
        // True 3D In-Game Card Flip (Hardwood Solitaire 3D trajectory with dynamic flutter & roll variance)
        Card3DTransform xf{};
        xf.baseW = baseW;
        xf.baseH = baseH;

        const float arc = std::sin(c.flipAngle);
        const float flutter = std::sin(c.flipAngle * 3.0f) * 0.025f; // subtle air resistance flutter
        const float rollVariance = cardHashNoise(0x7F4A23u, c.id, 501) * 0.05f;
        const float pitchVariance = cardHashNoise(0x7F4A23u, c.id, 502) * 0.04f;
        const float peelVariance = cardHashNoise(0x7F4A23u, c.id, 503) * 0.02f;

        xf.yaw = c.flipAngle;
        xf.cz = arc * (baseW * 0.55f) + c.elevation * 1.8f;
        xf.pitch = 0.22f + pitchVariance + arc * 0.10f;
        xf.roll = c.angle - arc * (0.08f + rollVariance) + flutter;

        const float peelShift = (std::cos(c.flipAngle) - 1.0f) * (baseW * (0.06f + peelVariance));
        xf.cx = baseX + baseW * 0.5f + peelShift;
        xf.cy = baseY + baseH * 0.5f;

        render3DCard(r, c, xf, pal, light);
        return;
    }
    const float liftScale = 1.0f + c.elevation * 0.0035f;
    const float flipCos = std::cos(c.flipAngle);
    const float scaleX = std::max(0.06f, std::abs(flipCos)) * liftScale;
    const float scaleY = liftScale;

    const float w = baseW * scaleX;
    const float h = baseH * scaleY;
    const float x = baseX + (baseW - w) * 0.5f;
    const float y = baseY + (baseH - h) * 0.5f - c.elevation * 0.65f;

    const float radius = std::max(4.0f, w * 0.08f);

    // Hardwood 3.0 Scoped 2D Rotation for resting micro-tilts & flight banking
    struct ScopedCardRotation {
        eatsbits::ui::BatchRenderer2D& renderer;
        bool active;
        ScopedCardRotation(eatsbits::ui::BatchRenderer2D& rIn, float angleRad, float cx, float cy)
            : renderer(rIn), active(std::abs(angleRad) > 0.0001f) {
            if (active) {
                renderer.setRotation(angleRad * (180.0f / 3.14159265358979323846f), cx, cy);
            }
        }
        ~ScopedCardRotation() {
            if (active) {
                renderer.resetRotation();
            }
        }
    };
    ScopedCardRotation rotGuard(r, c.angle, x + w * 0.5f, y + h * 0.5f);

    // Dynamic drop shadow driven by physical light source:
    // Expands and softens naturally when card is elevated or dragged
    const float shadowElevation = 2.5f + c.elevation;
    const float shadowOpacity = (c.elevation > 0.5f) ? 0.44f : 0.32f;
    r.drawRectDropShadow(x, y, w, h, radius, shadowElevation, light, shadowOpacity);

    const bool showBack = (!c.faceUp) || (c.flipAngle > kHalfPi);

    // Fast GPU Texture Atlas path
    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
        const auto& slot = showBack
            ? CardTextureCache::instance().getBackAtlasSlot()
            : CardTextureCache::instance().getAtlasSlot(c.suit, c.rank);
        r.drawTexturedRoundedRectGpu(x, y, w, h, radius, slot.u0, slot.v0, slot.u1, slot.v1, 1.0f, 1.0f, 1.0f, 1.0f);

        if (c.enshadedGlow) {
            r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
        } else {
            r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
        }
        return;
    }

    const CardTexture* tex = showBack
        ? CardTextureCache::instance().getCardBackTexture()
        : CardTextureCache::instance().getCardTexture(c.suit, c.rank);

    if (tex && !tex->argb32.empty()) {
        int mipW = tex->width;
        int mipH = tex->height;
        const uint32_t* mipData = tex->getMipData(w, mipW, mipH);
        if (mipData) {
            r.drawTexturedRoundedRect(x, y, w, h, radius, mipData, mipW, mipH, 1.0f);
        }

        if (c.enshadedGlow) {
            r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
        } else {
            r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
        }
        return;
    }

    if (showBack) {
        // Zero-flush procedural card back (cream border + gradient inner panel + gold/cyan filigree frame + center sigil)
        r.drawRoundedRect(x, y, w, h, radius, 0.93f, 0.89f, 0.80f, 1.0f, 4);
        const float pad = std::max(3.5f, w * 0.065f);
        if (w > 2.0f * pad + 4.0f) {
            r.drawRoundedRectGradient(
                x + pad, y + pad, w - 2.0f * pad, h - 2.0f * pad, radius * 0.65f,
                pal.cardBackR0, pal.cardBackG0, pal.cardBackB0,
                pal.cardBackR1, pal.cardBackG1, pal.cardBackB1, 1.0f, 4
            );
            r.drawRoundedRectOutline(x + pad, y + pad, w - 2.0f * pad, h - 2.0f * pad,
                                     radius * 0.65f, pal.trimR, pal.trimG, pal.trimB, 0.78f, 1.2f, 4);
            const float pad2 = pad + std::max(3.0f, w * 0.045f);
            if (w > 2.0f * pad2 + 4.0f) {
                r.drawRoundedRectOutline(x + pad2, y + pad2, w - 2.0f * pad2, h - 2.0f * pad2,
                                         radius * 0.45f, pal.trimR, pal.trimG, pal.trimB, 0.42f, 1.0f, 4);
            }
            drawSuitPip(r, Suit::Diamonds, x + w * 0.5f, y + h * 0.5f, w * 0.16f, baseH * 0.11f,
                        pal.trimR, pal.trimG, pal.trimB, 0.85f);
        }
        r.drawRoundedRectOutline(x, y, w, h, radius, 0.15f, 0.12f, 0.09f, 0.85f, 1.2f, 4);
        return;
    }

    // 2. Warm Antique Parchment Cardstock Face
    r.drawRoundedRectGradient(x, y, w, h, radius,
                              0.98f, 0.96f, 0.90f,
                              0.92f, 0.88f, 0.79f, 1.0f, 4);

    const float inset = std::max(3.5f, w * 0.055f);
    if (w > 2.0f * inset + 6.0f) {
        r.drawRoundedRectOutline(x + inset, y + inset, w - 2.0f * inset, h - 2.0f * inset,
                                 radius * 0.6f, 0.78f, 0.72f, 0.62f, 0.45f, 1.0f, 4);
    }

    const bool red = isRedSuit(c.suit);
    const float sr = red ? 0.76f : 0.13f;
    const float sg = red ? 0.14f : 0.14f;
    const float sb = red ? 0.14f : 0.18f;

    if (scaleX > 0.35f) {
        const char* rStr = rankString(c.rank);
        const float fontH = baseW * 0.15f * scaleX;
        eatsbits::ui::glyph::drawVectorText(r, rStr, x + w * 0.09f, y + h * 0.055f, fontH, sr, sg, sb, 1.0f);
        drawSuitPip(r, c.suit, x + w * 0.15f, y + h * 0.22f, w * 0.072f, baseW * 0.072f, sr, sg, sb, 1.0f);

        eatsbits::ui::glyph::drawVectorText(r, rStr, x + w * 0.76f, y + h * 0.82f, fontH * 0.92f, sr, sg, sb, 0.90f);
        drawSuitPip(r, c.suit, x + w * 0.83f, y + h * 0.75f, w * 0.065f, baseW * 0.065f, sr, sg, sb, 0.90f);

        const int rk = static_cast<int>(c.rank);
        if (rk >= 11) {
            r.drawCircle(x + w * 0.5f, y + h * 0.52f, w * 0.27f, pal.trimR, pal.trimG, pal.trimB, 0.16f, 16);
            r.drawCircleOutline(x + w * 0.5f, y + h * 0.52f, w * 0.27f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.3f, 16);
            drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.54f, w * 0.18f, baseW * 0.18f, sr, sg, sb, 0.95f);
            float cyCrown = y + h * 0.33f;
            r.drawTriangle(x + w * 0.36f, cyCrown + 6.0f, x + w * 0.64f, cyCrown + 6.0f, x + w * 0.5f, cyCrown - 5.0f,
                           pal.trimR, pal.trimG, pal.trimB, 0.92f);
        } else if (c.rank == Rank::Ace) {
            r.drawCircle(x + w * 0.5f, y + h * 0.52f, w * 0.26f, pal.trimR, pal.trimG, pal.trimB, 0.14f, 16);
            drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.52f, w * 0.23f, baseW * 0.23f, sr, sg, sb, 0.96f);
        } else {
            const float prX = w * 0.088f;
            const float prY = baseW * 0.088f;
            if (rk == 2 || rk == 3) {
                drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                if (rk == 3) drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
            } else {
                drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                if (rk >= 6) {
                    drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
                    drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
                }
                if (rk == 5 || rk == 7 || rk == 9) {
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * (rk == 7 ? 0.40f : 0.51f), prX, prY, sr, sg, sb, 0.90f);
                }
                if (rk >= 8) {
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.38f, prX * 0.9f, prY * 0.9f, sr, sg, sb, 0.90f);
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.64f, prX * 0.9f, prY * 0.9f, sr, sg, sb, 0.90f);
                }
            }
        }
    }

    if (c.enshadedGlow) {
        r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                 pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
    } else {
        r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
    }
}

void drawCarvedButton(
    eatsbits::ui::BatchRenderer2D& r,
    const HudButtonRect& b,
    bool hovered,
    bool highlighted,
    const ThemePalette& pal
) {
    const float bgBoost = hovered ? 0.08f : 0.0f;
    r.drawRoundedRectGradient(
        b.x, b.y, b.w, b.h, 5.0f,
        std::min(1.0f, pal.woodR0 + 0.06f + bgBoost),
        std::min(1.0f, pal.woodG0 + 0.05f + bgBoost),
        std::min(1.0f, pal.woodB0 + 0.04f + bgBoost),
        std::min(1.0f, pal.woodR1 + 0.02f + bgBoost),
        std::min(1.0f, pal.woodG1 + 0.02f + bgBoost),
        std::min(1.0f, pal.woodB1 + 0.02f + bgBoost),
        0.96f, 4
    );
    const float borderR = (hovered || highlighted) ? pal.accentR : pal.trimR;
    const float borderG = (hovered || highlighted) ? pal.accentG : pal.trimG;
    const float borderB = (hovered || highlighted) ? pal.accentB : pal.trimB;
    r.drawRoundedRectOutline(b.x, b.y, b.w, b.h, 5.0f, borderR, borderG, borderB,
                             (hovered || highlighted) ? 0.95f : 0.62f, (hovered || highlighted) ? 1.6f : 1.1f, 4);

    eatsbits::ui::glyph::drawVectorText(
        r, b.label, b.x + 10.0f, b.y + (b.h - 11.5f) * 0.5f, 11.5f,
        highlighted ? pal.accentR : 0.94f,
        highlighted ? pal.accentG : 0.90f,
        highlighted ? pal.accentB : 0.82f,
        1.0f
    );
}

void drawStaticTableChrome(
    eatsbits::ui::BatchRenderer2D& r,
    const SolitaireBoard& board,
    float width,
    float height,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    const float topBarH = 46.0f;
    const float botBarH = 42.0f;
    const float playfieldH = height - topBarH - botBarH;

    // 1. Physically Lit Procedural Playfield (Rosewood, Gothic Slate, Reflective Tile, Emerald Lawn, etc.)
    ProceduralBackgroundSystem::instance().renderBackground(
        r, 0.0f, topBarH, width, playfieldH, board.tableTheme(), light, board.seed()
    );

    r.drawDustSpecks(0.0f, topBarH, width, playfieldH, 30, board.seed(), pal.trimR, pal.trimG, pal.trimB, 0.12f);

    // 2. Hardwood Solitaire 3.0 Top & Bottom Carved Molding Bars
    r.drawPatternedRectGradient(
        0.0f, 0.0f, width, topBarH,
        pal.woodR0, pal.woodG0, pal.woodB0,
        pal.woodR1, pal.woodG1, pal.woodB1, 1.0f,
        eatsbits::ui::VectorPatternType::WoodGrain, 0.12f, 10.0f, 0.0f
    );
    r.drawRect(0.0f, topBarH - 1.5f, width, 1.5f, pal.trimR, pal.trimG, pal.trimB, 0.75f);

    r.drawPatternedRectGradient(
        0.0f, height - botBarH, width, botBarH,
        pal.woodR1, pal.woodG1, pal.woodB1,
        pal.woodR0, pal.woodG0, pal.woodB0, 1.0f,
        eatsbits::ui::VectorPatternType::WoodGrain, 0.12f, 10.0f, 0.0f
    );
    r.drawRect(0.0f, height - botBarH, width, 1.5f, pal.trimR, pal.trimG, pal.trimB, 0.65f);

    // 3. Recessed Empty Pile Wells & Foundation Runes
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();
    const float wellRadius = std::max(5.0f, cw * 0.08f);

    const auto& piles = board.piles();
    for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
        const Pile& p = piles[pIdx];
        r.drawRoundedRect(p.x, p.y, cw, ch, wellRadius, 0.0f, 0.0f, 0.0f, 0.25f, 4);
        r.drawRoundedRectOutline(p.x, p.y, cw, ch, wellRadius, pal.trimR, pal.trimG, pal.trimB, 0.40f, 1.2f, 4);

        if (p.type == PileType::Foundation) {
            Suit foundationSuit = static_cast<Suit>(p.index % 4);
            drawSuitPip(r, foundationSuit, p.x + cw * 0.5f, p.y + ch * 0.5f, cw * 0.18f, cw * 0.18f,
                        pal.trimR, pal.trimG, pal.trimB, 0.28f);
        } else if (p.type == PileType::Stock) {
            r.drawCircleOutline(p.x + cw * 0.5f, p.y + ch * 0.5f, cw * 0.20f,
                                pal.trimR, pal.trimG, pal.trimB, 0.40f, 2.0f, 16);
        }
    }
}

} // namespace

void TableRenderer::renderBoard(
    eatsbits::ui::BatchRenderer2D& r,
    const SolitaireBoard& board,
    float width,
    float height,
    int draggedPile,
    int draggedCardIdx,
    float dragOffsetX,
    float dragOffsetY,
    int hoveredButtonId
) {
    const ThemePalette pal = getPalette(board.tableTheme());
    // Overhead table illumination: centered horizontally, situated over upper tableau, elevated 550px
    const eatsbits::ui::LightSource2D light{width * 0.5f, height * 0.28f, 550.0f, 1.0f};

    // 1. Static Table Chrome (Recorded once into FBO cache and blitted via fast memcpy!)
    static TableThemeId s_cachedTheme = static_cast<TableThemeId>(-1);
    static GameVariant s_cachedVariant = GameVariant::KlondikeDraw1;
    static float s_cachedW = 0.0f;
    static float s_cachedH = 0.0f;

    if (s_cachedTheme != board.tableTheme() ||
        s_cachedVariant != board.variant() ||
        std::abs(s_cachedW - width) > 0.5f ||
        std::abs(s_cachedH - height) > 0.5f) {
        r.invalidateChromeCache();
        ProceduralBackgroundSystem::instance().invalidateCache();
        s_cachedTheme = board.tableTheme();
        s_cachedVariant = board.variant();
        s_cachedW = width;
        s_cachedH = height;
    }

    if (r.isChromeCacheValid()) {
        r.drawCachedChrome(1.0f);
    } else if (r.beginChromeRecording()) {
        drawStaticTableChrome(r, board, width, height, pal, light);
        r.endChromeRecording();
    } else {
        drawStaticTableChrome(r, board, width, height, pal, light);
    }

    // 2. Interactive HUD Buttons & Live Telemetry
    auto btns = buildHudButtons(width, height, board, r.getAntiAliasingMode());
    for (const auto& b : btns) {
        bool hi = (b.id == BtnAutoFinish && board.canAutoFinish());
        drawCarvedButton(r, b, b.id == hoveredButtonId, hi, pal);
    }

    int mins = static_cast<int>(board.elapsedSeconds()) / 60;
    int secs = static_cast<int>(board.elapsedSeconds()) % 60;
    char headerRight[128];
    if (width >= 1180.0f) {
        std::snprintf(headerRight, sizeof(headerRight), "SEED #%u   SCORE: %d   MOVES: %u   %02d:%02d",
                      board.seed(), board.score(), board.moveCount(), mins, secs);
    } else {
        std::snprintf(headerRight, sizeof(headerRight), "SCORE: %d   MOVES: %u   %02d:%02d",
                      board.score(), board.moveCount(), mins, secs);
    }
    const float headerTextLen = static_cast<float>(std::strlen(headerRight)) * 6.6f;
    const float topButtonsEnd = (btns.size() > 5) ? (btns[5].x + btns[5].w) : 800.0f;
    const float headerX = std::max(topButtonsEnd + 14.0f, width - headerTextLen - 16.0f);
    eatsbits::ui::glyph::drawVectorText(r, headerRight, headerX, 16.0f, 12.5f,
                                        0.94f, 0.90f, 0.82f, 0.96f);

    const char* footerText = (width >= 980.0f)
        ? "SOLITAIRE ENSHADED  |  RIGHT-CLICK OR TAP CARD FOR SMART MOVE"
        : "RIGHT-CLICK OR TAP CARD FOR SMART MOVE";
    const float footerLen = static_cast<float>(std::strlen(footerText)) * 6.2f;
    const float botButtonsEnd = (btns.size() > 9) ? (btns[9].x + btns[9].w) : 540.0f;
    const float footerX = std::max(botButtonsEnd + 16.0f, width - footerLen - 16.0f);
    eatsbits::ui::glyph::drawVectorText(r, footerText, footerX, height - 27.0f, 11.0f,
                                        pal.accentR, pal.accentG, pal.accentB, 0.88f);

    // 3. Resting Cards (culls buried invisible cards in Stock/Waste/Foundation!)
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();
    const auto& piles = board.piles();

    auto isCardMoving = [](const Card& c) noexcept -> bool {
        const float dx = c.x - c.targetX;
        const float dy = c.y - c.targetY;
        const float da = c.angle - c.targetAngle;
        return (dx * dx + dy * dy > 0.25f) || (std::abs(da) > 0.003f) || (c.elevation > 0.15f) || (c.flipAngle > 0.005f);
    };

    std::vector<const Card*> animatingCards;

    for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
        const Pile& p = piles[pIdx];
        if (p.cards.empty()) continue;

        size_t startIdx = 0;
        if (p.type == PileType::Stock) {
            // Render up to top 5 cards to show the stratified physical deck edge thickness
            startIdx = (p.cards.size() > 5) ? (p.cards.size() - 5) : 0;
        } else if (p.type == PileType::Foundation) {
            // Render up to top 4 cards to show stratified foundation stack depth
            startIdx = (p.cards.size() > 4) ? (p.cards.size() - 4) : 0;
        } else if (p.type == PileType::FreeCell) {
            startIdx = (p.cards.size() > 2) ? (p.cards.size() - 2) : 0;
        } else if (p.type == PileType::Waste) {
            // At most top 4 cards are visible during Draw-3 fan
            startIdx = (p.cards.size() > 4) ? (p.cards.size() - 4) : 0;
        }

        for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
            if (static_cast<int>(pIdx) == draggedPile &&
                draggedCardIdx >= 0 &&
                static_cast<int>(cIdx) >= draggedCardIdx) {
                continue;
            }
            if (board.isVictory() && p.type == PileType::Foundation) {
                uint8_t fIdx = p.index;
                float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + fIdx * 0.06f;
                if (board.victorySeconds() >= tLaunch) {
                    continue; // Card launched into 3D victory cascade
                }
            }
            const Card& c = p.cards[cIdx];
            if (isCardMoving(c)) {
                animatingCards.push_back(&c);
            } else if (cIdx >= startIdx) {
                drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light);
            }
        }
    }

    // 4. Spring-Animated & 3D Flipping Cards (Always rendered above all resting cards)
    if (!animatingCards.empty()) {
        std::stable_sort(animatingCards.begin(), animatingCards.end(), [](const Card* a, const Card* b) noexcept {
            float elevA = a->elevation + std::sin(a->flipAngle) * 25.0f;
            float elevB = b->elevation + std::sin(b->flipAngle) * 25.0f;
            return elevA < elevB - 0.01f;
        });
        for (const Card* c : animatingCards) {
            drawProceduralCard(r, *c, c->x, c->y, cw, ch, pal, light);
        }
    }

    // 5. Dragged Card Sub-Stack (Elevated on top of all piles and animations)
    if (draggedPile >= 0 && draggedPile < static_cast<int>(piles.size()) && draggedCardIdx >= 0) {
        const Pile& p = piles[static_cast<size_t>(draggedPile)];
        for (size_t cIdx = static_cast<size_t>(draggedCardIdx); cIdx < p.cards.size(); ++cIdx) {
            Card c = p.cards[cIdx];
            c.elevation = 14.0f;
            // Dynamic drag lean based on horizontal movement
            c.angle += std::clamp(dragOffsetX * 0.0006f, -0.045f, 0.045f);
            drawProceduralCard(r, c, c.x + dragOffsetX, c.y + dragOffsetY, cw, ch, pal, light);
        }
    }

    // 6. Hardwood 3.0 3D Victory Cascade & Banner Overlay
    if (board.isVictory()) {
        struct VictoryAirborneCard {
            Card card;
            Card3DTransform xf;
        };

        std::vector<VictoryAirborneCard> airborne;
        airborne.reserve(52);

        const float floorY = height - ch * 0.75f;
        const float vTime = board.victorySeconds();

        for (const auto& p : board.piles()) {
            if (p.type != PileType::Foundation) continue;
            uint8_t fIdx = p.index;

            for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
                float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + fIdx * 0.06f;
                if (vTime < tLaunch) continue;

                float dt = vTime - tLaunch;
                const Card& c = p.cards[cIdx];
                uint32_t cardId = static_cast<uint32_t>(fIdx * 13 + cIdx);

                VictoryAirborneCard vac{};
                vac.card = c;
                vac.card.faceUp = true;
                vac.card.flipAngle = 0.0f;

                // Horizontal motion with edge bouncing
                float startX = p.x + cw * 0.5f;
                float startY = p.y + ch * 0.5f;
                float vx = ((static_cast<float>(fIdx) - 1.5f) * 170.0f) + std::sin(static_cast<float>(cardId) * 2.3f) * 85.0f;
                float rawX = startX + vx * dt;

                const float minX = cw * 0.6f;
                const float maxX = width - cw * 0.6f;
                const float spanX = std::max(10.0f, maxX - minX);
                float normX = std::fmod(std::abs(rawX - minX), spanX * 2.0f);
                if (normX > spanX) normX = spanX * 2.0f - normX;
                float posX = minX + normX;

                // Vertical bouncing physics
                const float bouncePeriod = 1.45f;
                float cycle = std::fmod(dt, bouncePeriod) / bouncePeriod;
                int bounceNum = static_cast<int>(dt / bouncePeriod);
                float decay = std::max(0.25f, 1.0f - static_cast<float>(bounceNum) * 0.12f);
                float apexH = (decay * 340.0f) + 60.0f;
                float posY = floorY - 4.0f * apexH * cycle * (1.0f - cycle);
                if (dt < 0.35f) {
                    float launchT = dt / 0.35f;
                    posY = startY + (posY - startY) * launchT;
                }

                // 3D elevation and tumbling rotations
                float cz = 45.0f + std::sin(dt * 3.6f + static_cast<float>(cardId)) * 35.0f + (1.0f - cycle) * 30.0f;

                Card3DTransform xf{};
                xf.baseW = cw;
                xf.baseH = ch;
                xf.cx = posX;
                xf.cy = posY;
                xf.cz = cz;
                xf.yaw = dt * (3.5f + static_cast<float>(cardId % 4) * 0.6f);
                xf.pitch = 0.22f + std::sin(dt * 4.0f + static_cast<float>(cardId) * 0.6f) * 0.40f;
                xf.roll = std::cos(dt * 3.0f + static_cast<float>(cardId) * 0.8f) * 0.30f;

                vac.xf = xf;
                airborne.push_back(vac);
            }
        }

        // Depth-sort airborne cards from back to front (lowest cz to highest cz)
        std::stable_sort(airborne.begin(), airborne.end(), [](const VictoryAirborneCard& a, const VictoryAirborneCard& b) noexcept {
            return a.xf.cz < b.xf.cz - 0.01f;
        });

        for (const auto& vac : airborne) {
            render3DCard(r, vac.card, vac.xf, pal, light);
        }

        const float bw = 460.0f;
        const float bh = 96.0f;
        const float bx = (width - bw) * 0.5f;
        const float by = (height - bh) * 0.5f;
        r.drawRectDropShadow(bx, by, bw, bh, 10.0f, 16.0f, light, 0.60f);
        r.drawRoundedRectGradient(bx, by, bw, bh, 10.0f,
                                  pal.woodR0, pal.woodG0, pal.woodB0,
                                  pal.woodR1, pal.woodG1, pal.woodB1, 0.98f, 4);
        r.drawRoundedRectOutline(bx, by, bw, bh, 10.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f, 2.2f, 4);
        eatsbits::ui::glyph::drawVectorText(r, "THE VEIL IS PARTED  -  VICTORY!", bx + 48.0f, by + 24.0f, 18.0f,
                                            pal.accentR, pal.accentG, pal.accentB, 1.0f);
        eatsbits::ui::glyph::drawVectorText(r, "PRESS [N] OR CLICK NEW DEAL FOR HANDS ANEW", bx + 64.0f, by + 58.0f, 12.5f,
                                            0.94f, 0.90f, 0.82f, 0.95f);
    }
}

bool TableRenderer::writePngFile(
    const std::string& filePath,
    uint32_t width,
    uint32_t height,
    const uint8_t* rgbaPixels
) {
    if (!rgbaPixels || width == 0 || height == 0) return false;

    auto crc32 = [](const uint8_t* data, size_t len) -> uint32_t {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i) {
            c ^= data[i];
            for (int k = 0; k < 8; ++k) {
                c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
            }
        }
        return ~c;
    };

    auto appendU32BE = [](std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
    };

    auto appendChunk = [&](std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data) {
        appendU32BE(png, static_cast<uint32_t>(data.size()));
        size_t typeOffset = png.size();
        png.push_back(static_cast<uint8_t>(type[0]));
        png.push_back(static_cast<uint8_t>(type[1]));
        png.push_back(static_cast<uint8_t>(type[2]));
        png.push_back(static_cast<uint8_t>(type[3]));
        png.insert(png.end(), data.begin(), data.end());
        uint32_t crc = crc32(png.data() + typeOffset, 4 + data.size());
        appendU32BE(png, crc);
    };

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, width);
    appendU32BE(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    const size_t rowBytes = static_cast<size_t>(width) * 4 + 1;
    raw.reserve(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgbaPixels + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    std::vector<uint8_t> zdata;
    zdata.push_back(0x78);
    zdata.push_back(0x01);
    size_t pos = 0;
    uint32_t s1 = 1, s2 = 0;
    while (pos < raw.size()) {
        size_t blockLen = std::min<size_t>(65535, raw.size() - pos);
        bool finalBlock = (pos + blockLen == raw.size());
        zdata.push_back(finalBlock ? 0x01 : 0x00);
        zdata.push_back(static_cast<uint8_t>(blockLen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((blockLen >> 8) & 0xFF));
        uint16_t nlen = static_cast<uint16_t>(~blockLen);
        zdata.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        for (size_t i = 0; i < blockLen; ++i) {
            uint8_t b = raw[pos + i];
            zdata.push_back(b);
            s1 = (s1 + b) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        pos += blockLen;
    }
    appendU32BE(zdata, (s2 << 16) | s1);
    appendChunk(png, "IDAT", zdata);
    appendChunk(png, "IEND", {});

    std::ofstream out(filePath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

bool TableRenderer::renderToPng(
    SolitaireBoard& board,
    uint32_t width,
    uint32_t height,
    const std::string& outputPngPath,
    int aaMode
) {
    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
    for (int i = 0; i < 90; ++i) {
        board.updateAnimations(1.0f / 60.0f);
    }

    eatsbits::ui::BatchRenderer2D renderer;
    if (!renderer.initialize(nullptr, width, height, eatsbits::ui::RenderBackendType::Filament)) {
        return false;
    }
    CardTextureCache::instance().initialize();
    renderer.setDirectPresent(false);
    renderer.setAntiAliasingMode(aaMode);
    renderer.beginFrame(static_cast<float>(width), static_cast<float>(height));
    renderBoard(renderer, board, static_cast<float>(width), static_cast<float>(height));
    renderer.endFrame();

    std::vector<uint32_t> fbPixels;
    uint32_t outW = width;
    uint32_t outH = height;
    renderer.snapshotFramebuffer(fbPixels, outW, outH);
    renderer.shutdown();

    if (fbPixels.empty()) return false;

    std::vector<uint8_t> rgba(fbPixels.size() * 4);
    for (size_t i = 0; i < fbPixels.size(); ++i) {
        uint32_t p = fbPixels[i];
        rgba[i * 4 + 0] = static_cast<uint8_t>((p >> 16) & 0xFF);
        rgba[i * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
        rgba[i * 4 + 2] = static_cast<uint8_t>(p & 0xFF);
        rgba[i * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
    }

    return writePngFile(outputPngPath, outW, outH, rgba.data());
}

struct InteractiveContext {
    SolitaireBoard& board;
    AudioFoleyEngine& foley;
    eatsbits::ui::BatchRenderer2D renderer;
    GLFWwindow* window = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
#if defined(__EMSCRIPTEN__)
    uint32_t fbWidth = 1280;
    uint32_t fbHeight = 720;
    float currentDpr = 1.0f;
#endif

    int draggedPile = -1;
    int draggedCardIdx = -1;
    double dragStartMouseX = 0.0;
    double dragStartMouseY = 0.0;
    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;

    bool prevLeftDown = false;
    bool prevRightDown = false;
    bool prevKeyN = false;
    bool prevKeyR = false;
    bool prevKeyZ = false;
    bool prevKeyY = false;
    bool prevKeyH = false;
    bool prevKeyA = false;
    bool prevKeyT = false;
    bool prevKeyD = false;
    bool prevKeyX = false;
    bool prevKey1 = false;
    bool prevKey2 = false;
    bool prevKey3 = false;
    bool prevKey4 = false;

    bool autoCascadeActive = false;
    float autoCascadeTimer = 0.0f;
    double lastTime = 0.0;

    InteractiveContext(SolitaireBoard& b, AudioFoleyEngine& f, uint32_t w, uint32_t h)
        : board(b), foley(f), width(w), height(h) {}

    void triggerAction(int btnId) {
        switch (btnId) {
            case BtnNewDeal:
                autoCascadeActive = false;
                board.newDeal(board.seed() + 1u, board.variant());
                foley.triggerSound(FoleySound::DeckShuffle, 0.9f);
                break;
            case BtnRestart:
                autoCascadeActive = false;
                board.restartDeal();
                foley.triggerSound(FoleySound::DeckShuffle, 0.8f);
                break;
            case BtnVariant: {
                autoCascadeActive = false;
                uint8_t nextVar = (static_cast<uint8_t>(board.variant()) + 1u) % 4u;
                board.newDeal(board.seed(), static_cast<GameVariant>(nextVar));
                foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
                break;
            }
            case BtnTheme: {
                uint8_t nextTheme = (static_cast<uint8_t>(board.tableTheme()) + 1u) % static_cast<uint8_t>(TableThemeId::Count);
                board.setTableTheme(static_cast<TableThemeId>(nextTheme));
                foley.triggerSound(FoleySound::WoodClick, 0.8f);
                break;
            }
            case BtnDeck: {
                uint8_t nextDeck = (static_cast<uint8_t>(CardTextureCache::instance().currentDeckTheme()) + 1u) %
                                   static_cast<uint8_t>(DeckThemeId::Count);
#if defined(__EMSCRIPTEN__)
                if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::SteampunkAi) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/steampunk.eats", ec) ||
                                  std::filesystem::exists("/decks/steampunk.eats", ec) ||
                                  std::filesystem::exists("/user/steampunk.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadSteampunkDeck) {
                                window.loadSteampunkDeck();
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::Anime) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/anime.eats", ec) ||
                                  std::filesystem::exists("/decks/anime.eats", ec) ||
                                  std::filesystem::exists("/user/anime.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadAnimeDeck) {
                                window.loadAnimeDeck();
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::Cyberpunk) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/cyberpunk.eats", ec) ||
                                  std::filesystem::exists("/decks/cyberpunk.eats", ec) ||
                                  std::filesystem::exists("/user/cyberpunk.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadCyberpunkDeck) {
                                window.loadCyberpunkDeck();
                            }
                        });
                        break;
                    }
                }
#endif
                CardTextureCache::instance().loadDeckTheme(static_cast<DeckThemeId>(nextDeck));
                if (renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
                    CardTextureCache::instance().buildGpuAtlas(renderer);
                }
                foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
                break;
            }
            case BtnUndo:
                autoCascadeActive = false;
                if (board.undo()) {
                    foley.triggerSound(FoleySound::CardSlide, 0.7f);
                }
                break;
            case BtnRedo:
                autoCascadeActive = false;
                if (board.redo()) {
                    foley.triggerSound(FoleySound::CardSnap, 0.75f);
                }
                break;
            case BtnHint:
                if (board.findBestHint().valid) {
                    foley.triggerSound(FoleySound::SpectreWhisper, 0.85f);
                } else {
                    foley.triggerSound(FoleySound::WoodClick, 0.5f);
                }
                break;
            case BtnAutoFinish:
                if (board.canAutoFinish()) {
                    autoCascadeActive = true;
                    autoCascadeTimer = 0.0f;
                } else if (board.stepAutoFoundation()) {
                    foley.triggerSound(FoleySound::FoundationChime, 0.85f);
                }
                break;
            case BtnAA: {
                int nextAa = (renderer.getAntiAliasingMode() + 1) % 3;
                renderer.setAntiAliasingMode(nextAa);
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                break;
            }
            default:
                break;
        }
    }

    void updateAndRender() {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;

#if defined(__EMSCRIPTEN__)
        double cssW = EM_ASM_DOUBLE({
            var container = document.getElementById('canvas-container');
            return (container && container.clientWidth > 0) ? container.clientWidth : (window.innerWidth || 1280);
        });
        double cssH = EM_ASM_DOUBLE({
            var container = document.getElementById('canvas-container');
            return (container && container.clientHeight > 0) ? container.clientHeight : (window.innerHeight || 720);
        });
        double dpr = EM_ASM_DOUBLE({
            return window.devicePixelRatio || 1.0;
        });
        if (dpr <= 0.0) dpr = 1.0;

        int targetW = static_cast<int>(std::round(cssW));
        int targetH = static_cast<int>(std::round(cssH));
        if (targetW < 320) targetW = 320;
        if (targetH < 240) targetH = 240;

        int targetFbW = static_cast<int>(std::round(static_cast<float>(targetW) * static_cast<float>(dpr)));
        int targetFbH = static_cast<int>(std::round(static_cast<float>(targetH) * static_cast<float>(dpr)));

        if (static_cast<uint32_t>(targetW) != width ||
            static_cast<uint32_t>(targetH) != height ||
            static_cast<uint32_t>(targetFbW) != fbWidth ||
            static_cast<uint32_t>(targetFbH) != fbHeight ||
            std::abs(currentDpr - static_cast<float>(dpr)) > 0.001f) {

            width = static_cast<uint32_t>(targetW);
            height = static_cast<uint32_t>(targetH);
            fbWidth = static_cast<uint32_t>(targetFbW);
            fbHeight = static_cast<uint32_t>(targetFbH);
            currentDpr = static_cast<float>(dpr);

            EM_ASM({
                var canvas = document.getElementById('canvas');
                if (canvas) {
                    canvas.width = $0;
                    canvas.height = $1;
                    canvas.style.width = $2 + 'px';
                    canvas.style.height = $3 + 'px';
                }
                if (typeof GLFW !== 'undefined' && GLFW.active) {
                    GLFW.active.width = $2;
                    GLFW.active.height = $3;
                    GLFW.active.framebufferWidth = $0;
                    GLFW.active.framebufferHeight = $1;
                }
            }, fbWidth, fbHeight, width, height);

            renderer.setRenderScale(currentDpr, currentDpr);
            renderer.resize(fbWidth, fbHeight);
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#else
        int winW = 0, winH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        if (winW > 0 && winH > 0 && (static_cast<uint32_t>(winW) != width || static_cast<uint32_t>(winH) != height)) {
            width = static_cast<uint32_t>(winW);
            height = static_cast<uint32_t>(winH);
            renderer.resize(width, height);
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#endif

        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        const float fmx = static_cast<float>(mx);
        const float fmy = static_cast<float>(my);

        int hoveredBtn = hitTestHudButtons(fmx, fmy, static_cast<float>(width), static_cast<float>(height), board, renderer.getAntiAliasingMode());

        const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (leftDown && !prevLeftDown) {
            if (hoveredBtn != BtnNone) {
                triggerAction(hoveredBtn);
            } else {
                int hitPile = -1;
                int hitCard = -1;
                if (board.hitTestCard(fmx, fmy, hitPile, hitCard)) {
                    if (board.piles()[static_cast<size_t>(hitPile)].type == PileType::Stock) {
                        if (board.drawFromStock()) {
                            foley.triggerSound(FoleySound::CardSlide, 0.85f);
                        }
                    } else if (hitCard >= 0) {
                        draggedPile = hitPile;
                        draggedCardIdx = hitCard;
                        dragStartMouseX = fmx;
                        dragStartMouseY = fmy;
                        dragOffsetX = 0.0f;
                        dragOffsetY = 0.0f;
                        foley.triggerSound(FoleySound::CardSlide, 0.55f);
                    }
                }
            }
        } else if (leftDown && draggedPile >= 0) {
            dragOffsetX = static_cast<float>(fmx - dragStartMouseX);
            dragOffsetY = static_cast<float>(fmy - dragStartMouseY);
        } else if (!leftDown && prevLeftDown && draggedPile >= 0) {
            const float dragDist = std::hypot(dragOffsetX, dragOffsetY);
            bool moved = false;

            if (draggedPile < static_cast<int>(board.piles().size()) &&
                draggedCardIdx >= 0 &&
                static_cast<size_t>(draggedCardIdx) < board.piles()[static_cast<size_t>(draggedPile)].cards.size()) {

                if (dragDist <= 6.0f) {
                    moved = board.smartMove(static_cast<uint8_t>(draggedPile), static_cast<size_t>(draggedCardIdx));
                } else {
                    auto& srcPile = board.piles()[static_cast<size_t>(draggedPile)];
                    const Card& lead = srcPile.cards[static_cast<size_t>(draggedCardIdx)];
                    const float dropCx = lead.x + dragOffsetX + board.cardWidth() * 0.5f;
                    const float dropCy = lead.y + dragOffsetY + board.cardHeight() * 0.5f;
                    int bestTarget = board.findBestDropPile(static_cast<uint8_t>(draggedPile),
                                                            static_cast<size_t>(draggedCardIdx),
                                                            dropCx, dropCy);

                    for (size_t cIdx = static_cast<size_t>(draggedCardIdx); cIdx < srcPile.cards.size(); ++cIdx) {
                        srcPile.cards[cIdx].x += dragOffsetX;
                        srcPile.cards[cIdx].y += dragOffsetY;
                        srcPile.cards[cIdx].elevation = 14.0f;
                    }

                    if (bestTarget >= 0) {
                        moved = board.executeMove(static_cast<uint8_t>(draggedPile),
                                                  static_cast<size_t>(draggedCardIdx),
                                                  static_cast<uint8_t>(bestTarget));
                    }
                }
            }

            if (moved) {
                foley.triggerSound(FoleySound::CardSnap, 0.85f);
            } else if (dragDist > 6.0f) {
                foley.triggerSound(FoleySound::CardSlide, 0.65f);
            }
            draggedPile = -1;
            draggedCardIdx = -1;
            dragOffsetX = 0.0f;
            dragOffsetY = 0.0f;
        }

        if (rightDown && !prevRightDown) {
            int hitPile = -1;
            int hitCard = -1;
            if (board.hitTestCard(fmx, fmy, hitPile, hitCard) && hitPile >= 0 && hitCard >= 0) {
                if (board.smartMove(static_cast<uint8_t>(hitPile), static_cast<size_t>(hitCard))) {
                    foley.triggerSound(FoleySound::CardSnap, 0.85f);
                }
            }
        }

        prevLeftDown = leftDown;
        prevRightDown = rightDown;

        auto edgeKey = [&](int key, bool& prev) -> bool {
            bool down = glfwGetKey(window, key) == GLFW_PRESS;
            bool trig = down && !prev;
            prev = down;
            return trig;
        };

        if (edgeKey(GLFW_KEY_N, prevKeyN)) triggerAction(BtnNewDeal);
        if (edgeKey(GLFW_KEY_R, prevKeyR)) triggerAction(BtnRestart);
        if (edgeKey(GLFW_KEY_Z, prevKeyZ)) triggerAction(BtnUndo);
        if (edgeKey(GLFW_KEY_Y, prevKeyY)) triggerAction(BtnRedo);
        if (edgeKey(GLFW_KEY_H, prevKeyH)) triggerAction(BtnHint);
        if (edgeKey(GLFW_KEY_A, prevKeyA)) triggerAction(BtnAutoFinish);
        if (edgeKey(GLFW_KEY_T, prevKeyT)) triggerAction(BtnTheme);
        if (edgeKey(GLFW_KEY_D, prevKeyD)) triggerAction(BtnDeck);
        if (edgeKey(GLFW_KEY_X, prevKeyX)) triggerAction(BtnAA);
        if (edgeKey(GLFW_KEY_1, prevKey1)) { board.newDeal(board.seed(), GameVariant::KlondikeDraw1); foley.triggerSound(FoleySound::DeckShuffle, 0.85f); }
        if (edgeKey(GLFW_KEY_2, prevKey2)) { board.newDeal(board.seed(), GameVariant::KlondikeDraw3); foley.triggerSound(FoleySound::DeckShuffle, 0.85f); }
        if (edgeKey(GLFW_KEY_3, prevKey3)) { board.newDeal(board.seed(), GameVariant::FreeCell);      foley.triggerSound(FoleySound::DeckShuffle, 0.85f); }
        if (edgeKey(GLFW_KEY_4, prevKey4)) { board.newDeal(board.seed(), GameVariant::SpectresPatience); foley.triggerSound(FoleySound::DeckShuffle, 0.85f); }

        if (autoCascadeActive) {
            autoCascadeTimer += dt;
            if (autoCascadeTimer >= 0.085f) {
                autoCascadeTimer = 0.0f;
                if (board.stepAutoFoundation()) {
                    foley.triggerSound(FoleySound::FoundationChime, 0.85f);
                } else {
                    autoCascadeActive = false;
                }
            }
        }

        board.updateAnimations(dt);

        if (width > 0 && height > 0) {
            renderer.beginFrame(static_cast<float>(width), static_cast<float>(height));
            TableRenderer::renderBoard(renderer, board, static_cast<float>(width), static_cast<float>(height),
                                       draggedPile, draggedCardIdx, dragOffsetX, dragOffsetY, hoveredBtn);
            renderer.endFrame();
        }
    }
};

static std::unique_ptr<InteractiveContext> s_activeContext;

int TableRenderer::runInteractiveWindow(
    SolitaireBoard& board,
    AudioFoleyEngine& foley,
    uint32_t width,
    uint32_t height,
    int initialAaMode
) {
#if defined(__EMSCRIPTEN__)
    double initCssW = EM_ASM_DOUBLE({
        var container = document.getElementById('canvas-container');
        return (container && container.clientWidth > 0) ? container.clientWidth : (window.innerWidth || 1280);
    });
    double initCssH = EM_ASM_DOUBLE({
        var container = document.getElementById('canvas-container');
        return (container && container.clientHeight > 0) ? container.clientHeight : (window.innerHeight || 720);
    });
    double initDpr = EM_ASM_DOUBLE({
        return window.devicePixelRatio || 1.0;
    });
    if (initDpr <= 0.0) initDpr = 1.0;
    if (initCssW > 0.0 && initCssH > 0.0) {
        width = static_cast<uint32_t>(std::round(initCssW));
        height = static_cast<uint32_t>(std::round(initCssH));
    }
    uint32_t initFbW = static_cast<uint32_t>(std::round(static_cast<float>(width) * static_cast<float>(initDpr)));
    uint32_t initFbH = static_cast<uint32_t>(std::round(static_cast<float>(height) * static_cast<float>(initDpr)));
#endif

    if (!glfwInit()) {
        std::cerr << "[SolEnshaded] Failed to initialize GLFW windowing.\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                                          "Solitaire Enshaded (SolEnshaded)", nullptr, nullptr);
    if (!window) {
#if !defined(__EMSCRIPTEN__)
        const char* desc = nullptr;
        glfwGetError(&desc);
        std::cerr << "[SolEnshaded] Failed to create GLFW window: " << (desc ? desc : "unknown error") << "\n";
#else
        std::cerr << "[SolEnshaded] Failed to create GLFW window.\n";
#endif
        glfwTerminate();
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    EM_ASM({
        var canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.width = $0;
            canvas.height = $1;
            canvas.style.width = $2 + 'px';
            canvas.style.height = $3 + 'px';
        }
        if (typeof GLFW !== 'undefined' && GLFW.active) {
            GLFW.active.width = $2;
            GLFW.active.height = $3;
            GLFW.active.framebufferWidth = $0;
            GLFW.active.framebufferHeight = $1;
        }
    }, initFbW, initFbH, width, height);
#endif

    s_activeContext = std::make_unique<InteractiveContext>(board, foley, width, height);
    InteractiveContext& ctx = *s_activeContext;
    ctx.window = window;

    glfwSetDropCallback(window, [](GLFWwindow* /*win*/, int count, const char* paths[]) {
        for (int i = 0; i < count; ++i) {
            if (paths && paths[i]) {
                std::cout << "[SolEnshaded] Drag-and-drop file received: " << paths[i] << std::endl;
                loadFile(paths[i]);
            }
        }
    });
#if defined(__EMSCRIPTEN__)
    ctx.fbWidth = initFbW;
    ctx.fbHeight = initFbH;
    ctx.currentDpr = static_cast<float>(initDpr);
    ctx.renderer.setRenderScale(ctx.currentDpr, ctx.currentDpr);
#endif

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    HWND hwnd = glfwGetWin32Window(window);
    void* nativeHandle = hwnd ? static_cast<void*>(hwnd) : static_cast<void*>(window);
#else
    void* nativeHandle = static_cast<void*>(window);
#endif

#if defined(__EMSCRIPTEN__)
    if (!ctx.renderer.initialize(nativeHandle, initFbW, initFbH, eatsbits::ui::RenderBackendType::WebGPU)) {
#else
    if (!ctx.renderer.initialize(nativeHandle, width, height, eatsbits::ui::RenderBackendType::WebGPU)) {
#endif
        std::cerr << "[SolEnshaded] Failed to initialize 2D batch renderer.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        s_activeContext.reset();
        return 1;
    }

    ctx.renderer.setAntiAliasingMode(initialAaMode);
    CardTextureCache::instance().initialize();
    if (ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
        CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
    }

    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
    foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
    ctx.lastTime = glfwGetTime();

#if defined(__EMSCRIPTEN__)
    std::cout << "[SolEnshaded Web] Registering Emscripten requestAnimationFrame main loop..." << std::endl;
    emscripten_set_main_loop_arg([](void* arg) {
        auto* c = static_cast<InteractiveContext*>(arg);
        if (c) {
            c->updateAndRender();
        }
    }, &ctx, 0, 1);
    return 0;
#else
    while (!glfwWindowShouldClose(window)) {
        ctx.updateAndRender();
    }

    ctx.renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    s_activeContext.reset();
    return 0;
#endif
}

bool loadEatsPackage(InteractiveContext& ctx, const std::string& filePath, int cardW, int cardH) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string scriptPart = content;
    std::string blobPart;
    size_t markerPos = content.find("__BLOB__");
    if (markerPos == std::string::npos) {
        markerPos = content.find("__DATA__");
    }
    if (markerPos != std::string::npos) {
        scriptPart = content.substr(0, markerPos);
        size_t nextLine = content.find('\n', markerPos);
        if (nextLine != std::string::npos) {
            blobPart = content.substr(nextLine + 1);
        }
    }

    eatsbits::eatscript::Evaluator evaluator;
    bool deckConfigured = false;
    bool bgConfigured = false;
    std::string themeName = "Custom Experience";

    auto setThemeFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                          const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        if (!args.empty() && args[0].isString()) {
            themeName = args[0].asString();
        } else if (kwargs.find("name") != kwargs.end()) {
            themeName = kwargs.at("name").asString();
        }
        return eatsbits::eatscript::Value(true);
    };

    auto setBgFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                       const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        std::string rawStr;
        if (!args.empty() && args[0].isString()) {
            rawStr = args[0].asString();
        } else if (kwargs.find("blob") != kwargs.end()) {
            rawStr = kwargs.at("blob").asString();
        }

        std::vector<uint8_t> bgBytes;
        if (rawStr.rfind("data:", 0) == 0) {
            auto parsed = eatsbits::ui::parseDataUri(rawStr);
            if (parsed && !parsed->data.empty()) {
                bgBytes = std::move(parsed->data);
            }
        }
        if (bgBytes.empty() && !rawStr.empty()) {
            bgBytes = eatsbits::ui::base64Decode(rawStr);
            if (bgBytes.empty()) {
                bgBytes.assign(rawStr.begin(), rawStr.end());
            }
        }
        if (bgBytes.empty() && !blobPart.empty()) {
            bgBytes.assign(blobPart.begin(), blobPart.end());
        }

        const eatsbits::eatscript::Value* optsDict = nullptr;
        if (args.size() > 1 && args[1].isDict()) {
            optsDict = &args[1];
        }

        auto getOptFloat = [&](const std::string& key, float defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
            return defVal;
        };

        float uvScale = getOptFloat("uv_scale", 2.0f);
        float normalStrength = getOptFloat("normal_strength", 1.35f);
        float specularIntensity = getOptFloat("specular_intensity", 0.65f);
        float ambientLevel = getOptFloat("ambient_level", 0.22f);
        float diffuseLevel = getOptFloat("diffuse_level", 0.88f);
        float tintR = getOptFloat("tint_r", 1.0f);
        float tintG = getOptFloat("tint_g", 1.0f);
        float tintB = getOptFloat("tint_b", 1.0f);

        if (optsDict && optsDict->hasKey("tint")) {
            auto tintVal = optsDict->get("tint");
            if (tintVal.isList() && tintVal.listVal.size() >= 3) {
                tintR = tintVal.listVal[0].asFloat(tintR);
                tintG = tintVal.listVal[1].asFloat(tintG);
                tintB = tintVal.listVal[2].asFloat(tintB);
            }
        }

        if (!bgBytes.empty() &&
            ProceduralBackgroundSystem::instance().loadCustomThemeFromMemory(
                bgBytes.data(), bgBytes.size(),
                uvScale, normalStrength, specularIntensity,
                ambientLevel, diffuseLevel, tintR, tintG, tintB)) {
            bgConfigured = true;
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    auto setDeckFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                         const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        std::string rawStr;
        if (!args.empty() && args[0].isString()) {
            rawStr = args[0].asString();
        } else if (kwargs.find("blob") != kwargs.end()) {
            rawStr = kwargs.at("blob").asString();
        }

        std::vector<uint8_t> deckBytes;
        std::string format = "svgz";

        if (rawStr.rfind("data:", 0) == 0) {
            auto parsed = eatsbits::ui::parseDataUri(rawStr);
            if (parsed && !parsed->data.empty()) {
                deckBytes = std::move(parsed->data);
                if (parsed->mimeType.find("svg") != std::string::npos) format = "svg";
                else if (parsed->mimeType.find("png") != std::string::npos) format = "png";
            }
        }
        if (deckBytes.empty() && !rawStr.empty()) {
            deckBytes = eatsbits::ui::base64Decode(rawStr);
            if (deckBytes.empty()) {
                deckBytes.assign(rawStr.begin(), rawStr.end());
            }
        }
        if (deckBytes.empty() && !blobPart.empty()) {
            deckBytes.assign(blobPart.begin(), blobPart.end());
        }

        const eatsbits::eatscript::Value* optsDict = nullptr;
        if (args.size() > 1 && args[1].isDict()) {
            optsDict = &args[1];
        }

        auto getOptString = [&](const std::string& key, const std::string& defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asString();
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asString();
            return defVal;
        };
        auto getOptInt = [&](const std::string& key, int defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asInt(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asInt(defVal);
            return defVal;
        };

        format = getOptString("format", format);
        int targetW = getOptInt("card_w", cardW);
        int targetH = getOptInt("card_h", cardH);

        if (!deckBytes.empty() &&
            CardTextureCache::instance().loadDeckFromMemory(
                deckBytes.data(), deckBytes.size(), format, targetW, targetH,
                DeckThemeId::Custom, filePath)) {
            deckConfigured = true;
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "create_theme", setThemeFn);
    evaluator.registerMemberFunction("sol", "set_theme", setThemeFn);
    evaluator.registerMemberFunction("sol", "set_background", setBgFn);
    evaluator.registerMemberFunction("sol", "set_deck", setDeckFn);

    evaluator.registerFunction("create_theme", setThemeFn);
    evaluator.registerFunction("set_theme", setThemeFn);
    evaluator.registerFunction("set_background", setBgFn);
    evaluator.registerFunction("set_deck", setDeckFn);

    evaluator.evaluateSource(scriptPart);

    // If neither background nor deck was loaded via API call, fall back to legacy single-purpose loaders
    if (!deckConfigured && !bgConfigured) {
        if (CardTextureCache::instance().loadEatsDeck(filePath, cardW, cardH)) {
            deckConfigured = true;
        } else if (ProceduralBackgroundSystem::instance().loadCustomEatsTheme(filePath)) {
            bgConfigured = true;
        }
    }

    if (deckConfigured && ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
        CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
    }

    if (deckConfigured && bgConfigured) {
        ctx.foley.triggerSound(FoleySound::DeckShuffle, 0.9f);
        ctx.foley.triggerSound(FoleySound::WoodClick, 0.85f);
        std::cout << "[SolEnshaded] Successfully loaded unified .eats experience (" << themeName
                  << "): deck + table background from: " << filePath << "\n";
        return true;
    } else if (deckConfigured) {
        ctx.foley.triggerSound(FoleySound::DeckShuffle, 0.9f);
        std::cout << "[SolEnshaded] Successfully loaded custom deck from: " << filePath << "\n";
        return true;
    } else if (bgConfigured) {
        ctx.foley.triggerSound(FoleySound::WoodClick, 0.85f);
        std::cout << "[SolEnshaded] Successfully loaded custom theme from: " << filePath << "\n";
        return true;
    }

    return false;
}

bool loadFile(const std::string& filePath) {
    if (filePath.empty() || !s_activeContext) return false;

    auto& ctx = *s_activeContext;
    int cardW = static_cast<int>(ctx.board.cardWidth());
    int cardH = static_cast<int>(ctx.board.cardHeight());
    if (cardW <= 0) cardW = 112;
    if (cardH <= 0) cardH = 156;

    std::string ext;
    size_t dot = filePath.rfind('.');
    if (dot != std::string::npos) {
        ext = filePath.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
    }

    if (ext == ".eats") {
        if (loadEatsPackage(ctx, filePath, cardW, cardH)) {
            return true;
        }
    } else if (ext == ".png") {
        if (CardTextureCache::instance().loadBitmapDeck(filePath, cardW, cardH)) {
            if (ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
                CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
            }
            ctx.foley.triggerSound(FoleySound::DeckShuffle, 0.9f);
            std::cout << "[SolEnshaded] Successfully loaded custom PNG deck from: " << filePath << "\n";
            return true;
        }
    } else if (ext == ".svg") {
        if (CardTextureCache::instance().loadSvgDeck(filePath, cardW, cardH)) {
            if (ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
                CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
            }
            ctx.foley.triggerSound(FoleySound::DeckShuffle, 0.9f);
            std::cout << "[SolEnshaded] Successfully loaded custom SVG deck from: " << filePath << "\n";
            return true;
        }
    } else {
        if (loadEatsPackage(ctx, filePath, cardW, cardH)) {
            return true;
        }
    }

    std::cerr << "[SolEnshaded] Unrecognized or invalid file payload: " << filePath << "\n";
    return false;
}

} // namespace solenshaded

extern "C" {

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
bool solenshaded_load_file_from_web(const char* virtualPath) {
    if (!virtualPath) return false;
    return solenshaded::loadFile(virtualPath);
}

} // extern "C"
