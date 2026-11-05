#include "solenshaded/card_texture_cache.hpp"
#include "solenshaded/card_canvas.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace solenshaded {

CardTextureCache& CardTextureCache::instance() {
    static CardTextureCache s_instance;
    return s_instance;
}

static inline size_t svgCellIndexForCard(Suit s, Rank r) noexcept {
    size_t rIdx = (static_cast<size_t>(r) >= 1 && static_cast<size_t>(r) <= 13)
                  ? (static_cast<size_t>(r) - 1) : 0;
    switch (s) {
        case Suit::Spades:   return 0 + rIdx;
        case Suit::Hearts:   return 13 + rIdx;
        case Suit::Clubs:    return 26 + rIdx;
        case Suit::Diamonds: return 39 + rIdx;
    }
    return 0;
}

static void syncArgb32AndMips(std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        auto& tex = textures[i];
        if (tex.width <= 0 || tex.height <= 0 || tex.rgba.empty()) continue;
        const int w = tex.width;
        const int h = tex.height;
        const size_t count = static_cast<size_t>(w * h);

        tex.argb32.resize(count);
        for (size_t p = 0; p < count; ++p) {
            uint32_t r = tex.rgba[p * 4 + 0];
            uint32_t g = tex.rgba[p * 4 + 1];
            uint32_t b = tex.rgba[p * 4 + 2];
            uint32_t a = tex.rgba[p * 4 + 3];
            tex.argb32[p] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        // Generate Mip 1 (W/2 x H/2, 2x2 box-supersampled)
        if (w >= 100 && h >= 140) {
            const int w1 = w / 2;
            const int h1 = h / 2;
            tex.mip1.resize(static_cast<size_t>(w1 * h1));
            for (int y = 0; y < h1; ++y) {
                const int y2 = y * 2;
                const uint32_t* row0 = &tex.argb32[y2 * w];
                const uint32_t* row1 = &tex.argb32[(y2 + 1) * w];
                uint32_t* dst = &tex.mip1[y * w1];
                for (int x = 0; x < w1; ++x) {
                    const int x2 = x * 2;
                    uint32_t p00 = row0[x2],     p10 = row0[x2 + 1];
                    uint32_t p01 = row1[x2],     p11 = row1[x2 + 1];

                    uint32_t r = (((p00 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF) + 2) >> 2;
                    uint32_t g = (((p00 >> 8)  & 0xFF) + ((p10 >> 8)  & 0xFF) + ((p01 >> 8)  & 0xFF) + ((p11 >> 8)  & 0xFF) + 2) >> 2;
                    uint32_t b = ((p00 & 0xFF)         + (p10 & 0xFF)         + (p01 & 0xFF)         + (p11 & 0xFF)         + 2) >> 2;
                    uint32_t a = (((p00 >> 24) & 0xFF) + ((p10 >> 24) & 0xFF) + ((p01 >> 24) & 0xFF) + ((p11 >> 24) & 0xFF) + 2) >> 2;

                    dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }

            // Generate Mip 2 (W/4 x H/4, 4x4 box-supersampled)
            if (w1 >= 80 && h1 >= 100) {
                const int w2 = w1 / 2;
                const int h2 = h1 / 2;
                tex.mip2.resize(static_cast<size_t>(w2 * h2));
                for (int y = 0; y < h2; ++y) {
                    const int y2 = y * 2;
                    const uint32_t* row0 = &tex.mip1[y2 * w1];
                    const uint32_t* row1 = &tex.mip1[(y2 + 1) * w1];
                    uint32_t* dst = &tex.mip2[y * w2];
                    for (int x = 0; x < w2; ++x) {
                        const int x2 = x * 2;
                        uint32_t p00 = row0[x2],     p10 = row0[x2 + 1];
                        uint32_t p01 = row1[x2],     p11 = row1[x2 + 1];

                        uint32_t r = (((p00 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF) + 2) >> 2;
                        uint32_t g = (((p00 >> 8)  & 0xFF) + ((p10 >> 8)  & 0xFF) + ((p01 >> 8)  & 0xFF) + ((p11 >> 8)  & 0xFF) + 2) >> 2;
                        uint32_t b = ((p00 & 0xFF)         + (p10 & 0xFF)         + (p01 & 0xFF)         + (p11 & 0xFF)         + 2) >> 2;
                        uint32_t a = (((p00 >> 24) & 0xFF) + ((p10 >> 24) & 0xFF) + ((p01 >> 24) & 0xFF) + ((p11 >> 24) & 0xFF) + 2) >> 2;

                        dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

static void applyCardConditioning(
    const std::array<CardTexture, CardTextureCache::kTotalTextures>& srcTextures,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& dstTextures,
    CardWearLevel wearLevel
) {
    if (wearLevel == CardWearLevel::Pristine) {
        for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
            dstTextures[i].width = srcTextures[i].width;
            dstTextures[i].height = srcTextures[i].height;
            dstTextures[i].rgba = srcTextures[i].rgba;
        }
        return;
    }

    float paperWarmth = 0.55f;
    float inkSoftening = 0.35f;
    float linenStrength = 0.016f;
    float edgePatina = 0.0f;
    float edgeScuff = 0.0f;
    float sepiaShift = 0.0f;

    switch (wearLevel) {
        case CardWearLevel::Natural:
            paperWarmth   = 0.55f;
            inkSoftening  = 0.35f;
            linenStrength = 0.016f;
            edgePatina    = 0.0f;
            edgeScuff     = 0.0f;
            sepiaShift    = 0.0f;
            break;
        case CardWearLevel::Played:
            paperWarmth   = 0.75f;
            inkSoftening  = 0.55f;
            linenStrength = 0.020f;
            edgePatina    = 0.45f;
            edgeScuff     = 0.40f;
            sepiaShift    = 0.18f;
            break;
        case CardWearLevel::Vintage:
            paperWarmth   = 1.0f;
            inkSoftening  = 0.85f;
            linenStrength = 0.024f;
            edgePatina    = 0.85f;
            edgeScuff     = 0.80f;
            sepiaShift    = 0.60f;
            break;
        default:
            break;
    }

    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        const auto& src = srcTextures[i];
        auto& dst = dstTextures[i];
        dst.width = src.width;
        dst.height = src.height;
        dst.rgba = src.rgba;
        if (dst.width <= 0 || dst.height <= 0 || dst.rgba.empty()) continue;

        const int w = dst.width;
        const int h = dst.height;
        const float invW = 1.0f / static_cast<float>(w);
        const float invH = 1.0f / static_cast<float>(h);
        const uint8_t cardId = (i == CardTextureCache::kCardBackIndex) ? 0xBCu : static_cast<uint8_t>(i);

        const float cardAgeVariance = cardHashNoise(0x7F4A23u, cardId, 601) * 0.15f;
        const float honorBias = (i < 52 && (i % 13 == 0 || i % 13 >= 10)) ? 0.08f : 0.0f;

        for (int y = 0; y < h; ++y) {
            const float ny = (static_cast<float>(y) + 0.5f) * invH;
            const float dyEdge = static_cast<float>(std::min(y, (h - 1) - y));

            for (int x = 0; x < w; ++x) {
                const size_t p = static_cast<size_t>(y * w + x) * 4;
                float r = static_cast<float>(dst.rgba[p + 0]);
                float g = static_cast<float>(dst.rgba[p + 1]);
                float b = static_cast<float>(dst.rgba[p + 2]);
                const float a = static_cast<float>(dst.rgba[p + 3]);
                if (a < 10.0f) continue;

                const float nx = (static_cast<float>(x) + 0.5f) * invW;
                const float dxEdge = static_cast<float>(std::min(x, (w - 1) - x));
                const float minEdgeDist = std::min(dxEdge, dyEdge);

                // 1. Linen / Air-Cushion Micro-Texture (subtle 2D weave)
                const float weave = std::sin(static_cast<float>(x) * 1.5707963f) * std::cos(static_cast<float>(y) * 1.5707963f);
                const float linenMod = 1.0f + weave * linenStrength;

                // 2. Paper Stock Tone (warm casino ivory / antique stock)
                const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                const float whiteMask = std::clamp((lum - 175.0f) / 65.0f, 0.0f, 1.0f);
                if (whiteMask > 0.0f) {
                    const float paperTargetR = 250.0f - sepiaShift * 16.0f;
                    const float paperTargetG = 246.0f - sepiaShift * 25.0f;
                    const float paperTargetB = 237.0f - sepiaShift * 44.0f;

                    const float blend = whiteMask * paperWarmth;
                    r = r * (1.0f - blend) + paperTargetR * blend;
                    g = g * (1.0f - blend) + paperTargetG * blend;
                    b = b * (1.0f - blend) + paperTargetB * blend;

                    r = std::clamp(r * linenMod, 0.0f, 255.0f);
                    g = std::clamp(g * linenMod, 0.0f, 255.0f);
                    b = std::clamp(b * linenMod, 0.0f, 255.0f);
                }

                // 3. Printing Press Ink Softening & Black Lift
                if (r > 140.0f && r > (g + b) * 0.9f) {
                    // Offset red inks: soften pure digital red to press crimson
                    const float redTargetR = 196.0f - sepiaShift * 22.0f;
                    const float redTargetG = 34.0f  + sepiaShift * 12.0f;
                    const float redTargetB = 34.0f  - sepiaShift * 8.0f;
                    const float inkBlend = inkSoftening * std::clamp((r - (g + b) * 0.9f) / 100.0f, 0.0f, 1.0f);
                    r = r * (1.0f - inkBlend) + redTargetR * inkBlend;
                    g = g * (1.0f - inkBlend) + redTargetG * inkBlend;
                    b = b * (1.0f - inkBlend) + redTargetB * inkBlend;
                } else if (lum < 55.0f) {
                    // Offset black inks: lift pitch black to charcoal
                    const float blackLiftR = 24.0f + sepiaShift * 18.0f;
                    const float blackLiftG = 24.0f + sepiaShift * 12.0f;
                    const float blackLiftB = 28.0f + sepiaShift * 6.0f;
                    const float blackBlend = inkSoftening * (1.0f - lum / 55.0f);
                    r = r * (1.0f - blackBlend) + blackLiftR * blackBlend;
                    g = g * (1.0f - blackBlend) + blackLiftG * blackBlend;
                    b = b * (1.0f - blackBlend) + blackLiftB * blackBlend;
                }

                // 4. Perimeter Handling Patina & Finger Oils (Played & Vintage)
                if (edgePatina > 0.01f) {
                    const float cdx = (nx - 0.5f) * 2.0f;
                    const float cdy = (ny - 0.5f) * 2.0f;
                    const float rDist = std::sqrt(cdx * cdx + cdy * cdy) * 0.70710678f;

                    // Asymmetric thumb grip zones (bottom-right and bottom-left)
                    const float thumb1 = std::exp(-((nx - 0.88f) * (nx - 0.88f) * 16.0f + (ny - 0.92f) * (ny - 0.92f) * 12.0f));
                    const float thumb2 = std::exp(-((nx - 0.12f) * (nx - 0.12f) * 16.0f + (ny - 0.92f) * (ny - 0.92f) * 12.0f));
                    const float thumbPatina = (thumb1 + thumb2 * 0.7f) * 0.09f;

                    float vignette = std::pow(std::clamp(rDist, 0.0f, 1.0f), 2.4f) * (0.08f + cardAgeVariance) + thumbPatina;
                    vignette *= edgePatina;

                    r *= (1.0f - vignette * 0.65f);
                    g *= (1.0f - vignette * 0.85f);
                    b *= (1.0f - vignette * 1.35f);
                }

                // 5. Physical Edge Wear & Paper Core Exposure (Played & Vintage)
                if (edgeScuff > 0.01f && minEdgeDist <= 5.0f) {
                    const float perim = (dxEdge < dyEdge) ? static_cast<float>(y) : static_cast<float>(x);
                    const uint32_t perimCoord = static_cast<uint32_t>(perim * 1.3f);
                    const float scuffNoise = cardHashNoise(0x9E3779B9u + perimCoord, cardId, 701);

                    const float edgeFactor = std::clamp((4.0f - minEdgeDist) / 3.5f, 0.0f, 1.0f);
                    const float wearThreshold = 0.35f - (edgeScuff * 0.25f) - (honorBias * 0.1f);

                    if (scuffNoise > wearThreshold && minEdgeDist <= (1.0f + edgeScuff * 2.8f)) {
                        const float pulpR = 236.0f - sepiaShift * 10.0f;
                        const float pulpG = 228.0f - sepiaShift * 18.0f;
                        const float pulpB = 212.0f - sepiaShift * 30.0f;

                        float pulpIntensity = edgeFactor * std::clamp((scuffNoise - wearThreshold) / (1.0f - wearThreshold), 0.0f, 1.0f);
                        pulpIntensity *= edgeScuff;

                        r = r * (1.0f - pulpIntensity) + pulpR * pulpIntensity;
                        g = g * (1.0f - pulpIntensity) + pulpG * pulpIntensity;
                        b = b * (1.0f - pulpIntensity) + pulpB * pulpIntensity;
                    }
                }

                dst.rgba[p + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
                dst.rgba[p + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
                dst.rgba[p + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
                dst.rgba[p + 3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 255.0f));
            }
        }
    }
}

static void bakeThemeCardBack(CardTexture& back, DeckThemeId theme, int cardW, int cardH) {
    back.width = cardW;
    back.height = cardH;
    back.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    const float pad = std::max(6.0f, static_cast<float>(cardW) * 0.065f);
    const float padInner = pad + std::max(4.0f, static_cast<float>(cardW) * 0.045f);

    for (int y = 0; y < cardH; ++y) {
        float ny = static_cast<float>(y) / static_cast<float>(cardH);
        for (int x = 0; x < cardW; ++x) {
            size_t p = static_cast<size_t>(y * cardW + x) * 4;
            uint8_t r = 240, g = 232, b = 214, a = 255;

            if (theme == DeckThemeId::SteampunkAi) {
                // Warm antique ivory border with Victorian brass & copper filigree
                r = 248; g = 242; b = 230;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(58.0f + 16.0f * t);
                    g = static_cast<uint8_t>(28.0f + 10.0f * t);
                    b = static_cast<uint8_t>(24.0f + 10.0f * t);

                    // Polished brass outer border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 200; g = 150; b = 62;
                    }

                    // Inner copper line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 184; g = 115; b = 51;
                        }

                        // Center Steampunk Clockwork Medallion
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f) {
                            r = 200; g = 150; b = 62;
                        }
                        if (dx + dy <= 0.45f) {
                            r = 140; g = 85; b = 35;
                        }
                    }
                }
            } else if (theme == DeckThemeId::Anime) {
                // Crisp anime cardstock with midnight navy / luminous sapphire & gold celestial frame
                r = 248; g = 250; b = 255;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(18.0f + 12.0f * t);
                    g = static_cast<uint8_t>(24.0f + 16.0f * t);
                    b = static_cast<uint8_t>(44.0f + 26.0f * t);

                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 255; g = 183; b = 3;
                    }

                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 58; g = 134; b = 255;
                        }

                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.18f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.18f);
                        if (dx + dy <= 1.0f || (dx <= 0.2f && dy <= 0.7f) || (dy <= 0.2f && dx <= 0.7f)) {
                            r = 255; g = 183; b = 3;
                        }
                    }
                }
            } else if (theme == DeckThemeId::Cyberpunk) {
                // Dark obsidian cardstock with neon cyan brackets and hot magenta circuit trace
                r = 12; g = 16; b = 23;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(10.0f + 6.0f * t);
                    g = static_cast<uint8_t>(14.0f + 8.0f * t);
                    b = static_cast<uint8_t>(25.0f + 16.0f * t);

                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 0; g = 240; b = 255;
                    }

                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 255; g = 0; b = 127;
                        }

                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f && dx + dy >= 0.82f) {
                            r = 0; g = 240; b = 255;
                        } else if (dx + dy <= 0.45f) {
                            r = 255; g = 230; b = 0;
                        }
                    }
                }
            } else if (theme == DeckThemeId::Custom) {
                // Dark Baroque Noir: deep charcoal/obsidian with sculpted antique silver border and central medallion
                r = 24; g = 26; b = 30; // Dark slate border
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(18.0f + 10.0f * t);
                    g = static_cast<uint8_t>(20.0f + 10.0f * t);
                    b = static_cast<uint8_t>(24.0f + 12.0f * t);

                    // Chiseled antique silver outer border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 192; g = 196; b = 204;
                    }

                    // Inner silver relief filigree line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.2f || dInnerY <= 1.2f) {
                            r = 140; g = 145; b = 155;
                        }

                        // Center Baroque Diamond Medallion
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f && dx + dy >= 0.80f) {
                            r = 192; g = 196; b = 204; // Bright antique silver rim
                        } else if (dx + dy < 0.80f && dx + dy >= 0.35f) {
                            r = 45; g = 48; b = 56;  // Deep relief dark field
                        } else if (dx + dy < 0.35f) {
                            r = 210; g = 215; b = 224; // Central silver flourish core
                        }
                    }
                }
            } else {
                // Classic baize felt panel
                r = 240; g = 232; b = 214;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(22.0f + (36.0f - 22.0f) * t);
                    g = static_cast<uint8_t>(44.0f + (68.0f - 44.0f) * t);
                    b = static_cast<uint8_t>(62.0f + (88.0f - 62.0f) * t);

                    // Gold filigree line
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.5f || dEdgeY <= 1.5f) {
                        r = 218; g = 178; b = 88;
                    }

                    // Inner second gold border
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 180; g = 145; b = 70;
                        }

                        // Center Diamond Sigil
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f) {
                            r = 224; g = 185; b = 92;
                        }
                    }
                }
            }

            back.rgba[p + 0] = r;
            back.rgba[p + 1] = g;
            back.rgba[p + 2] = b;
            back.rgba[p + 3] = a;
        }
    }
}

