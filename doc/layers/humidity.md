# Humidity

`TYPE_HUMIDITY`, name `LYR_HUMIDITY`.

Soil humidity. The layer does not receive deltas from other layers. It saturates itself where water stands, diffuses to its own neighbours in proportion to the difference, and evaporates by reading the published radiation and the published grass.

## Evolution

The layer was added as an empty `ST_8` grid, cell size 100 mm, value 0 for dry and 255 for saturated. That scale could not hold a slow change. The stored cell is now `ST16`: 0 is dry and 65535 is saturated. The minimum field is 0 and is not added to the cell. The shipped world uses `CLK_0016` (65536 ms) and starts at 0.

The first running function paid a flat amount across each edge once a cell held at least 4, and evaporation removed 10 % of saturation per hour at full sun. A wet cell and a nearly empty one then lost the same absolute amount, and two soil cells that could both pay exchanged the same amount, so the net between them was zero. The bank dried, and a long damp tail could not be kept at the same time. The function below replaces that kernel. Evaporation then reads the stored grass height, which the grass function itself does not change.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 16-bit value. 0 is 0 %. 65535 (`2^16 − 1`) is 100 %. A cell never stays below 0 or above 65535.

The hourly rates use world time. The clock only chooses how often the function runs. A cycle that fires on time covers the layer period. If the call is late, the evaporation interval is the milliseconds since `last_simulation_tick`, so a skipped wake does not drop hours of radiation.

The function owns every change of humidity. Water, light, and grass keep their values. Humidity reads them. Grass is stored and drawn, and its own function does not change the height, so the shade humidity sees is that static field.

### Latest function

No other layer deposits into humidity, so the cycle does not fold a foreign delta. The step has already copied `published` into `pending`. The three operations below write `pending`. `published` stays as it was until every due layer has finished reading.

**1. Standing water.** Every humidity cell whose centre lies on static water of depth greater than 0 is set to 65535. Depth 0 is dry. On the shipped world both grids are 100 mm, so this is the same index. Water is an absolute source: the assignment replaces the cell, and the water layer is not reduced. The same assignment runs again after evaporation, so a longer cycle cannot leave the pond below saturation.

**2. Diffusion.** Orthogonal neighbours only. The flow across one edge is proportional to the difference of the two cells. Soil exchange sums to zero. Standing water is an absolute source: a wet neighbour counts as 65535, and the water cell does not lose the humidity it gives.

On a 100 mm cell the rate is `0.25 · 3600000/65536` per hour per unit of difference. One on-time `CLK_0016` wake therefore moves a quarter of the difference with each neighbour. That is the largest explicit step that stays stable with four neighbours. A longer cycle is split into steps of that size; each step diffuses, evaporates, and saturates standing water again. A cell of another size moves `(100 / cell_mm)^2` times the 100 mm rate, because the stored value is a concentration. The clock divisor does not appear except as the elapsed hours.

```text
rate = (0.25 · 3600000/65536) · (100 / cell_mm)^2
flow = rate · hours_of_this_step · (neighbour − cell)
```

