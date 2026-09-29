# World viewer

A small OpenGL program that draws a Conscious world file in perspective. It
reads world files through the same `world` module as the simulator (every
layer type the engine can load). The build links `world.c` and the format
loaders from `../../conscious/src`; it does not link the simulation engine.

## Build

From this directory:

```bash
make
```

The binary is `build/world-view`. The build directory is not part of the
repository.

Dependencies: OpenGL, GLU, X11, and a C11 compiler.

## Run

```bash
./build/world-view WORLD [--diffuse VALUE] [--direct VALUE]
```

| Argument     | Meaning |
| ------------ | ------- |
| `WORLD`      | Path to a Conscious world file (required) |
| `--diffuse`  | Unlit floor, from 0 to 2. Default `0.25`. |
| `--direct`   | Weight of the directional term on every face, including night. Default `0.85`. |

The program prints `Daylight:` as the mean published irradiance divided by
the layer maximum, and the current diffuse/direct values. Slope shading
stays on at midnight.

Examples:

```bash
./build/world-view ../../data/world-po-mo-fer-hu-gr-v3-day.bin
./build/world-view ../../data/world.bin --diffuse 0.22 --direct 0.85
```

## Controls

| Input | Action |
| ----- | ------ |
| Mouse wheel | Zoom |
| Left drag | Orbit around the target. Elevation goes from below the world to above it. |
| Arrow keys | Pan across the map |
| Tab | Cycle underside sheet: humidity → fertility → grass age → humidity |
| `,` / `.` | Decrease / increase diffuse light (step 0.05, clamp 0..2) |
| `[` / `]` | Decrease / increase direct light (step 0.05, clamp 0..2) |
| Esc, q | Quit |

Input events are drained into camera and light state first; the scene is
redrawn at most once after that drain. Events that arrive while a frame is
being drawn do not each force their own redraw, so orbit and pan stay fluid.

## What is drawn

Coordinates follow UTM: X grows east, Y grows north, Z is elevation in
metres. The south-west corner of the world is the origin.

Each heightmap cell is one sample. A drawn corner is the average of the
cells that meet there, so a cell becomes a tilted quad split on the
south-west to north-east diagonal.

Draw order:

1. **Terrain** — brown, shaded by diffuse, a fixed light toward the
   north-east, and a small lift from the file's daylight.
2. **Grass** — green on the same terrain face. Opacity is `height / 255`.
3. **Static water** — cyan at terrain elevation plus depth (always drawn).
4. **Underside sheet** (Tab) — one of humidity, fertility, or grass age.
   Each is hung from the heightmap's stored zero (`min_height`) and grows
   downward. Visible from below the terrain.

Layers absent from the file are skipped. All layer types present in the
file are loaded and stored by `world_load` (heightmap, static water,
diffuse daylight, humidity, fertility, grass, grass age).

### Underside sheets

All three use the same hanging geometry. Depth scales with the cell value
and the world's greatest elevation:

```text
humidity:  depth = (cell / 65535) × greatest_elevation
fertility: depth = (cell / 255)   × greatest_elevation
grass age: depth = (cell / 255)   × greatest_elevation
z          = min_height − depth
```

| Sheet     | Colour  |
| --------- | ------- |
| Humidity  | blue    |
| Fertility | amber   |
| Grass age | magenta |

A zero cell is not drawn. Drag the view below the horizon to see the sheet
from underneath.

## Source

```text
src/world_view.c   viewer and OpenGL front end
Makefile           links world loaders from ../../conscious/src
```