const char* CardTextureCache::deckThemeName(DeckThemeId theme) noexcept {
    switch (theme) {
        case DeckThemeId::ClassicSvg:  return "CLASSIC";
        case DeckThemeId::SteampunkAi: return "STEAMPUNK";
        case DeckThemeId::Anime:       return "ANIME";
        case DeckThemeId::Cyberpunk:   return "CYBERPUNK";
        case DeckThemeId::Custom:      return "CUSTOM";
        case DeckThemeId::Count:       return "DEFAULT";
    }
    return "UNKNOWN";
}

const char* CardTextureCache::cardWearLevelName(CardWearLevel level) noexcept {
    switch (level) {
        case CardWearLevel::Pristine: return "PRISTINE";
        case CardWearLevel::Natural:  return "NATURAL";
        case CardWearLevel::Played:   return "PLAYED";
        case CardWearLevel::Vintage:  return "VINTAGE";
        case CardWearLevel::Count:    return "DEFAULT";
    }
    return "UNKNOWN";
}

void CardTextureCache::applyConditioningAndSync(CardWearLevel wear) {
    applyCardConditioning(baseTextures_, textures_, wear);
    syncArgb32AndMips(textures_);
}

void CardTextureCache::setCardWearLevel(CardWearLevel level) {
    if (wearLevel_ == level && isLoaded_) return;
    wearLevel_ = level;
    if (isLoaded_) {
        applyConditioningAndSync(wearLevel_);
    }
}

