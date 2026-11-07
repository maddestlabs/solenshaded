# Solitaire Enshaded (`SolEnshaded`)

> **A 100% Procedural, Zero-Bloat Tribute to *Hardwood Solitaire* Set in the World of *Encharted***  
> *Built in pure C++20 & WebGPU on the [Eatsbits](file:///c:/git/eatsbits/engine-blueprint.md) Engine Foundation.*

---

## Overview

**Solitaire Enshaded (`SolEnshaded`)** combines the timeless simplicity, tactile warmth, and effortless UI of **Hardwood Solitaire 3.0** with the dark-fantasy cartographic lore of **[Eatscharted](file:///c:/git/eatscharted/README.md)**.

In the world of Eatscharted, **Shades**—incorporeal remnants inspired by *ZAngband's* **Spectre** class—pass through the veil between the living kingdoms and the Sunken Sanctums, playing **"The Veil's Patience"** on carved hardwood and obsidian tables.

### Key Features
- **Authentic Hardwood Solitaire 3.0 Ergonomics:**
  - **One-Tap / Right-Click Smart Moves:** Automatically routes any playable card or sub-stack to its optimal Foundation or Tableau destination.
  - **Spring-Damper Card Flight & Elevation Shadows:** Airborne cards arc smoothly across the board with dynamic 2D drop-shadow elevation.
  - **Unlimited Zero-Allocation Undo / Redo & Deterministic Seeds:** Every deal is reproducible from a single 32-bit world seed.
- **Classic & Enshaded Modes:** Play pure, classic rules (**Klondike**, **FreeCell**, **Yukon**) on a traditional **Tavern Heartwood** table, or switch to **Enshaded Table Themes** (*Sunken Sanctum*, *Perilous Shores*) with Spectre-inspired *Nether Sight* hints.
- **Real-Time Procedural Foley Audio:** Synthesized card slides, parchment snaps, shuffle riffles, wooden clicks, and spectral harmonic chimes powered by `miniaudio`.

---

## Building & Running

### Desktop (Windows / WebGPU Native)

```powershell
# Build and launch interactive SolEnshaded desktop client
.\build.ps1

# Compile only without launching (Required for AI agents per AGENTS.md)
.\build.ps1 -NoRun -NoLaunch

# Render headless PNG preview snapshot
.\build.ps1 -RenderPreview
```

---

## Controls

- **Left Click / Drag:** Pick up and drag a card or face-up sub-stack; click Stock pile to deal.
- **Right Click / Single Tap / `Space`:** **Smart Move** hovered or selected card to its best Foundation or Tableau destination.
- **`Z` / `Ctrl+Z`:** Undo last move.
- **`Y` / `Ctrl+Y`:** Redo move.
- **`H`:** **Nether Sight (Hint)** — highlights the highest-priority legal move with a spectral glow.
- **`A`:** Auto-move eligible cards to Foundations.
- **`N`:** Deal a new seeded hand.
- **`1` / `2` / `3`:** Switch Game Variant (`1`: Klondike Draw-1, `2`: Klondike Draw-3, `3`: FreeCell).
- **`T`:** Cycle Table Theme (`Tavern Heartwood` $\to$ `Sunken Sanctum` $\to$ `Perilous Shores`).

---

## Documentation

- **[Architectural & Lore Blueprint (`BLUEPRINT.md`)](file:///c:/git/solenshaded/BLUEPRINT.md):** Full technical breakdown of how Eatsbits powers a procedural Hardwood Solitaire 3.0 clone, the ZAngband Spectre / Shade lore integration, and the phased roadmap.
- **[Deck Pipeline & AI Generation Guide (`DECK_PIPELINE.md`)](file:///c:/git/solenshaded/DECK_PIPELINE.md):** Complete guide to the dual vector/bitmap pipeline, Gemini multimodal style-reference conditioning, red/black solitaire contrast rules, and CLI tools.
- **[Eatscript Custom Deck Architecture (`EATSCRIPT_CARD_CUSTOMIZATION.md`)](file:///c:/git/solenshaded/EATSCRIPT_CARD_CUSTOMIZATION.md):** Specification for user-scripted vector card rendering via Eatscript and zero-overhead retained display lists.
- **[Agent & Build Safety Rules (`AGENTS.md`)](file:///c:/git/solenshaded/AGENTS.md):** Strict policies prohibiting automated test execution and background GPU/audio processes.

## TODO

- Procgen baroque art
- loading bars for decks and other heavy lifting
- New seed at startup. How will this affect procgen? Use different seed for procgen?
- ~~Card textures. We already have age/wear factor, implement realistic card textures so even default deck looks like real deck.~~
- Proper settings menu. Use Eatsbits Settings menu as base to build on.
- Icons for all settings and actions
- Allow editing of seed (via url for web build)

### Load time
Load speed is crucial. We need this thing to load instantly, no delays. So we need to start with very minimal aesthetics; maybe even solid color background, dead simple card graphics, lowest level effects (cheapest card stock texture, cheapest wear setting). This isn't a text editor so we don't need monospace fonts, we just need our single bundled condensed font and our api for more specifics.

### Baroque Noir
This is the most impressive card deck in modern solitaire gaming. So we need to hone this deck.
- Cards are currently mostly uncentered and most are heavily misaligned and inconsistent with both positioning and scale. We need to ensure consistency in alignment and scale. 2 of Clubs is perfectly centered and scaled but there's black/dark margins to left and right. The card's gray-ish texture background should extend fully to left and right. And that should be our baseline. All cards should be positioned and scaled like 2 of Clubs. And our gray-ish texture background should extend throughout the individual card textures to the ends so there are no margins.
- Red cards, Ace of Hearts (and to a fair degree Jack of Hearts and King of Hearts, though the bloom is a slight bit heavier than needed) represents the optimal glow/bloom lighting of diamonds and hearts. The glow is important to help users easily differentiate red and black cards especially on tinier mobile screens.
- Black cards, Ace of Spades and Ace of Clubs represent optimal color and lighting to differentiate clubs and spades from the rest of the background. The club and spade elements are noticeably darker than the other decorative elements, which makes it easy for users to discern which suit the card belongs to.
- We must maintain the current art, the card faces provide the perfect aesthetic and we can't lose that in redesigning. Can we redesign the other elements without affecting these? We're basically redesigning around these.