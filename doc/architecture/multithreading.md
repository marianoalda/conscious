# Multithreading

Conscious runs as one process with several POSIX threads. Thread names
are set with `pthread_setname_np` (Linux: at most 15 characters). See
`thread_name.c`.

## Thread map

```text
cons main          operator UI, configuration, load/save orchestration
   │
   ├── cons grid   dense-layer time engine (humidity, grass, …)
   │
   └── cons <TAG> <id>   one thread per living individual
         e.g. cons RAB 1
```

| Logical name | Role |
| ------------ | ---- |
| `cons main` | Process entry and operator loop. Owns simulation lifetime. |
| `cons grid` | Advances world ticks for **dense** layers only. |
| `cons <TAG> <id>` | One living being. `<TAG>` comes from the species (e.g. `WORLD_SPECIES_RABBIT_THREAD_TAG` → `RAB`). `<id>` is the individual serial, at most 999999. |

`top -c` shows the process command line on every row; use `top -H`
without `-c`, or `ps -L -o lwp,comm`, to see these names.

## Dense-layer engine (`cons grid`)

Owned by `simulation.c`. Same rules as before beings:

- Pause / resume / resume-for from `main`.
- On a due tick: copy `published` → `pending`, run layer functions,
  publish, advance `world_tick` (and jump empty milliseconds unless
  `step_delay_us` is set).
- **`TYPE_INDIVIDUAL` is never due** on this thread. Beings are not
  stepped here.

After each dense step the grid thread calls
`simulation_individuals_reap()` so disappeared beings are joined and
their records freed without blocking the being threads during the step.

## Being threads

Owned by `simulation_individuals.c`. Interface (also declared on
`simulation.h` where useful from `main`):

| Call | When |
| ---- | ---- |
| `simulation_individuals_start` | After the grid thread is created (startup). Also after a successful snapshot serialize, to recreate threads from the saved list. |
| `simulation_individuals_stop` | Before serialize (snapshots) and on `simulation_destroy` (shutdown). Signals terminate, joins, frees the in-memory registry. |
| `simulation_individual_request_disappear` | Ask one being’s thread to exit (death / vanish). |
| `simulation_individuals_reap` | Join threads that have exited after disappear; remove their records from the species payload; free empty registry. |
| `simulation_individual_find` | Look up a live slot by species string and id. |
| `simulation_individuals_live_count` | How many being threads are still running. |

Birth that adds a new record and starts its thread is not wired yet
(see issue [#6](https://github.com/marianoalda/conscious/issues/6)).

### What a being thread may touch

- **May write:** that individual’s own pose fields in the
  `TYPE_INDIVIDUAL` payload.
- **May read:** static world layout (width, depth, modularity) for
  wrapping; later, published **static** layers such as heightmap /
  standing water when helpers exist.
- **Must not** read or write dense **simulated** grids (humidity,
  fertility, grass, grass age, daylight) until a publish-safe protocol
  exists. Coordination with `cons grid` is still unsynchronised except
  for stop / disappear / reap.

Species behaviour (move, die, …) lives in `simulation_species_*.c` and
is called from the being thread. Lifecycle of the thread itself stays
in `simulation_individuals.*`.

## Sources

```text
modules/conscious/src/main.c                   cons main
modules/conscious/src/simulation.c             cons grid
modules/conscious/src/simulation_individuals.c being registry and threads
modules/conscious/src/thread_name.c            name helper
modules/conscious/src/simulation_species_*.c   per-species step (provisional)
```

Beings meaning and file layout: [doc/beings](../beings/README.md).
