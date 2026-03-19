import argparse
import io
import os

from PIL import Image

THRESHOLD = 128
DEFAULT_SIZE = 96
HOME_ICONS = ("folder", "wifi", "dwarf", "cog")


def svg_to_png_bytes(svg_path, width, height):
    import cairosvg

    with open(svg_path, "rb") as file_handle:
        svg_data = file_handle.read()
    return cairosvg.svg2png(bytestring=svg_data, output_width=width, output_height=height)


def load_image(path, width, height):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".svg":
        png_bytes = svg_to_png_bytes(path, width, height)
        img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    else:
        img = Image.open(path).convert("RGBA")

    img = img.resize((width, height), Image.Resampling.LANCZOS)

    background = Image.new("RGBA", img.size, (255, 255, 255, 255))
    background.paste(img, mask=img.split()[3])
    return background.rotate(90, expand=False)


def image_to_c_array(img, array_name):
    gray = img.convert("L")
    width, height = gray.size
    pixels = gray.tobytes()

    if width % 8 != 0:
        raise ValueError(f"Icon width must be divisible by 8, got {width}")

    packed = []
    for y in range(height):
        row_base = y * width
        for x in range(0, width, 8):
            byte = 0
            for bit_index in range(8):
                value = pixels[row_base + x + bit_index]
                bit = 1 if value >= THRESHOLD else 0
                byte |= bit << (7 - bit_index)
            packed.append(byte)

    c = "#pragma once\n#include <cstdint>\n\n"
    c += f"// size: {width}x{height}\n"
    c += f"static const uint8_t {array_name}[] = {{\n    "
    for index, value in enumerate(packed):
        c += f"0x{value:02X}, "
        if (index + 1) % 16 == 0:
            c += "\n    "
    c = c.rstrip(", \n") + "\n};\n"
    return c


def output_array_name(output_name):
    return output_name.capitalize() + "Icon"


def convert_one(input_path, output_name, width, height, output_dir):
    img = load_image(input_path, width, height)
    c_array = image_to_c_array(img, output_array_name(output_name))

    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f"{output_name}.h")
    with open(output_path, "w", encoding="utf-8") as file_handle:
        file_handle.write(c_array)
    print(f"Wrote {output_path} ({width}x{height})")


def convert_home_icons(project_root, width, height, output_dir):
    icons_dir = os.path.join(project_root, "src", "components", "icons")
    for icon in HOME_ICONS:
        input_path = os.path.join(icons_dir, f"{icon}.png")
        if not os.path.exists(input_path):
            raise FileNotFoundError(f"Missing icon source: {input_path}")
        convert_one(input_path, icon, width, height, output_dir)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Convert image files into packed 1-bit C icon headers.",
    )
    parser.add_argument("input_path", nargs="?", help="Input image path (png/svg/jpg, etc).")
    parser.add_argument("output_name", nargs="?", help="Output base name used for [name].h and [Name]Icon.")
    parser.add_argument("width", nargs="?", type=int, default=DEFAULT_SIZE, help="Target width (default: 96).")
    parser.add_argument("height", nargs="?", type=int, default=DEFAULT_SIZE, help="Target height (default: 96).")
    parser.add_argument(
        "--home",
        action="store_true",
        help="Convert home menu icons (folder/wifi/dwarf/cog) from src/components/icons at default size.",
    )
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_dir = os.path.join(project_root, "src", "components", "icons")

    if args.home:
        convert_home_icons(project_root, args.width, args.height, output_dir)
        return

    if not args.input_path or not args.output_name:
        parser.error("Either use --home or provide: input_path output_name [width] [height]")

    convert_one(args.input_path, args.output_name, args.width, args.height, output_dir)


if __name__ == "__main__":
    main()