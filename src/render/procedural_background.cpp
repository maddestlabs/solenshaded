#include "solenshaded/procedural_background.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace solenshaded {

namespace {

using eatsbits::ui::Point2D;

// Fast deterministic integer hash for procedural variations
inline float hash2D(int x, int y, uint32_t seed) noexcept {
    uint32_t n = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u +
                 seed * 1442695040888963407u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<float>(n & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF);
}

// ----------------------------------------------------------------------------
// 1. Plain Weave Green Linen / Canvas (ambientCG Fabric035 style)
// ----------------------------------------------------------------------------
void renderLinenCanvas(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    float baseR, float baseG, float baseB,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    // 1. Base gradient quad with chandelier illumination
    r.drawRectGradient(x, y, w, h,
                       baseR * 0.95f, baseG * 0.95f, baseB * 0.95f,
                       baseR * 0.55f, baseG * 0.55f, baseB * 0.55f, 1.0f);

    // 2. Chandelier light pool (natural physical vignette)
    float lightRad = std::max(w, h) * 0.75f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               baseR * 1.40f, baseG * 1.40f, baseB * 1.40f, 0.42f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    eatsbits::ui::ScissorScope scissor(&r, x, y, w, h);

    // 3. Interlaced Plain Weave (1/1 over-under orthogonal yarns)
    const float yarnStep = 7.0f;
    const float halfStep = yarnStep * 0.5f;

    // Horizontal Weft Threads
    int weftIdx = 0;
    for (float sy = y + 1.0f; sy < y + h; sy += yarnStep, ++weftIdx) {
        float tY = (h > 0.0f) ? (sy - y) / h : 0.0f;
        float rY = baseR * (1.10f - tY * 0.35f);
        float gY = baseG * (1.10f - tY * 0.35f);
        float bY = baseB * (1.10f - tY * 0.35f);

        // Weft thread highlight crown and furrow shadow
        r.drawRect(x, sy, w, 2.0f, rY * 1.18f, gY * 1.18f, bY * 1.18f, 0.70f);
        r.drawRect(x, sy + 2.0f, w, 1.0f, rY * 0.65f, gY * 0.65f, bY * 0.65f, 0.65f);
    }

    // Vertical Warp Threads (Crossed Over/Under)
    int warpIdx = 0;
    for (float sx = x + 1.0f; sx < x + w; sx += yarnStep, ++warpIdx) {
        float rX = baseR * 1.05f;
        float gX = baseG * 1.05f;
        float bX = baseB * 1.05f;

        // Alternating thread intersections (over-under check pattern)
        for (float sy = y + (warpIdx % 2 == 0 ? 0.0f : halfStep); sy < y + h; sy += yarnStep) {
            // Crown bump of vertical thread crossing over
            r.drawRect(sx, sy, 2.5f, 2.5f, rX * 1.30f, gX * 1.30f, bX * 1.30f, 0.55f);
            // Shadow pocket at thread dip
            r.drawRect(sx + 2.5f, sy, 1.0f, 2.5f, rX * 0.50f, gX * 0.50f, bX * 0.50f, 0.50f);
        }
    }

    // 4. Linen Slub Knots & Micro-fibers
    r.drawDustSpecks(x, y, w, h, 60, seed, baseR * 1.45f, baseG * 1.45f, baseB * 1.45f, 0.22f);
    r.drawRectVignette(x, y, w, h, 0.0f, 0.40f, 0.0f, 0.0f, 0.0f);
}

// ----------------------------------------------------------------------------
// 2. Basketweave / Hopsack Tweed (ambientCG Fabric054 style)
// ----------------------------------------------------------------------------
void renderBasketweaveHopsack(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    // 1. Base heather neutral
    r.drawRectGradient(x, y, w, h, 0.42f, 0.42f, 0.40f, 0.22f, 0.22f, 0.20f, 1.0f);

    // 2. Chandelier light pool
    float lightRad = std::max(w, h) * 0.72f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               0.68f, 0.68f, 0.65f, 0.38f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    eatsbits::ui::ScissorScope scissor(&r, x, y, w, h);

    // 3. 2x2 Basketweave Block Pattern (alternating horizontal & vertical thread bundles)
    const float blockSize = 9.0f;
    int row = 0;
    for (float by = y; by < y + h; by += blockSize, ++row) {
        int col = 0;
        for (float bx = x; bx < x + w; bx += blockSize, ++col) {
            bool isHorizontal = ((row + col) % 2 == 0);
            if (isHorizontal) {
                // Horizontal thread bundle: off-white with center groove
                r.drawRect(bx, by, blockSize - 1.0f, blockSize * 0.45f, 0.64f, 0.64f, 0.62f, 0.85f);
                r.drawRect(bx, by + blockSize * 0.45f, blockSize - 1.0f, 1.0f, 0.24f, 0.24f, 0.22f, 0.75f);
                r.drawRect(bx, by + blockSize * 0.55f, blockSize - 1.0f, blockSize * 0.40f, 0.58f, 0.58f, 0.56f, 0.85f);
            } else {
                // Vertical thread bundle: medium heather gray with vertical groove
                r.drawRect(bx, by, blockSize * 0.45f, blockSize - 1.0f, 0.42f, 0.42f, 0.40f, 0.85f);
                r.drawRect(bx + blockSize * 0.45f, by, 1.0f, blockSize - 1.0f, 0.18f, 0.18f, 0.16f, 0.75f);
                r.drawRect(bx + blockSize * 0.55f, by, blockSize * 0.40f, blockSize - 1.0f, 0.38f, 0.38f, 0.36f, 0.85f);
            }
            // Recessed shadow seam around block
            r.drawRect(bx + blockSize - 1.0f, by, 1.0f, blockSize, 0.12f, 0.12f, 0.10f, 0.65f);
            r.drawRect(bx, by + blockSize - 1.0f, blockSize, 1.0f, 0.12f, 0.12f, 0.10f, 0.65f);
        }
    }

    // 4. Multicolor Tweed Neps / Flecks (pink, cyan, amber wool specks like Image 3!)
    r.drawDustSpecks(x, y, w, h, 28, seed + 101u, 0.88f, 0.42f, 0.52f, 0.45f); // Pink neps
    r.drawDustSpecks(x, y, w, h, 24, seed + 202u, 0.38f, 0.72f, 0.82f, 0.45f); // Cyan neps
    r.drawDustSpecks(x, y, w, h, 30, seed + 303u, 0.88f, 0.75f, 0.38f, 0.45f); // Amber neps

    r.drawRectVignette(x, y, w, h, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f);
}

// ----------------------------------------------------------------------------
// 3. Denim Chambray Cloth (ambientCG Fabric073 style)
// ----------------------------------------------------------------------------
void renderDenimChambray(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    // 1. Deep indigo base
    r.drawRectGradient(x, y, w, h, 0.14f, 0.24f, 0.38f, 0.07f, 0.12f, 0.22f, 1.0f);

    // 2. Chandelier light pool
    float lightRad = std::max(w, h) * 0.75f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               0.26f, 0.45f, 0.65f, 0.45f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    eatsbits::ui::ScissorScope scissor(&r, x, y, w, h);

    // 3. Fine horizontal weft slub streaks (white/light-blue weft showing through indigo warp)
    const float slubStep = 4.0f;
    int sIdx = 0;
    for (float sy = y; sy < y + h; sy += slubStep, ++sIdx) {
        float slubLen = 22.0f + static_cast<float>((sIdx * 37) % 65);
        float slubGap = 6.0f + static_cast<float>((sIdx * 19) % 25);
        float startX = x + static_cast<float>((sIdx * 29) % 40);

        for (float sx = startX; sx < x + w; sx += (slubLen + slubGap)) {
            r.drawRect(sx, sy, slubLen, 1.0f, 0.48f, 0.60f, 0.78f, 0.42f);
        }
    }

    // 4. Micro-twill diagonal rib texture (2/1 twill lines)
    const float twillStep = 6.0f;
    for (float d = -h; d < w + h; d += twillStep) {
        r.drawLine(x + d, y, x + d + h, y + h, 0.28f, 0.42f, 0.60f, 0.22f, 1.0f);
    }

    // 5. Cotton micro-nap
    r.drawDustSpecks(x, y, w, h, 40, seed, 0.60f, 0.72f, 0.85f, 0.16f);
    r.drawRectVignette(x, y, w, h, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f);
}

// ----------------------------------------------------------------------------
// 4. Striped Knitted Jersey (ambientCG Fabric018 style)
// ----------------------------------------------------------------------------
void renderStripedJersey(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    // 1. Dark charcoal base
    r.drawRectGradient(x, y, w, h, 0.16f, 0.17f, 0.19f, 0.09f, 0.10f, 0.11f, 1.0f);

    eatsbits::ui::ScissorScope scissor(&r, x, y, w, h);

    // 2. Alternating Horizontal Knit Stripes (32px wide bands)
    const float bandH = 32.0f;
    int bIdx = 0;
    for (float by = y; by < y + h; by += bandH, ++bIdx) {
        bool isLightBand = (bIdx % 2 == 1);
        if (isLightBand) {
            // Heather gray stripe
            r.drawRect(x, by, w, bandH, 0.42f, 0.44f, 0.46f, 0.90f);
        }

        // Fine horizontal knit rib wales (every 4px)
        for (float ry = by + 2.0f; ry < by + bandH; ry += 4.0f) {
            float ribAlpha = isLightBand ? 0.35f : 0.28f;
            float rColor = isLightBand ? 0.58f : 0.28f;
            float gColor = isLightBand ? 0.60f : 0.30f;
            float bColor = isLightBand ? 0.62f : 0.32f;
            r.drawRect(x, ry, w, 1.2f, rColor, gColor, bColor, ribAlpha);
            r.drawRect(x, ry + 1.2f, w, 1.0f, 0.05f, 0.05f, 0.06f, ribAlpha * 0.8f);
        }
    }

    // 3. Chandelier light pool
    float lightRad = std::max(w, h) * 0.72f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               1.0f, 1.0f, 1.0f, 0.25f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    r.drawDustSpecks(x, y, w, h, 35, seed, 0.70f, 0.72f, 0.75f, 0.15f);
    r.drawRectVignette(x, y, w, h, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f);
}

// ----------------------------------------------------------------------------
// 5. Sunken Sanctum (Obsidian Runes)
// ----------------------------------------------------------------------------
void renderSunkenSanctum(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    r.drawRectGradient(x, y, w, h, 0.10f, 0.14f, 0.20f, 0.05f, 0.08f, 0.13f, 1.0f);

    float lightRad = std::max(w, h) * 0.70f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               0.18f, 0.32f, 0.45f, 0.35f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    r.drawPatternedRectGradient(x, y, w, h,
                                0.10f, 0.14f, 0.20f, 0.05f, 0.08f, 0.13f, 1.0f,
                                eatsbits::ui::VectorPatternType::DiagonalMesh, 0.06f, 24.0f, 0.0f);

    r.drawDustSpecks(x, y, w, h, 45, seed, 0.36f, 0.68f, 0.78f, 0.20f);
    r.drawRectVignette(x, y, w, h, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f);
}

// ----------------------------------------------------------------------------
// 6. Perilous Shores (Ship Teak Deck)
// ----------------------------------------------------------------------------
void renderPerilousShores(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    r.drawRectGradient(x, y, w, h, 0.24f, 0.20f, 0.15f, 0.16f, 0.13f, 0.09f, 1.0f);

    float lightRad = std::max(w, h) * 0.72f;
    r.drawCircleRadialGradient(light.x, light.y, lightRad,
                               0.42f, 0.36f, 0.28f, 0.40f,
                               0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 36);

    r.drawPatternedRectGradient(x, y, w, h,
                                0.24f, 0.20f, 0.15f, 0.16f, 0.13f, 0.09f, 1.0f,
                                eatsbits::ui::VectorPatternType::HorizontalSlats, 0.10f, 14.0f, 0.0f);

    r.drawDustSpecks(x, y, w, h, 30, seed, 0.78f, 0.62f, 0.30f, 0.18f);
    r.drawRectVignette(x, y, w, h, 0.0f, 0.40f, 0.0f, 0.0f, 0.0f);
}

void renderVectorFallback(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    TableThemeId theme,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    switch (theme) {
        case TableThemeId::PastelTan:
            r.drawRect(x, y, w, h, 0.83f, 0.77f, 0.69f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.15f, 0.68f, 0.61f, 0.52f);
            break;
        case TableThemeId::EmeraldLawn:
            renderLinenCanvas(r, x, y, w, h, 0.08f, 0.28f, 0.14f, light, seed);
            break;
        case TableThemeId::HopsackTweed:
            renderBasketweaveHopsack(r, x, y, w, h, light, seed);
            break;
        case TableThemeId::DenimChambray:
            renderDenimChambray(r, x, y, w, h, light, seed);
            break;
        case TableThemeId::StripedJersey:
            renderStripedJersey(r, x, y, w, h, light, seed);
            break;
        case TableThemeId::SunkenSanctum:
            renderSunkenSanctum(r, x, y, w, h, light, seed);
            break;
        case TableThemeId::PerilousShores:
            renderPerilousShores(r, x, y, w, h, light, seed);
            break;
        default:
            renderLinenCanvas(r, x, y, w, h, 0.08f, 0.28f, 0.14f, light, seed);
            break;
    }
}

#pragma pack(push, 1)
struct BackgroundBlobHeader {
    char magic[4];          // "EBKG" (Eats Background)
    uint32_t version;       // 1
    uint32_t colorOffset;   // Byte offset from start of this header
    uint32_t colorSize;     // Byte size of Color image (JPEG/PNG)
    uint32_t normalOffset;  // Byte offset from start of this header
    uint32_t normalSize;    // Byte size of Normal image (JPEG/PNG)
    uint32_t ormOffset;     // Byte offset from start of this header
    uint32_t ormSize;       // Byte size of ORM image (JPEG/PNG)
};
#pragma pack(pop)

struct PbrMaterialDef {
    const char* materialName;
    const char* themeSlug;
    float uvScale;
    float normalStrength;
    float specularIntensity;
    float ambientLevel;
    float diffuseLevel;
    float tintR;
    float tintG;
    float tintB;
};

struct PbrTextureData {
    int width{0};
    int height{0};
    std::vector<uint8_t> color;     // RGB
    std::vector<uint8_t> normal;    // RGB
    std::vector<uint8_t> orm;       // RGB (Channel packed: R=AO, G=Roughness, B=Displacement/Metallic)
    std::vector<uint8_t> roughness; // 1-ch (fallback if loose textures loaded)
    std::vector<uint8_t> ao;        // 1-ch (fallback if loose textures loaded)
    bool loaded{false};
    bool loadAttempted{false};
};

std::string resolveThemePath(const std::string& filename) {
    const std::string candidates[] = {
        "themes/" + filename,
        "/themes/" + filename,
        "../themes/" + filename,
        "../../themes/" + filename,
        "assets/themes/" + filename,
        "../assets/themes/" + filename,
        filename
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return "";
}

std::string resolveTexturePath(const std::string& filename) {
    const std::string candidates[] = {
        "textures/" + filename,
        "/textures/" + filename,
        "../textures/" + filename,
        "../../textures/" + filename,
        filename
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return "";
}

const PbrMaterialDef* getMaterialDefForTheme(TableThemeId theme) {
    switch (theme) {
        case TableThemeId::PastelTan:
            return nullptr;
        case TableThemeId::EmeraldLawn: {
            static const PbrMaterialDef d{
                "Fabric037", "emerald_lawn", 2.8f, 1.25f, 0.18f, 0.26f, 0.85f, 0.50f, 1.15f, 0.65f
            };
            return &d;
        }
        case TableThemeId::HopsackTweed: {
            static const PbrMaterialDef d{
                "Fabric063", "hopsack_tweed", 2.5f, 1.50f, 0.15f, 0.24f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::DenimChambray: {
            static const PbrMaterialDef d{
                "Fabric077", "denim_chambray", 3.0f, 1.25f, 0.18f, 0.22f, 0.85f, 0.65f, 0.80f, 1.15f
            };
            return &d;
        }
        case TableThemeId::StripedJersey: {
            static const PbrMaterialDef d{
                "Fabric079", "striped_jersey", 2.6f, 1.40f, 0.20f, 0.24f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::SunkenSanctum: {
            static const PbrMaterialDef d{
                "Onyx013", "sunken_sanctum", 1.7f, 1.15f, 0.85f, 0.16f, 0.84f, 0.75f, 0.88f, 1.05f
            };
            return &d;
        }
        case TableThemeId::PerilousShores: {
            static const PbrMaterialDef d{
                "Ground091", "perilous_shores", 2.0f, 1.50f, 0.20f, 0.22f, 0.88f, 1.0f, 0.98f, 0.92f
            };
            return &d;
        }
        case TableThemeId::PebbledCowhide: {
            static const PbrMaterialDef d{
                "LeatherPebbled", "pebbled_cowhide", 2.0f, 1.60f, 0.35f, 0.20f, 0.88f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::LeatherNubuck: {
            static const PbrMaterialDef d{
                "Leather006", "leather006", 2.0f, 1.40f, 0.25f, 0.22f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PaintedWood: {
            static const PbrMaterialDef d{
                "PaintedWood008C", "paintedwood008c", 2.2f, 1.50f, 0.30f, 0.22f, 0.88f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PixelDungeon: {
            static const PbrMaterialDef d{
                "PixelDungeon", "pixel_dungeon_brick", 1.5f, 1.80f, 0.20f, 0.25f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PixelWater: {
            static const PbrMaterialDef d{
                "PixelWater", "pixel_emerald_water", 1.5f, 1.20f, 0.70f, 0.25f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        default:
            return nullptr;
    }
}

bool loadMaterialFromBytes(const uint8_t* blobPtr, size_t blobSize, PbrTextureData& outTex, [[maybe_unused]] PbrMaterialDef& ioDef) {
    if (!blobPtr || blobSize == 0) return false;

    // 1. Check for EBKG container (Channel-packed PBR)
    if (blobSize >= sizeof(BackgroundBlobHeader) &&
        std::memcmp(blobPtr, "EBKG", 4) == 0) {
        const auto* header = reinterpret_cast<const BackgroundBlobHeader*>(blobPtr);
        if (header->version != 1) return false;

        if (static_cast<size_t>(header->colorOffset) + header->colorSize > blobSize ||
            static_cast<size_t>(header->normalOffset) + header->normalSize > blobSize ||
            static_cast<size_t>(header->ormOffset) + header->ormSize > blobSize) {
            return false;
        }

        int w = 0, h = 0, c = 0;
        stbi_uc* colorData = stbi_load_from_memory(blobPtr + header->colorOffset, static_cast<int>(header->colorSize), &w, &h, &c, 3);
        if (!colorData || w <= 0 || h <= 0) {
            if (colorData) stbi_image_free(colorData);
            return false;
        }
        outTex.width = w;
        outTex.height = h;
        outTex.color.assign(colorData, colorData + static_cast<size_t>(w * h * 3));
        stbi_image_free(colorData);

        int nw = 0, nh = 0;
        stbi_uc* normalData = stbi_load_from_memory(blobPtr + header->normalOffset, static_cast<int>(header->normalSize), &nw, &nh, &c, 3);
        if (!normalData || nw != w || nh != h) {
            if (normalData) stbi_image_free(normalData);
            outTex.color.clear();
            return false;
        }
        outTex.normal.assign(normalData, normalData + static_cast<size_t>(w * h * 3));
        stbi_image_free(normalData);

        int ow = 0, oh = 0;
        stbi_uc* ormData = stbi_load_from_memory(blobPtr + header->ormOffset, static_cast<int>(header->ormSize), &ow, &oh, &c, 3);
        if (!ormData || ow != w || oh != h) {
            if (ormData) stbi_image_free(ormData);
            outTex.color.clear();
            outTex.normal.clear();
            return false;
        }
        outTex.orm.assign(ormData, ormData + static_cast<size_t>(w * h * 3));
        stbi_image_free(ormData);

        outTex.loaded = true;
        return true;
    }

    // 2. Generic image fallback (PNG, JPEG, WebP, etc.)
    int w = 0, h = 0, c = 0;
    stbi_uc* imgData = stbi_load_from_memory(blobPtr, static_cast<int>(blobSize), &w, &h, &c, 3);
    if (imgData && w > 0 && h > 0) {
        outTex.width = w;
        outTex.height = h;
        outTex.color.assign(imgData, imgData + static_cast<size_t>(w * h * 3));
        stbi_image_free(imgData);

        // Synthesize flat tangent normal (128, 128, 255)
        outTex.normal.resize(static_cast<size_t>(w * h * 3));
        for (size_t i = 0; i < static_cast<size_t>(w * h); ++i) {
            outTex.normal[i * 3 + 0] = 128;
            outTex.normal[i * 3 + 1] = 128;
            outTex.normal[i * 3 + 2] = 255;
        }

        // Synthesize neutral ORM (R=AO=255, G=Roughness=140, B=Metallic=0)
        outTex.orm.resize(static_cast<size_t>(w * h * 3));
        for (size_t i = 0; i < static_cast<size_t>(w * h); ++i) {
            outTex.orm[i * 3 + 0] = 255;
            outTex.orm[i * 3 + 1] = 140;
            outTex.orm[i * 3 + 2] = 0;
        }

        outTex.loaded = true;
        return true;
    }

    return false;
}

bool loadMaterialFromEats(const std::string& filePath, PbrTextureData& outTex, PbrMaterialDef& ioDef) {
    if (outTex.loaded) return true;
    outTex.loadAttempted = true;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Check if this package or script contains procedural Eatscript synthesis
    if (isProceduralScript(content)) {
        ProceduralPbrMaps maps;
        ProceduralBakeOptions opts;
        opts.width = 512;
        opts.height = 512;
        opts.overrideDimensions = true;
        if (bakeProceduralPbrBundle(content, maps, opts)) {
            outTex.width = maps.width;
            outTex.height = maps.height;
            outTex.color = std::move(maps.colorRgb);
            outTex.normal = std::move(maps.normalRgb);
            outTex.orm = std::move(maps.orm);
            outTex.loaded = true;
            std::cout << "[SolEnshaded] Successfully synthesized procedural theme script: " << filePath
                      << " (" << outTex.width << "x" << outTex.height << ")\n";
            return true;
        }
    }

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

    auto setBgFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
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

        auto getOptFloat = [&](const std::string& key, float defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
            return defVal;
        };

        ioDef.uvScale = getOptFloat("uv_scale", ioDef.uvScale);
        ioDef.normalStrength = getOptFloat("normal_strength", ioDef.normalStrength);
        ioDef.specularIntensity = getOptFloat("specular_intensity", ioDef.specularIntensity);
        ioDef.ambientLevel = getOptFloat("ambient_level", ioDef.ambientLevel);
        ioDef.diffuseLevel = getOptFloat("diffuse_level", ioDef.diffuseLevel);
        ioDef.tintR = getOptFloat("tint_r", ioDef.tintR);
        ioDef.tintG = getOptFloat("tint_g", ioDef.tintG);
        ioDef.tintB = getOptFloat("tint_b", ioDef.tintB);

        // Check array tint: [r, g, b]
        if (optsDict && optsDict->hasKey("tint")) {
            auto tintVal = optsDict->get("tint");
            if (tintVal.isList() && tintVal.listVal.size() >= 3) {
                ioDef.tintR = tintVal.listVal[0].asFloat(ioDef.tintR);
                ioDef.tintG = tintVal.listVal[1].asFloat(ioDef.tintG);
                ioDef.tintB = tintVal.listVal[2].asFloat(ioDef.tintB);
            }
        }

        return eatsbits::eatscript::Value(true);
    };

    auto themeDummyFn = [](const auto&, const auto&) { return eatsbits::eatscript::Value(true); };
    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "create_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_background", setBgFn);
    evaluator.registerFunction("create_theme", themeDummyFn);
    evaluator.registerFunction("set_theme", themeDummyFn);
    evaluator.registerFunction("set_background", setBgFn);

    evaluator.evaluateSource(scriptPart);

    std::vector<uint8_t> rawBytes;
    if (apiCalled && !apiBlob.empty()) {
        rawBytes = std::move(apiBlob);
    } else {
        if (evaluator.hasVariable("uv_scale")) {
            ioDef.uvScale = evaluator.getVariable("uv_scale").asFloat(ioDef.uvScale);
        }
        if (evaluator.hasVariable("normal_strength")) {
            ioDef.normalStrength = evaluator.getVariable("normal_strength").asFloat(ioDef.normalStrength);
        }
        if (evaluator.hasVariable("specular_intensity")) {
            ioDef.specularIntensity = evaluator.getVariable("specular_intensity").asFloat(ioDef.specularIntensity);
        }
        if (evaluator.hasVariable("ambient_level")) {
            ioDef.ambientLevel = evaluator.getVariable("ambient_level").asFloat(ioDef.ambientLevel);
        }
        if (evaluator.hasVariable("diffuse_level")) {
            ioDef.diffuseLevel = evaluator.getVariable("diffuse_level").asFloat(ioDef.diffuseLevel);
        }
        if (evaluator.hasVariable("tint_r")) {
            ioDef.tintR = evaluator.getVariable("tint_r").asFloat(ioDef.tintR);
        }
        if (evaluator.hasVariable("tint_g")) {
            ioDef.tintG = evaluator.getVariable("tint_g").asFloat(ioDef.tintG);
        }
        if (evaluator.hasVariable("tint_b")) {
            ioDef.tintB = evaluator.getVariable("tint_b").asFloat(ioDef.tintB);
        }

        if (!blobPart.empty()) {
            rawBytes.assign(blobPart.begin(), blobPart.end());
        } else {
            std::string blobStr;
            if (evaluator.hasVariable("bg_blob")) {
                blobStr = evaluator.getVariable("bg_blob").asString();
            } else if (evaluator.hasVariable("background_blob")) {
                blobStr = evaluator.getVariable("background_blob").asString();
            } else if (evaluator.hasVariable("blob")) {
                blobStr = evaluator.getVariable("blob").asString();
            }
            if (!blobStr.empty()) {
                rawBytes = eatsbits::ui::base64Decode(blobStr);
            }
        }
    }

    if (rawBytes.empty()) {
        std::cerr << "[SolEnshaded] No background payload found in Eatscript theme: " << filePath << "\n";
        return false;
    }

    if (!loadMaterialFromBytes(rawBytes.data(), rawBytes.size(), outTex, ioDef)) {
        std::cerr << "[SolEnshaded] Failed to decode background material in: " << filePath << "\n";
        return false;
    }

    std::cout << "[SolEnshaded] Successfully loaded single-file .eats theme: " << filePath
              << " (" << outTex.width << "x" << outTex.height << ", channel-packed ORM)\n";
    return true;
}

bool loadMaterialFromDisk(const std::string& materialName, PbrTextureData& outTex) {
    if (outTex.loaded) return true;
    outTex.loadAttempted = true;

    std::string colorFile = materialName + "_1K-JPG_Color.jpg";
    std::string normalFile = materialName + "_1K-JPG_NormalGL.jpg";
    std::string roughFile = materialName + "_1K-JPG_Roughness.jpg";
    std::string aoFile = materialName + "_1K-JPG_AmbientOcclusion.jpg";
    std::string dispFile = materialName + "_1K-JPG_Displacement.jpg";

    std::string colorPath = resolveTexturePath(colorFile);
    std::string normalPath = resolveTexturePath(normalFile);
    if (normalPath.empty()) {
        normalPath = resolveTexturePath(materialName + "_1K-JPG_NormalDX.jpg");
    }
    std::string roughPath = resolveTexturePath(roughFile);
    std::string aoPath = resolveTexturePath(aoFile);
    if (aoPath.empty()) {
        aoPath = resolveTexturePath(dispFile);
    }

    if (colorPath.empty() || normalPath.empty() || roughPath.empty()) {
        return false;
    }

    int w = 0, h = 0, c = 0;
    stbi_uc* colorData = stbi_load(colorPath.c_str(), &w, &h, &c, 3);
    if (!colorData || w <= 0 || h <= 0) {
        if (colorData) stbi_image_free(colorData);
        return false;
    }
    outTex.width = w;
    outTex.height = h;
    outTex.color.assign(colorData, colorData + static_cast<size_t>(w * h * 3));
    stbi_image_free(colorData);

    int nw = 0, nh = 0;
    stbi_uc* normalData = stbi_load(normalPath.c_str(), &nw, &nh, &c, 3);
    if (!normalData || nw != w || nh != h) {
        if (normalData) stbi_image_free(normalData);
        outTex.color.clear();
        return false;
    }
    outTex.normal.assign(normalData, normalData + static_cast<size_t>(w * h * 3));
    stbi_image_free(normalData);

    int rw = 0, rh = 0;
    stbi_uc* roughData = stbi_load(roughPath.c_str(), &rw, &rh, &c, 1);
    if (!roughData || rw != w || rh != h) {
        if (roughData) stbi_image_free(roughData);
        outTex.color.clear();
        outTex.normal.clear();
        return false;
    }
    outTex.roughness.assign(roughData, roughData + static_cast<size_t>(w * h));
    stbi_image_free(roughData);

    if (!aoPath.empty()) {
        int aow = 0, aoh = 0;
        stbi_uc* aoData = stbi_load(aoPath.c_str(), &aow, &aoh, &c, 1);
        if (aoData && aow == w && aoh == h) {
            outTex.ao.assign(aoData, aoData + static_cast<size_t>(w * h));
        }
        if (aoData) stbi_image_free(aoData);
    }

    // Populate channel-packed ORM in memory from loose components
    outTex.orm.resize(static_cast<size_t>(w * h * 3));
    for (size_t i = 0; i < static_cast<size_t>(w * h); ++i) {
        uint8_t aoVal = outTex.ao.empty() ? 255 : outTex.ao[i];
        uint8_t roughVal = outTex.roughness[i];
        outTex.orm[i * 3 + 0] = aoVal;
        outTex.orm[i * 3 + 1] = roughVal;
        outTex.orm[i * 3 + 2] = 0;
    }

    outTex.loaded = true;
    std::cout << "[SolEnshaded] Successfully loaded AmbientCG PBR Texture Set: " << materialName
              << " (" << w << "x" << h << ")\n";
    return true;
}

bool bakePbrSurface(
    const PbrTextureData& tex,
    const PbrMaterialDef& def,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    std::vector<uint8_t>& outRgba,
    int& outW, int& outH
) {
    int bakeW = static_cast<int>(std::ceil(w));
    int bakeH = static_cast<int>(std::ceil(h));
    if (bakeW <= 0 || bakeH <= 0 || !tex.loaded) return false;

    outW = bakeW;
    outH = bakeH;
    outRgba.resize(static_cast<size_t>(bakeW) * bakeH * 4);

    const int texW = tex.width;
    const int texH = tex.height;
    const float uvScale = def.uvScale;
    const float normStrength = def.normalStrength;
    const float specIntensity = def.specularIntensity;
    const float amb = def.ambientLevel;
    const float diffLev = def.diffuseLevel;
    const float tintR = def.tintR;
    const float tintG = def.tintG;
    const float tintB = def.tintB;

    const float lightRefR = std::max(w, h) * 0.72f;
    const float invRefR = (lightRefR > 0.0f) ? (1.0f / lightRefR) : 1.0f;
    const float lightZ = std::max(100.0f, light.z);
    const float lightIntens = light.intensity;

    // Chandelier warm light tint
    const float lightColR = 1.04f;
    const float lightColG = 1.00f;
    const float lightColB = 0.94f;

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int py = 0; py < bakeH; ++py) {
        float worldY = y + static_cast<float>(py);
        float dy = light.y - worldY;

        float vNorm = (worldY * uvScale) / static_cast<float>(texH);
        float vWrap = vNorm - std::floor(vNorm);
        float fy = vWrap * static_cast<float>(texH) - 0.5f;
        int y0 = static_cast<int>(std::floor(fy));
        float wy = fy - static_cast<float>(y0);
        int y1 = y0 + 1;
        y0 = (y0 % texH + texH) % texH;
        y1 = (y1 % texH + texH) % texH;

        uint8_t* dstRow = &outRgba[static_cast<size_t>(py * bakeW) * 4];

        for (int px = 0; px < bakeW; ++px) {
            float worldX = x + static_cast<float>(px);
            float dx = light.x - worldX;

            float uNorm = (worldX * uvScale) / static_cast<float>(texW);
            float uWrap = uNorm - std::floor(uNorm);
            float fx = uWrap * static_cast<float>(texW) - 0.5f;
            int x0 = static_cast<int>(std::floor(fx));
            float wx = fx - static_cast<float>(x0);
            int x1 = x0 + 1;
            x0 = (x0 % texW + texW) % texW;
            x1 = (x1 % texW + texW) % texW;

            float w00 = (1.0f - wx) * (1.0f - wy);
            float w10 = wx * (1.0f - wy);
            float w01 = (1.0f - wx) * wy;
            float w11 = wx * wy;

            // 1. Bilinear Sample Color (RGB)
            size_t idx00 = static_cast<size_t>(y0 * texW + x0);
            size_t idx10 = static_cast<size_t>(y0 * texW + x1);
            size_t idx01 = static_cast<size_t>(y1 * texW + x0);
            size_t idx11 = static_cast<size_t>(y1 * texW + x1);

            float colR = (tex.color[idx00 * 3 + 0] * w00 +
                          tex.color[idx10 * 3 + 0] * w10 +
                          tex.color[idx01 * 3 + 0] * w01 +
                          tex.color[idx11 * 3 + 0] * w11) * (1.0f / 255.0f) * tintR;
            float colG = (tex.color[idx00 * 3 + 1] * w00 +
                          tex.color[idx10 * 3 + 1] * w10 +
                          tex.color[idx01 * 3 + 1] * w01 +
                          tex.color[idx11 * 3 + 1] * w11) * (1.0f / 255.0f) * tintG;
            float colB = (tex.color[idx00 * 3 + 2] * w00 +
                          tex.color[idx10 * 3 + 2] * w10 +
                          tex.color[idx01 * 3 + 2] * w01 +
                          tex.color[idx11 * 3 + 2] * w11) * (1.0f / 255.0f) * tintB;

            // 2. Bilinear Sample Normal (OpenGL convention: R=X, G=Y, B=Z)
            float nr = (tex.normal[idx00 * 3 + 0] * w00 +
                        tex.normal[idx10 * 3 + 0] * w10 +
                        tex.normal[idx01 * 3 + 0] * w01 +
                        tex.normal[idx11 * 3 + 0] * w11) * (1.0f / 255.0f);
            float ng = (tex.normal[idx00 * 3 + 1] * w00 +
                        tex.normal[idx10 * 3 + 1] * w10 +
                        tex.normal[idx01 * 3 + 1] * w01 +
                        tex.normal[idx11 * 3 + 1] * w11) * (1.0f / 255.0f);
            float nb = (tex.normal[idx00 * 3 + 2] * w00 +
                        tex.normal[idx10 * 3 + 2] * w10 +
                        tex.normal[idx01 * 3 + 2] * w01 +
                        tex.normal[idx11 * 3 + 2] * w11) * (1.0f / 255.0f);

            float nx = (nr * 2.0f - 1.0f) * normStrength;
            float ny = -(ng * 2.0f - 1.0f) * normStrength; // Invert GL green for screen Y down
            float nz = (nb * 2.0f - 1.0f);
            float invLenN = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz + 1e-6f);
            nx *= invLenN;
            ny *= invLenN;
            nz *= invLenN;

            // 3. Bilinear Sample Roughness and AO (from channel-packed ORM or discrete buffers)
            float rough = 0.5f;
            float aoVal = 1.0f;

            if (!tex.orm.empty()) {
                // Channel 0: Ambient Occlusion
                aoVal = (tex.orm[idx00 * 3 + 0] * w00 +
                         tex.orm[idx10 * 3 + 0] * w10 +
                         tex.orm[idx01 * 3 + 0] * w01 +
                         tex.orm[idx11 * 3 + 0] * w11) * (1.0f / 255.0f);

                // Channel 1: Roughness
                rough = (tex.orm[idx00 * 3 + 1] * w00 +
                         tex.orm[idx10 * 3 + 1] * w10 +
                         tex.orm[idx01 * 3 + 1] * w01 +
                         tex.orm[idx11 * 3 + 1] * w11) * (1.0f / 255.0f);
            } else {
                if (!tex.roughness.empty()) {
                    rough = (tex.roughness[idx00] * w00 +
                             tex.roughness[idx10] * w10 +
                             tex.roughness[idx01] * w01 +
                             tex.roughness[idx11] * w11) * (1.0f / 255.0f);
                }
                if (!tex.ao.empty()) {
                    aoVal = (tex.ao[idx00] * w00 +
                             tex.ao[idx10] * w10 +
                             tex.ao[idx01] * w01 +
                             tex.ao[idx11] * w11) * (1.0f / 255.0f);
                }
            }

            // 5. 3D Light Vector
            float distSq = dx * dx + dy * dy + lightZ * lightZ;
            float dist = std::sqrt(distSq);
            float invDist = 1.0f / std::max(dist, 1.0f);
            float lDirX = dx * invDist;
            float lDirY = dy * invDist;
            float lDirZ = lightZ * invDist;

            // Radial inverse-square falloff (natural vignette)
            float r2D = std::sqrt(dx * dx + dy * dy);
            float rRel = r2D * invRefR;
            float atten = (1.0f / (1.0f + 1.18f * rRel * rRel)) * lightIntens;

            // Diffuse: wrapped Lambertian
            float nDotL = nx * lDirX + ny * lDirY + nz * lDirZ;
            float diff = 0.20f + 0.80f * std::max(0.0f, nDotL);

            // Specular: Blinn-Phong with view vector V = (0, 0, 1)
            float hx = lDirX;
            float hy = lDirY;
            float hz = lDirZ + 1.0f;
            float invH = 1.0f / std::sqrt(hx * hx + hy * hy + hz * hz + 1e-6f);
            hx *= invH;
            hy *= invH;
            hz *= invH;
            float nDotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);
            float gloss = 1.0f - rough;
            float specPower = 4.0f + gloss * gloss * 128.0f;
            float spec = std::pow(nDotH, specPower) * (gloss * gloss) * specIntensity * atten;

            // Composite final lit color
            float diffuseFactor = diff * atten;
            float litR = colR * aoVal * (amb + diffLev * diffuseFactor) + spec * lightColR;
            float litG = colG * aoVal * (amb + diffLev * diffuseFactor) + spec * lightColG;
            float litB = colB * aoVal * (amb + diffLev * diffuseFactor) + spec * lightColB;

            size_t pOffset = static_cast<size_t>(px) * 4;
            dstRow[pOffset + 0] = static_cast<uint8_t>(std::clamp(litR * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 1] = static_cast<uint8_t>(std::clamp(litG * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 2] = static_cast<uint8_t>(std::clamp(litB * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 3] = 255;
        }
    }
    return true;
}

} // namespace

struct ProceduralBackgroundSystem::Impl {
    bool pbrEnabled{true};
    std::unordered_map<TableThemeId, PbrTextureData> materials;
    std::unordered_map<TableThemeId, PbrMaterialDef> materialDefs;

    bool customThemeLoaded{false};
    PbrTextureData customTexture;
    PbrMaterialDef customDef;

    TableThemeId cachedTheme{static_cast<TableThemeId>(-1)};
    bool cachedCustom{false};
    int cachedW{0};
    int cachedH{0};
    float cachedLightX{-9999.0f};
    float cachedLightY{-9999.0f};
    std::vector<uint8_t> cachedBakedRgba;

    void invalidate() noexcept {
        cachedTheme = static_cast<TableThemeId>(-1);
        cachedCustom = false;
        cachedW = 0;
        cachedH = 0;
        cachedLightX = -9999.0f;
        cachedLightY = -9999.0f;
        cachedBakedRgba.clear();
    }

    bool ensureMaterial(TableThemeId theme, const PbrMaterialDef& defaultDef) {
        auto& tex = materials[theme];
        if (tex.loaded) return true;
        if (tex.loadAttempted) return false;

        // 1. Priority 1: Check procedural .eat script (Zero heavy PNGs, CPU SIMD synthesis)
        std::string procPath = resolveProceduralScriptPath(defaultDef.themeSlug);
        if (procPath.empty()) {
            procPath = resolveProceduralScriptPath(defaultDef.materialName);
        }
        if (!procPath.empty()) {
            ProceduralPbrMaps maps;
            ProceduralBakeOptions opts;
            opts.width = 512;
            opts.height = 512;
            opts.overrideDimensions = true;
            if (bakeProceduralPbrBundle(procPath, maps, opts)) {
                tex.width = maps.width;
                tex.height = maps.height;
                tex.color = std::move(maps.colorRgb);
                tex.normal = std::move(maps.normalRgb);
                tex.orm = std::move(maps.orm);
                tex.loaded = true;
                materialDefs[theme] = defaultDef;
                std::cout << "[SolEnshaded] Synthesized procedural PBR theme: " << defaultDef.themeSlug
                          << " from " << procPath << " (" << tex.width << "x" << tex.height << ")\n";
                return true;
            }
        }

        // 2. Priority 2: Try single-file .eats theme package
        std::string themeFile = std::string(defaultDef.themeSlug) + ".eats";
        std::string themePath = resolveThemePath(themeFile);
        if (themePath.empty()) {
            themePath = resolveThemePath(std::string(defaultDef.materialName) + ".eats");
        }

        if (!themePath.empty()) {
            PbrMaterialDef activeDef = defaultDef;
            if (loadMaterialFromEats(themePath, tex, activeDef)) {
                materialDefs[theme] = activeDef;
                return true;
            }
        }

        // 2. Fall back to loose textures in /textures/
        materialDefs[theme] = defaultDef;
        return loadMaterialFromDisk(defaultDef.materialName, tex);
    }
};

ProceduralBackgroundSystem::ProceduralBackgroundSystem()
    : impl_(std::make_unique<Impl>()) {}

ProceduralBackgroundSystem::~ProceduralBackgroundSystem() = default;

ProceduralBackgroundSystem& ProceduralBackgroundSystem::instance() {
    static ProceduralBackgroundSystem s_instance;
    return s_instance;
}

void ProceduralBackgroundSystem::invalidateCache() noexcept {
    if (impl_) {
        impl_->invalidate();
    }
}

bool ProceduralBackgroundSystem::prebakeTheme(
    TableThemeId theme,
    float w, float h,
    const eatsbits::ui::LightSource2D& light,
    const std::function<bool(float progress, const std::string& status)>& onProgress
) {
    if (!impl_) return false;

    if (onProgress && !onProgress(0.10f, "Analyzing theme material definition...")) {
        return false;
    }

    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) {
        if (onProgress) onProgress(1.0f, "Vector table surface ready.");
        return true;
    }

    if (onProgress && !onProgress(0.30f, "Synthesizing procedural PBR normal & color maps...")) {
        return false;
    }

    if (!impl_->ensureMaterial(theme, *def)) {
        if (onProgress) onProgress(1.0f, "Procedural surface fallback ready.");
        return true;
    }

    if (onProgress && !onProgress(0.60f, "Baking Blinn-Phong specular & normal-mapped relief...")) {
        return false;
    }

    const auto& tex = impl_->materials[theme];
    const auto& activeDef = impl_->materialDefs[theme];

    if (bakePbrSurface(tex, activeDef, 0.0f, 0.0f, w, h, light, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
        impl_->cachedCustom = false;
        impl_->cachedTheme = theme;
        impl_->cachedLightX = light.x;
        impl_->cachedLightY = light.y;
    }

    if (onProgress && !onProgress(1.0f, "Table surface synthesis complete.")) {
        return false;
    }
    return true;
}

void ProceduralBackgroundSystem::setPbrTexturesEnabled(bool enable) noexcept {
    if (impl_) {
        impl_->pbrEnabled = enable;
        impl_->invalidate();
    }
}

bool ProceduralBackgroundSystem::isPbrTexturesEnabled() const noexcept {
    return impl_ ? impl_->pbrEnabled : false;
}

bool ProceduralBackgroundSystem::hasProceduralMaterial(TableThemeId theme) const noexcept {
    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) return false;
    if (!resolveProceduralScriptPath(def->themeSlug).empty()) return true;
    if (!resolveProceduralScriptPath(def->materialName).empty()) return true;
    return false;
}

bool ProceduralBackgroundSystem::hasPbrMaterial(TableThemeId theme) const noexcept {
    if (impl_ && impl_->customThemeLoaded) return true;
    if (hasProceduralMaterial(theme)) return true;
    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) return false;
    std::string themeFile = std::string(def->themeSlug) + ".eats";
    if (!resolveThemePath(themeFile).empty()) return true;
    if (!resolveThemePath(std::string(def->materialName) + ".eats").empty()) return true;
    std::string colorFile = std::string(def->materialName) + "_1K-JPG_Color.jpg";
    return !resolveTexturePath(colorFile).empty();
}

bool ProceduralBackgroundSystem::loadCustomThemeFromMemory(
    const uint8_t* blobData,
    size_t blobSize,
    float uvScale,
    float normalStrength,
    float specularIntensity,
    float ambientLevel,
    float diffuseLevel,
    float tintR,
    float tintG,
    float tintB
) {
    if (!impl_ || !blobData || blobSize == 0) return false;

    PbrMaterialDef customDef{
        "CustomMaterial", "custom",
        uvScale, normalStrength, specularIntensity,
        ambientLevel, diffuseLevel,
        tintR, tintG, tintB
    };
    PbrTextureData customTex;
    if (loadMaterialFromBytes(blobData, blobSize, customTex, customDef)) {
        impl_->customThemeLoaded = true;
        impl_->customTexture = std::move(customTex);
        impl_->customDef = customDef;
        impl_->invalidate();
        return true;
    }
    return false;
}

bool ProceduralBackgroundSystem::loadProceduralTheme(
    const std::string& scriptPathOrSource,
    const ProceduralBakeOptions& options
) {
    if (!impl_) return false;

    ProceduralPbrMaps maps;
    if (!bakeProceduralPbrBundle(scriptPathOrSource, maps, options)) {
        std::cerr << "[SolEnshaded] Failed to synthesize procedural theme: " << maps.errorMessage << "\n";
        return false;
    }

    PbrTextureData customTex;
    customTex.width = maps.width;
    customTex.height = maps.height;
    customTex.color = std::move(maps.colorRgb);
    customTex.normal = std::move(maps.normalRgb);
    customTex.orm = std::move(maps.orm);
    customTex.loaded = true;

    PbrMaterialDef customDef{
        "ProceduralTheme", "procedural",
        2.0f, 1.35f, 0.65f, 0.22f, 0.88f,
        1.0f, 1.0f, 1.0f
    };

    impl_->customThemeLoaded = true;
    impl_->customTexture = std::move(customTex);
    impl_->customDef = customDef;
    impl_->invalidate();
    std::cout << "[SolEnshaded] Successfully loaded procedural theme ("
              << customTex.width << "x" << customTex.height << ")\n";
    return true;
}

bool ProceduralBackgroundSystem::loadCustomEatsTheme(const std::string& eatsFilePath) {
    if (!impl_) return false;
    std::string resolved = resolveProceduralScriptPath(eatsFilePath);
    if (resolved.empty()) resolved = resolveThemePath(eatsFilePath);
    if (resolved.empty()) resolved = eatsFilePath;

    if (isProceduralScript(resolved)) {
        return loadProceduralTheme(resolved);
    }

    PbrMaterialDef customDef{
        "CustomMaterial", "custom", 2.0f, 1.35f, 0.65f, 0.22f, 0.88f, 1.0f, 1.0f, 1.0f
    };
    PbrTextureData customTex;
    if (loadMaterialFromEats(resolved, customTex, customDef)) {
        impl_->customThemeLoaded = true;
        impl_->customTexture = std::move(customTex);
        impl_->customDef = customDef;
        impl_->invalidate();
        return true;
    }
    return false;
}

void ProceduralBackgroundSystem::clearCustomEatsTheme() noexcept {
    if (impl_) {
        impl_->customThemeLoaded = false;
        impl_->customTexture = PbrTextureData{};
        impl_->invalidate();
    }
}

bool ProceduralBackgroundSystem::hasCustomEatsTheme() const noexcept {
    return impl_ ? impl_->customThemeLoaded : false;
}

void ProceduralBackgroundSystem::renderBackground(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    TableThemeId theme,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    if (w <= 0.0f || h <= 0.0f) return;

    if (theme == TableThemeId::PastelTan) {
        // Pure solid pastel tan background: zero baking, instant clear & draw
        r.drawRect(x, y, w, h, 0.83f, 0.77f, 0.69f, 1.0f);
        r.drawRectVignette(x, y, w, h, 0.0f, 0.15f, 0.68f, 0.61f, 0.52f);
        return;
    }

    if (impl_ && impl_->pbrEnabled) {
        // 1. Check if custom user-loaded .eats theme is active
        if (impl_->customThemeLoaded && impl_->customTexture.loaded) {
            int bakeW = static_cast<int>(std::ceil(w));
            int bakeH = static_cast<int>(std::ceil(h));

            if (!impl_->cachedCustom ||
                impl_->cachedW != bakeW ||
                impl_->cachedH != bakeH ||
                std::abs(impl_->cachedLightX - light.x) > 1.0f ||
                std::abs(impl_->cachedLightY - light.y) > 1.0f ||
                impl_->cachedBakedRgba.empty()) {
                if (bakePbrSurface(impl_->customTexture, impl_->customDef, x, y, w, h, light, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                    impl_->cachedCustom = true;
                    impl_->cachedTheme = static_cast<TableThemeId>(-1);
                    impl_->cachedLightX = light.x;
                    impl_->cachedLightY = light.y;
                }
            }

            if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                return;
            }
        }

        // 2. Built-in theme: single-file .eats package or loose textures
        const PbrMaterialDef* def = getMaterialDefForTheme(theme);
        if (def && impl_->ensureMaterial(theme, *def)) {
            const auto& tex = impl_->materials[theme];
            const auto& activeDef = impl_->materialDefs[theme];
            int bakeW = static_cast<int>(std::ceil(w));
            int bakeH = static_cast<int>(std::ceil(h));

            if (impl_->cachedCustom ||
                impl_->cachedTheme != theme ||
                impl_->cachedW != bakeW ||
                impl_->cachedH != bakeH ||
                std::abs(impl_->cachedLightX - light.x) > 1.0f ||
                std::abs(impl_->cachedLightY - light.y) > 1.0f ||
                impl_->cachedBakedRgba.empty()) {
                if (bakePbrSurface(tex, activeDef, x, y, w, h, light, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                    impl_->cachedCustom = false;
                    impl_->cachedTheme = theme;
                    impl_->cachedLightX = light.x;
                    impl_->cachedLightY = light.y;
                }
            }

            if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                return;
            }
        }
    }

    renderVectorFallback(r, x, y, w, h, theme, light, seed);
}

} // namespace solenshaded
