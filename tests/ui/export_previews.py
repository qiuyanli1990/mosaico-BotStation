#!/usr/bin/env python3
"""Export the real LVGL RGB565 test frames as PNGs without extra packages."""

import binascii
from pathlib import Path
import struct
import sys
import zlib


def load_frame(path):
    header, size, maximum, pixels = path.read_bytes().split(b"\n", 3)
    assert header == b"P6" and size == b"480 480" and maximum == b"255"
    assert len(pixels) == 480 * 480 * 3
    return pixels


def save_png(path, width, height, pixels):
    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)
        )

    rows = b"".join(
        b"\0" + pixels[y * width * 3 : (y + 1) * width * 3]
        for y in range(height)
    )
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def contact_sheet(directory, names, destination):
    padding = 16
    width = height = 480 * 2 + padding * 3
    pixels = bytearray(bytes((255, 248, 239)) * width * height)
    for index, name in enumerate(names):
        frame = load_frame(directory / f"{name}.ppm")
        x = padding + (index % 2) * (480 + padding)
        y = padding + (index // 2) * (480 + padding)
        for row in range(480):
            offset = ((y + row) * width + x) * 3
            pixels[offset : offset + 480 * 3] = frame[row * 480 * 3 : (row + 1) * 480 * 3]
    save_png(destination, width, height, pixels)


def main():
    directory = Path(sys.argv[1])
    for frame in directory.glob("*.ppm"):
        save_png(frame.with_suffix(".png"), 480, 480, load_frame(frame))
    contact_sheet(directory, ("listening", "thinking", "speaking", "error"), directory / "dashboard-states.png")
    contact_sheet(directory, ("pairing", "wifi", "conversation", "password"), directory / "setup-pages.png")
    contact_sheet(directory, ("brightness", "battery-charging", "battery-low", "battery-unavailable"),
                  directory / "display-controls.png")
    print(f"LVGL previews: {directory}")


if __name__ == "__main__":
    main()