bool CardTextureCache::initialize(int cardW, int cardH) {
    if (isLoaded_ && cardW_ == cardW && cardH_ == cardH) {
        return true;
    }
    return loadDeckTheme(currentTheme_, cardW, cardH);
}

static bool tryLoadBinaryCache(const std::string& binPath, const std::string& srcPath,
                               int cardW, int cardH,
                               std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
    bool cacheFresh = false;
    std::error_code ec;
    if (std::filesystem::exists(binPath, ec) && std::filesystem::exists(srcPath, ec)) {
        auto binTime = std::filesystem::last_write_time(binPath, ec);
        auto srcTime = std::filesystem::last_write_time(srcPath, ec);
        if (!ec && binTime >= srcTime) {
            cacheFresh = true;
        } else {
            std::cout << "[SolEnshaded] Source deck is newer than binary cache (" << binPath << "), reloading...\n";
        }
    } else if (std::filesystem::exists(binPath, ec)) {
        cacheFresh = true;
    }

    if (!cacheFresh) return false;

    FILE* fp = fopen(binPath.c_str(), "rb");
    if (!fp) return false;

    uint32_t header[4] = {0};
    if (fread(header, sizeof(uint32_t), 4, fp) == 4) {
        const uint32_t kMagic = 0x4344434B; // 'CDCK'
        if (header[0] == kMagic &&
            header[1] == static_cast<uint32_t>(cardW) &&
            header[2] == static_cast<uint32_t>(cardH) &&
            header[3] == static_cast<uint32_t>(CardTextureCache::kTotalTextures)) {
            bool readOk = true;
            const size_t byteCount = static_cast<size_t>(cardW * cardH * 4);
            for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
                textures[i].width = cardW;
                textures[i].height = cardH;
                textures[i].rgba.resize(byteCount);
                if (fread(textures[i].rgba.data(), 1, byteCount, fp) != byteCount) {
                    readOk = false;
                    break;
                }
            }
            fclose(fp);
            if (readOk) {
                std::cout << "[SolEnshaded] Loaded deck binary cache from " << binPath << " (" << cardW << "x" << cardH << ")\n";
                return true;
            }
        } else {
            fclose(fp);
        }
    } else {
        fclose(fp);
    }
    return false;
}

