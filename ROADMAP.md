# Roadmap

What each tagged version is for. The aim of the project stays in
[README.original.md](README.original.md) and in [Future ideas](README.md#future-ideas).
This file only says which slice is expected in which version. An issue
will hold the detail of a slice when work on it starts.

## 0.1.1 — shipped

Tag [`v0.1.1`](https://github.com/marianoalda/conscious/releases/tag/v0.1.1).
Documentation aligned with the 0.1 engine and shipped world (layer
files, `world.bin`, day snapshot wording, module README). Code and
world behaviour are the 0.1 line; this patch closes the wording gaps
left in 0.1.0. Follow-up [#3](https://github.com/marianoalda/conscious/issues/3)
puts remaining Spanish headings and the operator UI into English.

## 0.1.0 — shipped

Tag [`v0.1.0`](https://github.com/marianoalda/conscious/releases/tag/v0.1.0).
Time engine, world format v3, and the layered world (heightmap, water,
daylight, humidity, fertility, grass, grass age). Prefer 0.1.1 for
docs.

## 0.2 — next

- Multithreaded layer of living beings with behaviour.
- Rebuild of heightmap-view as [`world-view`](modules/utils/world-view/)
  ([#2](https://github.com/marianoalda/conscious/issues/2)): load all
  simulatable layers, underside-sheet cycling, fluid redraw, optional
  light CLI/keys — done.

Beings as a layer of their own, each on their thread, with behaviour,
inhabiting the world that 0.1 already simulates. The viewer utility lives
under `modules/utils/world-view` and opens the worlds that version produces.

## Later

Unversioned items stay under [Future ideas](README.md#future-ideas)
until they are named for a release.
