#!/usr/bin/env python3

"""
Create a World Format v3 file with a central mound.

The world matches the current development world except for the
heightmap geometry:

    World dimensions:       10 m × 10 m
    Terrain cell size:       1 m × 1 m
    Terrain cells:          10 × 10 = 100
    Modularity:             CLOSED
    World age:              0
    Heightmap clock:        CLK_NOEV
    Last simulation tick:   0
    Minimum height:         1 m

Elevations are stored in millimetres, the unit defined by the
world format. As in the existing ramp world, each heightmap cell
holds the elevation itself.

The border cells are flat at 1000 mm. Interior cells form a smooth
mound whose four central cells rise 2000 mm above that plain, so
the centre of the world is 3 m above the reference level.

All binary integers are written in big-endian byte order.

Usage, from the modules directory:

    ./utils/create_world_v3_mound.py

By default, the output file is:

    data/world-v3-mound.bin
"""

import argparse
import math
import struct
from pathlib import Path


WORLD_MAGIC = b"CWLD"
WORLD_VERSION = 3

WORLD_WIDTH_MM = 10_000
WORLD_DEPTH_MM = 10_000

CELL_SIZE_MM = 1_000
MIN_HEIGHT_MM = 1_000
BORDER_ELEVATION_MM = 1_000
MOUND_RISE_MM = 2_000

MODULARITY = b"CLOSED"
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


def cell_elevations(cell_width, cell_depth):
    """
    Return one elevation in millimetres per cell, row by row.

    Border cells stay at BORDER_ELEVATION_MM. Interior samples use
    a separable cosine dome about the world centre. The weights are
    scaled so the highest cells, the four around the centre, are
    exactly MOUND_RISE_MM above the plain.
    """
    weights = []

    for row in range(cell_depth):
        for column in range(cell_width):
            on_border = (
                row == 0
                or column == 0
                or row == cell_depth - 1
                or column == cell_width - 1
            )
            if on_border:
                weights.append(0.0)
                continue

            x_mm = (column + 0.5) * CELL_SIZE_MM
            y_mm = (row + 0.5) * CELL_SIZE_MM
            half_width = WORLD_WIDTH_MM / 2
            half_depth = WORLD_DEPTH_MM / 2
            inner_half_width = half_width - CELL_SIZE_MM
            inner_half_depth = half_depth - CELL_SIZE_MM

            u = (x_mm - half_width) / inner_half_width
            v = (y_mm - half_depth) / inner_half_depth

            if abs(u) >= 1.0 or abs(v) >= 1.0:
                weights.append(0.0)
                continue

            fx = 0.5 * (1.0 + math.cos(math.pi * u))
            fy = 0.5 * (1.0 + math.cos(math.pi * v))
            weights.append(fx * fy)

    peak = max(weights)
    if peak == 0.0:
        raise ValueError("mound weights are all zero")

    elevations = []
    for weight in weights:
        if weight == 0.0:
            elevations.append(BORDER_ELEVATION_MM)
        else:
            rise = int(round(MOUND_RISE_MM * weight / peak))
            elevations.append(BORDER_ELEVATION_MM + rise)

    return elevations


def create_world(output_path):
    """Create the World Format v3 mound file."""
    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM
    elevations = cell_elevations(cell_width, cell_depth)

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

        for elevation in elevations:
            write_uint32(file, elevation)

    return elevations


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Create a World Format v3 world whose heightmap is a "
            "2 m mound on a flat 1 m plain."
        ),
    )

    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("data/world-v3-mound.bin"),
        help=(
            "output world file "
            "(default: data/world-v3-mound.bin)"
        ),
    )

    return parser.parse_args()


def main():
    args = parse_arguments()
    output_path = args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)

    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM
    elevations = create_world(output_path)

    print(f"Created World Format v{WORLD_VERSION}: {output_path}")
    print(f"  World:      {WORLD_WIDTH_MM / 1000:g} m × "
          f"{WORLD_DEPTH_MM / 1000:g} m")
    print(f"  Cell size:  {CELL_SIZE_MM / 1000:g} m")
    print(f"  Cells:      {cell_width} × {cell_depth}")
    print(f"  Border:     {BORDER_ELEVATION_MM} mm")
    print(f"  Centre:     {max(elevations)} mm")
    print("  Heightmap (mm):")
    for row in range(cell_depth):
        start = row * cell_width
        cells = elevations[start:start + cell_width]
        print("   ", " ".join(f"{value:4d}" for value in cells))


if __name__ == "__main__":
    main()
