# SPDX-License-Identifier: BSD-2-Clause

import struct

PIXEL_CLOCK_HZ = 69750 * 1000
HORZ_ADDR_PX = 1152
HORZ_BLANK_PX = 160
VERT_ADDR_LINES = 864
VERT_BLANK_LINES = 25
HORZ_FRONT_PORCH_PX = 48
HORZ_SPW_PX = 32
VERT_FRONT_PORCH_LINES = 3
VERT_SPW_LINES = 4
HORZ_IMG_MM = 0
VERT_IMG_MM = 0
HORZ_BORDER_PX = 0
VERT_BORDER_LINES = 0

b = b""
b += struct.pack("<H", (PIXEL_CLOCK_HZ + 10000 // 2) // 10000)
b += struct.pack("B", HORZ_ADDR_PX & 0xFF)
b += struct.pack("B", HORZ_BLANK_PX & 0xFF)
b += struct.pack("B", ((HORZ_ADDR_PX & 0xF00) >> 4) | ((HORZ_BLANK_PX & 0xF00) >> 8))
b += struct.pack("B", VERT_ADDR_LINES & 0xFF)
b += struct.pack("B", VERT_BLANK_LINES & 0xFF)
b += struct.pack(
    "B", ((VERT_ADDR_LINES & 0xF00) >> 4) | ((VERT_BLANK_LINES & 0xF00) >> 8)
)
b += struct.pack("B", HORZ_FRONT_PORCH_PX & 0xFF)
b += struct.pack("B", HORZ_SPW_PX & 0xFF)
b += struct.pack("B", ((VERT_FRONT_PORCH_LINES & 0xF) << 4) | (VERT_SPW_LINES & 0xF))
b += struct.pack(
    "B",
    (((HORZ_FRONT_PORCH_PX >> 8) & 3) << 6)
    | (((HORZ_SPW_PX >> 8) & 3) << 4)
    | (((VERT_FRONT_PORCH_LINES >> 4) & 3) << 2)
    | ((VERT_SPW_LINES >> 4) & 3),
)
b += struct.pack("B", HORZ_IMG_MM & 0xFF)
b += struct.pack("B", VERT_IMG_MM & 0xFF)
b += struct.pack("B", ((HORZ_IMG_MM & 0xF00) >> 4) | ((VERT_IMG_MM & 0xF00) >> 8))
b += struct.pack("B", HORZ_BORDER_PX)
b += struct.pack("B", VERT_BORDER_LINES)
# fixed byte: non-interlaced, no stereo, digital +HSync -VSync
b += b"\x1a"

assert len(b) == 18
for i in b:
    print(f"0x{i:02X}, ", end="")
print()
