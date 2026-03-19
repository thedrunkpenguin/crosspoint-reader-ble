#!/usr/bin/env python3
"""
Convert PDF files to XTC (1-bit) for CrossPoint Reader.

Requirements:
  pip install pymupdf pillow

Example:
  python3 scripts/pdf_to_xtc.py input.pdf output.xtc
  python3 scripts/pdf_to_xtc.py input.pdf output.xtc --fit crop --threshold 170
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Tuple

try:
    import fitz  # PyMuPDF
except Exception:  # pragma: no cover
    fitz = None

try:
    from PIL import Image, ImageOps
except Exception:  # pragma: no cover
    Image = None
    ImageOps = None


# XTC constants (little-endian)
XTC_MAGIC = 0x00435458  # "XTC\0"
XTG_MAGIC = 0x00475458  # "XTG\0" page payload


@dataclass
class PageEntry:
    offset: int
    data_size: int
    width: int
    height: int


def require_dependencies() -> None:
    missing: List[str] = []
    if fitz is None:
        missing.append("pymupdf")
    if Image is None or ImageOps is None:
        missing.append("pillow")

    if missing:
        pkg_str = " ".join(missing)
        print(f"Missing dependency/dependencies: {pkg_str}", file=sys.stderr)
        print("Install with:", file=sys.stderr)
        print(f"  python3 -m pip install {pkg_str}", file=sys.stderr)
        sys.exit(2)


def render_page_to_target(page, target_w: int, target_h: int, fit_mode: str) -> "Image.Image":
    rect = page.rect
    src_w = float(rect.width)
    src_h = float(rect.height)

    if src_w <= 0 or src_h <= 0:
        raise ValueError("Invalid PDF page dimensions")

    sx = target_w / src_w
    sy = target_h / src_h
    scale = min(sx, sy) if fit_mode == "fit" else max(sx, sy)

    matrix = fitz.Matrix(scale, scale)
    pix = page.get_pixmap(matrix=matrix, alpha=False, colorspace=fitz.csGRAY)

    # Rendered grayscale image
    rendered = Image.frombytes("L", (pix.width, pix.height), pix.samples)

    if fit_mode == "fit":
        # Letterbox on white background
        canvas = Image.new("L", (target_w, target_h), 255)
        paste_x = (target_w - rendered.width) // 2
        paste_y = (target_h - rendered.height) // 2
        canvas.paste(rendered, (paste_x, paste_y))
        return canvas

    # crop mode: center-crop to exact target
    crop_x = max(0, (rendered.width - target_w) // 2)
    crop_y = max(0, (rendered.height - target_h) // 2)
    return rendered.crop((crop_x, crop_y, crop_x + target_w, crop_y + target_h))


def pack_1bit_msb(image_l: "Image.Image", threshold: int) -> bytes:
    width, height = image_l.size
    if width % 8 != 0:
        raise ValueError(f"Width must be divisible by 8, got {width}")

    # Improve contrast slightly before thresholding
    img = ImageOps.autocontrast(image_l)
    pix = img.tobytes()

    out = bytearray((width // 8) * height)
    out_idx = 0
    for y in range(height):
        row_base = y * width
        for xb in range(0, width, 8):
            value = 0
            for bit in range(8):
                p = pix[row_base + xb + bit]
                # XTC expects: 0 = black, 1 = white
                is_white = 1 if p >= threshold else 0
                value |= is_white << (7 - bit)
            out[out_idx] = value
            out_idx += 1
    return bytes(out)


def xtg_page_chunk(width: int, height: int, bitmap_1bit: bytes) -> bytes:
    # XtgPageHeader (22 bytes): <IHHBBIQ
    header = struct.pack(
        "<IHHBBIQ",
        XTG_MAGIC,
        width,
        height,
        0,  # colorMode
        0,  # compression
        len(bitmap_1bit),
        0,  # md5 (unused)
    )
    return header + bitmap_1bit


def write_xtc(output_path: Path, pages: Iterable[bytes], page_count: int, width: int, height: int) -> None:
    # Header layout (56 bytes)
    # <I B B H B B B B I Q Q Q Q I I
    # Note: the 8th byte is a pad to keep currentPage aligned with existing struct layout.

    page_table_offset = 56
    page_table_size = page_count * 16  # each PageTableEntry is 16 bytes
    data_offset = page_table_offset + page_table_size

    entries: List[PageEntry] = []

    with output_path.open("wb") as out:
        # Reserve header + page table
        out.write(b"\x00" * data_offset)

        for page_chunk in pages:
            page_off = out.tell()
            out.write(page_chunk)
            entries.append(PageEntry(offset=page_off, data_size=len(page_chunk), width=width, height=height))

        # Write page table
        out.seek(page_table_offset)
        for e in entries:
            out.write(struct.pack("<Q I H H", e.offset, e.data_size, e.width, e.height))

        # Write header
        out.seek(0)
        header = struct.pack(
            "<I B B H B B B B I Q Q Q Q I I",
            XTC_MAGIC,
            1,   # version major
            0,   # version minor
            page_count,
            0,   # readDirection
            0,   # hasMetadata
            0,   # hasThumbnails
            0,   # hasChapters
            1,   # currentPage (1-based)
            0,   # metadataOffset
            page_table_offset,
            data_offset,
            0,   # thumbOffset
            0,   # chapterOffset
            0,   # padding
        )
        out.write(header)


def convert_pdf_to_xtc(
    input_pdf: Path,
    output_xtc: Path,
    width: int,
    height: int,
    threshold: int,
    fit_mode: str,
) -> None:
    doc = fitz.open(str(input_pdf))
    page_count = len(doc)
    if page_count == 0:
        raise RuntimeError("PDF has no pages")

    def page_chunks() -> Iterable[bytes]:
        for idx in range(page_count):
            page = doc[idx]
            img_l = render_page_to_target(page, width, height, fit_mode)
            bitmap = pack_1bit_msb(img_l, threshold)
            yield xtg_page_chunk(width, height, bitmap)
            if (idx + 1) % 25 == 0:
                print(f"Processed {idx + 1}/{page_count} pages...")

    write_xtc(output_xtc, page_chunks(), page_count, width, height)
    doc.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert PDF to CrossPoint XTC format")
    parser.add_argument("input_pdf", type=Path, help="Input PDF file")
    parser.add_argument("output_xtc", type=Path, help="Output .xtc file")
    parser.add_argument("--width", type=int, default=480, help="Target page width (default: 480)")
    parser.add_argument("--height", type=int, default=800, help="Target page height (default: 800)")
    parser.add_argument(
        "--fit",
        choices=["fit", "crop"],
        default="fit",
        help="fit = preserve full page with margins, crop = fill screen (default: fit)",
    )
    parser.add_argument("--threshold", type=int, default=176, help="B/W threshold 0-255 (default: 176)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    require_dependencies()

    if not args.input_pdf.exists():
        raise FileNotFoundError(f"Input not found: {args.input_pdf}")

    if args.threshold < 0 or args.threshold > 255:
        raise ValueError("--threshold must be between 0 and 255")

    args.output_xtc.parent.mkdir(parents=True, exist_ok=True)

    print(f"Converting {args.input_pdf} -> {args.output_xtc}")
    print(f"Mode: {args.fit}, target: {args.width}x{args.height}, threshold: {args.threshold}")

    convert_pdf_to_xtc(
        input_pdf=args.input_pdf,
        output_xtc=args.output_xtc,
        width=args.width,
        height=args.height,
        threshold=args.threshold,
        fit_mode=args.fit,
    )

    print("Done.")


if __name__ == "__main__":
    main()
