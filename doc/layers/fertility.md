# Fertility

`TYPE_FERTILITY`, name `LYR_FERTILITY`.

Soil fertility. The stored cell is the sum of the living soil biome and the nutrients it mineralises. On disk, 0 is sterile and 255 is the maximum. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The shipped world is an empty grid on `CLK_0016` (65536 ms).

## Evolution

The layer was added with humidity and grass so the three could feed one another. The first function treats the byte as one generic store. A later sketch still holds: the range is coarse for slow transport, and a 16-bit cell would be the working scale. Grass deltas sit in a signed accumulator until this function folds them. Fractional leftovers that do not round to a whole unit stay in a remainder that is not in the file.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 8-bit value. 0 is 0 %. 255 (`FERTILITY_MAX`) is 100 %. A cell never stays below 0 or above 255.

The hourly rates use world time. The clock only chooses how often the function runs. A cycle that fires on time covers the layer period. If the call is late, the interval is the milliseconds since `last_simulation_tick`.

The function owns every change of the fertility grid. Humidity, water, and the heightmap keep their values. Fertility reads them. Grass does not write this grid. When it later grows or dies it will push a signed delta; this function already folds that inbox at the start of the wake. The inbox stays empty while grass is a placeholder.

### Latest function

The step has already copied `published` into `pending`. The operations below write `pending`. `published` stays as it was until every due layer has finished reading.

**1. Fold grass deltas.** Each cell adds the unposted signed delta and the fractional remainder from the previous wake. Growth will subtract `FERTILITY_UPTAKE_PER_MM` (1) per millimetre. Death will add `FERTILITY_RETURN_PER_MM` (2) per millimetre. The inbox is then zero. Grass is not running yet, so this step is a no-op except for the remainder.

**2. Standing flood.** Drought does not change the stored number. In a single channel, biome death becomes nutrient in the same cell, so the net is zero. What dryness does is close the factory: a later grass function would not grow, and grass that dies would be the N pulse. Waterlogging is different. Above 85 % of saturation the cell loses up to 2 % of its store per day to anaerobic gas:

```text
flood = (H / 65535 − 0.85) / 0.15     clamped to 0 at or below 0.85
loss  = fertility · 0.02 · flood · days
```

`H` is the published humidity of this cell, not forced to saturation by standing water. A missing humidity layer skips the loss.

**3. Humidity drag.** Half of the cell (`FERTILITY_HUMIDITY_DRAG`, 0.5) is mobile. The rest stays. Across each orthogonal edge the humidity kernel of this step is reused: the same rate, the same capillary head, the same standing-water source, and the same free-surface slope buffer. The humidity that crosses the edge carries mobile fertility in proportion to how much of the source cell's water that flux is:

```text
flow    = humidity_edge_flow(H_here, H_neighbour, dz, coefficient)
carried = 0.5 · fertility_source · min(1, |flow| / H_source)
```

Soil exchange of that carried amount sums to zero. A source at humidity 0 carries nothing. A wake longer than one stable humidity step is split the same way humidity is. The slope is the same heightmap and water buffer humidity uses, at this fertility cell centre. When humidity, water, and fertility share a cell size, the published grids are indexed directly; otherwise the sample goes through millimetre coordinates. On `MODULAR` the neighbour past an edge is the cell on the opposite edge. On `CLOSED` a missing side carries nothing.

The net is rounded to the nearest integer and clamped to 0..255. The unused fraction stays in the remainder until the next wake.

### Why this moisture rule

A generic cell cannot raise N and kill the biome as two numbers. Dryness therefore does not subtract from the store. The living part stopping is an activity gate on grass, not a source of sterility. That gate is not applied here: grass does not run. Flood is the exception, because denitrification removes N from the cell as gas.

### Why this rate

The drag uses humidity's explicit step, so it stays stable on the same clock. On the shipped world, both layers use `CLK_0016`. A different divisor on either layer repeats that full-grid flux, or splits a longer wake into several substeps; see [Clock divisors on coupled layers](README.md#clock-divisors-on-coupled-layers). The 8-bit cell cannot hold a slow leak: leftovers below half a unit wait in the remainder.

## Viewer

The heightmap viewer does not paint this layer.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Humidity          | Published field. The flux across each edge is the carrier. Same clock as this layer; a different divisor repeats the flux walk, see [Clock divisors on coupled layers](README.md#clock-divisors-on-coupled-layers). |
| Static water      | Wet cells count as humidity 65535 when computing that flux. Water is unchanged. |
| Heightmap         | Published slope, through humidity's free-surface head.     |
| Grass             | Signed inbox, folded here. Empty while grass is a placeholder. Grass is not read as a height. |
| Diffuse daylight  | None.                                                      |

Fertility writes no other layer. The grass inbox is not part of the file.
