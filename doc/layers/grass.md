# Grass

`TYPE_GRASS`, name `LYR_GRASS`.

Grass height in millimetres, from 0 to 255. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The value is the height, not an offset.

The shipped grass world starts at 255 on the pool and inside it, and falls in a straight line to 0 at 5 m beyond the pool edge. Farther cells are 0. The clock on that file is `CLK_0016` (65536 ms). A daily clock has been discussed and is not what the file contains.

## Evolution

The layer was added empty, then filled with that ring so the viewer could show it. The [heightmap viewer](../../modules/heightmap-view/README.md) paints the cell green with opacity `height / 255` over the brown terrain. Water is drawn afterwards, so grass under the pool is hidden. Humidity is drawn last, as a blue sheet below the heightmap zero.

The trophic sketch, not a function definition: grass would be born, grow, and die from fertility, humidity, nearby grass, and radiation. Growth would spend fertility. Death would return more biomass than growth had taken, because the plant builds mass from water, air, and light. The height is both the stored state and the stand-in for that biomass. A later herbivore would lower the height without triggering the death return. The cell stays one byte. A daily step is enough for a change of a few millimetres; the slow diffusion that needs more than eight bits is fertility, not grass.

## Simulation

**Placeholder.** `simulate_coupled_u8` does not change a cell. Birth, growth, and death have no step sequence, no rates, and no scale between one millimetre and one fertility unit.

## Dependencies

Humidity reads the published height as a divisor on evaporation: 1 at height 0 and 0.5 at height 255. Humidity does not change grass. No function writes this layer.

The sketch says grass would read published humidity, published daylight, nearby grass, and fertility including deltas not yet folded. It would push a negative fertility delta when it grows and a positive one when it dies. Those deltas are not defined as numbers.