static void saveBinaryCache(const std::string& binPath, int cardW, int cardH,
                            const std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
    FILE* outFp = fopen(binPath.c_str(), "wb");
    if (!outFp) return;
    const uint32_t kMagic = 0x4344434B; // 'CDCK'
    uint32_t header[4] = {
        kMagic,
        static_cast<uint32_t>(cardW),
        static_cast<uint32_t>(cardH),
        static_cast<uint32_t>(CardTextureCache::kTotalTextures)
    };
    fwrite(header, sizeof(uint32_t), 4, outFp);
    const size_t byteCount = static_cast<size_t>(cardW * cardH * 4);
    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        fwrite(textures[i].rgba.data(), 1, byteCount, outFp);
    }
    fclose(outFp);
    std::cout << "[SolEnshaded] Saved deck binary cache to " << binPath << "\n";
}

static bool rasterizeSvgToTextures(NSVGimage* image, int cardW, int cardH,
                                   std::array<CardTexture, CardTextureCache::kTotalTextures>& textures,
                                   DeckThemeId currentTheme,
                                   const std::string& binPath) {
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }

    constexpr float kCellW = 750.0f;
    const float scale = static_cast<float>(cardW) / kCellW;

    const int cols = 9;
    const int rows = 6;
    const int totalW = cols * cardW;
    const int totalH = rows * cardH;

    std::cout << "[SolEnshaded] Rasterizing full SVG deck sheet (" << totalW << "x" << totalH << ") ...\n";

    std::vector<uint8_t> sheetRgba(static_cast<size_t>(totalW * totalH * 4), 0);
    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, sheetRgba.data(), totalW, totalH, totalW * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    // Extract and composite each card face onto background color
    const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
    for (Suit s : suits) {
        for (uint8_t r = 1; r <= 13; ++r) {
            Rank rank = static_cast<Rank>(r);
            size_t cell = svgCellIndexForCard(s, rank);
            size_t destIdx = CardTextureCache::cardIndex(s, rank);

            int col = static_cast<int>(cell % 9);
            int row = static_cast<int>(cell / 9);
            int cellX = col * cardW;
            int cellY = row * cardH;

            CardTexture& tex = textures[destIdx];
            tex.width = cardW;
            tex.height = cardH;
            tex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

            int sampleX = std::clamp(cellX + cardW / 6, 0, totalW - 1);
            int sampleY = std::clamp(cellY + cardH / 6, 0, totalH - 1);
            const uint8_t* marginPixel = &sheetRgba[static_cast<size_t>(sampleY * totalW + sampleX) * 4];
            const uint8_t bgR = (marginPixel[3] > 0) ? marginPixel[0] : 255;
            const uint8_t bgG = (marginPixel[3] > 0) ? marginPixel[1] : 255;
            const uint8_t bgB = (marginPixel[3] > 0) ? marginPixel[2] : 255;

            for (int y = 0; y < cardH; ++y) {
                const uint8_t* src = &sheetRgba[static_cast<size_t>((cellY + y) * totalW + cellX) * 4];
                uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                for (int x = 0; x < cardW; ++x) {
                    uint8_t sa = src[3];
                    if (sa == 255) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 255;
                    } else if (sa == 0) {
                        dst[0] = bgR;
                        dst[1] = bgG;
                        dst[2] = bgB;
                        dst[3] = 255;
                    } else {
                        uint32_t invA = 255 - sa;
                        dst[0] = static_cast<uint8_t>((src[0] * sa + bgR * invA) / 255);
                        dst[1] = static_cast<uint8_t>((src[1] * sa + bgG * invA) / 255);
                        dst[2] = static_cast<uint8_t>((src[2] * sa + bgB * invA) / 255);
                        dst[3] = 255;
                    }
                    src += 4;
                    dst += 4;
                }
            }
        }
    }

    bakeThemeCardBack(textures[CardTextureCache::kCardBackIndex], currentTheme, cardW, cardH);
    syncArgb32AndMips(textures);

    if (!binPath.empty()) {
        saveBinaryCache(binPath, cardW, cardH, textures);
    }
    return true;
}

