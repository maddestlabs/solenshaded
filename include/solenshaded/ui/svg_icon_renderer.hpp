#ifndef SOLENSHADED_SVG_ICON_RENDERER_HPP
#define SOLENSHADED_SVG_ICON_RENDERER_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace solenshaded {

enum class SvgIconId : uint16_t {
    None = 0,
    NewDeal,
    Restart,
    Variant,
    Theme,
    Deck,
    CardStock,
    CardCorners,
    CardWear,
    Undo,
    Redo,
    Hint,
    AutoFinish,
    MenuAnchor,
    Settings,
    Dice,
    VolumeHigh,
    VolumeMute,
    Close,
    Check,
    Copy,
    ChevronLeft,
    ChevronRight,
    Count
};

class SvgIconRenderer {
public:
    static SvgIconRenderer& instance();

    // Render a predefined stock SVG icon by ID, tinted with (r, g, b, a)
    void renderIcon(
        eatsbits::ui::BatchRenderer2D& r,
        SvgIconId iconId,
        float x,
        float y,
        float w,
        float h,
        float red,
        float green,
        float blue,
        float alpha = 1.0f
    );

    // Render an arbitrary SVG XML fragment directly onto the batch renderer
    void renderSvgXml(
        eatsbits::ui::BatchRenderer2D& r,
        const std::string& svgXml,
        float x,
        float y,
        float w,
        float h,
        float red,
        float green,
        float blue,
        float alpha = 1.0f
    );

    // Get the SVG XML source code for a stock icon
    [[nodiscard]] static const char* getStockSvgXml(SvgIconId iconId) noexcept;

private:
    SvgIconRenderer();
    ~SvgIconRenderer();

    struct CachedIconTexture {
        int width{0};
        int height{0};
        std::vector<uint8_t> alphaMask; // Single-channel 8-bit coverage mask
    };

    const CachedIconTexture* getOrRasterize(SvgIconId id, int targetW, int targetH);
    const CachedIconTexture* getOrRasterizeXml(const std::string& xml, int targetW, int targetH);

    std::unordered_map<uint64_t, CachedIconTexture> stockCache_;
    std::unordered_map<std::string, CachedIconTexture> xmlCache_;
    std::vector<uint32_t> blitBuffer_;
    void* rasterizer_{nullptr};
};

} // namespace solenshaded

#endif // SOLENSHADED_SVG_ICON_RENDERER_HPP

