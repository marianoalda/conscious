# World Format

This directory documents the binary file format used to store Conscious world state.

The format is versioned so that the application can continue to load worlds created by older versions of Conscious as the world model evolves.

## Units

The format does not store a unit field. The units are defined here and in each version that uses them.

| Quantity                                         | Unit        | Symbol |
| ------------------------------------------------ | ----------- | ------ |
| Width, depth, cell size, and heights             | millimetre  | mm     |
| World age and a layer's last simulation tick     | millisecond | ms     |

One world tick is one millisecond.

Version 1 introduces the distance fields. Version 2 introduces the layer clock, whose period is a power of two of the world tick. Version 3 is the first format that stores the world age and each layer's last simulation tick, both in milliseconds.

## Format versions

| Version | Status                         | Documentation      |
| ------- | ------------------------------ | ------------------ |
| 0       | Historical, header only        | [Version 0](v0.md) |
| 1       | Historical, one heightmap      | [Version 1](v1.md) |
| 2       | Historical, modularity and clock | [Version 2](v2.md) |
| 3       | Current format                 | [Version 3](v3.md) |

Future versions are added as separate documents. The document of an older version stays in place. A factual error in an older document may be corrected. Its binary layout is not revised in order to describe a newer format.

## What each version added

```text
v0    CWLD + version
v1    dimensions + one dense heightmap
v2    world modularity + layer clock
v3    world age + last simulation tick of the layer
```

Version 2 removes version 1's `EV_N` field. The clock is a new field, not a new reading of `EV_N`.

## Compatibility

`world_load()` reads the magic and the version, then calls the deserializer for that version.

```text
world_load()
    │
    ├── CWLD and version
    │
    ├── version 0 ──> world_v0_load()
    │
    ├── version 1 ──> world_v1_load()
    │
    ├── version 2 ──> world_v2_load()
    │
    ├── version 3 ──> world_v3_load()
    │
    └── any other version ──> unsupported version
```

Each deserializer produces the current in-memory world. The simulation does not need to know which file version was loaded.

Fields that an older file does not contain are filled as follows:

| Loaded version | Modularity | Age   | Heightmap clock      | Last simulation tick |
| -------------- | ---------- | ----- | -------------------- | -------------------- |
| 0              | `CLOSED`   | 0 ms  | no layer             | no layer             |
| 1              | `CLOSED`   | 0 ms  | `CLK_NOEV`           | 0 ms                 |
| 2              | from file  | 0 ms  | from file            | 0 ms                 |
| 3              | from file  | from file | from file         | from file            |

Version 0 has no heightmap. Versions 1, 2, and 3 each contain exactly one.

## Writing world files

`world_serialize()` writes version 3.

Older formats remain readable. They are not used for new files. Loading an older world and saving it upgrades the file:

```text
old world file
      │
      ▼
 world_load()
      │
      ▼
 current world representation
      │
      ▼
world_serialize()
      │
      ▼
World Format v3
```

Compatibility is asymmetric:

* older formats stay supported for reading;
* writing uses only the current format.

The version-specific writers for versions 1 and 2 still exist and still emit those layouts. `world_serialize()` does not call them.

## Format evolution

A new format version is introduced when the current version cannot represent the required world state without changing its binary structure or its meaning.

When a new version is introduced:

1. The older version documents stay, and their binary layouts stay.
2. A new `vN.md` document is created.
3. The new format has its own version number.
4. A deserializer for the new version is added.
5. `world_load()` keeps every previous version.
6. `world_serialize()` writes only the current format.

## Binary format conventions

Each version document defines the byte layout of that version.

Unless a version says otherwise:

* Integer fields have an explicit width.
* Every multi-byte integer is big-endian.
* Offsets and sizes are part of the specification.
* The file does not depend on the size or layout of C types such as `int`, `long`, or structures.
* A change to the binary layout requires a new format version.

Fixed text fields are written zero-padded to their full width. The magic, the layer marker, the clock, the storage code, and the modularity field are matched in full, including the padding defined for that field.

## Version documentation

* [World Format Version 0](v0.md)
* [World Format Version 1](v1.md)
* [World Format Version 2](v2.md)
* [World Format Version 3](v3.md)

Each version document is the specification of that version's bytes.
