#include "solenshaded/solitaire_board.hpp"
#include "solenshaded/audio_foley.hpp"
#include "solenshaded/table_renderer.hpp"
#include "solenshaded/procedural_background.hpp"
#include "solenshaded/proctex_api.hpp"
#include "eatstextures/png_io.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    uint32_t seed = 2026u;
    uint32_t width = 1280u;
    uint32_t height = 720u;
    std::string outputPngPath;
    solenshaded::GameVariant variant = solenshaded::GameVariant::KlondikeDraw1;
    solenshaded::TableThemeId theme = solenshaded::TableThemeId::EmeraldLawn;
    constexpr int effectiveAa = 2; // Locked to 4x RGSS high-fidelity supersampling
    std::string bakeScript;
    std::string exportPbrPrefix;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--render-png" || arg == "--snapshot") && i + 1 < argc) {
            outputPngPath = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--freecell") {
            variant = solenshaded::GameVariant::FreeCell;
        } else if (arg == "--spectre") {
            variant = solenshaded::GameVariant::SpectresPatience;
            theme = solenshaded::TableThemeId::SunkenSanctum;
        } else if (arg == "--theme" && i + 1 < argc) {
            std::string tName = argv[++i];
            std::string lowerT;
            for (char c : tName) lowerT += static_cast<char>(std::tolower(c));
            if (lowerT == "emerald" || lowerT == "lawn" || lowerT == "linen" || lowerT == "green") {
                theme = solenshaded::TableThemeId::EmeraldLawn;
            } else if (lowerT == "tweed" || lowerT == "hopsack") {
                theme = solenshaded::TableThemeId::HopsackTweed;
            } else if (lowerT == "denim" || lowerT == "chambray" || lowerT == "blue") {
                theme = solenshaded::TableThemeId::DenimChambray;
            } else if (lowerT == "jersey" || lowerT == "knit" || lowerT == "striped") {
                theme = solenshaded::TableThemeId::StripedJersey;
            } else if (lowerT == "sanctum" || lowerT == "onyx" || lowerT == "obsidian") {
                theme = solenshaded::TableThemeId::SunkenSanctum;
            } else if (lowerT == "shores" || lowerT == "perilous" || lowerT == "ground" || lowerT == "moss") {
                theme = solenshaded::TableThemeId::PerilousShores;
            } else if (lowerT == "cowhide" || lowerT == "pebbled") {
                theme = solenshaded::TableThemeId::PebbledCowhide;
            } else if (lowerT == "leather" || lowerT == "nubuck" || lowerT == "distressed") {
                theme = solenshaded::TableThemeId::LeatherNubuck;
            } else if (lowerT == "paintedwood" || lowerT == "wood" || lowerT == "barnwood") {
                theme = solenshaded::TableThemeId::PaintedWood;
            } else if (lowerT == "dungeon" || lowerT == "brick" || lowerT == "pixeldungeon") {
                theme = solenshaded::TableThemeId::PixelDungeon;
            } else if (lowerT == "water" || lowerT == "ocean" || lowerT == "pixelwater") {
                theme = solenshaded::TableThemeId::PixelWater;
            } else if (!lowerT.empty() && std::isdigit(lowerT[0])) {
                int tIdx = std::clamp(std::stoi(lowerT), 0, static_cast<int>(solenshaded::TableThemeId::Count) - 1);
                theme = static_cast<solenshaded::TableThemeId>(tIdx);
            }
        } else if (arg == "--no-pbr") {
            solenshaded::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(false);
        } else if (arg == "--pbr") {
            solenshaded::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(true);
        } else if ((arg == "--bake-texture" || arg == "--bake-proctex" || arg == "--bake") && i + 1 < argc) {
            bakeScript = argv[++i];
        } else if ((arg == "--export-pbr" || arg == "--pbr-export") && i + 1 < argc) {
            exportPbrPrefix = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputPngPath = argv[++i];
        }
    }

    // 0. Check for Headless Procedural Texture Baking CLI Command
    if (!bakeScript.empty()) {
        solenshaded::ProceduralBakeOptions opts;
        opts.width = width;
        opts.height = height;
        opts.seed = seed;
        opts.overrideDimensions = true;
        opts.overrideSeed = true;

        if (!exportPbrPrefix.empty()) {
            std::cout << "[SolEnshaded] Baking procedural PBR bundle: " << bakeScript << " ("
                      << opts.width << "x" << opts.height << ") -> " << exportPbrPrefix << "_*.png ...\n";
            if (solenshaded::exportProceduralPbrToDisk(bakeScript, exportPbrPrefix, opts)) {
                std::cout << "[SolEnshaded] Successfully exported PBR maps to " << exportPbrPrefix << "_*.png\n";
                return 0;
            }
            std::cerr << "[SolEnshaded] Failed to export procedural PBR maps.\n";
            return 1;
        } else {
            std::string outPath = !outputPngPath.empty() ? outputPngPath : "baked_texture.png";
            std::cout << "[SolEnshaded] Baking procedural texture: " << bakeScript << " ("
                      << opts.width << "x" << opts.height << ") -> " << outPath << " ...\n";
            std::vector<uint8_t> rgba;
            uint32_t outW = 0, outH = 0;
            if (solenshaded::bakeProceduralTexture(bakeScript, rgba, outW, outH, opts)) {
                if (eatstextures::writePngRGBA(outPath, outW, outH, rgba.data())) {
                    std::cout << "[SolEnshaded] Successfully wrote " << outPath << " (" << outW << "x" << outH << ")\n";
                    return 0;
                }
            }
            std::cerr << "[SolEnshaded] Failed to bake procedural texture.\n";
            return 1;
        }
    }

    solenshaded::SolitaireBoard board;
    board.setTableTheme(theme);
    board.newDeal(seed, variant);
    board.computeLayout(static_cast<float>(width), static_cast<float>(height));

    std::cout << "[SolEnshaded] Initialized Deal #" << board.seed()
              << " | Variant: " << solenshaded::SolitaireBoard::variantName(board.variant())
              << " | Theme: " << solenshaded::SolitaireBoard::themeName(board.tableTheme())
              << "\n";

    if (!outputPngPath.empty()) {
        std::cout << "[SolEnshaded] Rendering procedural table snapshot (" << width << "x" << height
                  << ") -> " << outputPngPath << " ...\n";
        if (solenshaded::TableRenderer::renderToPng(board, width, height, outputPngPath, effectiveAa)) {
            std::cout << "[SolEnshaded] Successfully wrote " << outputPngPath << "\n";
            return 0;
        }
        std::cerr << "[SolEnshaded] Failed to write PNG snapshot.\n";
        return 1;
    }

    solenshaded::AudioFoleyEngine foley;
    foley.init(true);
    return solenshaded::TableRenderer::runInteractiveWindow(board, foley, width, height, effectiveAa);
}