static bool processDecodedSheet(const uint8_t* sheetData, int sheetW, int sheetH,
                                int cardW, int cardH,
                                std::array<CardTexture, CardTextureCache::kTotalTextures>& textures,
                                DeckThemeId currentTheme,
                                const std::string& binPath) {
    constexpr int cols = 9;
    constexpr int rows = 6;
    const int cellSrcW = sheetW / cols;
    const int cellSrcH = sheetH / rows;

    const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
    for (Suit s : suits) {
        for (uint8_t r = 1; r <= 13; ++r) {
            Rank rank = static_cast<Rank>(r);
            size_t cell = svgCellIndexForCard(s, rank);
            size_t destIdx = CardTextureCache::cardIndex(s, rank);

            int col = static_cast<int>(cell % cols);
            int row = static_cast<int>(cell / cols);
            int srcStartX = col * cellSrcW;
            int srcStartY = row * cellSrcH;

            CardTexture& tex = textures[destIdx];
            tex.width = cardW;
            tex.height = cardH;
            tex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

            if (cellSrcW == cardW && cellSrcH == cardH) {
                for (int y = 0; y < cardH; ++y) {
                    const uint8_t* src = &sheetData[static_cast<size_t>((srcStartY + y) * sheetW + srcStartX) * 4];
                    uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                    std::memcpy(dst, src, static_cast<size_t>(cardW * 4));
                }
            } else {
                for (int y = 0; y < cardH; ++y) {
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(cardH) * static_cast<float>(cellSrcH) - 0.5f;
                    int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, cellSrcH - 1);
                    int y1 = std::clamp(y0 + 1, 0, cellSrcH - 1);
                    float fy = v - std::floor(v);

                    uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                    for (int x = 0; x < cardW; ++x) {
                        float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(cardW) * static_cast<float>(cellSrcW) - 0.5f;
                        int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, cellSrcW - 1);
                        int x1 = std::clamp(x0 + 1, 0, cellSrcW - 1);
                        float fx = u - std::floor(u);

                        const uint8_t* p00 = &sheetData[static_cast<size_t>((srcStartY + y0) * sheetW + srcStartX + x0) * 4];
                        const uint8_t* p10 = &sheetData[static_cast<size_t>((srcStartY + y0) * sheetW + srcStartX + x1) * 4];
                        const uint8_t* p01 = &sheetData[static_cast<size_t>((srcStartY + y1) * sheetW + srcStartX + x0) * 4];
                        const uint8_t* p11 = &sheetData[static_cast<size_t>((srcStartY + y1) * sheetW + srcStartX + x1) * 4];

                        for (int c = 0; c < 4; ++c) {
                            float top = p00[c] * (1.0f - fx) + p10[c] * fx;
                            float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
                            dst[x * 4 + c] = static_cast<uint8_t>(std::clamp(std::round(top * (1.0f - fy) + bot * fy), 0.0f, 255.0f));
                        }
                    }
                }
            }
        }
    }

    bakeThemeCardBack(textures[CardTextureCache::kCardBackIndex], currentTheme, cardW, cardH);
    syncArgb32AndMips(textures);

    if (!binPath.empty()) {
        saveBinaryCache(binPath, cardW, cardH, textures);
    }
    return true;
}

