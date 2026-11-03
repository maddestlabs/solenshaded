#ifndef SOLENSHADED_PROCEDURAL_BACKGROUND_HPP
#define SOLENSHADED_PROCEDURAL_BACKGROUND_HPP

#include "solenshaded/solitaire_types.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/geometry.hpp"
#include <cstdint>
#include <memory>

namespace solenshaded {

/**
 * High-performance procedural and AmbientCG PBR table surface generator with
 * physically informed 3D chandelier lighting, normal-mapped surface relief,
 * Blinn-Phong specular sheen, and radial distance falloff.
 * 
 * Supports seamless normal-mapped PBR materials:
 * - Rosewood Parlor (AmbientCG Wood049: growth rings, longitudinal pores, satin varnish)
 * - Tavern Heartwood (AmbientCG WoodFloor051: chiseled parquet planks, oiled oak sheen)
 * - Emerald Lawn (AmbientCG Fabric037 / Carpet012: rich woven baize felt)
 * - Hopsack Tweed (AmbientCG Fabric063: tactile 2x2 multi-ply wool yarn weave)
 * - Denim Chambray (AmbientCG Fabric077: indigo twill weft relief)
 * - Striped Jersey (AmbientCG Fabric079: ribbed knit jersey texture)
 * - Reflective Tile (AmbientCG Tiles052: glazed subway tile with specular hotspot)
 * - Carved Stone (AmbientCG Rocks011: chiseled mineral paver relief)
 * - Sunken Sanctum (AmbientCG Onyx013: polished dark obsidian veins)
 * - Perilous Shores (AmbientCG Ground091 / Moss002: weathered coastal earth)
 */
class ProceduralBackgroundSystem {
public:
    static ProceduralBackgroundSystem& instance();

    ProceduralBackgroundSystem();
    ~ProceduralBackgroundSystem();

    ProceduralBackgroundSystem(const ProceduralBackgroundSystem&) = delete;
    ProceduralBackgroundSystem& operator=(const ProceduralBackgroundSystem&) = delete;

    /**
     * Render the procedural table background across the specified rectangle.
     * When PBR is enabled and AmbientCG texture sets are present in /textures/,
     * a seamless physically-lit normal-mapped surface is baked and rendered.
     * Otherwise, falls back to pure vector geometry passes.
     */
    void renderBackground(
        eatsbits::ui::BatchRenderer2D& r,
        float x, float y, float w, float h,
        TableThemeId theme,
        const eatsbits::ui::LightSource2D& light,
        uint32_t seed = 2026u
    );

    /**
     * Invalidate cached procedural textures (e.g. on window resize or theme switch).
     */
    void invalidateCache() noexcept;

    /**
     * Enable or disable seamless AmbientCG PBR texture baking.
     */
    void setPbrTexturesEnabled(bool enable) noexcept;
    [[nodiscard]] bool isPbrTexturesEnabled() const noexcept;

    /**
     * Check if a PBR material is available for the given theme.
     */
    [[nodiscard]] bool hasPbrMaterial(TableThemeId theme) const noexcept;

    /**
     * Load a custom background theme directly from a single-file .eats package.
     * When loaded, overrides the table surface with this custom PBR material.
     */
    bool loadCustomEatsTheme(const std::string& eatsFilePath);

    /**
     * Load a custom background theme directly from memory buffer (EBKG container or image).
     */
    bool loadCustomThemeFromMemory(
        const uint8_t* blobData,
        size_t blobSize,
        float uvScale = 2.0f,
        float normalStrength = 1.35f,
        float specularIntensity = 0.65f,
        float ambientLevel = 0.22f,
        float diffuseLevel = 0.88f,
        float tintR = 1.0f,
        float tintG = 1.0f,
        float tintB = 1.0f
    );

    /**
     * Clear any loaded custom .eats theme and return to the active TableThemeId theme.
     */
    void clearCustomEatsTheme() noexcept;

    /**
     * Check if a custom theme is currently loaded and active.
     */
    [[nodiscard]] bool hasCustomEatsTheme() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace solenshaded

#endif // SOLENSHADED_PROCEDURAL_BACKGROUND_HPP
