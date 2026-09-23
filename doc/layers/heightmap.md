# Heightmap

`TYPE_HEIGHTMAP`, name `LYR_HEIGHTMAP`.

Terrain elevation in millimetres:

```text
elevation = min_height + stored_offset
```

The cell size divides the world width and the world depth. The shipped worlds use 100 mm.

## Evolution

Version 1 stored this grid as the whole world. Version 2 added the layer clock. Version 3 kept the same grid and allowed further layers after it. A file still has to contain one heightmap to be written back.

The shipped heightmaps use `CLK_NOEV`. Nothing in the engine changes the elevation.

## Simulation

**Placeholder.** `simulate_heightmap` ignores the grid. With `CLK_NOEV` the engine does not call it.

## Dependencies

It reads no other layer. No other simulation function reads it. Static water uses the same cell size, and the water surface is the terrain elevation plus the water depth. That sum is a meaning of the two stored grids, not a step in this function.
