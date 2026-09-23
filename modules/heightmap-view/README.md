# Heightmap viewer

A small OpenGL program that draws a Conscious world file in perspective. It reads version 3 worlds through the same `world` module as the simulator. The build links `world.c` and the format loaders from `../conscious/src`; it does not link the simulation engine.

## Build

From this directory:

```bash
make
```

The binary is `build/heightmap-view`. The build directory is not part of the repository.

Dependencies: OpenGL, GLU, X11, and a C11 compiler.

## Run

```bash
./build/heightmap-view WORLD DIFFUSE DIRECT
```

| Argument  | Meaning |
| --------- | ------- |
| `WORLD`   | Path to a Conscious world file |
| `DIFFUSE` | Light that remains at night, from 0 to 1 |
| `DIRECT`  | Weight of the directional term, scaled by the world's daylight |

The program prints `Daylight:` as the mean published irradiance divided by the layer maximum. That fraction fills the range from `DIFFUSE` up to full day and scales the slope shading. At night only `DIFFUSE` remains, so a snapshot taken in the dark is still visible.

Example with the pool world that includes grass and humidity:

```bash
./build/heightmap-view ../data/world-po-mo-fer-hu-gr-v3-day.bin 0.25 0.85
```

## Controls

| Input | Action |
| ----- | ------ |
| Mouse wheel | Zoom |
| Left drag | Orbit around the target. Elevation goes from below the world to above it. |
| Arrow keys | Pan across the map |
| Esc, q | Quit |

## What is drawn

Coordinates follow UTM: X grows east, Y grows north, Z is elevation in metres. The south-west corner of the world is the origin.

Each heightmap cell is one sample. A drawn corner is the average of the cells that meet there, so a cell becomes a tilted quad split on the south-west to north-east diagonal.

Draw order, back to front within the depth test:

1. **Terrain** — brown, shaded by `DIFFUSE`, daylight, and a fixed light toward the north-east above the horizon.
2. **Grass** — green on the same terrain face. Opacity is `height / 255`. Height 0 leaves the face brown; 255 covers it.
3. **Static water** — cyan at the same luminance as the terrain, shaded like the ground face underneath, drawn at terrain elevation plus depth.
4. **Humidity** — blue sheet hung from the heightmap's stored zero (`min_height`), not from the terrain surface.

Layers that are absent from the file are skipped.

### Humidity

Humidity is drawn as a downward sheet from the heightmap zero. It is not extruded from the local terrain surface.

For each humidity cell the viewer computes:

```text
depth = (cell / 65535) × greatest_elevation
z     = min_height − depth
```

`greatest_elevation` is the largest `min_height + stored_offset` on the heightmap. A saturated cell (65535) therefore reaches down by the full height span of the world. A dry cell (0) is not drawn.

The sheet uses the same horizontal footprint as the terrain grid. Humidity is sampled on its own 10 cm grid when that differs from the heightmap step; the cell that contains the terrain sample centre is used.

Because the sheet hangs from `min_height`, it lies at or below that plane. Terrain that rises above it hides the humidity from a camera above. To inspect it, drag the view downward until the orbit passes below the horizon and the underside of the world is visible. The pool shows a blue disk under the water once those cells hold humidity; soil around the pool shows a blue halo after the simulation has spread moisture.

World files for trying this:

| File | Humidity |
| ---- | -------- |
| `../data/world-po-mo-fer-hu-gr-v3.bin` | Pool cells start saturated; soil starts dry |
| `../data/world-po-mo-fer-hu-gr-v3-day.bin` | Same world after 86400000 ms (one day) of simulation, with moisture in the soil |

Further detail on the humidity layer is in [doc/layers/humidity.md](../../doc/layers/humidity.md).

## Source

```text
src/heightmap_view.c   viewer and OpenGL front end
Makefile               links world loaders from ../conscious/src
```
