#!/usr/bin/env python3

"""
Add humidity, fertility, grass, and grass age to the pool world.

Reads data/world-pool-mountain-v3.bin and writes
data/world-po-mo-fer-hu-gr-v3.bin. The source file is not modified.

Humidity is a 10 cm grid of uint16 (storage ST16). Cells on standing
water start saturated (65535); the rest start at 0. Grass occupies
the pool and a 5 m ring outside it. Living cells get a seeded random
height from 1 to 255 mm and a seeded random age from 0 to 254.
Fertility is 128 on cells that have grass and 0 elsewhere. Humidity
and fertility use clock CLK_0016. Grass uses CLK_0022. Grass age
uses CLK_0026.

The pool matches create_world_v3_pool_mountain.py: centre 13.6 m east
and 10.0 m north, radius 1 m.

Usage, from the modules directory:

    ./utils/create_world_v3_pool_mountain.py
    ./utils/create_world_v3_po_mo_fer_hu_gr.py
    rm -f data/world.bin
    ln data/world-po-mo-fer-hu-gr-v3.bin data/world.bin

The first command is needed only when the pool world is missing or
has changed. The last two make world.bin a hard link to this file
so conscious-dev.cfg does not need editing.
"""

import math
import random
import struct
from pathlib import Path


LAYER_MAGIC = b"_LYR"
STORAGE_U8 = b"ST_8"
STORAGE_U16 = b"ST16"
CLOCK_EVERY_MINUTE = b"CLK_0016"
CLOCK_GRASS = b"CLK_0022"
CLOCK_GRASS_AGE = b"CLK_0026"
CELL_SIZE_MM = 100

LAYERS = (
    (b"TYPE_HUMIDITY", b"LYR_HUMIDITY", CLOCK_EVERY_MINUTE),
    (b"TYPE_FERTILITY", b"LYR_FERTILITY", CLOCK_EVERY_MINUTE),
    (b"TYPE_GRASS", b"LYR_GRASS", CLOCK_GRASS),
    (b"TYPE_GRASSAGE", b"LYR_GRASSAGE", CLOCK_GRASS_AGE),
)

POOL_EAST_MM = 13_600
POOL_NORTH_MM = 10_000
POOL_RADIUS_MM = 1_000
GRASS_REACH_MM = 5_000
GRASS_MAX = 255
GRASS_AGE_MAX = 255
FERTILITY_MAX = 255
FERTILITY_INITIAL = int(round(FERTILITY_MAX * 0.5))
HUMIDITY_SATURATED = 65535
GRASS_RNG_SEED = 1


def padded(text, width):
    if len(text) > width:
        raise ValueError(text)
    return text + bytes(width - len(text))


def grass_in_stand(column, row):
    east = column * CELL_SIZE_MM + CELL_SIZE_MM / 2
    north = row * CELL_SIZE_MM + CELL_SIZE_MM / 2
    distance = math.hypot(east - POOL_EAST_MM, north - POOL_NORTH_MM)
    beyond = distance - POOL_RADIUS_MM
    return beyond < GRASS_REACH_MM


def fill_grass_and_age(columns, rows):
    rng = random.Random(GRASS_RNG_SEED)
    grass = bytearray(columns * rows)
    age = bytearray(columns * rows)

    for row in range(rows):
        for column in range(columns):
            if not grass_in_stand(column, row):
                continue

            index = row * columns + column
            grass[index] = rng.randint(1, GRASS_MAX)
            age[index] = rng.randint(0, GRASS_AGE_MAX - 1)

    return grass, age


def read_layers(data, width, depth):
    """Return static-water depths in millimetres, one per 10 cm cell."""
    off = 32
    water = None

    while off + 4 <= len(data) and data[off : off + 4] == LAYER_MAGIC:
        layer_type = data[off + 4 : off + 20].split(b"\x00")[0]
        storage = data[off + 52 : off + 56]
        cell_size = struct.unpack(">I", data[off + 56 : off + 60])[0]
        minimum = struct.unpack(">i", data[off + 60 : off + 64])[0]
        payload = off + 64
        columns = width // cell_size
        rows = depth // cell_size
        count = columns * rows

        if layer_type == b"TYPE_STATICWATER" and storage == b"ST_D":
            values = struct.unpack(">" + "I" * count, data[payload : payload + 4 * count])
            water = [minimum + value for value in values]
            payload += 4 * count
        elif storage == b"ST_D":
            payload += 4 * count
        elif storage == b"ST16":
            payload += 2 * count
        elif storage == b"ST_8":
            payload += count
        else:
            raise SystemExit(f"unknown storage {storage!r} in {layer_type!r}")

        off = payload

    return water


def humidity_initial(water_depths):
    values = bytearray(len(water_depths) * 2)
    for index, depth_mm in enumerate(water_depths):
        if depth_mm > 0:
            struct.pack_into(">H", values, index * 2, HUMIDITY_SATURATED)
    return values


def layer_block(
    type_name,
    layer_name,
    clock,
    columns,
    rows,
    grass,
    age,
    water_depths=None,
):
    block = bytearray()
    block += LAYER_MAGIC
    block += padded(type_name, 16)
    block += padded(layer_name, 16)
    block += clock
    block += struct.pack(">Q", 0)
    if type_name == b"TYPE_HUMIDITY":
        block += STORAGE_U16
    else:
        block += STORAGE_U8
    block += struct.pack(">I", CELL_SIZE_MM)
    block += struct.pack(">i", 0)

    if type_name == b"TYPE_GRASS":
        block += grass
    elif type_name == b"TYPE_GRASSAGE":
        block += age
    elif type_name == b"TYPE_FERTILITY":
        values = bytearray(columns * rows)
        for index, height in enumerate(grass):
            if height > 0:
                values[index] = FERTILITY_INITIAL
        block += values
    elif type_name == b"TYPE_HUMIDITY":
        if water_depths is None:
            raise ValueError("humidity needs the static-water grid")
        block += humidity_initial(water_depths)
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
    water_depths = read_layers(data, width, depth)
    if water_depths is None:
        raise SystemExit("source world has no static water")

    grass, age = fill_grass_and_age(columns, rows)
    output = bytearray(data)

    for type_name, layer_name, clock in LAYERS:
        output += layer_block(
            type_name,
            layer_name,
            clock,
            columns,
            rows,
            grass,
            age,
            water_depths=water_depths,
        )

    target.write_bytes(output)
    humidity_values = struct.unpack(
        ">" + "H" * (columns * rows),
        humidity_initial(water_depths),
    )
    living = [height for height in grass if height > 0]
    living_age = [value for height, value in zip(grass, age) if height > 0]
    fertility = bytes(
        FERTILITY_INITIAL if height > 0 else 0 for height in grass
    )
    print(f"wrote {target}")
    print(
        "humidity saturated "
        f"{sum(value == HUMIDITY_SATURATED for value in humidity_values)} "
        f"of {columns * rows}"
    )
    print(f"grass cells {len(living)} of {columns * rows}")
    print(f"grass height {min(living)}..{max(living)}")
    print(f"grass age {min(living_age)}..{max(living_age)}")
    print(
        "fertility under grass "
        f"{sum(value == FERTILITY_INITIAL for value in fertility)} "
        f"at {FERTILITY_INITIAL}"
    )
    print("grass clock CLK_0022")
    print(f"size {len(output)} bytes")


if __name__ == "__main__":
    main()
