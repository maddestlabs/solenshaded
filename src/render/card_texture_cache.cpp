#include "solenshaded/card_texture_cache.hpp"
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
        case DeckThemeId::Count:       return "DEFAULT";
    }
    return "UNKNOWN";
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
    }

    // Fall back to classic SVG if themed SVG is missing
    if (theme != DeckThemeId::ClassicSvg) {
        std::cout << "[SolEnshaded] Steampunk deck not found, falling back to classic SVG.\n";
        return loadDeckTheme(DeckThemeId::ClassicSvg, cardW, cardH);
    }

    // Fall back to built-in procedural deck
    std::cout << "[SolEnshaded] Falling back to built-in procedural high-res card textures.\n";
    bakeProceduralDeck(cardW, cardH);
    return true;
}

bool CardTextureCache::loadEatsDeck(const std::string& eatsFilePath, int cardW, int cardH) {
    if (eatsFilePath.find("steampunk") != std::string::npos) {
        currentTheme_ = DeckThemeId::SteampunkAi;
    } else if (eatsFilePath.find("classic") != std::string::npos) {
        currentTheme_ = DeckThemeId::ClassicSvg;
    }

    std::string binPath = eatsFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, eatsFilePath, cardW, cardH, textures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        syncArgb32AndMips(textures_);
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

    // Evaluate script metadata
    eatsbits::eatscript::Evaluator evaluator;
    evaluator.evaluateSource(scriptPart);

    std::string format = "svgz";
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

    // Extract binary payload
    std::vector<uint8_t> rawBytes;
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

    if (rawBytes.empty()) {
        std::cerr << "[SolEnshaded] No deck payload found in Eatscript deck: " << eatsFilePath << "\n";
        return false;
    }

    cardW_ = cardW;
    cardH_ = cardH;

    if (format == "svgz" || format == "svg_zlib" || format == "svg_deflate") {
        int outLen = 0;
        char* decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(rawBytes.data()),
                                               static_cast<int>(rawBytes.size()), &outLen);
        if (!decomp) {
            decomp = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(rawBytes.data()),
                                                      static_cast<int>(rawBytes.size()), &outLen);
        }
        if (!decomp || outLen <= 0) {
            std::cerr << "[SolEnshaded] Failed to decompress SVGZ payload in " << eatsFilePath << "\n";
            return false;
        }

        std::vector<char> svgXml(decomp, decomp + outLen);
        svgXml.push_back('\0');
        free(decomp);

        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEnshaded] Failed to parse decompressed SVG in " << eatsFilePath << "\n";
            return false;
        }

        isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, textures_, currentTheme_, binPath);
        return isLoaded_;
    } else if (format == "svg") {
        std::vector<char> svgXml(rawBytes.begin(), rawBytes.end());
        svgXml.push_back('\0');

        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEnshaded] Failed to parse SVG in " << eatsFilePath << "\n";
            return false;
        }

        isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, textures_, currentTheme_, binPath);
        return isLoaded_;
    } else if (format == "png") {
        int sheetW = 0, sheetH = 0, channels = 0;
        stbi_uc* sheetData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(rawBytes.data()),
                                                  static_cast<int>(rawBytes.size()),
                                                  &sheetW, &sheetH, &channels, 4);
        if (!sheetData) {
            std::cerr << "[SolEnshaded] Failed to decode PNG in " << eatsFilePath << ": " << stbi_failure_reason() << "\n";
            return false;
        }

        isLoaded_ = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, textures_, currentTheme_, binPath);
        stbi_image_free(sheetData);
        return isLoaded_;
    }

    std::cerr << "[SolEnshaded] Unknown deck format: " << format << " in " << eatsFilePath << "\n";
    return false;
}

bool CardTextureCache::loadBitmapDeck(const std::string& bitmapFilePath, int cardW, int cardH) {
    std::string binPath = bitmapFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, bitmapFilePath, cardW, cardH, textures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        syncArgb32AndMips(textures_);
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
    isLoaded_ = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, textures_, currentTheme_, binPath);
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

    if (tryLoadBinaryCache(binPath, svgFilePath, cardW, cardH, textures_)) {
        cardW_ = cardW;
        cardH_ = cardH;
        syncArgb32AndMips(textures_);
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
    isLoaded_ = rasterizeSvgToTextures(image, cardW, cardH, textures_, currentTheme_, binPath);
    return isLoaded_;
}

void CardTextureCache::bakeProceduralDeck(int cardW, int cardH) {
    cardW_ = cardW;
    cardH_ = cardH;

    for (size_t i = 0; i < kTotalTextures; ++i) {
        CardTexture& tex = textures_[i];
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

    syncArgb32AndMips(textures_);
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
