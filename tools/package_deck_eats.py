#!/usr/bin/env python3
"""
tools/package_deck_eats.py
Packages loose SVG/PNG deck assets into self-contained .eats deck recipes
with compressed binary blobs (deflate + base64).
"""

import os
import sys
import zlib
import base64

def package_svg_deck(svg_path, out_eats_path, name="Classic Victorian", theme_id="classic"):
    if not os.path.exists(svg_path):
        print(f"Error: {svg_path} not found.")
        return False
    with open(svg_path, "rb") as f:
        svg_data = f.read()

    comp_svg = zlib.compress(svg_data, level=9)
    b64_svg = base64.b64encode(comp_svg).decode("ascii")

    content = f'''# SolEnshaded Deck Recipe — {name}
# Connects Hardwood Solitaire 3.0 vector elegance with compressed binary asset embedding.

name        = "{name}"
theme_id    = "{theme_id}"
format      = "svgz"
card_w      = 360
card_h      = 504
author      = "Public Domain CC0"
description = "Lossless Deflate-compressed SVG vector master sheet."

deck_blob = """{b64_svg}"""
'''
    with open(out_eats_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[SolEnshaded] Packaged {svg_path} ({len(svg_data):,} bytes) -> {out_eats_path} ({os.path.getsize(out_eats_path):,} bytes)")
    return True

def package_png_deck(png_path, out_eats_path, name="Steampunk AI", theme_id="steampunk"):
    if not os.path.exists(png_path):
        print(f"Error: {png_path} not found.")
        return False
    with open(png_path, "rb") as f:
        png_data = f.read()

    b64_png = base64.b64encode(png_data).decode("ascii")

    content = f'''# SolEnshaded Deck Recipe — {name}
# Connects Hardwood Solitaire 3.0 tactile style with high-res raster asset embedding.

name        = "{name}"
theme_id    = "{theme_id}"
format      = "png"
card_w      = 360
card_h      = 504
author      = "SolEnshaded / AI"
description = "High-resolution 3240x3024 9x6 PNG master sheet embedded in Eatscript."

deck_blob = """{b64_png}"""
'''
    with open(out_eats_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[SolEnshaded] Packaged {png_path} ({len(png_data):,} bytes) -> {out_eats_path} ({os.path.getsize(out_eats_path):,} bytes)")
    return True

if __name__ == "__main__":
    package_svg_deck("decks/classic_svg.svg", "decks/classic.eats", name="Classic Victorian", theme_id="classic")
    package_png_deck("decks/steampunk.png", "decks/steampunk.eats", name="Steampunk AI", theme_id="steampunk")

