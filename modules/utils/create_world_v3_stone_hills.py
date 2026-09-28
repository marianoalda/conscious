#!/usr/bin/env python3

"""
Create a 100 m × 100 m modular world with stone hills, a pool, and grass.

    World:              100 m × 100 m, modular
    Cell size:          10 cm (heightmap, water, humidity, fertility, grass, grass age)
    Cells:              1000 × 1000
    Plain:              1 m elevation, 12 m flat on every edge so the
                        terrain matches across a modular wrap
    Hills:              several steep-sided stone mesas in the centre
    Pool:               1 m radius on the eastern inner plain, 50 cm deep
    Static water:       50 cm in that cut only
    Grass:              255 on the pool, falling to 0 at 5 m past the rim
    Grass age:          empty, CLK_0026
    Humidity:           saturated on standing water, dry elsewhere
    Fertility:          empty
    Diffuse light:      one cell covering the world, CLK_0018

Elevation:

    elevation = min_height + stored_offset

min_height is the pool floor, 0.5 m. The plain is stored as +500 mm.

Usage, from the modules directory:

    ./utils/create_world_v3_stone_hills.py
"""

import argparse
import math
import struct
from pathlib import Path


WORLD_MAGIC = b"CWLD"
WORLD_VERSION = 3

WORLD_WIDTH_MM = 100_000
WORLD_DEPTH_MM = 100_000
CELL_SIZE_MM = 100

MIN_HEIGHT_MM = 500
PLAIN_OFFSET_MM = 500
POOL_DEPTH_MM = 500

POOL_RADIUS_M = 1.0
POOL_EAST_M = 82.0
POOL_NORTH_M = 50.0
GRASS_REACH_M = 5.0
FLAT_MARGIN_M = 12.0

MODULARITY = b"MODULAR"
LAYER_MAGIC = b"_LYR"
STORAGE_DENSE = b"ST_D"
STORAGE_U8 = b"ST_8"
STORAGE_U16 = b"ST16"

CLOCK_NOEV = b"CLK_NOEV"
CLOCK_DIFFLIGHT = b"CLK_0018"
CLOCK_U8 = b"CLK_0016"
CLOCK_GRASS_AGE = b"CLK_0026"

WATER_MIN_DEPTH_MM = 0
WATER_POOL_DEPTH_MM = 500
LIGHT_CELL_MM = WORLD_WIDTH_MM
LIGHT_MAX_IRRADIANCE = 1000

WORLD_AGE = 0
LAST_SIMULATION_TICK = 0
HUMIDITY_SATURATED = 65535
GRASS_MAX = 255

# Centre east, north, half-width, half-depth (m), rotation (deg),
# corner radius (m), rise above the plain (mm), cliff width (m).
HILLS = (
    (38.0, 56.0, 6.2, 4.0, 22.0, 0.35, 8200, 0.40),
    (51.0, 64.0, 3.4, 7.0, -28.0, 0.22, 11800, 0.32),
    (58.5, 50.0, 4.8, 3.1, 12.0, 0.18, 5400, 0.28),
    (44.0, 41.5, 3.6, 3.4, 40.0, 0.20, 9600, 0.30),
    (31.5, 47.0, 7.5, 2.2, -12.0, 0.25, 6100, 0.35),
    (49.0, 48.5, 2.1, 2.4, 8.0, 0.15, 7300, 0.25),
    (54.0, 38.0, 2.8, 1.7, 55.0, 0.12, 4500, 0.22),
)


def write_uint16(file, value):
    file.write(struct.pack(">H", value))


def write_uint32(file, value):
    file.write(struct.pack(">I", value))


def write_int32(file, value):
    file.write(struct.pack(">i", value))


def write_uint64(file, value):
    file.write(struct.pack(">Q", value))


def write_fixed_string(file, value, size):
    if len(value) > size:
        raise ValueError(f"Value {value!r} is too long for a {size}-byte field")
    file.write(value)
    file.write(b"\0" * (size - len(value)))


def cell_centre_m(column, row):
    x_m = (column + 0.5) * CELL_SIZE_MM / 1000.0
    y_m = (row + 0.5) * CELL_SIZE_MM / 1000.0
    return x_m, y_m


def sd_rounded_box(local_east, local_north, half_east, half_north, corner):
    """Signed distance to a rounded rectangle. Negative is inside."""
    ax = abs(local_east) - half_east + corner
    ay = abs(local_north) - half_north + corner
    outside_east = max(ax, 0.0)
    outside_north = max(ay, 0.0)
    return (
        math.hypot(outside_east, outside_north)
        + min(max(ax, ay), 0.0)
        - corner
    )


