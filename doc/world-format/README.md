# World Format

This directory documents the binary file format used to store Conscious world state.

The format is versioned so that the application can continue to load worlds created by older versions of Conscious as the world model evolves.

## Format versions

| Version | Status         | Documentation      |
| ------- | -------------- | ------------------ |
| 0       | Initial format | [Version 0](v0.md) |
| 1       | Defined        | [Version 1](v1.md) |

Future versions will be added as separate documents without replacing the documentation of previous versions.

## Compatibility

`world_load()` is responsible for detecting the format version stored in a world file and dispatching to the appropriate deserializer.

Conceptually:

```text
world_load()
    │
    ├── detect file format
    │
    ├── version 0 ──> deserialize_v0()
    │
    ├── version 1 ──> deserialize_v1()
    │
    └── future versions ──> corresponding deserializer
```

Each version-specific deserializer converts its corresponding file format into the current in-memory world representation.

The simulation itself does not need to know which file format version was used to load the world.

## Writing world files

`world_serialize()` writes the current world format.

At present, the current format is **version 1**. Therefore, `world_serialize()` writes World Format v1 regardless of the format version from which the world was originally loaded.

Older formats are retained for reading compatibility, but they are not used for new world files.

When an older world is loaded and subsequently serialized, the world may therefore be upgraded to the current format:

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
World Format v1
```

This means that format compatibility is asymmetric:

* older formats may remain supported for reading;
* writing uses only the current format.

## Format evolution

A new format version should be introduced when an existing version cannot represent the required world state without changing its binary structure or semantics.

When a new version is introduced:

1. The existing version documentation must remain unchanged.
2. A new `vN.md` document must be created.
3. The new format must have an unambiguous version identifier.
4. A deserializer for the new version must be added.
5. `world_load()` must retain support for all previously supported versions.
6. `world_serialize()` should normally write only the current format.

This preserves the ability to open historical world files while allowing the internal world representation and file format to evolve independently.

## Binary format conventions

Individual format specifications define the exact byte layout of each version.

Unless a version explicitly specifies otherwise:

* Integer fields have an explicitly defined width.
* Byte order is explicitly specified for every multi-byte numeric field.
* Field offsets and sizes are part of the format specification.
* The binary representation must not depend on the size or layout of C implementation-defined types such as `int`, `long`, or structures.
* Changes to the binary layout require a new format version.

## Version documentation

* [World Format Version 0](v0.md)
* [World Format Version 1](v1.md)

Each version document is the authoritative specification for that version's binary representation.