A missing `CLOSED` edge carries no flow. On a `MODULAR` world the neighbour past an edge is the cell on the opposite edge. How that fold is measured, and how the stored grass changes the steady profile, is in [Modularity and grass](#modularity-and-grass). The net added to a cell is rounded to the nearest integer and clamped to 0..65535.

**3. Evaporation.** Radiation removes 10 % of the humidity still in the cell per hour at maximum radiation. At a lower irradiance the loss scales by the published fraction `irradiance / max_irradiance`, clamped to 0..1. If the maximum irradiance is 0, or the light layer is absent, the fraction is 0. The loss does not scale with the cell size.

Published grass scales that loss. Height 0, or a missing grass layer, leaves the divisor at 1. Height 255 halves it. Between the two it falls in a straight line:

```text
grass_divisor = 1 − 0.5 · (grass_height / 255)
loss          = 0.1 · humidity · radiation_fraction · grass_divisor · hours_of_this_step
```

`hours` is the milliseconds since the previous humidity tick, divided by 3600000, split across the stable steps above. The loss is rounded to the nearest integer and subtracted after the diffusion of that step. With no grass, below 275 units the product of one `CLK_0016` wake at full sun rounds to 0, so that tail is not removed. At height 255 the same wake needs 550 units before it removes one. Standing water is set back to 65535 after the subtraction.

The published light is the last value the daylight layer wrote. Humidity does not wait for the next daylight tick. With daylight on `CLK_0018` and humidity on `CLK_0016`, several humidity cycles reuse the same irradiance.

### Why this rate

The rate is the fastest explicit step that stays stable. The lengths below are the scale of a uniform plain. The profile of the shipped pool is computed afterwards, on the real square, with the stored grass and with either modular or closed edges. This is an estimate of the continuous equation, not a recorded run of the world. The function status stays implemented, not validated.

An explicit step with four neighbours stays stable while each neighbour contributes at most a quarter of the difference. On an on-time `CLK_0016` wake that quarter is the whole step, so the hourly rate on a 100 mm cell is fixed:

```text
hours     = 65536 / 3600000          ≈ 0.01820 h
rate      = 0.25 / hours             ≈ 13.733 / h
```

The stored value is a concentration. The diffusivity that keeps a length in metres is that rate times the cell area:

```text
D = 13.733 · (0.1 m)² ≈ 0.1373 m²/h
```

At full sun and with no grass the loss is `0.1 · H` per hour. The steady balance `D ∇²H = 0.1 H` decays over

```text
λ_noon = sqrt(D / 0.1) ≈ 1.17 m
```

Over a full day the half-sine averages `1/π` of full sun, because the 12 daylight hours integrate to `24/π` hours of noon. The mean loss coefficient is `0.1/π` per hour, and the mean length is longer by `sqrt(π)`:

```text
λ_mean = 1.17 · sqrt(π) ≈ 2.08 m
```

### Modularity and grass

Those lengths describe a uniform plain with no border. The shipped pool sits on a 20 m × 20 m square. The steady estimate uses the same `D` and the same loss, on that square, with the grass that is actually stored and with the world's own edge rule.

A neighbour is one cell east, west, north, or south. On `MODULAR` the index past an edge is the index on the opposite edge: column −1 is the last column, and the column count is column 0. Rows do the same. The grid covers the world, so that one-cell step is the world width or depth in millimetres. On `CLOSED` the neighbour does not exist and that side carries no flow. The code does this in `world_neighbor`, which any layer uses. Humidity only decides what the returned neighbour means.

Distance from the pool, when reading the result, is the shorter of the straight path and the path that crosses an edge. On this torus the farthest soil is about 13.1 m from the shore. A closed world does not fold that path. The east border of this pool is 5.4 m of soil past the shore, and humidity stops there instead of re-entering from the west.

Grass is not a second distance. The divisor is the published height of the cell that contains the centre, found with `world_cell_at`. Modularity does not move that sample. The shipped ring was written with straight distance from the pool: 255 on the pool and its rim, falling in a straight line to 0 at 5 m past the rim. It ends before the east edge, so the shade does not cross the seam. Near the shore the divisor is 0.5. At 5 m and beyond it is 1.

The steady balance on a soil cell is

```text
D ∇²H = 0.1 · L · grass_divisor · H
```

Standing water is held at 65535. `L` is 1 at noon and `1/π` over the day. Solving that on this square:

| | Modular, noon | Modular, daily mean | Closed, noon |
| --- | --- | --- | --- |
| Still about a third of saturation | 1.1 m | 1.7 m | 1.1 m |
| Still at least 1 unit | 13.1 m, the far point of the torus | 13.1 m | 12.8 m |

At noon the modular world keeps the first 1.1 m near a third saturated (about 21 000 at that distance, about 24 000 at 1 m). The far point of the torus still holds about 2 units at noon and about 170 over the day. Closed matches the modular field near the pool. Against the east border the closed noon value rises to about 860, because the wall does not let the water through, while the modular cell there is about 450 and the rest has gone around. The closed far corner is drier, about 1 unit at noon.

One wake shows the same arithmetic before it has settled into that field. With grass divisor 1, a dry soil cell with a single water neighbour and three dry ones receives `0.25 · 65535 = 16383.75`, which rounds to 16384. At full sun the same wake then removes `0.1 · 16384 · 65536/3600000 ≈ 29.83`, which rounds to 30, and the cell is left at 16354. A cell at 2000 whose four neighbours are also 2000 does not diffuse; the same sun removes `0.1 · 2000 · 65536/3600000 ≈ 3.64`, which rounds to 4, and the cell is left at 1996. The same cell under grass of height 255 loses half of that, `≈ 1.82`, which rounds to 2, and is left at 1998.

The rounding cuts the far tail. At full sun and with no grass, one `CLK_0016` wake removes a unit only when `0.1 · H · 65536/3600000` reaches 0.5, which is `H ≥ 275`. Below that the wake removes nothing, so a trace past the grass ring lasts longer than the steady curve. At height 255 the same threshold is 550.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Static water      | Absolute source. Wet cells are set to 65535. Water is unchanged. |
| Diffuse daylight  | Published irradiance. Sampled, not pushed by the light function. |
| Grass             | Published height, as the evaporation divisor. Grass is unchanged. |
| Fertility         | None.                                                      |
| Heightmap         | None.                                                      |

Humidity writes no other layer.
