#!/usr/bin/env python3
"""Convert the supplied rainbow Apple reference into an ESP32 RGB565 asset."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


WIDTH = 96
HEIGHT = 112


def remove_checkerboard(image: Image.Image) -> Image.Image:
    source = image.convert("RGB")
    cleaned = Image.new("RGB", source.size, "white")
    source_pixels = source.load()
    output_pixels = cleaned.load()

    for y in range(source.height):
        for x in range(source.width):
            red, green, blue = source_pixels[x, y]
            chroma = max(red, green, blue) - min(red, green, blue)
            # The reference background is neutral grey/white. Preserve the
            # saturated logo and smoothly fade its antialiased edge to white.
            alpha = max(0.0, min(1.0, (chroma - 4.0) / 24.0))
            output_pixels[x, y] = tuple(
                round(255 + (channel - 255) * alpha)
                for channel in (red, green, blue)
            )
    return cleaned


def write_rgb565(image: Image.Image, destination: Path) -> None:
    payload = bytearray()
    for red, green, blue in image.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        payload.extend((value & 0xFF, value >> 8))
    destination.write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()

    output_directory = args.output_directory
    output_directory.mkdir(parents=True, exist_ok=True)
    cleaned = remove_checkerboard(Image.open(args.source))
    resized = cleaned.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    resized.save(output_directory / "boot_apple.png", optimize=True)
    write_rgb565(resized, output_directory / "boot_apple.rgb565")


if __name__ == "__main__":
    main()
