# Grass

`TYPE_GRASS`, name `LYR_GRASS`.

Grass height in millimetres, from 0 to 255. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The value is the height, not an offset.

The shipped grass world fills the pool and a 5 m ring past its edge. Living cells get a seeded random height from 1 to 255 mm and a seeded random age from 0 to 254, so the stand is mixed in length and age. Farther cells are 0. Fertility under that stand is 128. The clock on that file is `CLK_0022` (4194304 ms, about 1.17 hours). Humidity and fertility stay on `CLK_0016`. That file is `modules/data/world-po-mo-fer-hu-gr-v3.bin`, written by [create_world_v3_po_mo_fer_hu_gr.py](../../modules/utils/create_world_v3_po_mo_fer_hu_gr.py) from `world-pool-mountain-v3.bin`. How to rebuild it and point `world.bin` at it is in the [root README](../../README.md#generating-the-shipped-world).

## Evolution

The layer was added empty, then filled with that ring so the viewer could show it. The [heightmap viewer](../../modules/heightmap-view/README.md) paints the cell green with opacity `height / 255` over the brown terrain. Water is drawn afterwards, so grass under the pool is hidden. Humidity is drawn last, as a blue sheet below the heightmap zero.

The height is both the stored state and the stand-in for biomass. Birth, growth, and death of old age now run. A later herbivore would lower the height without triggering the death return. The shipped fertility under the ring is 128, half of 255, so growth is not limited by fertility at the start. Mixed ages keep a living parent when the oldest plants die.

Fertility folds a signed inbox. Birth and growth subtract `FERTILITY_UPTAKE_PER_MM` (1) per millimetre gained. Death of old age computes the fertility that would grow the plant to the height it has (`FERTILITY_UPTAKE_PER_MM · height`), then returns twice that amount: half on the dying cell, half split equally among its eight Moore neighbours. That is the soil spend of the current height, not millimetres ever grown.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 8-bit value. 0 is bare. 255 (`GRASS_HEIGHT_MAX`) is the maximum height. A cell never stays below 0 or above 255.

The hourly rates use world time. The clock only chooses how often the function runs. A cycle that fires on time covers the layer period (`CLK_0022`). If the call is late, the interval is the milliseconds since `last_simulation_tick`. Tick 0 adds no growth.

The step has already copied `published` into `pending`. The operations below write `pending`. `published` stays as it was until every due layer has finished reading. Other layers are sampled as published values. Fertility is not written here: this function posts a signed delta into fertility's inbox.

### Latest function

**1. Birth.** A bare cell (height 0) becomes 1 mm when all of these hold:

* published humidity is strictly greater than 10 % of 65535
* published fertility is strictly greater than 10 % of 255
* at least one orthogonal neighbour is a living plant (height greater than 0) whose published age is strictly greater than 25 % of 255
* the fertility inbox at this cell can pay `FERTILITY_UPTAKE_PER_MM` for that millimetre

The cell then spends that unit. A missing humidity, fertility, or age layer, or a `CLOSED` edge with no neighbour, fails the test. On `MODULAR` the neighbour past an edge is the cell on the opposite edge. Fractional growth leftover is cleared on a bare cell. Birth does not grow further in the same wake.

**2. Death of old age.** A living cell whose published grass age is 255 dies in this wake:

* grass height, published and pending, is 0
* grass age, published and pending, is 0
* fertility's inbox receives twice the fertility that would grow this height (`FERTILITY_UPTAKE_PER_MM · height`): half on this cell, half split equally among the eight neighbours. A missing `CLOSED` neighbour, and leftover units that do not divide by 8, stay on this cell.

Height is zeroed in both buffers so [grass age](grass-age.md), if it is due on the same tick, already sees a dead cell. Grass age only counts days. It caps a living cell at 255 and does not zero the height. This function sees that published 255 on a later `CLK_0022` wake.

**3. Growth.** A living cell below age 255 grows toward 255. In one calendar day, at full radiation and with fertility and humidity at or above half of each layer maximum, the gain is 5 mm. Each wake scales that rate by the elapsed fraction of a day and by three factors in `0..1`:

```text
radiation = irradiance / max_irradiance     (0 if light is missing or the maximum is 0)
fertility = 1 if F ≥ 0.5 · 255, else F / (0.5 · 255)
humidity  = 1 if H ≥ 0.5 · 65535, else H / (0.5 · 65535)
gain      = 5 · days · radiation · fertility · humidity
```

Fertility and humidity are limiting only below 50 % of their stored maximum. Above that the factor is 1. Night, a missing light layer, or a dry or sterile cell stops growth. Whole millimetres are applied and paid at `FERTILITY_UPTAKE_PER_MM` each, limited by room up to 255 and by fertility still in the inbox. The unused fraction of a millimetre stays in a remainder that is not in the file. Operator `[E]`stadísticas on this layer also shows `born` and `died` for the last wake, and `Σborn` / `Σdied` since load.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Humidity          | Published cell. Birth threshold and growth factor.         |
| Fertility         | Published cell for the 10 % / 50 % tests. Inbox for spend and death return. |
| Diffuse daylight  | Published irradiance as a fraction of `max_irradiance`.    |
| Grass age         | Published age of this cell and of orthogonal neighbours. Death zeros age. |
| Heightmap         | None.                                                      |
| Static water      | None. Humidity already saturates wet soil.                 |

Humidity reads the published height as a divisor on evaporation: 1 at height 0 and 0.5 at height 255. Humidity does not change grass.
