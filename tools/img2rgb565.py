#!/usr/bin/env python3
"""
Convert PNG/JPG images to RGB565 C header arrays for SolWearOS.
Usage: python img2rgb565.py input.png output.h --name wallpaper_1 --width 240 --height 280
"""

import sys
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow required. Install with: pip install Pillow")
    sys.exit(1)


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert_image(input_path, output_path, name, width, height):
    img = Image.open(input_path).convert("RGB")
    img = img.resize((width, height), Image.LANCZOS)

    pixels = list(img.getdata())
    rgb565_values = [rgb888_to_rgb565(r, g, b) for r, g, b in pixels]

    with open(output_path, "w") as f:
        f.write(f"#pragma once\n")
        f.write(f"#include <Arduino.h>\n\n")
        f.write(f"// Generated from: {Path(input_path).name}\n")
        f.write(f"// Size: {width}x{height} RGB565\n")
        f.write(f"// Memory: {len(rgb565_values) * 2} bytes\n\n")
        f.write(f"const uint16_t {name}[{len(rgb565_values)}] PROGMEM = {{\n")

        for i in range(0, len(rgb565_values), 16):
            chunk = rgb565_values[i:i + 16]
            line = ", ".join(f"0x{v:04X}" for v in chunk)
            f.write(f"    {line},\n")

        f.write(f"}};\n")

    print(f"Converted {input_path} -> {output_path}")
    print(f"  Array: {name}[{len(rgb565_values)}] ({len(rgb565_values) * 2} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert image to RGB565 C header")
    parser.add_argument("input", help="Input image file (PNG/JPG)")
    parser.add_argument("output", help="Output .h file")
    parser.add_argument("--name", default="image_data", help="C array name")
    parser.add_argument("--width", type=int, default=240, help="Target width")
    parser.add_argument("--height", type=int, default=280, help="Target height")
    args = parser.parse_args()

    convert_image(args.input, args.output, args.name, args.width, args.height)
