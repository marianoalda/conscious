# Beings

Living individuals in Conscious: how they are stored, what “being”
means in the engine today, and the FUNCTIONAL / DEEP depths.

Related:

- Byte layout of the world file: [world format v4](../world-format/v4.md)
- Dense layers (terrain, grass, …): [doc/layers](../layers/README.md)
- Threads and lifecycle API: [multithreading](../architecture/multithreading.md)

---

## Beings format

Beings enter the world file only with **format version 4**. Older
versions have no individual records.

| World version | Beings on disk |
| ------------- | -------------- |
| 0–2 | None. |
| 3 | Dense layers only. No `TYPE_INDIVIDUAL`. |
| 4 (current) | Zero or more `TYPE_INDIVIDUAL` layers after the dense blocks. `world_serialize()` always writes version 4. |

### Version 4 individual-layer frame

Shared by every species. Documented in full in
[v4.md](../world-format/v4.md). Summary:

```text
_LYR + TYPE_INDIVIDUAL + LYR_INDIVIDUAL
clock, last_simulation_tick
species[32], storage_depth[16], species_version, individual_count
then individual_count records (species-specific layout)
```

- At most one layer per **species** string.
- `storage_depth` is `FUNCTIONAL` or `DEEP` (loader rejects `DEEP` until
  a species defines it).
- `species_version` versions the **per-record** payload for that species.

Further changes to what one species stores inside a record belong to
that species’ serialize/deserialize (`world_species_*.c`), not to a new
world-format version, unless this shared frame must change.

### Species record versions (on disk)

| Species | Depth | species_version | Record (today) |
| ------- | ----- | ----------------- | -------------- |
| `SPECIES_RABBIT_FUNCTIONAL` | `FUNCTIONAL` | 1 | `_IND`, id, x/y/z mm, orientation mrad (24 bytes) |

Helpers declared for later use (not required for load/save):
`world_individual_alloc_id`, `world_individual_surface_z_mm`.

There is no separate “beings format v1 / v2” number outside the world
version and `species_version`.

---

## Beings

A **being** is one record in a `TYPE_INDIVIDUAL` layer, identified by a
nonzero serial `id` within that species.

In the running engine:

1. The world holds the pose (and later other fields) in the layer
   payload.
2. `simulation_individuals_start` creates one thread per living record
   after load (name `cons <TAG> <id>`).
3. The species step runs on that thread. The dense-layer thread does
   not wake individual layers.
4. Death / vanish uses `simulation_individual_request_disappear`; the
   grid thread’s `simulation_individuals_reap` joins the thread and
   removes the record from the payload.
5. Serialize and shutdown call `simulation_individuals_stop` so no
   being thread mutates state during save.

Beings are not dense cells. They do not use `published` / `pending`
grids. They inhabit the same millimetre world as the heightmap.

Test world with two rabbits:
`modules/data/world.bin.107479040.rabbit-test` (built with
`modules/utils/add_rabbit_test.c`).

---

## Functional beings

`storage_depth = FUNCTIONAL`: pose and (later) basic behaviour
parameters. No mind payload.

### Rabbit (`SPECIES_RABBIT_FUNCTIONAL`)

| Item | Value |
| ---- | ----- |
| Thread tag | `RAB` (`WORLD_SPECIES_RABBIT_THREAD_TAG`) |
| I/O | `world_species_rabbit.*` |
| Step | `simulate_species_rabbit_one` in `simulation_species_rabbit.*` |
| Status | **Provisional** — not the real 0.2 behaviour model |

Provisional step (for checking threads and teardown only):

- Each calendar day (`86400000` ms): move 1 m along orientation
  (0 = east, positive CCW about +Z).
- After `RABBIT_LIFESPAN_DAYS` (3) calendar days since thread birth:
  return died → disappear → reap frees the record and the thread slot.
- Must not read or write humidity, fertility, grass, grass age, or
  daylight.

Real hunger, thirst, flee, reproduction, and birth→spawn remain future
work under roadmap 0.2 / issue [#1](https://github.com/marianoalda/conscious/issues/1).

---

## Deep beings (future)

`storage_depth = DEEP` means FUNCTIONAL fields plus a **mind** (internal
model, memory, or similar). The file format reserves the depth string;
the loader **rejects** `DEEP` until a species_version defines the extra
bytes.

Expected direction (not implemented):

- Same individual-layer frame; longer or versioned records per species.
- Threading model unchanged in principle (one thread per living being),
  with stricter rules for what the mind may read from published world
  state.
- Snapshots of mind state, protolanguage, and external explainers stay
  under [Future ideas](../../README.md#future-ideas) until named for a
  release.