def outline_nudge(x_m, y_m):
    """A few tenths of a metre of crenellation, not a smooth dome."""
    return (
        0.28 * math.sin(x_m * 2.31 + y_m * 1.73)
        + 0.16 * math.sin(x_m * 5.11 - y_m * 3.07)
        + 0.10 * math.sin(x_m * 8.40 + y_m * 6.20)
    )


def hill_rise_mm(x_m, y_m):
    rise = 0
    for (
        centre_east,
        centre_north,
        half_east,
        half_north,
        angle_deg,
        corner,
        height_mm,
        cliff_m,
    ) in HILLS:
        angle = math.radians(angle_deg)
        dx = x_m - centre_east
        dy = y_m - centre_north
        local_east = dx * math.cos(angle) + dy * math.sin(angle)
        local_north = -dx * math.sin(angle) + dy * math.cos(angle)
        signed = sd_rounded_box(
            local_east,
            local_north,
            half_east,
            half_north,
            corner,
        )
        signed -= outline_nudge(x_m, y_m)
        if signed >= 0.0:
            continue
        if signed <= -cliff_m:
            weight = 1.0
        else:
            weight = -signed / cliff_m
        cell_rise = int(round(height_mm * weight))
        if cell_rise > rise:
            rise = cell_rise
    return rise


def grass_height_mm(x_m, y_m):
    beyond = math.hypot(x_m - POOL_EAST_M, y_m - POOL_NORTH_M) - POOL_RADIUS_M
    if beyond <= 0.0:
        return GRASS_MAX
    if beyond >= GRASS_REACH_M:
        return 0
    return int(round(GRASS_MAX * (1.0 - beyond / GRASS_REACH_M)))


def in_flat_margin(x_m, y_m):
    world_m = WORLD_WIDTH_MM / 1000.0
    return min(x_m, y_m, world_m - x_m, world_m - y_m) < FLAT_MARGIN_M


def grids(cell_width, cell_depth):
    """Height offsets, water depths, grass, and humidity, row-major south to north."""
    offsets = []
    water = []
    grass = []
    humidity = []
    world_m = WORLD_WIDTH_MM / 1000.0

    for row in range(cell_depth):
        for column in range(cell_width):
            x_m, y_m = cell_centre_m(column, row)
            if in_flat_margin(x_m, y_m):
                rise = 0
            else:
                rise = hill_rise_mm(x_m, y_m)

            offset = PLAIN_OFFSET_MM + rise
            pool_distance = math.hypot(x_m - POOL_EAST_M, y_m - POOL_NORTH_M)
            wet = False
            if pool_distance <= POOL_RADIUS_M:
                if in_flat_margin(x_m, y_m):
                    raise ValueError("pool enters the flat margin")
                if rise != 0:
                    raise ValueError("pool overlaps a hill")
                if min(x_m, y_m, world_m - x_m, world_m - y_m) < FLAT_MARGIN_M:
                    raise ValueError("pool enters the flat margin")
                offset -= POOL_DEPTH_MM
                wet = True

            if offset < 0:
                raise ValueError("offset dropped below min_height")

            offsets.append(offset)
            water.append(WATER_POOL_DEPTH_MM if wet else 0)
            grass.append(grass_height_mm(x_m, y_m))
            humidity.append(HUMIDITY_SATURATED if wet else 0)

    return offsets, water, grass, humidity


def write_layer_header(file, layer_type, layer_name, clock, storage, cell_size, minimum):
    file.write(LAYER_MAGIC)
    write_fixed_string(file, layer_type, 16)
    write_fixed_string(file, layer_name, 16)
    file.write(clock)
    write_uint64(file, LAST_SIMULATION_TICK)
    file.write(storage)
    write_uint32(file, cell_size)
    write_int32(file, minimum)


