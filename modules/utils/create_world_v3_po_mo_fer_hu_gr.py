#!/usr/bin/env python3

"""
Add humidity, fertility, and grass to the pool world.

Reads data/world-pool-mountain-v3.bin and writes
data/world-po-mo-fer-hu-gr-v3.bin. The source file is not modified.

Humidity is a 10 cm grid of uint16 zeros (storage ST16). Fertility
is a 10 cm grid of uint8 zeros. Grass is the same uint8 grid: 255 at
the pool edge and inside it, falling linearly to 0 at 5 m outside
that edge. All three use clock CLK_0016.

The pool matches create_world_v3_pool_mountain.py: centre 13.6 m east
and 10.0 m north, radius 1 m.

Usage, from the modules directory:

    ./utils/create_world_v3_po_mo_fer_hu_gr.py
"""

import math
import struct
from pathlib import Path


LAYER_MAGIC = b"_LYR"
STORAGE_U8 = b"ST_8"
STORAGE_U16 = b"ST16"
CLOCK_EVERY_MINUTE = b"CLK_0016"
CELL_SIZE_MM = 100

LAYERS = (
    (b"TYPE_HUMIDITY", b"LYR_HUMIDITY"),
    (b"TYPE_FERTILITY", b"LYR_FERTILITY"),
    (b"TYPE_GRASS", b"LYR_GRASS"),
)

POOL_EAST_MM = 13_600
POOL_NORTH_MM = 10_000
POOL_RADIUS_MM = 1_000
GRASS_REACH_MM = 5_000
GRASS_MAX = 255


def padded(text, width):
    if len(text) > width:
        raise ValueError(text)
    return text + bytes(width - len(text))


def grass_height(column, row):
    east = column * CELL_SIZE_MM + CELL_SIZE_MM / 2
    north = row * CELL_SIZE_MM + CELL_SIZE_MM / 2
    distance = math.hypot(east - POOL_EAST_MM, north - POOL_NORTH_MM)
    beyond = distance - POOL_RADIUS_MM

    if beyond <= 0:
        return GRASS_MAX
    if beyond >= GRASS_REACH_MM:
        return 0
    return int(round(GRASS_MAX * (1.0 - beyond / GRASS_REACH_MM)))


def layer_block(type_name, layer_name, columns, rows):
    block = bytearray()
    block += LAYER_MAGIC
    block += padded(type_name, 16)
    block += padded(layer_name, 16)
    block += CLOCK_EVERY_MINUTE
    block += struct.pack(">Q", 0)
    if type_name == b"TYPE_HUMIDITY":
        block += STORAGE_U16
    else:
        block += STORAGE_U8
    block += struct.pack(">I", CELL_SIZE_MM)
    block += struct.pack(">i", 0)

    if type_name == b"TYPE_GRASS":
        values = bytearray(columns * rows)
        for row in range(rows):
            for column in range(columns):
                values[row * columns + column] = grass_height(column, row)
        block += values
    elif type_name == b"TYPE_HUMIDITY":
        block += bytes(columns * rows * 2)
    else:
        block += bytes(columns * rows)
    return block


def main():
    modules = Path(__file__).resolve().parents[1]
    source = modules / "data" / "world-pool-mountain-v3.bin"
    target = modules / "data" / "world-po-mo-fer-hu-gr-v3.bin"
    data = source.read_bytes()
    width = struct.unpack(">I", data[8:12])[0]
    depth = struct.unpack(">I", data[12:16])[0]

    if width % CELL_SIZE_MM != 0 or depth % CELL_SIZE_MM != 0:
        raise SystemExit("world dimensions are not a multiple of 10 cm")

    columns = width // CELL_SIZE_MM
    rows = depth // CELL_SIZE_MM
    output = bytearray(data)

    for type_name, layer_name in LAYERS:
        output += layer_block(type_name, layer_name, columns, rows)

    target.write_bytes(output)
    grass = output[-columns * rows:]
    print(f"wrote {target}")
    print(f"grass cells {sum(value > 0 for value in grass)} of {columns * rows}")
    print(f"grass height {min(grass)}..{max(grass)}")
    print(f"size {len(output)} bytes")


if __name__ == "__main__":
    main()
