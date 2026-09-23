# Conscious

This is the Conscious Project, a try to investigate if artificial consciousness can arise from proprioception, i.e., integration by a being of a model of itself (and the world itself) in the system that the being uses to interact with its world.

The original statement of that aim is kept in [README.original.md](README.original.md). What follows is the implementation as it stands.

There are no beings yet. The present code is the time engine and the world those beings would inhabit: a quantized terrain, a clock, and a way to suspend the clock and keep the world.

## Modules

- [conscious](modules/conscious/) — the main module. It owns the process, the operator interface, the world, and the time engine.

`modules/conscious/README.md` describes an earlier stage of that module. This document is the one that matches the code.

## Architecture

One process, three parts:

```text
main
 │
 ├── world          persistent state
 │
 └── simulation     the time engine
```

`main` owns the lifetime of the simulation. The simulation owns its thread and its synchronization. The world owns the persistent state. The simulation operates on the world; `main` does not see the simulation's internal threads.

The time engine can be suspended. While it runs, one step is one millisecond of world time.

## World

The world is a repository. It is quantized in cells. Distances are millimetres. One world tick is one millisecond.

In memory the world holds an array of layers. Each layer has its metadata and a payload with two dense grids of the same size. `published` is the grid other layers, the viewer, and the file see. `pending` is the grid filled during the current tick. A step reads `published` only. When every layer that is due has finished reading, the two grids exchange roles. On disk, version 3 stores that published sequence: a dense heightmap, and any further layer blocks that follow it. Static water (`TYPE_STATICWATER`) is a depth added to the terrain elevation. It does not move or change. Diffuse daylight (`TYPE_DIFFLIGHT`) stores the current irradiance of each cell, in W/m², and is the first layer the step updates. Humidity (`TYPE_HUMIDITY`) is a 10 cm grid of 16-bit cells, from 0 (dry) to 65535 (saturated). On its clock it saturates where static water stands, diffuses to orthogonal neighbours in proportion to the difference, and evaporates 10 % of the humidity still in the cell per hour at full sun, scaled by the published daylight and by the published grass (height 255 halves the loss). Fertility and grass (`TYPE_FERTILITY`, `TYPE_GRASS`) are 10 cm cells of one byte each. Fertility runs from 0 (sterile) to 255 (maximum). Grass is millimetres of height, from 0 to 255. Their simulation function is a prototype and does not change the cells. The step still publishes their grids with the other due layers.

A cell stores an offset above the layer's minimum height:

```text
elevation = min_height + height_value
```

A layer clock says when that layer is simulated:

* `CLK_NOEV` — the layer is static;
* `CLK_` followed by an exponent — the layer is simulated every `2^exponent` milliseconds.

The heightmap shipped with the current worlds is `CLK_NOEV`. The step walks the array and runs only the layers that are due on that tick. The heightmap and static water have no evolution rules yet, so those layers are skipped. When `step_delay_us` is set, the step waits that many microseconds so the clock does not run away while nothing is being simulated. `config/conscious.cfg` sets it to 1. `conscious-dev.cfg` omits it, and so does any configuration that leaves the parameter out: the step does not wait.

The world also stores whether it is `CLOSED` or `MODULAR`. That property is saved and loaded. Joining opposite edges is not a rule of one layer. `world_neighbor` answers any layer that asks for an orthogonal neighbour: on `MODULAR` the cell past one side is the cell on the other side, and on `CLOSED` that neighbour does not exist. `world_cell_at` finds the cell that contains a point. A point outside the map has no cell, on either kind of world.

The step itself stays in `modules/conscious/src/simulation.c`: which layer is due, the copy from `published` to `pending`, the publish, and the thread. A layer that changes cells has its own file. Diffuse daylight is `simulation_layer_difflight.c`. Humidity is `simulation_layer_humidity.c`. The heightmap, fertility, and grass do not change a cell yet, so their functions remain in `simulation.c`.

### File format

The world file is self-describing. Magic `CWLD`, then a version. Older versions are still read. `world_serialize()` writes version 3 only.

| Version | What it stores                                      |
| ------- | --------------------------------------------------- |
| 0       | Magic and version                                   |
| 1       | Dimensions and one dense heightmap                  |
| 2       | Modularity of the world, and a clock on the layer   |
| 3       | World age, and the layer's last simulation tick     |

The specification is in [doc/world-format](doc/world-format/README.md). What each layer means, and how far its simulation function has got, is in [doc/layers](doc/layers/README.md).

### Files used in development

`modules/conscious/config/conscious-dev.cfg` always names `world.bin`. That name stays fixed. The file is a hard link to the world under test.

Today `modules/data/world.bin` and `modules/data/world-pool-mountain-v3.bin` are the same file: a 20 m × 20 m world with a mountain, a pool of static water, and one diffuse-daylight cell on `CLK_0018` (262144 ms, about 4.4 minutes). `modules/data/world-po-mo-fer-hu-gr-v3.bin` is that world plus humidity, fertility, and grass. Humidity is `ST16`: the pool starts saturated and the soil starts dry. Fertility is one byte at 0. Grass falls from 255 at the pool edge to 0 at 5 m outside it. Those three layers use `CLK_0016` (65536 ms). `modules/data/world-po-mo-fer-hu-gr-v3-day.bin` is the same world after one day of simulation, with humidity spread into the soil. `modules/data/world-v3-ramp.bin` remains the 10 m × 10 m ramp. `modules/data/world-v3-mound.bin` is another version 3 world, a mound 2 m above a flat border, generated by [modules/utils/create_world_v3_mound.py](modules/utils/create_world_v3_mound.py).

