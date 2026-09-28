# Grass age

`TYPE_GRASSAGE`, name `LYR_GRASSAGE`.

Age of the grass stand, in days of this layer's clock. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The value is the age, not an offset. 0 is a dead cell. 255 is the last living day: on that wake the stand dies.

The shipped worlds that carry grass also carry this grid, empty, on `CLK_0026` (67108864 ms, a little under one calendar day). One on-time wake therefore adds one stored day. After 255 living wakes the plant is about 198 calendar days old.

## Evolution

The layer was added so grass can die of old age before growth exists. Height still lives in `TYPE_GRASS`. This grid only counts days while that height is greater than 0.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 8-bit value. A cell with published grass height 0 is dead: pending age is 0, even if the stored age was not. A living cell adds the number of layer periods since `last_simulation_tick`. An on-time wake adds 1. A skipped wake adds the missed periods, so a late call does not stall ageing.

When the sum would reach 255 (`GRASS_AGE_MAX`), the cell dies in this wake:

* pending age is 0
* grass height, published and pending, is 0
* fertility's grass inbox receives `GRASS_DEATH_FERTILITY_RETURN`

That return is twice `GRASS_SOIL_FERTILITY_TO_MAX`. The soil budget to grow from height 0 to 255 mm is `FERTILITY_UPTAKE_PER_MM` (1) times 255, which is 255. Death returns 510: the soil N plus the mass the plant built from photosynthesis. The fertility inbox holds that signed 510 until the next fertility wake. The 8-bit fertility cell then clamps to 255. Growth, when it runs, will subtract from the same 255-unit soil budget in proportion to millimetres gained.

Grass height is written here because death must take effect in the same wake. `CLK_0026` ticks are also `CLK_0016` ticks, so the grass layer is due and can publish the zeros. A missing grass layer leaves age at 0. A missing fertility layer drops the inbox write.

Tick 0 does not add a day: `last_simulation_tick` starts at 0.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Grass             | Published height. 0 keeps age at 0. Death writes height 0. |
| Fertility         | Signed inbox, `GRASS_DEATH_FERTILITY_RETURN` on old age.   |
| Humidity          | None.                                                      |
| Diffuse daylight  | None.                                                      |
| Heightmap         | None.                                                      |
| Static water      | None.                                                      |

The viewer does not paint this layer.
