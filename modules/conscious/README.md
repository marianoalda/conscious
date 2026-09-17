# Conscious

A modular C simulation project exploring the implementation of artificial creatures and their interaction with a simulated world.

The project is currently in an early architectural and infrastructure phase. The simulation engine exists, but the world model is still a placeholder.

## Current architecture

The application is divided into three main components:

```text
main.c
  │
  ├── configuration
  ├── operator interface
  ├── world lifecycle
  │
  └── simulation engine
          │
          ├── simulation thread(s)
          ├── synchronization
          └── simulation state
```

The important architectural rule is:

> `main.c` owns the lifetime of the simulation engine. `simulation.c` owns everything required to implement the simulation, including its internal threads and synchronization.

The world is a separate component:

```text
main
 │
 ├── world
 │
 └── simulation
```

The simulation operates on the world, but the simulation's internal implementation is deliberately hidden from `main`.

---

# Implemented features

## Configuration

The application reads a configuration file containing:

* `save_state_on_shutdown`
* `state_on_start`
* `world_file`

Example:

```ini
save_state_on_shutdown = true
state_on_start = SIMULATE
world_file = world.bin
```

The configuration parser supports:

* boolean values
* startup state selection
* world file selection
* whitespace around configuration values

Default configuration is also provided by `main.c`.

### Startup states

Two startup modes are currently supported:

```text
SIMULATE
HOLD
```

These determine whether the simulation starts running or paused.

---

## World file path handling

Relative world paths are resolved relative to the executable directory.

Absolute paths are used unchanged.

The executable directory is obtained through:

```text
/proc/self/exe
```

`realpath()` is deliberately not used because the world file may not exist yet.

---

## World component

A separate `world` module exists:

```text
src/world.h
src/world.c
```

Current public interface:

```c
int world_load(const char *filename);
int world_save(const char *filename);
```

The implementation is currently a placeholder. It verifies that the file can be opened but does not yet contain actual world data.

The intended model is that the world will eventually become an opaque `world_t` object containing the complete in-memory world state.

---

## Simulation engine

The simulation is implemented in:

```text
src/simulation.h
src/simulation.c
```

The implementation uses an opaque simulation object:

```c
typedef struct simulation simulation_t;
```

`main.c` therefore does not know anything about the simulation's internal representation.

Current interface:

```c
int simulation_init(simulation_t **simulation);
int simulation_start(simulation_t *simulation);

int simulation_pause(simulation_t *simulation);
int simulation_resume(simulation_t *simulation);

int simulation_wait_until_paused(simulation_t *simulation);

long long simulation_get_step_count(simulation_t *simulation);

void simulation_destroy(simulation_t *simulation);
```

---

## Simulation thread

The simulation currently runs in its own POSIX thread.

The simulation engine internally owns:

* the simulation thread
* a mutex
* a condition variable
* requested state
* actual state
* termination state
* step counter

`main.c` does not directly manage the simulation thread.

The simulation currently performs a dummy simulation step consisting of a 1 ms delay.

---

## Pause and resume

The simulation has two internal states:

```text
SIMULATION_PAUSED
SIMULATION_RUNNING
```

`main.c` has a separate runtime state:

```text
ON_HOLD
SIMULATING
```

The configuration startup states are deliberately separate from the runtime states.

### Pause

When the operator requests a pause:

1. `main.c` requests a pause.
2. The simulation finishes its current step.
3. The simulation changes its actual state to paused.
4. `main.c` waits for the pause acknowledgement.
5. `main.c` changes its runtime state to `ON_HOLD`.

### Resume

When the operator requests a resume:

1. `main.c` requests running.
2. The simulation wakes up.
3. The simulation resumes execution.
4. `main.c` changes its runtime state to `SIMULATING`.

---

## Simulation termination

Termination is deliberately not represented as a simulation state.

The simulation has an internal:

```text
terminate_requested
```

flag used when destroying the simulation engine.

`simulation_destroy()`:

1. requests termination;
2. wakes the simulation thread;
3. waits for it with `pthread_join()`;
4. destroys synchronization objects;
5. frees the simulation object.

This ensures that the simulation is no longer modifying the world before the world is saved during shutdown.

---

## Operator interface

The terminal is temporarily placed in noncanonical mode with echo disabled.