def create_world(output_path, offsets, water, grass, humidity):
    fertility = bytes(len(grass))

    with output_path.open("wb") as file:
        file.write(WORLD_MAGIC)
        write_uint32(file, WORLD_VERSION)
        write_uint32(file, WORLD_WIDTH_MM)
        write_uint32(file, WORLD_DEPTH_MM)
        write_fixed_string(file, MODULARITY, 8)
        write_uint64(file, WORLD_AGE)

        write_layer_header(
            file,
            b"TYPE_HEIGHTMAP",
            b"LYR_HEIGHTMAP",
            CLOCK_NOEV,
            STORAGE_DENSE,
            CELL_SIZE_MM,
            MIN_HEIGHT_MM,
        )
        for value in offsets:
            write_uint32(file, value)

        write_layer_header(
            file,
            b"TYPE_STATICWATER",
            b"LYR_STATICWATER",
            CLOCK_NOEV,
            STORAGE_DENSE,
            CELL_SIZE_MM,
            WATER_MIN_DEPTH_MM,
        )
        for value in water:
            write_uint32(file, value)

        write_layer_header(
            file,
            b"TYPE_DIFFLIGHT",
            b"LYR_DIFFLIGHT",
            CLOCK_DIFFLIGHT,
            STORAGE_DENSE,
            LIGHT_CELL_MM,
            LIGHT_MAX_IRRADIANCE,
        )
        write_uint32(file, 0)

        write_layer_header(
            file,
            b"TYPE_HUMIDITY",
            b"LYR_HUMIDITY",
            CLOCK_U8,
            STORAGE_U16,
            CELL_SIZE_MM,
            0,
        )
        for value in humidity:
            write_uint16(file, value)

        write_layer_header(
            file,
            b"TYPE_FERTILITY",
            b"LYR_FERTILITY",
            CLOCK_U8,
            STORAGE_U8,
            CELL_SIZE_MM,
            0,
        )
        file.write(fertility)

        write_layer_header(
            file,
            b"TYPE_GRASS",
            b"LYR_GRASS",
            CLOCK_U8,
            STORAGE_U8,
            CELL_SIZE_MM,
            0,
        )
        file.write(bytes(grass))

        write_layer_header(
            file,
            b"TYPE_GRASSAGE",
            b"LYR_GRASSAGE",
            CLOCK_GRASS_AGE,
            STORAGE_U8,
            CELL_SIZE_MM,
            0,
        )
        file.write(bytes(len(grass)))


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Create a 100 m modular world with steep stone hills, "
            "a pool of static water, grass, humidity, and one daylight cell."
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("data/world-stone-hills-v3.bin"),
        help="output world file (default: data/world-stone-hills-v3.bin)",
    )
    return parser.parse_args()


def main():
    args = parse_arguments()
    output_path = args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)

    cell_width = WORLD_WIDTH_MM // CELL_SIZE_MM
    cell_depth = WORLD_DEPTH_MM // CELL_SIZE_MM
    offsets, water, grass, humidity = grids(cell_width, cell_depth)
    create_world(output_path, offsets, water, grass, humidity)

    plain = sum(offset == PLAIN_OFFSET_MM for offset in offsets)
    pool = sum(offset == 0 for offset in offsets)
    peak = max(offsets)
    wet = sum(depth == WATER_POOL_DEPTH_MM for depth in water)
    hill_cells = sum(offset > PLAIN_OFFSET_MM for offset in offsets)

    print(f"Created World Format v{WORLD_VERSION}: {output_path}")
    print(
        f"  World:     {WORLD_WIDTH_MM / 1000:g} m × "
        f"{WORLD_DEPTH_MM / 1000:g} m, modular"
    )
    print(f"  Cells:     {cell_width} × {cell_depth} at {CELL_SIZE_MM} mm")
    print(
        f"  Plain:     {MIN_HEIGHT_MM + PLAIN_OFFSET_MM} mm "
        f"({plain} cells)"
    )
    print(
        f"  Summit:    {MIN_HEIGHT_MM + peak} mm "
        f"({hill_cells} hill cells)"
    )
    print(f"  Pool floor:{MIN_HEIGHT_MM} mm ({pool} cells)")
    print(
        f"  Water:     {WATER_POOL_DEPTH_MM} mm in the pool "
        f"({wet} cells)"
    )
    print(
        f"  Grass:     {sum(height > 0 for height in grass)} cells, "
        f"{min(grass)}..{max(grass)} mm"
    )
    print("  Grass age: 0 on CLK_0026")
    print(
        "  Humidity:  saturated "
        f"{sum(value == HUMIDITY_SATURATED for value in humidity)} "
        f"of {cell_width * cell_depth}"
    )
    print(
        f"  Daylight:  1 cell of {LIGHT_CELL_MM} mm, "
        f"max {LIGHT_MAX_IRRADIANCE} W/m2, "
        f"{CLOCK_DIFFLIGHT.decode()} (2^18 ms)"
    )
    print(f"  Size:      {output_path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
