#!/usr/bin/env python3

"""
Create World Format v3 file data/world-pool-mountain-v3.bin.

    World:              20 m × 20 m, modular
    Cell size:          10 cm
    Cells:              200 × 200
    Plain:              1 m, and at least 5 m from every edge
    Mountain:           cosine dome, 4 m above the plain, radius 2.5 m
    Pool:               circular cut, 1 m radius, 50 cm below the plain

Elevations use the format rule

    elevation = min_height + stored_offset

min_height is the pool floor, 0.5 m. The plain is stored as +500 mm
and the summit as +4500 mm.

The mountain of world-v3-mound.bin rises 2 m. This one rises 4 m
and its base is 2.5 m across the radius, so the outer 5 m of the
world stay exactly flat. The pool sits on that plain, east of the
mountain, still inside the flat margin.

All integers are big-endian.

Usage, from the modules directory:

    ./utils/create_world_v3_pool_mountain.py
"""

import argparse
import math
import struct
from pathlib import Path


WORLD_MAGIC = b"CWLD"
WORLD_VERSION = 3

WORLD_WIDTH_MM = 20_000
WORLD_DEPTH_MM = 20_000
CELL_SIZE_MM = 100

# Pool floor. The plain is 500 mm above this, so a 50 cm hole
# is stored as offset 0 and never goes below min_height.
MIN_HEIGHT_MM = 500
PLAIN_OFFSET_MM = 500
MOUNTAIN_RISE_MM = 4_000
POOL_DEPTH_MM = 500

MOUNTAIN_RADIUS_M = 2.5
POOL_RADIUS_M = 1.0
POOL_EAST_M = 13.6
POOL_NORTH_M = 10.0
FLAT_MARGIN_M = 5.0

MODULARITY = b"MODULAR"
LAYER_MAGIC = b"_LYR"
LAYER_TYPE = b"TYPE_HEIGHTMAP"
LAYER_NAME = b"LYR_HEIGHTMAP"
LAYER_CLOCK = b"CLK_NOEV"
LAYER_STORAGE = b"ST_D"

WORLD_AGE = 0
LAST_SIMULATION_TICK = 0


def write_uint32(file, value):
    """Write an unsigned 32-bit integer in big-endian byte order."""
    file.write(struct.pack(">I", value))


def write_int32(file, value):
    """Write a signed 32-bit integer in big-endian byte order."""
    file.write(struct.pack(">i", value))


def write_uint64(file, value):
    """Write an unsigned 64-bit integer in big-endian byte order."""
    file.write(struct.pack(">Q", value))


def write_fixed_string(file, value, size):
    """Write a fixed-size byte string, padded with zero bytes."""
    if len(value) > size:
        raise ValueError(
            f"Value {value!r} is too long for a {size}-byte field"
        )
    file.write(value)
    file.write(b"\0" * (size - len(value)))


def cell_position_m(column, row):
    """Cell centre in metres. X grows east, Y grows north."""
    x_m = (column + 0.5) * CELL_SIZE_MM / 1000.0
    y_m = (row + 0.5) * CELL_SIZE_MM / 1000.0
    return x_m, y_m


def mountain_weight(x_m, y_m):
    """Cosine weight of the dome, zero outside its radius and in the margin."""
    world_m = WORLD_WIDTH_MM / 1000.0
    if min(x_m, y_m, world_m - x_m, world_m - y_m) < FLAT_MARGIN_M:
        return 0.0

    distance = math.hypot(x_m - world_m / 2.0, y_m - world_m / 2.0)
    if distance >= MOUNTAIN_RADIUS_M:
        return 0.0
    return 0.5 * (1.0 + math.cos(math.pi * distance / MOUNTAIN_RADIUS_M))


def cell_offsets(cell_width, cell_depth):
    """Return one stored offset per cell, row by row from south to north."""
    positions = [
        cell_position_m(column, row)
        for row in range(cell_depth)
        for column in range(cell_width)
    ]
    weights = [mountain_weight(x_m, y_m) for x_m, y_m in positions]
    peak = max(weights)
    if peak == 0.0:
        raise ValueError("mountain weights are all zero")

    offsets = []
    for (x_m, y_m), weight in zip(positions, weights):
        if weight == 0.0:
            rise = 0
        else:
            rise = int(round(MOUNTAIN_RISE_MM * weight / peak))
        offset = PLAIN_OFFSET_MM + rise

        pool_distance = math.hypot(x_m - POOL_EAST_M, y_m - POOL_NORTH_M)
        if pool_distance <= POOL_RADIUS_M:
            if min(x_m, y_m, WORLD_WIDTH_MM / 1000.0 - x_m,
                   WORLD_DEPTH_MM / 1000.0 - y_m) < FLAT_MARGIN_M:
                raise ValueError("pool enters the flat margin")
            if rise != 0:
                raise ValueError("pool overlaps the mountain")
            offset -= POOL_DEPTH_MM

        if offset < 0:
            raise ValueError("offset dropped below min_height")
        offsets.append(offset)
    return offsets


def create_world(output_path, offsets):
    """Write a version 3 world whose heightmap is offsets."""
    with output_path.open("wb") as file:
        file.write(WORLD_MAGIC)
        write_uint32(file, WORLD_VERSION)
        write_uint32(file, WORLD_WIDTH_MM)
        write_uint32(file, WORLD_DEPTH_MM)
        write_fixed_string(file, MODULARITY, 8)
        write_uint64(file, WORLD_AGE)

        file.write(LAYER_MAGIC)
        write_fixed_string(file, LAYER_TYPE, 16)
        write_fixed_string(file, LAYER_NAME, 16)
        file.write(LAYER_CLOCK)
        write_uint64(file, LAST_SIMULATION_TICK)
        file.write(LAYER_STORAGE)

        write_uint32(file, CELL_SIZE_MM)
        write_int32(file, MIN_HEIGHT_MM)

        for offset in offsets:
            write_uint32(file, offset)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Create a 20 m modular world with a 4 m mountain "
            "and a 50 cm circular pool."
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("data/world-pool-mountain-v3.bin"),
        help="output world file (default: data/world-pool-mountain-v3.bin)",
    )
    return parser.parse_args()


def main():
    args = parse_arguments()
    output_path = args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)

    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM
    offsets = cell_offsets(cell_width, cell_depth)
    create_world(output_path, offsets)

    plain = sum(offset == PLAIN_OFFSET_MM for offset in offsets)
    pool = sum(offset == 0 for offset in offsets)
    peak = max(offsets)

    print(f"Created World Format v{WORLD_VERSION}: {output_path}")
    print(f"  World:     {WORLD_WIDTH_MM / 1000:g} m × "
          f"{WORLD_DEPTH_MM / 1000:g} m, modular")
    print(f"  Cells:     {cell_width} × {cell_depth} "
          f"at {CELL_SIZE_MM} mm")
    print(f"  Plain:     {MIN_HEIGHT_MM + PLAIN_OFFSET_MM} mm "
          f"({plain} cells)")
    print(f"  Summit:    {MIN_HEIGHT_MM + peak} mm")
    print(f"  Pool floor:{MIN_HEIGHT_MM} mm ({pool} cells)")


if __name__ == "__main__":
    main()
