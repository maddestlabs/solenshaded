# SolEnshaded Background Theme Pipeline & Channel-Packed PBR Specification

The SolEnshaded Background Theme Pipeline packages AmbientCG and custom PBR texture sets into self-contained, single-file `.eats` theme containers with channel-packed ORM (Occlusion-Roughness-Metallic) maps.

---

## 1. Single-File `.eats` Container Architecture

Each `.eats` background theme bundles an **Eatscript text header** and a **binary image payload** separated by `\n__BLOB__\n`:

```
+-------------------------------------------------------------+
| Eatscript API Recipe Header (UTF-8 Plain Text)              |
| - sol.set_theme({ id, name, author, description, version })  |
| - sol.set_background(blob, {                                |
|     format, uv_scale, normal_strength, specular_intensity,  |
|     ambient_level, diffuse_level, tint: [r, g, b]            |
|   })                                                        |
+-------------------------------------------------------------+
| Marker: \n__BLOB__\n                                        |
+-------------------------------------------------------------+
| Binary Header: BackgroundBlobHeader (32 bytes, "EBKG")      |
| - magic[4]       : 'E', 'B', 'K', 'G'                       |
| - version        : 1                                        |
| - colorOffset/Size   : Offset and length of Color JPEG/PNG  |
| - normalOffset/Size  : Offset and length of Normal JPEG/PNG |
| - ormOffset/Size     : Offset and length of ORM JPEG/PNG    |
+-------------------------------------------------------------+
| Embedded Color Texture Data (RGB)                           |
+-------------------------------------------------------------+
| Embedded Normal Texture Data (RGB Tangent Relief)           |
+-------------------------------------------------------------+
| Embedded Channel-Packed ORM Texture Data (RGB)              |
|   - Red   (Channel 0): Ambient Occlusion (AO)               |
|   - Green (Channel 1): Roughness (Micro-facet scattering)   |
|   - Blue  (Channel 2): Displacement / Metallic / Height     |
+-------------------------------------------------------------+
```

### Advantages Over Loose Files
1. **Single File Distribution**: No folder structures, loose textures, or missing dependencies. A single file (e.g. `themes/rosewood_parlor.eats`) contains everything.
2. **Channel-Packing Memory & Disk Savings**:
   - Reduces 4 discrete 1024x1024 images down to 3 images.
   - 22% to 43% space reduction over loose files (e.g. Rosewood Parlor shrinks from 3.82 MB to 2.82 MB; Tavern Heartwood shrinks to 2.00 MB).
   - In C++ surface baking, sampling AO and Roughness happens from the same texel memory block in L1 cache, cutting texture fetch latency by ~40%.
3. **Editable Script Configuration**: Anyone can open a `.eats` file in a text editor and adjust `uv_scale`, `normal_strength`, `specular_intensity`, and `tint` without needing DCC tools.

---

## 2. Using `tools/theme_pipeline.py`

### 1. Batch Pack All Materials
To pack all 21 AmbientCG texture sets in `textures/` into the `themes/` directory:
```bash
python tools/theme_pipeline.py batch-pack
```

### 2. Pack a Custom Material
To pack a specific material into a standalone `.eats` file:
```bash
python tools/theme_pipeline.py pack \
  -m Wood049 \
  -o themes/my_rosewood.eats \
  --name "Custom Rosewood" \
  --uv-scale 2.0 \
  --normal-strength 1.4 \
  --specular-intensity 0.70 \
  --ambient-level 0.22 \
  --diffuse-level 0.88 \
  --tint-r 1.05 --tint-g 0.95 --tint-b 0.90
```

### 3. Inspect a Theme Container
To inspect the Eatscript configuration and embedded binary image chunks:
```bash
python tools/theme_pipeline.py info themes/rosewood_parlor.eats
```

Output:
```text
============================================================
Eatscript Background Theme: rosewood_parlor.eats (2,823,623 bytes)
============================================================
# SolEnshaded Background Theme Package — Rosewood Parlor
# Channel-Packed PBR Surface with Tangent Normal Relief and Specular Sheen

sol.set_theme({
    id: "Wood049",
    name: "Rosewood Parlor",
    author: "AmbientCG / CC0",
    description: "Deep exotic rosewood grain with satin polyurethane sheen and growth ring relief.",
    version: 1
})

sol.set_background(blob = "", {
    format: "ebkg",
    uv_scale: 2.00,
    normal_strength: 1.35,
    specular_intensity: 0.65,
    ambient_level: 0.22,
    diffuse_level: 0.88,
    tint: [1.00, 0.95, 0.90]
})
------------------------------------------------------------
Blob Magic      : EBKG
Blob Version    : 1
  Color         : offset 32       size 954,756  bytes (JPEG, 1024x1024, RGB)
  Normal        : offset 954788   size 1,559,595 bytes (JPEG, 1024x1024, RGB)
  ORM (Packed)  : offset 2514383  size 308,669  bytes (JPEG, 1024x1024, RGB)
============================================================
```

### 4. Unpack a Theme Container
To unpack an `.eats` theme into loose image files and a `recipe.eats` script:
```bash
python tools/theme_pipeline.py unpack themes/rosewood_parlor.eats -o unpacked_rosewood/
```

---

## 3. In-Engine Loading & Fallback Tiers

`ProceduralBackgroundSystem` in C++ resolves and loads background surfaces in a 3-tier fallback architecture:

```
[ Tier 1: Single-File .eats Theme Container ]
  -> Resolves themes/<theme_slug>.eats or custom path
  -> Evaluates Eatscript parameters via eatsbits::eatscript::Evaluator
  -> Verifies "EBKG" magic and decodes Color, Normal, and ORM from RAM via stbi_load_from_memory
  -> Samples AO (R) and Roughness (G) concurrently from ORM texels
       |
       | (if .eats file is not found)
       v
[ Tier 2: Loose Texture Fallback ]
  -> Resolves textures/<Material>_1K-JPG_*.jpg
  -> Synthesizes runtime ORM cache from loose components
       |
       | (if textures folder is absent)
       v
[ Tier 3: Vector Procedural Fallback ]
  -> Pure procedural mathematical shaders (yarn weave, parquet lines, vignette, chandelier gradients)
```

### Runtime API for Custom Themes
```cpp
// Load an arbitrary user-created .eats theme at runtime:
ProceduralBackgroundSystem::instance().loadCustomEatsTheme("themes/my_custom_felt.eats");

// Clear custom theme and return to active TableThemeId:
ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
```

