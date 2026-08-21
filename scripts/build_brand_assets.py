#!/usr/bin/env python3
"""Build CrossDiTo's firmware and web logos from one source image."""

import base64
import io
from pathlib import Path

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "images" / "crossdito-owl.png"
FIRMWARE_HEADER = ROOT / "src" / "images" / "Logo120.h"
WEB_LOGO = ROOT / "web" / "assets" / "logo.png"
SITE_FAVICON = ROOT / "site" / "public" / "favicon.svg"

INK_THRESHOLD = 128
FIRMWARE_SIZE = (120, 120)
WEB_SIZE = (52, 80)
FAVICON_SIZE = (80, 80)


def load_ink_mask() -> Image.Image:
    """Return a tightly cropped mask where white pixels mean visible ink."""
    source = ImageOps.exif_transpose(Image.open(SOURCE)).convert("L")
    mask = source.point(lambda value: 255 if value < INK_THRESHOLD else 0)
    bounds = mask.getbbox()
    if bounds is None:
        raise ValueError(f"No dark logo pixels found in {SOURCE}")
    return mask.crop(bounds)


def fit_mask(mask: Image.Image, size: tuple[int, int], margin: int) -> Image.Image:
    """Fit a mask without changing its aspect ratio and center it on a canvas."""
    available_width = size[0] - margin * 2
    available_height = size[1] - margin * 2
    if available_width <= 0 or available_height <= 0:
        raise ValueError("Logo margin leaves no drawable area")

    scale = min(available_width / mask.width, available_height / mask.height)
    scaled_size = (
        max(1, round(mask.width * scale)),
        max(1, round(mask.height * scale)),
    )
    fitted = mask.resize(scaled_size, Image.Resampling.LANCZOS)
    canvas = Image.new("L", size, 0)
    offset = ((size[0] - fitted.width) // 2, (size[1] - fitted.height) // 2)
    canvas.paste(fitted, offset)
    return canvas


def write_firmware_header(mask: Image.Image) -> None:
    # The display stores portrait artwork rotated counter-clockwise in panel
    # memory. This matches all existing drawImage() branding assets.
    upright = mask.point(lambda value: 0 if value >= INK_THRESHOLD else 255)
    stored = upright.rotate(90, expand=True)

    packed = bytearray()
    for y in range(stored.height):
        for x in range(0, stored.width, 8):
            value = 0
            for bit in range(8):
                if stored.getpixel((x + bit, y)) >= INK_THRESHOLD:
                    value |= 1 << (7 - bit)
            packed.append(value)

    expected_size = FIRMWARE_SIZE[0] * FIRMWARE_SIZE[1] // 8
    if len(packed) != expected_size:
        raise ValueError(f"Firmware logo is {len(packed)} bytes; expected {expected_size}")

    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "// Generated from src/images/crossdito-owl.png by scripts/build_brand_assets.py.",
        "// CrossDiTo minimal owl logo, 120x120px, stored for the portrait display orientation.",
        "static const uint8_t Logo120[] = {",
    ]
    row_bytes = FIRMWARE_SIZE[0] // 8
    for offset in range(0, len(packed), row_bytes):
        row = ", ".join(f"0x{value:02x}" for value in packed[offset : offset + row_bytes])
        lines.append(f"    {row},")
    lines.extend(
        [
            "};",
            'static_assert(sizeof(Logo120) == 1800, "Logo120 must be exactly 120x120 / 8 bytes");',
            "",
        ]
    )
    FIRMWARE_HEADER.write_text("\n".join(lines), encoding="utf-8")


def rgba_mark(mask: Image.Image, color: tuple[int, int, int]) -> Image.Image:
    mark = Image.new("RGBA", mask.size, (*color, 0))
    mark.putalpha(mask)
    return mark


def write_web_logo(mask: Image.Image) -> None:
    rgba_mark(mask, (255, 255, 255)).save(WEB_LOGO, optimize=True)


def write_favicon(mask: Image.Image) -> None:
    favicon_png = io.BytesIO()
    rgba_mark(mask, (0, 0, 0)).save(favicon_png, format="PNG", optimize=True)
    encoded = base64.b64encode(favicon_png.getvalue()).decode("ascii")
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 80 80">
  <style>
    @media (prefers-color-scheme: dark) {{ image {{ filter: invert(1); }} }}
  </style>
  <image href="data:image/png;base64,{encoded}" width="80" height="80" />
</svg>
'''
    SITE_FAVICON.write_text(svg, encoding="utf-8")


def main() -> None:
    ink = load_ink_mask()
    write_firmware_header(fit_mask(ink, FIRMWARE_SIZE, margin=12))
    write_web_logo(fit_mask(ink, WEB_SIZE, margin=3))
    write_favicon(fit_mask(ink, FAVICON_SIZE, margin=8))
    print(f"Built {FIRMWARE_HEADER.relative_to(ROOT)}")
    print(f"Built {WEB_LOGO.relative_to(ROOT)}")
    print(f"Built {SITE_FAVICON.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