bool CardTextureCache::loadDeckTheme(DeckThemeId theme, int cardW, int cardH) {
    currentTheme_ = theme;
    cardW_ = cardW;
    cardH_ = cardH;

    if (theme == DeckThemeId::ClassicSvg) {
        // 1. Try modern self-contained .eats deck recipe (compressed SVGZ embedded in script)
        const std::string candidateEats[] = {
            "decks/classic.eats",
            "../decks/classic.eats",
            "../../decks/classic.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }

        // 2. Fallback to loose SVG vector master sheet
        const std::string candidatePaths[] = {
            "decks/classic_svg.svg",
            "../decks/classic_svg.svg",
            "../../decks/classic_svg.svg"
        };
        for (const auto& path : candidatePaths) {
            if (loadSvgDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::SteampunkAi) {
        // 1. Try modern self-contained .eats deck recipe (PNG embedded in script)
        const std::string candidateEats[] = {
            "decks/steampunk.eats",
            "/decks/steampunk.eats",
            "/user/steampunk.eats",
            "../decks/steampunk.eats",
            "../../decks/steampunk.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }

        // 2. Fallback to loose PNG master sheet
        const std::string candidateBitmaps[] = {
            "decks/steampunk.png",
            "../decks/steampunk.png",
            "../../decks/steampunk.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }

        // 3. Fallback to loose SVG if PNG is missing
        const std::string candidateSvgs[] = {
            "decks/steampunk_svg.svg",
            "../decks/steampunk_svg.svg",
            "../../decks/steampunk_svg.svg"
        };
        for (const auto& path : candidateSvgs) {
            if (loadSvgDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::Custom) {
        const std::string candidateEats[] = {
            "decks/baroque_noir.eats",
            "/decks/baroque_noir.eats",
            "/user/baroque_noir.eats",
            "decks/custom.eats",
            "/decks/custom.eats",
            "/user/custom.eats",
            "../decks/baroque_noir.eats",
            "../../decks/baroque_noir.eats",
            "../decks/custom.eats",
            "../../decks/custom.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_noir.png",
            "decks/custom.png",
            "../decks/baroque_noir.png",
            "../../decks/baroque_noir.png",
            "../decks/custom.png",
            "../../decks/custom.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::Anime) {
        const std::string candidateEats[] = {
            "decks/anime.eats",
            "/decks/anime.eats",
            "/user/anime.eats",
            "../decks/anime.eats",
            "../../decks/anime.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/anime.png",
            "../decks/anime.png",
            "../../decks/anime.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateSvgs[] = {
            "decks/anime_svg.svg",
            "../decks/anime_svg.svg",
            "../../decks/anime_svg.svg"
        };
        for (const auto& path : candidateSvgs) {
            if (loadSvgDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::Cyberpunk) {
        const std::string candidateEats[] = {
            "decks/cyberpunk.eats",
            "/decks/cyberpunk.eats",
            "/user/cyberpunk.eats",
            "../decks/cyberpunk.eats",
            "../../decks/cyberpunk.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/cyberpunk.png",
            "../decks/cyberpunk.png",
            "../../decks/cyberpunk.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateSvgs[] = {
            "decks/cyberpunk_svg.svg",
            "../decks/cyberpunk_svg.svg",
            "../../decks/cyberpunk_svg.svg"
        };
        for (const auto& path : candidateSvgs) {
            if (loadSvgDeck(path, cardW, cardH)) return true;
        }
    }

    if (theme != DeckThemeId::ClassicSvg) {
        std::cout << "[SolEnshaded] Themed deck not found, falling back to classic SVG.\n";
        return loadDeckTheme(DeckThemeId::ClassicSvg, cardW, cardH);
    }

    // Fall back to built-in procedural deck
    std::cout << "[SolEnshaded] Falling back to built-in procedural high-res card textures.\n";
    bakeProceduralDeck(cardW, cardH);
    return true;
}

bool CardTextureCache::loadDeckFromMemory(
    const uint8_t* data,
    size_t size,
    const std::string& format,
    int cardW,
    int cardH,
    DeckThemeId theme,
    const std::string& cacheKey
) {
    if (!data || size == 0) return false;
    cardW_ = cardW;
    cardH_ = cardH;
    currentTheme_ = theme;

    std::string binPath;
    if (!cacheKey.empty()) {
        binPath = cacheKey;
        size_t dotPos = binPath.rfind('.');
        if (dotPos != std::string::npos) {
            binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
        } else {
            binPath += ".bin";
        }
        if (tryLoadBinaryCache(binPath, cacheKey, cardW, cardH, baseTextures_)) {
            applyConditioningAndSync(wearLevel_);
            isLoaded_ = true;
            return true;
        }
    }

    if (format == "svgz" || format == "svg_zlib" || format == "svg_deflate") {
        int outLen = 0;
        char* decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data),
                                               static_cast<int>(size), &outLen);
        if (!decomp) {
            decomp = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(data),
                                                      static_cast<int>(size), &outLen);
        }
        if (!decomp || outLen <= 0) {
            std::cerr << "[SolEnshaded] Failed to decompress SVGZ payload\n";
            return false;
        }

        std::vector<char> svgXml(decomp, decomp + outLen);
        svgXml.push_back('\0');
        free(decomp);

        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEnshaded] Failed to parse decompressed SVG deck\n";
            return false;
        }

        isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, baseTextures_, currentTheme_, binPath);
        if (isLoaded_) {
            applyConditioningAndSync(wearLevel_);
        }
        return isLoaded_;
    } else if (format == "svg") {
        std::vector<char> svgXml(data, data + size);
        svgXml.push_back('\0');

        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEnshaded] Failed to parse SVG deck\n";
            return false;
        }

        isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, baseTextures_, currentTheme_, binPath);
        if (isLoaded_) {
            applyConditioningAndSync(wearLevel_);
        }
        return isLoaded_;
    } else if (format == "png" || format == "bitmap") {
        int sheetW = 0, sheetH = 0, channels = 0;
        stbi_uc* sheetData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                                  static_cast<int>(size),
                                                  &sheetW, &sheetH, &channels, 4);
        if (!sheetData) {
            std::cerr << "[SolEnshaded] Failed to decode PNG deck: " << stbi_failure_reason() << "\n";
            return false;
        }

        isLoaded_ = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, baseTextures_, currentTheme_, binPath);
        if (isLoaded_) {
            applyConditioningAndSync(wearLevel_);
        }
        stbi_image_free(sheetData);
        return isLoaded_;
    }

    std::cerr << "[SolEnshaded] Unknown deck format: " << format << "\n";
    return false;
}