The operator does not need to press Enter after commands.

### While running

```text
[SIMULATING]  ...  [p] pause  [q] shutdown
```

Available commands:

```text
p   Pause simulation
q   Shutdown
```

### While paused

```text
[ON HOLD]     [s] resume  [w] snapshot  [q] shutdown
```

Available commands:

```text
s   Resume simulation
w   Save a snapshot
q   Shutdown
```

The operator interface is implemented by `main.c`.

---

## Simulation progress feedback

The simulation maintains a completed-step counter:

```c
long long step_count;
```

The counter is protected by the simulation mutex.

`main.c` can obtain the current value through:

```c
long long simulation_get_step_count(simulation_t *simulation);
```

The operator interface displays approximately once per second:

```text
[SIMULATING]  943 steps/s  total: 18231  [p] pause  [q] shutdown
```

The displayed rate is calculated from the difference between the current and previous counter values.

The counter currently starts at zero for every application execution.

---

## Snapshot mechanism

While the simulation is paused, `w` saves a snapshot of the current world without overwriting the normal world file.

The snapshot filename is constructed from the original world filename and the current simulation step:

```text
world.bin.12537
```

For example:

```text
data/
    world.bin
    world.bin.12537
```

The original world file is not modified by the snapshot operation.

Currently the world implementation is only a placeholder, so the snapshot mechanism creates the file but does not yet contain real world data.

---

## Build system

The project uses a Makefile.

Compiler:

```text
gcc
```

Current compiler options:

```text
-Wall
-Wextra
-std=c11
-D_POSIX_C_SOURCE=200809L
```

POSIX threads are linked with:

```text
-pthread
```

Build:

```bash
make
```

Clean:

```bash
make clean
```

Executable:

```text
build/conscious
```

---

# World format

The current world file is not yet implemented.

The intended world format will be a self-describing binary format.

The world file should contain the metadata required to interpret its own contents rather than relying on `conscious.cfg`.

The planned structure is conceptually:

```text
World file
│
├── File header
│   ├── magic number
│   └── format version
│
├── World metadata
│   ├── dimensions
│   ├── number of layers
│   └── persistent simulation step
│
├── Layer 0 metadata
│   ├── layer type
│   ├── encoding
│   ├── dimensions
│   └── data size
│
├── Layer 0 data
│
├── Layer 1 metadata
│
├── Layer 1 data
│
└── ...
```

The first implementation will contain only one layer, but the format should be designed as a multilayer format from the beginning.

The world file represents persistent world state.

The configuration file represents application/runtime configuration.

---

# Planned persistent step number

The simulation step number is currently held by `simulation_t` and starts at zero on every execution.

This is temporary.

Once the world format is implemented, the step number will become part of the persistent world state.

For example:

```text
world.bin
    step = 125000
```

After restarting:

```text
load world.bin
    step = 125000

next simulation step
    step = 125001
```

This makes the step counter cumulative across executions.

Snapshots will therefore also preserve their corresponding simulation step.

For example:

```text
world.bin
world.bin.100000
world.bin.200000
world.bin.300000
```

A snapshot loaded as a world should resume from the step stored in that snapshot.

---

# Planned world utility

A separate utility for creating and inspecting worlds is planned.

Possible executable:

```text
conscious-world
```

The utility should use the same `world.c` implementation as the main simulation rather than implementing a second interpretation of the world format.

Possible commands:

```text
conscious-world create
conscious-world info
conscious-world display
conscious-world dump
conscious-world validate
conscious-world convert
```

An initial textual display could be sufficient before developing a graphical viewer.

Example:

```text
...........^^^......
.........^^^^^^.....
......~~~^^^^^^.....
.....~~~~~^^^^......
.....~~~~~..........
```

---

# TODO

## World implementation

* [ ] Replace the dummy `world.c` implementation with a real in-memory world.
* [ ] Introduce an opaque `world_t`.
* [ ] Define world dimensions.
* [ ] Define the first world layer.
* [ ] Define the representation/encoding of layer data.
* [ ] Implement creation and destruction of worlds.
* [ ] Implement actual world loading.
* [ ] Implement actual world saving.
* [ ] Ensure world saving serializes the complete in-memory state.

## World file format