When the world structure changes, a new file is produced, either by a script or by loading an older world and writing a snapshot. The old `world.bin` link is removed and the new file is linked to that same name. The configuration file is not edited.

## Time engine

The simulation runs in its own thread. `main` can pause it and wait until the current step has finished, then resume it.

From `modules/conscious`:

```text
[SIMULATING]  … ms/s  age: … ms   [p] pause  [q] shutdown
[ON HOLD]     [s] resume  [i] increment  [w] snapshot  [q] shutdown
```

`p` suspends the engine. `s` resumes it. `q` shuts down. `i`, only while suspended, runs `incremental_steps` milliseconds and then suspends again. The default is 3600000 ms. While that run is in progress the status line shows the age at which it will stop. `w`, only while suspended, writes a snapshot beside the world file. The name is the world path plus the age in milliseconds, for example `world.bin.13987`. The original file is not overwritten. The snapshot is another world file and can be loaded later; its age is the age at which it was taken. Files under `modules/data` whose name ends in a dot and digits are ignored by git.

On shutdown the world is written back only if `save_state_on_shutdown` is true. The development configuration leaves that false, so a run does not replace `world.bin`.

## Build and run

From `modules/conscious`:

```bash
make
./build/conscious -v -c config/conscious-dev.cfg
```

`make` produces `build/conscious`. The build directory is not part of the repository.

Useful options:

```text
-h, --help
-v, --verbose
-c, --config FILE
-n, --name NAME
-w, --validate-world FILE
```

`-v` prints the loaded world and each layer, with units. `-w` loads a world file, reports whether it is valid, and exits.

`conscious-dev.cfg` starts in `HOLD`, leaves `step_delay_us` unset, and points at `../../data/world.bin`. `config/conscious.cfg` starts in `SIMULATE`, sets `step_delay_us` to 1, and saves the world on shutdown.

Startup is `SIMULATE` or `HOLD`.

## Heightmap viewer

From `modules/heightmap-view`:

```bash
make
./build/heightmap-view ../data/world-po-mo-fer-hu-gr-v3-day.bin 0.25 0.85
```

The program draws terrain, grass, static water, and humidity from a world file. Humidity is a blue sheet hung from the heightmap's stored zero and growing downward; it is visible only from below the terrain. Full usage, controls, and layer colours are in [modules/heightmap-view/README.md](modules/heightmap-view/README.md).

## References

- [Project in Github](https://github.com/marianoalda/conscious)
- [ChatGPT conversation including implementation and ethics](https://chatgpt.com/share/6aaab1ce-9c64-83ed-971f-411bacf42cb4)
- [Gemini conversation including the own basic theory about artificial consciousness](https://share.gemini.google/azB9mMfjqQD8)
- [Original project statement](README.original.md)

## Ideas futuras

These are still the aim. They are not in the program.

- An open, modular and distributed architecture: the world as a shared memory segment, and beings written in another language (the original example was Smalltalk) so they can evolve and coexist with other differently-evolved beings in the same engine.
- Beings as a repository and an engine: object oriented, with their own rules, able to evolve so that different specimens with different features (DNA) and feature expressions can exist simultaneously. Able to emit messages. Basic circuits (thirst, hunger, reproduction, cold) in the reality and in the model. A lifecycle. An integrated model of the world and of the being itself, not necessarily synchronized. Surviving instinct as the spark that keeps them alive. Behaviours that trigger anomalies, such as curiosity.
- A world of several layers with their own rules: food and its growth, a surface of water, difficulty to walk because grass has grown. A layer may have its own cell size. The array of layers is that place. Static water, diffuse daylight, humidity, fertility, and grass are there today. Humidity changes. Fertility and grass do not yet.
- The time engine able to be accelerated or slowed down, not only suspended.
- Snapshots of beings as well as of the world, and external tools that translate to human language what happens in the world and inside the beings: evolution, thoughts, analysis of protolanguage.
- A world console or control panel: suspend, explain, explain changes between snapshots, translate the world and the beings. A real-time representation, graphical or textual, that can feed other agents. Orders from a remote control panel.
- Debugging across a heterogeneous set of languages.
- Compatibility among engines beyond the world file: versions, tags, releases, and a written account of which features each engine requires.
- Setup and understanding of the Github issues subsystem.
- Automation of build and execution beyond `make` in `modules/conscious`.

## Obsoleto

These phrases from the original statement no longer match the code. They are kept here so the old text is not read as the design.

- The world is not itself the engine. It is the repository. The simulation engine is separate and is what advances the tick. Rules such as "the grass grows every tick" are not implemented. The step selects the layers that are due. Diffuse daylight evolves with the world age. Humidity saturates, diffuses, and evaporates. The heightmap, static water, fertility, and grass do not change.
- The runtime world is not one embedded heightmap. It is an array of layer pointers. A version 3 file is the same sequence of layer blocks, read until the file ends.
- New worlds are not written as version 0 or version 1. Those formats, and version 2, are read. Saving writes version 3.
- "How to automate the build" is no longer an open question for this module. The build is `make` in `modules/conscious`.
- `modules/conscious/README.md` still says the world implementation is a placeholder that only checks whether the file opens, and that version 1 is what gets written. That description is obsolete.