bool CardTextureCache::loadEatsDeck(const std::string& eatsFilePath, int cardW, int cardH) {
    if (eatsFilePath.find("cyberpunk") != std::string::npos) {
        currentTheme_ = DeckThemeId::Cyberpunk;
    } else if (eatsFilePath.find("anime") != std::string::npos) {
        currentTheme_ = DeckThemeId::Anime;
    } else if (eatsFilePath.find("steampunk") != std::string::npos) {
        currentTheme_ = DeckThemeId::SteampunkAi;
    } else if (eatsFilePath.find("classic") != std::string::npos) {
        currentTheme_ = DeckThemeId::ClassicSvg;
    } else {
        currentTheme_ = DeckThemeId::Custom;
    }

    std::string binPath = eatsFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, eatsFilePath, cardW, cardH, baseTextures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        applyConditioningAndSync(wearLevel_);
        isLoaded_ = true;
        return true;
    }

    std::ifstream file(eatsFilePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::cout << "[SolEnshaded] Parsing Eatscript deck recipe: " << eatsFilePath << " ...\n";

    // Split at __BLOB__ or __DATA__ marker if present
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

    // Evaluate script with API bindings
    eatsbits::eatscript::Evaluator evaluator;
    bool apiCalled = false;
    std::vector<uint8_t> apiBlob;
    std::string apiFormat = "svgz";
    int apiCardW = cardW;
    int apiCardH = cardH;

    auto setDeckFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                         const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        apiCalled = true;
        std::string rawStr;
        if (!args.empty() && args[0].isString()) {
            rawStr = args[0].asString();
        } else if (kwargs.find("blob") != kwargs.end()) {
            rawStr = kwargs.at("blob").asString();
        }

        if (rawStr.rfind("data:", 0) == 0) {
            auto parsed = eatsbits::ui::parseDataUri(rawStr);
            if (parsed && !parsed->data.empty()) {
                apiBlob = std::move(parsed->data);
                if (parsed->mimeType.find("svg") != std::string::npos) apiFormat = "svg";
                else if (parsed->mimeType.find("png") != std::string::npos) apiFormat = "png";
            }
        }
        if (apiBlob.empty() && !rawStr.empty()) {
            apiBlob = eatsbits::ui::base64Decode(rawStr);
            if (apiBlob.empty()) {
                apiBlob.assign(rawStr.begin(), rawStr.end());
            }
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

        apiFormat = getOptString("format", apiFormat);
        apiCardW = getOptInt("card_w", apiCardW);
        apiCardH = getOptInt("card_h", apiCardH);

        return eatsbits::eatscript::Value(true);
    };

    auto themeDummyFn = [](const auto&, const auto&) { return eatsbits::eatscript::Value(true); };
    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "create_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_deck", setDeckFn);
    evaluator.registerFunction("create_theme", themeDummyFn);
    evaluator.registerFunction("set_theme", themeDummyFn);
    evaluator.registerFunction("set_deck", setDeckFn);

    CardCanvas::bindToEvaluator(evaluator, nullptr);
    evaluator.evaluateSource(scriptPart);

    if (evaluator.hasFunction("on_draw_card_face") || evaluator.hasFunction("on_draw_card_back")) {
        std::cout << "[SolEnshaded] Generating procedural card deck via Eatscript drawing API...\n";
        cardW_ = cardW;
        cardH_ = cardH;

        const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
        const char* suitNames[4] = { "spades", "hearts", "diamonds", "clubs" };
        const char* rankNames[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };

        CardCanvas canvas(cardW, cardH);
        CardCanvas::bindToEvaluator(evaluator, &canvas);

        for (int sIdx = 0; sIdx < 4; ++sIdx) {
            Suit s = suits[sIdx];
            for (int rIdx = 1; rIdx <= 13; ++rIdx) {
                Rank r = static_cast<Rank>(rIdx);
                size_t destIdx = cardIndex(s, r);
                CardTexture& tex = baseTextures_[destIdx];
                tex.width = cardW;
                tex.height = cardH;

                canvas.clear(0x00000000);
                if (evaluator.hasFunction("on_draw_card_face")) {
                    evaluator.callFunction("on_draw_card_face", {
                        eatsbits::eatscript::Value("ctx"),
                        eatsbits::eatscript::Value(rankNames[rIdx - 1]),
                        eatsbits::eatscript::Value(suitNames[sIdx]),
                        eatsbits::eatscript::Value(static_cast<double>(cardW)),
                        eatsbits::eatscript::Value(static_cast<double>(cardH))
                    });
                } else {
                    canvas.drawCardFace(rankNames[rIdx - 1], s, static_cast<float>(cardW), static_cast<float>(cardH));
                }
                tex.rgba = canvas.rgba();
            }
        }

        // Card Back (Index: kTotalTextures - 1)
        size_t backIdx = kTotalTextures - 1;
        CardTexture& backTex = baseTextures_[backIdx];
        backTex.width = cardW;
        backTex.height = cardH;

        canvas.clear(0x00000000);
        if (evaluator.hasFunction("on_draw_card_back")) {
            evaluator.callFunction("on_draw_card_back", {
                eatsbits::eatscript::Value("ctx"),
                eatsbits::eatscript::Value(static_cast<double>(cardW)),
                eatsbits::eatscript::Value(static_cast<double>(cardH))
            });
            backTex.rgba = canvas.rgba();
        } else {
            canvas.fillRoundedRect(0, 0, static_cast<float>(cardW), static_cast<float>(cardH), 8.0f, 0xFF1B365D);
            canvas.strokeRoundedRect(cardW * 0.05f, cardH * 0.04f, cardW * 0.90f, cardH * 0.92f, 6.0f, 0xFFD4AF37, 1.5f);
            backTex.rgba = canvas.rgba();
        }

        syncArgb32AndMips(baseTextures_);
        applyConditioningAndSync(wearLevel_);
        isLoaded_ = true;
        return true;
    }

    std::vector<uint8_t> rawBytes;
    std::string format = "svgz";

    if (apiCalled && !apiBlob.empty()) {
        rawBytes = std::move(apiBlob);
        format = apiFormat;
        cardW = apiCardW;
        cardH = apiCardH;
    } else {
        if (evaluator.hasVariable("format")) {
            format = evaluator.getVariable("format").asString();
        } else if (evaluator.hasVariable("deck_format")) {
            format = evaluator.getVariable("deck_format").asString();
        }

        if (evaluator.hasVariable("card_w")) {
            cardW = static_cast<int>(evaluator.getVariable("card_w").asNumber());
        }
        if (evaluator.hasVariable("card_h")) {
            cardH = static_cast<int>(evaluator.getVariable("card_h").asNumber());
        }

        if (!blobPart.empty()) {
            rawBytes.assign(blobPart.begin(), blobPart.end());
        } else {
            std::string blobStr;
            if (evaluator.hasVariable("deck_blob")) {
                blobStr = evaluator.getVariable("deck_blob").asString();
            } else if (evaluator.hasVariable("blob")) {
                blobStr = evaluator.getVariable("blob").asString();
            }
            if (!blobStr.empty()) {
                rawBytes = eatsbits::ui::base64Decode(blobStr);
                if (rawBytes.empty() && format == "svg") {
                    rawBytes.assign(blobStr.begin(), blobStr.end());
                }
            }
        }
    }

    if (rawBytes.empty()) {
        std::cerr << "[SolEnshaded] No deck payload found in Eatscript deck: " << eatsFilePath << "\n";
        return false;
    }

    return loadDeckFromMemory(rawBytes.data(), rawBytes.size(), format, cardW, cardH, currentTheme_, binPath);
}

bool CardTextureCache::loadBitmapDeck(const std::string& bitmapFilePath, int cardW, int cardH) {
    std::string binPath = bitmapFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, bitmapFilePath, cardW, cardH, baseTextures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        applyConditioningAndSync(wearLevel_);
        isLoaded_ = true;
        return true;
    }

    std::cout << "[SolEnshaded] Loading bitmap deck sheet: " << bitmapFilePath << " ...\n";
    int sheetW = 0, sheetH = 0, channels = 0;
    stbi_uc* sheetData = stbi_load(bitmapFilePath.c_str(), &sheetW, &sheetH, &channels, 4);
    if (!sheetData) {
        std::cerr << "[SolEnshaded] Failed to load bitmap deck: " << bitmapFilePath << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    cardW_ = cardW;
    cardH_ = cardH;
    isLoaded_ = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, baseTextures_, currentTheme_, binPath);
    if (isLoaded_) {
        applyConditioningAndSync(wearLevel_);
    }
    stbi_image_free(sheetData);
    return isLoaded_;
}

