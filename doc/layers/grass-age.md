# Grass age

`TYPE_GRASSAGE`, name `LYR_GRASSAGE`.

Age of the grass stand, in days of this layer's clock. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The value is the age, not an offset. 0 is a dead cell. 255 is the last living day: [grass](grass.md) kills the stand on a later wake of its own clock.

The shipped worlds that carry grass also carry this grid, empty, on `CLK_0026` (67108864 ms, a little under one calendar day). One on-time wake therefore adds one stored day. After 255 living wakes the plant is about 198 calendar days old. `CLK_0026` ticks are also `CLK_0022` ticks, so grass is due on the same world tick that publishes age 255, and it sees that value on the next grass period.

## Evolution

The layer was added so grass can die of old age. Height lives in `TYPE_GRASS`. This grid only counts days while that height is greater than 0. Death of the stand, and the fertility return, belong to the grass function.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 8-bit value. A cell with published grass height 0 is dead: pending age is 0, even if the stored age was not. A living cell adds the number of layer periods since `last_simulation_tick`. An on-time wake adds 1. A skipped wake adds the missed periods, so a late call does not stall ageing. The sum is capped at 255 (`GRASS_AGE_MAX`). This function does not change grass height and does not post a fertility delta.

Grass reads the published age. When that age is 255, grass zeros the height, zeros this age (published and pending), and posts `2 · height` into fertility. Writing age 0 here on death keeps a corpse from counting as a mature neighbour.

A missing grass layer leaves age at 0.

Tick 0 does not add a day: `last_simulation_tick` starts at 0.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Grass             | Published height. 0 keeps age at 0.                        |
| Fertility         | None. Grass posts the death return.                        |
| Humidity          | None.                                                      |
| Diffuse daylight  | None.                                                      |
| Heightmap         | None.                                                      |
| Static water      | None.                                                      |

The viewer does not paint this layer.
