# Procedural Baroque, Relief & Sketch Art Pipeline

> **Document:** `PROCEDURAL_BAROQUE_ART_PIPELINE.md`  
> **Topic:** Procedural Generation of Baroque/Rococo Decks, 3D Bas-Relief Sculpting, Hand-Sketched Shading, and Multi-Material Colorization via Eatscript  
> **Target Subsystems:** [CardCanvas](file:///c:/git/solenshaded/include/solenshaded/card_canvas.hpp), [CardTextureCache](file:///c:/git/solenshaded/include/solenshaded/card_texture_cache.hpp), [eatstextures](file:///c:/git/solenshaded/include/eatstextures/field_ops.hpp), WebGPU WGSL Compute & Fragment Pipelines  

---

## 1. Executive Summary & Vision

SolEnshaded empowers creators to design high-fidelity playing card decks entirely via script, eliminating massive pre-rendered bitmap downloads. While classical decks rely on flat vector layouts, high-end luxury decks often feature **intricate baroque/rococo scrollwork, chiseled gold-leaf relief, carved antique ivory, or vintage copperplate hand engravings**.

This document specifies an optimal, production-grade architecture that facilitates:
1. **Procedural Baroque Ornamental Primitives:** Parametric generation of acanthus leaves, logarithmic volutes, cartouche frames, and guilloché beaded borders with native quad-symmetry.
2. **Pluggable Artistic Finishes:** The exact same vector layout can be rendered as:
   - **Crisp Flat Vector:** Precision luxury graphic printing.
   - **Hand-Sketched / Engraved:** Copperplate intaglio with quill-jitter, procedural cross-hatching, and paper ink-bleed.
   - **3D Bas-Relief Sculpting:** Physical displacement with chiseled/fluted profiles, cavity ambient occlusion, and PBR metallic lighting.
3. **Full Multi-Color & Multi-Material Control:** Granular palette assignment allowing gold foil accents alongside dark slate backgrounds, antique bone/ivory under-layers, and suit-specific pigments (crimson for hearts/diamonds, obsidian for spades/clubs).
4. **Dual-Backend Acceleration:** High-performance WebGPU Compute Shaders (using the Jump Flood Algorithm for sub-millisecond distance fields) with a robust pure C++ CPU fallback via `eatstextures`.

---

## 2. Architectural Overview

```mermaid
flowchart TD
    subgraph S1 ["Stage 1: Eatscript Procedural Layout"]
        UserScript["custom_baroque_deck.eat"]
        SymmetryScope["Quad / Bilateral Symmetry Engine"]
        ParametricMotifs["Acanthus Fronds, Volutes, Cartouches, Bead Chains"]
        ColorPalette["Multi-Material Palette (Gold, Slate, Ivory, Suit Inks)"]
        
        UserScript --> SymmetryScope
        SymmetryScope --> ParametricMotifs
        ParametricMotifs --> ColorPalette
    end

    subgraph S2 ["Stage 2: Geometry & Mask Compilation"]
        VectorCanvas["CardCanvas (2D Vector Path & Alpha Buffers)"]
        ColorPalette --> VectorCanvas
    end

    subgraph S3 ["Stage 3: Pluggable Shading & Finish Pipeline"]
        VectorCanvas --> FinishSwitch{"ctx.apply_finish(style=...)"}
        
        FinishSwitch -->|"style='flat'"| FlatPass["Flat Vector Rasterizer (MSAA / Analytical)"]
        FinishSwitch -->|"style='hand_sketched'"| SketchPass["Hand-Sketched Intaglio Shader (Wobble + Cross-Hatch + Paper Bleed)"]
        FinishSwitch -->|"style='relief'"| ReliefPipeline["3D Bas-Relief Compute Pipeline"]
    end

    subgraph S4 ["Stage 3B: Bas-Relief Compute Pipeline Detail"]
        ReliefPipeline --> JFA["Jump Flood Algorithm (GPU/CPU SDF)"]
        JFA --> Profiler["Profile Shaper (Chisel, Dome, Fluted Ridge)"]
        Profiler --> HeightMap["Composite Heightfield H(x, y)"]
        HeightMap --> SurfaceOps["Curvature (Laplacian) + Cavity AO + Sobel Normals"]
        SurfaceOps --> PBR["PBR Shading (GGX Specular, Gold Foil, Crevice Patina)"]
    end

    subgraph S5 ["Stage 4: Texture Cache & Render"]
        FlatPass --> CardTextureCache["CardTextureCache (Mipmapped RGBA8 / GPU Textures)"]
        SketchPass --> CardTextureCache
        PBR --> CardTextureCache
        CardTextureCache --> TableRenderer["TableRenderer / Game Table (60+ FPS)"]
    end
```

---

## 3. Stage 1: Symmetrical Ornamental Geometry Engine

Baroque aesthetics require mathematical rhythm and balance. Authoring thousands of individual Bézier points manually in script is impractical. The engine exposes high-level parametric building blocks:

### 3.1 Symmetry Scope (`ctx.symmetry`)
A scoped context that automatically reflects and rotates all drawing commands across specified axes:
- `mirror_x`: Bilateral reflection across the vertical centerline.
- `mirror_y`: Inverted top-to-bottom reflection.
- `quad`: 4-quadrant reflection (author top-left quadrant; the engine stamps all four corners).

### 3.2 Parametric Volutes & C/S-Scrolls (`ctx.draw_volute`)
A logarithmic spiral defined by:
$$r(\theta) = a \cdot e^{b\theta}, \quad \theta \in [\theta_{\text{start}}, \theta_{\text{end}}]$$
- **Variable Stroke Width:** Tapers smoothly from a substantial base into a delicate spiral eye via cubic Bézier easing $w(t) = w_0 (1 - t^2) + w_{\text{tip}} t^2$.
- **Spine Ridge:** Calculates the centerline curve and outer envelope boundaries for subsequent relief beveling.

### 3.3 Acanthus Leaf Generator (`ctx.draw_acanthus`)
A foundational element of baroque cartouches:
- **Spine Spine:** A quadratic or cubic Bézier arc connecting `start` to `end`.
- **Lobes (Serrations):** $N$ overlapping leafy scallops radiating outwards at parametric angle $\phi_i$, with scalloped tips that curl backwards.
- **Fluting / Ribbing:** Internal vein lines tracing from the central stem to each lobe tip.

### 3.4 Path Repeaters & Guilloché (`ctx.repeat_along_path`)
Stamps decorative motifs along any path or perimeter:
- **Beaded Pearl Trim:** Uniform or size-tapered spheres spaced along a card boundary.
- **Egg-and-Dart / Dentils:** Classical architectural frieze patterns stamped at set arc-length intervals.

---

## 4. Stage 2: The "Relief-ify" Bas-Relief Engine

To turn any 2D vector graphic—including the standard Ace of Spades pip, court figures, or custom scrollwork—into physical 3D carvings, the engine uses **Distance Field Profiling**.

```
Input 2D Silhouette ──► Euclidean SDF d(x,y) ──► Profile Function f(d) ──► Heightmap H(x,y)
```

### 4.1 GPU Jump Flood Algorithm (JFA)
For a card of dimension $1024 \times 1536$, a naïve distance transform takes $O(W^2 \cdot H^2)$ operations. The **Jump Flood Algorithm** computes the exact Euclidean distance field in $O(\log_2(\max(W, H)))$ passes:
1. **Pass 0 (Seed):** Initialize edge pixels with their own coordinate $(x_0, y_0)$; background pixels with $\infty$.
2. **Passes $k = \log_2(N) \dots 1$:** In each compute dispatch, every pixel samples 8 neighbors at step size $2^k$. If a neighbor points to a closer boundary coordinate, the pixel adopts that coordinate.
3. Total dispatch time on modern GPU: **$< 0.4 \text{ ms}$**.

### 4.2 Cross-Sectional Profile Functions
Given distance $d(x, y)$ to the nearest edge and maximum bevel radius $R$:
Normalized distance: $u = \text{clamp}(d / R, 0.0, 1.0)$.

| Profile Name | Formula $h(u)$ | Visual Characteristic | Typical Baroque Use |
| :--- | :--- | :--- | :--- |
| **`chisel`** | $h = u \cdot h_{\max}$ | Sharp pyramidal ridge along medial axis | Chiseled lettering, royal crest edges |
| **`dome`** | $h = \sqrt{1.0 - (1.0 - u)^2} \cdot h_{\max}$ | Smooth rounded semi-circular cross section | Heavy volute stems, structural cartouches |
| **`fluted_ridge`** | $h = (u + 0.35 \sin(3\pi u)) \cdot h_{\max}$ | Central ridge flanked by parallel scooped hollows | Acanthus leaf scrolls (as in attached images) |
| **`incised`** | $h = (1.0 - u) \cdot (-h_{\max})$ | Inverted negative trough | Engraved background cross-hatching, recessed pips |

### 4.3 Micro-Surface Analysis & PBR Lighting
From the generated heightfield $H(x, y)$:
1. **Normal Mapping:** 
   $$\mathbf{N}(x, y) = \text{normalize}\left(-\frac{\partial H}{\partial x} \cdot s,\, -\frac{\partial H}{\partial y} \cdot s,\, 1.0\right)$$
2. **Cavity Ambient Occlusion (AO):**
   Calculated by sampling horizon angles over a radial kernel. Crevices and under-folds receive strong shadow occlusion, accentuating sculptural depth.
3. **Curvature / Wear Detection:**
   Discrete Laplacian:
   $$\nabla^2 H = H(x+1,y) + H(x-1,y) + H(x,y+1) + H(x,y-1) - 4H(x,y)$$
   - $\nabla^2 H < 0$ (Convex ridges): Highlights catching edge wear, gold leaf burnishing, and bright specular reflections.
   - $\nabla^2 H > 0$ (Concave crevices): Traps dark patina, oil stains, and oxidized metal grime.

---

## 5. Stage 3: The Hand-Sketched & Engraved Pipeline

For users desiring an artisanal, vintage printed aesthetic rather than physical 3D relief, the pipeline provides a dedicated **Copperplate & Intaglio Engraving Shader**.

### 5.1 Procedural Line Wobble (Domain Noise)
Human hands cannot draw mathematically ideal Bézier splines. The shader modulates sample coordinates along curve normals using multi-frequency 1D simplex noise:
$$\mathbf{p}_{\text{wobbled}}(t) = \mathbf{p}(t) + \mathbf{n}(t) \cdot \left[A_1 \cdot \text{noise}(f_1 t) + A_2 \cdot \text{noise}(f_2 t)\right]$$
- Amplitude $A_1 \approx 0.5 \text{ px}$ (natural tremor).
- Frequency $f_1 \approx 4.0$ cycles per stroke.

### 5.2 Dynamic Tonal Cross-Hatching
To fill shadow gradients (e.g. inside the spade pip or cartouche frame):
- The surface tone $T(x, y) \in [0, 1]$ (derived from an analytical shadow field or distance gradient) selects the hatch tier:
  - $T \in [0.75, 1.0]$: White / unhatched.
  - $T \in [0.50, 0.75]$: Primary hatch lines at angle $\theta$ (e.g., $45^\circ$).
  - $T \in [0.25, 0.50]$: Cross-hatch: secondary lines at $\theta + 90^\circ$.
  - $T \in [0.00, 0.25]$: Dense cross-hatch with staggered dot stippling.

### 5.3 Micro-Capillary Ink Bleed
At the boundary of every stroke, a 2D Worley/cellular noise threshold simulates ink bleeding into the porous surface of cotton rag paper:
$$\alpha_{\text{final}} = \text{smoothstep}(w - \delta, w + \delta, d + \text{noise}_{\text{fiber}}(x, y) \cdot \sigma)$$

---

## 6. Multi-Color Palette & Material Specification

To support contrasting layers (e.g., dark slate background + antique gold filigree + ivory under-scrolls + suit-specific red/black iconography), the engine decouples **color** and **material semantics**:

### 6.1 Material Channels
When drawing elements in Eatscript, each call can specify color and physical attributes:
```eatscript
ctx.draw_volute(..., color="#D4AF37", material="gold_leaf")
```

| Material Preset | Albedo Tint | Metallic | Roughness | Cavity Patina | Specular Tone |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`gold_leaf`** | `#D4AF37` (Rich Gold) | `0.95` | `0.22` | `#3A2A10` (Warm Umber) | `#FFF0B5` |
| **`blackened_silver`** | `#B0B5BA` (Pewter/Silver) | `0.90` | `0.30` | `#0A0D12` (Cold Pitch) | `#FFFFFF` |
| **`carved_ivory`** | `#EDE6D6` (Antique Bone) | `0.00` | `0.45` | `#5C4D3C` (Sepia Tea Stain) | `#FAF6ED` |
| **`slate_cardstock`** | `#16171A` (Charcoal Slate) | `0.00` | `0.85` | `#0B0C0E` (Near-Black) | `#3A3D45` |
| **`lacquer_crimson`** | `#C81D25` (Imperial Red) | `0.05` | `0.18` | `#4A080C` (Deep Claret) | `#FFA8AC` |

---

## 7. Concrete Eatscript API Reference

### 7.1 Symmetry & Scoping
* `ctx.symmetry(mirror_x=true, mirror_y=true)`  
  Executes the enclosed block with mirroring transforms applied across the card's horizontal and/or vertical midpoints.

### 7.2 Ornamental Primitives
* `ctx.draw_volute(cx, cy, radius, turns, angle_start=0.0, color="#d4af37", width=3.0, profile="fluted_ridge")`  
  Renders an expanding logarithmic spiral flourish.
* `ctx.draw_acanthus(from=[x1, y1], to=[x2, y2], lobes=5, curl_dir="left", color="#ede6d6", stroke="#d4af37")`  
  Renders a multi-lobed acanthus leaf cluster along a guide curve.
* `ctx.stroke_cartouche(cx, cy, rx, ry, style="baroque_oval", color="#d4af37", line_width=4.0)`  
  Renders a decorated ornamental frame medallion.
* `ctx.repeat_along_path(path, count, motif="bead", radius=3.5, color="#d4af37")`  
  Stamps beaded pearls or decorative marks at equidistant intervals along an SVG path or rectangle perimeter.

### 7.3 Relief & Shading Finishes
* `ctx.apply_relief(profile="chisel", height=1.0, bevel_width=12.0)`  
  Applies the Jump Flood distance transform and height extrusion to all elements drawn in the current layer.
* `ctx.apply_finish(style="relief" | "hand_sketched" | "flat", [options])`  
  Applies the global surface post-process before uploading to `CardTextureCache`:
  - `style="flat"`: Standard high-precision 2D vector anti-aliased output.
  - `style="hand_sketched"`: Intaglio engraving with `line_wobble`, `cross_hatch`, and `paper_grain`.
  - `style="relief"`: PBR surface lighting with `light_azimuth`, `light_elevation`, `ao_intensity`, and `patina_color`.

---

## 8. Complete Working Example Deck Script

Here is how an author creates an entire imperial baroque deck with dark slate, antique gold, and suit-themed accents:

```python
# =============================================================================
# Imperial Baroque Custom Deck (imperial_baroque.eat)
# =============================================================================

def on_draw_card_face(ctx, rank, suit, w, h):
    # 1. Palette Setup based on suit
    let is_red = (suit == "hearts" or suit == "diamonds")
    let suit_color = is_red ? "#C81D25" : "#141518"
    let gold_trim  = "#D4AF37"
    let ivory_trim = "#EDE6D6"
    let slate_bg   = "#1A1B20"

    # 2. Base Card Plate
    ctx.fill_rounded_rect(0, 0, w, h, radius=10.0, color=slate_bg)
    ctx.stroke_rounded_rect(1.0, 1.0, w - 2.0, h - 2.0, radius=9.0, color=gold_trim, width=1.5)

    # 3. Quad-Symmetrical Baroque Corner Filigree
    with ctx.symmetry(mirror_x=true, mirror_y=true):
        # Outer heavy corner volute
        ctx.draw_volute(cx=55, cy=75, radius=70, turns=1.35, 
                        color=gold_trim, width=4.0, profile="fluted_ridge")
        
        # Inward-reaching acanthus frond
        ctx.draw_acanthus(from=[55, 145], to=[150, 240], lobes=5, 
                          color=ivory_trim, stroke=gold_trim)
        
        # Beaded frame border
        ctx.repeat_beads(inset=20.0, radius=3.2, spacing=9.5, color=gold_trim)

    # 4. Central Baroque Cartouche Frame
    ctx.stroke_cartouche(cx=w*0.5, cy=h*0.5, rx=w*0.32, ry=h*0.28, 
                         style="baroque_oval", color=gold_trim, line_width=4.0)
    ctx.stroke_oval(cx=w*0.5, cy=h*0.5, rx=w*0.28, ry=h*0.24, 
                    color=ivory_trim, line_width=1.5)

    # 5. Central Suit Icon with Filigree Infill
    # Court cards receive court figures; numbered cards receive ornate central pip
    if rank == "J" or rank == "Q" or rank == "K":
        ctx.draw_court(rank, suit, w*0.22, h*0.22, w*0.56, h*0.56, opacity=1.0, tint=gold_trim)
    else:
        ctx.draw_ornate_suit(
            suit=suit,
            cx=w*0.5, cy=h*0.5,
            size=w*0.35,
            body_color=suit_color,
            filigree_color=gold_trim,
            profile="chisel"
        )

    # 6. Corner Indices (Rank + Small Suit Pip)
    ctx.draw_corner_indices(rank, suit, w, h, color=suit_color, font_size=w*0.09)

    # 7. Apply Desired Finish
    # Switch between: "relief", "hand_sketched", or "flat"
    ctx.apply_finish(
        style = "relief",
        light_azimuth = 135.0,
        light_elevation = 45.0,
        ao_intensity = 1.6,
        relief_depth = 4.0,
        patina_color = "#0B0C0E",
        highlight_color = "#FFF5D0"
    )

def on_draw_card_back(ctx, w, h):
    let gold_trim  = "#D4AF37"
    let ivory_trim = "#EDE6D6"
    let slate_bg   = "#1A1B20"

    ctx.fill_rounded_rect(0, 0, w, h, radius=10.0, color=slate_bg)
    ctx.stroke_rounded_rect(2.0, 2.0, w - 4.0, h - 4.0, radius=8.0, color=gold_trim, width=2.0)

    # All-over ornate mirror lattice for the card back
    with ctx.symmetry(mirror_x=true, mirror_y=true):
        ctx.draw_volute(cx=w*0.5, cy=h*0.5, radius=w*0.4, turns=2.2, 
                        color=gold_trim, width=5.0, profile="fluted_ridge")
        ctx.draw_acanthus(from=[40, 40], to=[w*0.5 - 20, h*0.5 - 20], lobes=7, 
                          color=ivory_trim, stroke=gold_trim)
        ctx.repeat_beads(inset=16.0, radius=3.5, spacing=10.0, color=gold_trim)

    ctx.apply_finish(
        style = "relief",
        light_azimuth = 135.0,
        light_elevation = 45.0,
        ao_intensity = 1.8,
        relief_depth = 4.5
    )
```

---

## 9. Performance, Memory & Execution Architecture

### 9.1 Pre-Bake Compilation Performance
* **Bake Timing:** Executed once when the deck recipe loads or during hot-reload.
  - Symmetrical 2D vector compilation: **$< 1.5 \text{ ms}$ per card**.
  - Jump Flood Algorithm (JFA) compute pass: **$< 0.4 \text{ ms}$ per card**.
  - PBR normal & AO shading pass: **$< 0.8 \text{ ms}$ per card**.
  - **Total 53-card deck bake time:** **$\approx 140 \text{ ms}$** on GPU (or $\approx 350 \text{ ms}$ multi-threaded CPU).
* **Zero Runtime Overhead:** During active solitaire gameplay, the cards are standard mipmapped GPU textures rendered via `BatchRenderer2D` at full monitor refresh rate (60/120/144+ FPS).

### 9.2 Optional Dynamic 2.5D Lighting
Because the compute pipeline can retain the normal map and heightfield in GPU memory, the engine can optionally update card surface lighting dynamically:
- When a player drags or hovers a card, table coordinates map to the light direction uniform.
- As the card moves across the table, the gold filigree glints and shadows sweep across the relief in real time.

---

## 10. Implementation Roadmap

1. **Phase 1: 2D Symmetrical Curves & Motifs**
   - Add `ctx.symmetry(mirror_x, mirror_y)` transform scope to [card_canvas.cpp](file:///c:/git/solenshaded/src/render/card_canvas.cpp).
   - Implement parametric logarithmic spiral (`draw_volute`) and lobe fan (`draw_acanthus`).
   - Add equidistant bead repeater along boundaries.

2. **Phase 2: Hand-Sketched / Engraved Finish**
   - Implement 1D domain warp noise for stroke boundaries.
   - Implement procedural cross-hatch fill shader in `CardCanvas` rasterizer.
   - Add cotton paper fiber stipple blending.

3. **Phase 3: Jump Flood Relief & PBR Shading**
   - Hook JFA compute shader into WebGPU backend (with CPU fallback via `eatstextures`).
   - Add cross-section profiles (`chisel`, `dome`, `fluted_ridge`, `incised`).
   - Hook `computeCurvature` and `computeAmbientOcclusion` to drive metallic specular highlights and crevice grime.

4. **Phase 4: Deck Templates & Documentation**
   - Ship `decks/imperial_baroque.eat` and `decks/engraved_intaglio.eat` as standard builtin procedural decks.

