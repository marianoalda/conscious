#!/usr/bin/env python3

"""
Create a test World Format v1 file.

The generated world is intentionally small:

    World dimensions:       10 m × 10 m
    Terrain cell size:       1 m × 1 m
    Terrain cells:          10 × 10 = 100
    Minimum height:         1 m
    Height increment:       1 mm per column

Each row of the heightmap contains:

    1000, 1001, 1002, ..., 1009 mm

All binary integers are written in big-endian byte order.

Usage:

    ./utils/create_world_v1.py

By default, the output file is:

    data/world.bin

Use --output to select another file:

    ./utils/create_world_v1.py --output /tmp/test-world.bin

Use --help for a complete description.
"""

import argparse
import struct
from pathlib import Path


WORLD_MAGIC = b"CWLD"
WORLD_VERSION = 1

WORLD_WIDTH_MM = 10_000
WORLD_DEPTH_MM = 10_000

CELL_SIZE_MM = 1_000
MIN_HEIGHT_MM = 1_000
HEIGHT_STEP_MM = 1

LAYER_MAGIC = b"_LYR"
LAYER_TYPE = b"TYPE_HEIGHTMAP"
LAYER_NAME = b"LYR_HEIGHTMAP"
LAYER_EVOLUTION = b"EV_N"
LAYER_STORAGE = b"ST_D"


def write_uint32(file, value):
    """Write an unsigned 32-bit integer in big-endian byte order."""
    file.write(struct.pack(">I", value))


def write_int32(file, value):
    """Write a signed 32-bit integer in big-endian byte order."""
    file.write(struct.pack(">i", value))


def write_fixed_string(file, value, size):
    """
    Write a fixed-size byte string, padded with zero bytes on the right.
    """
    if len(value) > size:
        raise ValueError(
            f"Value {value!r} is too long for a {size}-byte field"
        )

    file.write(value)
    file.write(b"\0" * (size - len(value)))


def create_world(output_path):
    """Create the World Format v1 test file."""
    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM

    with output_path.open("wb") as file:
        # World header
        file.write(WORLD_MAGIC)
        write_uint32(file, WORLD_VERSION)

        # World metadata
        write_uint32(file, WORLD_WIDTH_MM)
        write_uint32(file, WORLD_DEPTH_MM)

        # Heightmap layer
        file.write(LAYER_MAGIC)
        write_fixed_string(file, LAYER_TYPE, 16)
        write_fixed_string(file, LAYER_NAME, 16)
        file.write(LAYER_EVOLUTION)
        file.write(LAYER_STORAGE)

        write_uint32(file, CELL_SIZE_MM)
        write_int32(file, MIN_HEIGHT_MM)

        # Heightmap data.
        #
        # The height increases by 1 mm from left to right.
        # Every row therefore contains:
        #
        #   1000, 1001, ..., 1009
        #
        # and all rows are identical.
        for row in range(cell_depth):
            for column in range(cell_width):
                height = MIN_HEIGHT_MM + column * HEIGHT_STEP_MM
                write_uint32(file, height)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Create a small World Format v1 test file containing "
            "a 10 m × 10 m dense heightmap."
        ),
        epilog=(
            "The generated terrain has 100 cells (10 × 10), "
            "a 1 m cell size, a minimum height of 1 m, and "
            "a 1 mm height increase from left to right."
        ),
    )

    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("data/world.bin"),
        help=(
            "output world file "
            "(default: data/world.bin)"
        ),
    )

    return parser.parse_args()


def main():
    args = parse_arguments()

    output_path = args.output

    output_path.parent.mkdir(parents=True, exist_ok=True)

    create_world(output_path)

    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM
    cell_count = cell_width * cell_depth

    print(f"Created World Format v{WORLD_VERSION}: {output_path}")
    print(f"  World:       {WORLD_WIDTH_MM / 1000:g} m × "
          f"{WORLD_DEPTH_MM / 1000:g} m")
    print(f"  Cell size:   {CELL_SIZE_MM / 1000:g} m")
    print(f"  Cells:       {cell_width} × {cell_depth} = {cell_count}")
    print(f"  Min height:  {MIN_HEIGHT_MM / 1000:g} m")
    print(f"  Height step: {HEIGHT_STEP_MM} mm per column")


if __name__ == "__main__":
    main()