#include "solenshaded/solitaire_board.hpp"
#include "solenshaded/audio_foley.hpp"
#include "solenshaded/table_renderer.hpp"
#include "solenshaded/procedural_background.hpp"
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
    solenshaded::TableThemeId theme = solenshaded::TableThemeId::RosewoodParlor;
    int aaMode = -1;

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
            if (lowerT == "rosewood" || lowerT == "wood") {
                theme = solenshaded::TableThemeId::RosewoodParlor;
            } else if (lowerT == "tavern" || lowerT == "heartwood" || lowerT == "parquet") {
                theme = solenshaded::TableThemeId::TavernHeartwood;
            } else if (lowerT == "emerald" || lowerT == "lawn" || lowerT == "linen" || lowerT == "green") {
                theme = solenshaded::TableThemeId::EmeraldLawn;
            } else if (lowerT == "tweed" || lowerT == "hopsack") {
                theme = solenshaded::TableThemeId::HopsackTweed;
            } else if (lowerT == "denim" || lowerT == "chambray" || lowerT == "blue") {
                theme = solenshaded::TableThemeId::DenimChambray;
            } else if (lowerT == "jersey" || lowerT == "knit" || lowerT == "striped") {
                theme = solenshaded::TableThemeId::StripedJersey;
            } else if (lowerT == "tile" || lowerT == "reflective") {
                theme = solenshaded::TableThemeId::ReflectiveTile;
            } else if (lowerT == "stone" || lowerT == "slate" || lowerT == "carved") {
                theme = solenshaded::TableThemeId::CarvedStone;
            } else if (lowerT == "sanctum" || lowerT == "onyx" || lowerT == "obsidian") {
                theme = solenshaded::TableThemeId::SunkenSanctum;
            } else if (lowerT == "shores" || lowerT == "perilous" || lowerT == "ground") {
                theme = solenshaded::TableThemeId::PerilousShores;
            } else if (!lowerT.empty() && std::isdigit(lowerT[0])) {
                int tIdx = std::clamp(std::stoi(lowerT), 0, static_cast<int>(solenshaded::TableThemeId::Count) - 1);
                theme = static_cast<solenshaded::TableThemeId>(tIdx);
            }
        } else if (arg == "--no-pbr") {
            solenshaded::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(false);
        } else if (arg == "--pbr") {
            solenshaded::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(true);
        } else if ((arg == "--aa" || arg == "--anti-aliasing") && i + 1 < argc) {
            aaMode = std::clamp(std::stoi(argv[++i]), 0, 2);
        } else if (arg == "--no-aa") {
            aaMode = 0;
        }
    }

    solenshaded::SolitaireBoard board;
    board.setTableTheme(theme);
    board.newDeal(seed, variant);
    board.computeLayout(static_cast<float>(width), static_cast<float>(height));

    const char* aaNames[] = {
        "0 (Off / Integer-snapped)",
        "1 (2x Fast Continuous Box Coverage)",
        "2 (4x RGSS Rotated Grid Supersampling)"
    };
    int effectiveAa = (aaMode >= 0) ? aaMode : 2;

    std::cout << "[SolEnshaded] Initialized Deal #" << board.seed()
              << " | Variant: " << solenshaded::SolitaireBoard::variantName(board.variant())
              << " | Theme: " << solenshaded::SolitaireBoard::themeName(board.tableTheme())
              << " | AA: " << aaNames[effectiveAa] << "\n";

    if (!outputPngPath.empty()) {
        std::cout << "[SolEnshaded] Rendering procedural table snapshot (" << width << "x" << height
                  << " | AA: " << aaNames[effectiveAa] << ") -> " << outputPngPath << " ...\n";
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
