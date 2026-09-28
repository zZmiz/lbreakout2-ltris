
#!/usr/bin/env python3
"""Prepare dependency-free RGB565 SD assets for the LGames Port ports."""

import argparse
import shutil
import struct
import zlib
from pathlib import Path

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c

def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, raw, palette = 8, bytearray(), None
    while pos < len(data):
        size = struct.unpack_from(">I", data, pos)[0]
        kind, payload = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + size]
        pos += size + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if depth not in (1, 2, 4, 8) or (depth != 8 and color != 3) or compression or filtering or interlace:
                raise ValueError("unsupported PNG depth, color type, or interlace")
        elif kind == b"PLTE":
            palette = [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]
        elif kind == b"IDAT":
            raw.extend(payload)
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if not channels:
        raise ValueError(f"unsupported PNG color type {color}")
    packed = zlib.decompress(raw)
    stride = (width * channels * depth + 7)
    filter_bpp = max(1, (channels * depth + 7)
    offset, previous, pixels = 0, bytearray(stride), []
    for _ in range(height):
        filter_type, offset = packed[offset], offset + 1
        scan = bytearray(packed[offset:offset + stride])
        offset += stride
        for i, value in enumerate(scan):
            left = scan[i - filter_bpp] if i >= filter_bpp else 0
            up = previous[i]
            upper_left = previous[i - filter_bpp] if i >= filter_bpp else 0
            if filter_type == 1:
                scan[i] = (value + left) & 255
            elif filter_type == 2:
                scan[i] = (value + up) & 255
            elif filter_type == 3:
                scan[i] = (value + ((left + up) >> 1)) & 255
            elif filter_type == 4:
                scan[i] = (value + paeth(left, up, upper_left)) & 255
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        for x in range(width):
            if depth < 8:
                bit = x * depth
                index = (scan[bit
                pixels.append(palette[index])
                continue
            sample = scan[x * channels:(x + 1) * channels]
            if color == 0:
                pixels.append((sample[0], sample[0], sample[0]))
            elif color == 3:
                pixels.append(palette[sample[0]])
            else:
                pixels.append(tuple(sample[:3]))
        previous = scan
    return width, height, pixels

def read_bmp(path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, signed_height = struct.unpack_from("<ii", data, 18)
    planes, depth = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    if planes != 1 or depth not in (1, 4, 8, 16, 24, 32) or compression not in (0, 3):
        raise ValueError(f"unsupported BMP depth/compression {depth}/{compression}")
    if compression == 3:
        mask_offset = 14 + 40
        red_mask, green_mask, blue_mask = struct.unpack_from("<III", data, mask_offset)

        def masked(value, mask):
            shift = (mask & -mask).bit_length() - 1
            maximum = mask >> shift
            return ((value & mask) >> shift) * 255
    height, bottom_up = abs(signed_height), signed_height > 0
    palette = []
    if depth <= 8:
        colors = struct.unpack_from("<I", data, 46)[0] or (1 << depth)
        start = 14 + header_size
        palette = [(data[i + 2], data[i + 1], data[i]) for i in range(start, start + colors * 4, 4)]
    row_size = ((width * depth + 31)
    pixels = []
    for y in range(height):
        source_y = height - 1 - y if bottom_up else y
        row = pixel_offset + source_y * row_size
        for x in range(width):
            if depth <= 8:
                bit = x * depth
                packed = data[row + bit
                index = (packed >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
                pixels.append(palette[index])
            elif compression == 3:
                size = depth
                value = int.from_bytes(data[row + x * size:row + (x + 1) * size], "little")
                pixels.append((masked(value, red_mask), masked(value, green_mask), masked(value, blue_mask)))
            else:
                i = row + x * (depth
                pixels.append((data[i + 2], data[i + 1], data[i]))
    return width, height, pixels

def convert_image(source, destination):
    width, height, pixels = read_png(source) if source.suffix.lower() == ".png" else read_bmp(source)
    if width > 65535 or height > 65535:
        raise ValueError("image dimensions exceed R565 format")
    payload = bytearray(b"R565" + struct.pack("<HH", width, height))
    for red, green, blue in pixels:
        payload += struct.pack("<H", ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)

def copy_tree(source, destination):
    for item in source.rglob("*"):
        if not item.is_file() or item.name.startswith("Makefile"):
            continue
        relative = item.relative_to(source)
        output = destination / relative
        if item.suffix.lower() in (".png", ".bmp"):
            convert_image(item, output.with_name(output.name + ".r565"))
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, output)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("sdcard"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    games = args.output.resolve() / "games"
    ltris = games / "ltris"
    breakout = games / "lbreakout2"
    copy_tree(root / "upstream/ltris/src/gfx", ltris / "gfx")
    shutil.copy2(root / "upstream/ltris/src/figures", ltris / "figures")
    copy_tree(root / "upstream/lbreakout2/client/gfx", breakout / "gfx")
    copy_tree(root / "upstream/lbreakout2/client/gui_theme", breakout / "gui_theme")
    copy_tree(root / "upstream/lbreakout2/client/levels", breakout / "levels")
    print(f"Prepared SD data under {games}")

if __name__ == "__main__":
    main()
