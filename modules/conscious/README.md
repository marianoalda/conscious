# Conscious

The main process, operator interface, world load/save, and time engine.

The description of the project as it stands is the [root README](../../README.md). Layer meanings are in [doc/layers](../../doc/layers/README.md). World file bytes are in [doc/world-format](../../doc/world-format/README.md). Version aims are in [ROADMAP.md](../../ROADMAP.md).

## Build and run

From this directory:

```bash
make
./build/conscious -v -c config/conscious-dev.cfg
```

`make` writes `build/conscious`. That directory is not in the repository.

Useful options: `-h`, `-v`, `-d`, `-c FILE`, `-n NAME`, `-w FILE`, `-r MS`. `--run-ms` advances that many world milliseconds, prints grass births and deaths, and exits. `config/run-20-days.cfg` points at the shipped grass world.

## Layout

```text
src/main.c                 process, operator UI, configuration
src/simulation.c           due layers, publish, simulation thread
src/simulation_layer_*.c   daylight, humidity, fertility, grass, grass age
src/world*.c               load, save, format versions 0..3
config/                    development and shipped run configs
tests/                     standalone checks for grass and humidity
```

Relative `world_file` paths are resolved from the executable directory (`/proc/self/exe`). Absolute paths are unchanged.

## Tests

The programs under `tests/` are not built by `make`. Each links selected `src` objects by hand; see the compile lines in a past check or build against `src/` with `-I src`. They load `modules/data/world-po-mo-fer-hu-gr-v3.bin` by default.