bool CardTextureCache::loadSvgDeck(const std::string& svgFilePath, int cardW, int cardH) {
    std::string binPath = svgFilePath;
    if (binPath.size() > 4 && binPath.substr(binPath.size() - 4) == ".svg") {
        binPath.replace(binPath.size() - 4, 4, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, svgFilePath, cardW, cardH, baseTextures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        applyConditioningAndSync(wearLevel_);
        isLoaded_ = true;
        return true;
    }

    std::cout << "[SolEnshaded] Parsing SVG deck: " << svgFilePath << " ...\n";
    NSVGimage* image = nsvgParseFromFile(svgFilePath.c_str(), "px", 96.0f);
    if (!image) {
        std::cerr << "[SolEnshaded] Could not parse SVG deck: " << svgFilePath << "\n";
        return false;
    }

    cardW_ = cardW;
    cardH_ = cardH;
    isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, baseTextures_, currentTheme_, binPath);
    if (isLoaded_) {
        applyConditioningAndSync(wearLevel_);
    }
    return isLoaded_;
}

void CardTextureCache::bakeProceduralDeck(int cardW, int cardH) {
    cardW_ = cardW;
    cardH_ = cardH;

    for (size_t i = 0; i < kTotalTextures; ++i) {
        CardTexture& tex = baseTextures_[i];
        tex.width = cardW;
        tex.height = cardH;
        tex.rgba.assign(static_cast<size_t>(cardW * cardH * 4), 255);

        for (int y = 0; y < cardH; ++y) {
            float t = static_cast<float>(y) / static_cast<float>(cardH);
            uint8_t cr = static_cast<uint8_t>(250.0f - 18.0f * t);
            uint8_t cg = static_cast<uint8_t>(245.0f - 20.0f * t);
            uint8_t cb = static_cast<uint8_t>(232.0f - 28.0f * t);

            for (int x = 0; x < cardW; ++x) {
                size_t p = static_cast<size_t>(y * cardW + x) * 4;
                tex.rgba[p + 0] = cr;
                tex.rgba[p + 1] = cg;
                tex.rgba[p + 2] = cb;
                tex.rgba[p + 3] = 255;
            }
        }
    }

    applyConditioningAndSync(wearLevel_);
    isLoaded_ = true;
}

const CardTexture* CardTextureCache::getCardTexture(Suit s, Rank r) const noexcept {
    size_t idx = cardIndex(s, r);
    return (idx < textures_.size() && isLoaded_) ? &textures_[idx] : nullptr;
}

const CardTexture* CardTextureCache::getCardBackTexture() const noexcept {
    return isLoaded_ ? &textures_[kCardBackIndex] : nullptr;
}

const CardTexture* CardTextureCache::getTextureByIndex(size_t index) const noexcept {
    return (index < textures_.size() && isLoaded_) ? &textures_[index] : nullptr;
}

bool CardTextureCache::buildGpuAtlas(eatsbits::ui::BatchRenderer2D& renderer) {
    if (!isLoaded_) return false;

    // 2048 x 2048 texture atlas for the GPU
    constexpr int kAtlasDim = 2048;
    std::vector<uint32_t> atlasRgba(static_cast<size_t>(kAtlasDim * kAtlasDim), 0);

    for (size_t i = 0; i < kTotalTextures; ++i) {
        const CardTexture& tex = textures_[i];
        int srcW = 0;
        int srcH = 0;
        const uint32_t* srcData = nullptr;

        // Prefer Mip 1 (e.g. 180x252) which is pre-filtered and fits 53 cards in 2048x2048
        if (!tex.mip1.empty()) {
            srcW = tex.width / 2;
            srcH = tex.height / 2;
            srcData = tex.mip1.data();
        } else if (!tex.argb32.empty()) {
            srcW = tex.width;
            srcH = tex.height;
            srcData = tex.argb32.data();
        } else {
            continue;
        }

        int slotPitchX = srcW + 4;
        int slotPitchY = srcH + 4;
        int cols = std::max(1, (kAtlasDim - 4) / slotPitchX);
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;

        int slotX = 2 + col * slotPitchX;
        int slotY = 2 + row * slotPitchY;

        if (slotX + srcW > kAtlasDim || slotY + srcH > kAtlasDim) {
            std::cerr << "[SolEnshaded] Card texture atlas overflow at card " << i << "!\n";
            break;
        }

        // Copy source ARGB32 into atlas RGBA8
        for (int y = 0; y < srcH; ++y) {
            uint32_t* dstRow = &atlasRgba[static_cast<size_t>(slotY + y) * kAtlasDim + slotX];
            const uint32_t* srcRow = &srcData[static_cast<size_t>(y) * srcW];
            for (int x = 0; x < srcW; ++x) {
                uint32_t c = srcRow[x];
                uint32_t r = (c >> 16) & 0xFF;
                uint32_t g = (c >> 8) & 0xFF;
                uint32_t b = c & 0xFF;
                uint32_t a = (c >> 24) & 0xFF;
                dstRow[x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }

        atlasSlots_[i].u0 = (static_cast<float>(slotX) + 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].v0 = (static_cast<float>(slotY) + 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].u1 = (static_cast<float>(slotX + srcW) - 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].v1 = (static_cast<float>(slotY + srcH) - 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].width = srcW;
        atlasSlots_[i].height = srcH;
    }

    renderer.updateRgbaTexture(0, 0, kAtlasDim, kAtlasDim,
                               reinterpret_cast<const uint8_t*>(atlasRgba.data()),
                               kAtlasDim, kAtlasDim);
    hasGpuAtlas_ = true;
    std::cout << "[SolEnshaded] Card texture GPU atlas built successfully (2048x2048 RGBA8, 53 cards).\n";
    return true;
}

const CardAtlasSlot& CardTextureCache::getAtlasSlot(Suit s, Rank r) const noexcept {
    size_t idx = cardIndex(s, r);
    return atlasSlots_[idx];
}

const CardAtlasSlot& CardTextureCache::getBackAtlasSlot() const noexcept {
    return atlasSlots_[kCardBackIndex];
}

const CardAtlasSlot& CardTextureCache::getAtlasSlotByIndex(size_t index) const noexcept {
    return atlasSlots_[std::min(index, kTotalTextures - 1)];
}

} // namespace solenshaded
