# Diffuse daylight

`TYPE_DIFFLIGHT`, name `LYR_DIFFLIGHT`.

Current diffuse irradiance in W/m². The field that a heightmap calls the minimum is `max_irradiance`, and it is zero or positive. Each cell stores the irradiance itself, not an offset above that maximum. Zero is night.

The cell size need not match the terrain. On the pool world one cell covers the whole 20 m × 20 m square: `cell_size` equals both width and depth, and `max_irradiance` is 1000 W/m². The stored value starts at 0.

## Evolution

The layer was added as the first grid the step changes. The curve was a half sine of the absolute world age, the same value in every cell. The first pool file used `CLK_0000`, so the cell was rewritten every millisecond. The current pool file, and the grass world copied from it, use `CLK_0018` (262144 ms, about 4.4 minutes). Between due ticks the published irradiance stays at the last computed value. The curve does not integrate the gap: when the tick is due, the cell becomes the irradiance for that absolute age.

## Simulation

**Implemented.** Not validated. Hourly snapshots of the pool world matched the curve below (0 through 06:00, then about 259, 500, 707, 866, 966, and 1000 W/m²). That check is not kept as a test.

The function does not read another layer and does not write into another layer. Evaporation is a humidity rule that reads this grid.

### Latest function

Tick 0 is 00:00. One day is 86400000 ms. The time of day is the world tick modulo one day.

| Instant | Tick within the day | Fraction of `max_irradiance` |
| ------- | ------------------- | ---------------------------- |
| 00:00 to 06:00 | 0 .. 21600000 | 0 |
| 06:00 to 18:00 | 21600000 .. 64800000 | `sin(π · (t − 21600000) / 43200000)` |
| 18:00 to 24:00 | 64800000 .. 86400000 | 0 |

Noon, tick 43200000, is fraction 1. The stored cell is that fraction times `max_irradiance`, rounded to the nearest integer. Every cell of the layer receives the same value. The result is written to `pending`. The common step then publishes it. `published` is left unchanged until that swap, so a reader on the same tick still sees the previous irradiance.

If `max_irradiance` is 0, every cell stays 0.

## Dependencies

Reads the world age only.

Read by the humidity function, as the radiation that sets evaporation. Grass is expected to read it later for growth. The grass read is not implemented. This function does not push a delta into them: the irradiance remains, and the other layer samples the published value on its own clock.