* [ ] Define the binary world-file format precisely.
* [ ] Define the magic number.
* [ ] Define the file-format version.
* [ ] Define integer sizes and binary representation.
* [ ] Define endianness.
* [ ] Define world dimensions in the file.
* [ ] Define the number of layers in the file.
* [ ] Define layer metadata.
* [ ] Define layer encoding/type identifiers.
* [ ] Define layer data sizes.
* [ ] Include the persistent simulation step number.
* [ ] Define validation/error handling for malformed world files.
* [ ] Consider format evolution/version compatibility.

## Persistent simulation step

* [ ] Move persistent step ownership from `simulation_t` into the world state.
* [ ] Load the step number from the world file.
* [ ] Continue the step counter across application restarts.
* [ ] Save the step number with the world.
* [ ] Make snapshots preserve the step number.
* [ ] Allow a loaded snapshot to continue from its stored step.

## Snapshots

* [x] Add snapshot operator command.
* [x] Restrict snapshots to the paused state.
* [x] Generate snapshot filename from original filename + step number.
* [x] Avoid overwriting the original world file.
* [ ] Save actual world contents.
* [ ] Save persistent world metadata.
* [ ] Decide what should happen if a snapshot filename already exists.
* [ ] Consider whether snapshots should eventually have a dedicated directory or naming convention.

## World utility

* [ ] Create a separate `conscious-world` utility.
* [ ] Create worlds independently of the simulator.
* [ ] Display world metadata.
* [ ] Display world contents.
* [ ] Dump world contents in a machine-readable/debug format.
* [ ] Validate world files.
* [ ] Eventually support multiple layers.
* [ ] Consider graphical visualization.

## Simulation engine

* [ ] Replace the dummy 1 ms `simulate_step()` with actual simulation logic.
* [ ] Define how the simulation interacts with the world.
* [ ] Determine whether simulation execution needs a master/coordinator thread.
* [ ] Add worker threads if required.
* [ ] Consider queues, barriers, and other synchronization mechanisms as the simulation architecture evolves.
* [ ] Keep all simulation-internal threading hidden from `main.c`.
* [ ] Consider explicit acknowledgement of the running state, analogous to the existing pause acknowledgement.

## Operator interface

* [ ] Clear the remainder of the terminal line when changing between states/display lengths.
* [ ] Improve status/error messages.
* [ ] Consider more operator commands as simulation functionality grows.

## World/simulation architecture

The long-term intended relationship is:

```text
                         ┌───────────────────┐
                         │       main        │
                         │                   │
                         │ configuration     │
                         │ operator input    │
                         │ application life  │
                         └─────────┬─────────┘
                                   │
                  ┌────────────────┴────────────────┐
                  │                                 │
                  ▼                                 ▼
        ┌──────────────────┐             ┌──────────────────┐
        │      world       │             │    simulation    │
        │                  │◄────────────│                  │
        │ dimensions       │             │ master thread    │
        │ layers           │             │ worker threads   │
        │ world state      │             │ synchronization  │
        │ step number      │             │ execution        │
        └──────────────────┘             └──────────────────┘
```

The world owns persistent state.

The simulation owns the machinery that evolves that state.

`main.c` orchestrates the application without depending on the internal implementation of either component.

---

# Development principles

Several architectural principles have been established during development:

1. **Keep simulation internals opaque.**
   `main.c` should not know how many simulation threads exist or how they communicate.

2. **Separate persistent state from application configuration.**
   World state belongs in the world file; startup and runtime preferences belong in `conscious.cfg`.

3. **Make the world format self-describing.**
   A world file should contain the metadata necessary to interpret its own contents.

4. **Protect shared simulation state.**
   The simulation mutex protects state shared between the simulation thread and the main/operator thread.

5. **Treat condition variables as synchronization mechanisms, not state.**
   The actual state is stored explicitly and condition variables are used to wake threads so that they can re-evaluate that state.

6. **Keep lifecycle ownership clear.**
   `main.c` owns the simulation engine's lifetime; `simulation.c` owns its internal execution machinery.

7. **Avoid premature coupling.**
   The world implementation should not know about terminal input, shutdown commands, or operator interaction.

8. **Use a single authoritative world implementation.**
   The simulator and future world-management utilities should both use the same world module and file-format implementation.

# Backlog of features

- In the future, receive orders from a remote control panel.

# References

- [Project documentation head](../../README.md)