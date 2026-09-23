# Humidity

`TYPE_HUMIDITY`, name `LYR_HUMIDITY`.

Soil humidity. The layer does not receive deltas from other layers. It saturates itself where water stands, diffuses to its own neighbours in proportion to the difference, and evaporates by reading the published radiation.

## Evolution

The layer was added as an empty `ST_8` grid, cell size 100 mm, value 0 for dry and 255 for saturated. That scale could not hold a slow change. The stored cell is now `ST16`: 0 is dry and 65535 is saturated. The minimum field is 0 and is not added to the cell. The shipped world uses `CLK_0016` (65536 ms) and starts at 0.

The first running function paid a flat amount across each edge once a cell held at least 4, and evaporation removed 10 % of saturation per hour at full sun. A wet cell and a nearly empty one then lost the same absolute amount, and two soil cells that could both pay exchanged the same amount, so the net between them was zero. The bank dried, and a long damp tail could not be kept at the same time. The function below replaces that kernel.

## Simulation

**Implemented.** Not validated.

Each cell is an unsigned 16-bit value. 0 is 0 %. 65535 (`2^16 − 1`) is 100 %. A cell never stays below 0 or above 65535.

The hourly rates use world time. The clock only chooses how often the function runs. A cycle that fires on time covers the layer period. If the call is late, the evaporation interval is the milliseconds since `last_simulation_tick`, so a skipped wake does not drop hours of radiation.

The function owns every change of humidity. Water and light keep their values. Humidity reads them. The function is provisional: the grass divisor is not part of it yet.

### Latest function

No other layer deposits into humidity, so the cycle does not fold a foreign delta. The step has already copied `published` into `pending`. The three operations below write `pending`. `published` stays as it was until every due layer has finished reading.

**1. Standing water.** Every humidity cell whose centre lies on static water of depth greater than 0 is set to 65535. Depth 0 is dry. On the shipped world both grids are 100 mm, so this is the same index. Water is an absolute source: the assignment replaces the cell, and the water layer is not reduced. The same assignment runs again after evaporation, so a longer cycle cannot leave the pond below saturation.

**2. Diffusion.** Orthogonal neighbours only. The flow across one edge is proportional to the difference of the two cells. Soil exchange sums to zero. Standing water is an absolute source: a wet neighbour counts as 65535, and the water cell does not lose the humidity it gives.

On a 100 mm cell the rate is `0.25 · 3600000/65536` per hour per unit of difference. One on-time `CLK_0016` wake therefore moves a quarter of the difference with each neighbour. That is the largest explicit step that stays stable with four neighbours. A longer cycle is split into steps of that size; each step diffuses, evaporates, and saturates standing water again. A cell of another size moves `(100 / cell_mm)^2` times the 100 mm rate, because the stored value is a concentration. The clock divisor does not appear except as the elapsed hours.

```text
rate = (0.25 · 3600000/65536) · (100 / cell_mm)^2
flow = rate · hours_of_this_step · (neighbour − cell)
```

A missing `CLOSED` edge carries no flow. On a `MODULAR` world the neighbour past an edge is the cell on the opposite edge. The net added to a cell is rounded to the nearest integer and clamped to 0..65535. The section below is why the rate is this one, and what profile it implies on the shipped pool.

**3. Evaporation.** Radiation removes 10 % of the humidity still in the cell per hour at maximum radiation. At a lower irradiance the loss scales by the published fraction `irradiance / max_irradiance`, clamped to 0..1. If the maximum irradiance is 0, or the light layer is absent, the fraction is 0. The loss does not scale with the cell size.

```text
loss = 0.1 · humidity · radiation_fraction · hours_of_this_step
```

`hours` is the milliseconds since the previous humidity tick, divided by 3600000, split across the stable steps above. The loss is rounded to the nearest integer and subtracted after the diffusion of that step. Below a few hundred units the product of one `CLK_0016` wake rounds to 0, so that tail is not removed. Standing water is set back to 65535 after the subtraction.

**Pending: the grass divisor.** When grass has a simulation, its published height will scale this loss. The intended factor is 1 at height 0 and 0.5 at height 255, linear in between:

```text
grass_divisor = 1 − 0.5 · (grass_height / 255)
```

That factor is not applied. This function does not read the grass layer. Until the divisor exists, evaporation uses the loss above with grass divisor 1.

The published light is the last value the daylight layer wrote. Humidity does not wait for the next daylight tick. With daylight on `CLK_0018` and humidity on `CLK_0016`, several humidity cycles reuse the same irradiance.

### Why this rate

The rate is the fastest explicit step that stays stable, and it is fast enough for a wet metre and a tail at 10 m. This is an estimate of the continuous equation, not a recorded run of the world. The function status stays implemented, not validated.

An explicit step with four neighbours stays stable while each neighbour contributes at most a quarter of the difference. On an on-time `CLK_0016` wake that quarter is the whole step, so the hourly rate on a 100 mm cell is fixed:

```text
hours     = 65536 / 3600000          ≈ 0.01820 h
rate      = 0.25 / hours             ≈ 13.733 / h
```

The stored value is a concentration. The diffusivity that keeps a length in metres is that rate times the cell area:

```text
D = 13.733 · (0.1 m)² ≈ 0.1373 m²/h
```

At full sun the loss is `0.1 · H` per hour. The steady balance `D ∇²H = 0.1 H` decays over

```text
λ_noon = sqrt(D / 0.1) ≈ 1.17 m
```

Over a full day the half-sine averages `1/π` of full sun, because the 12 daylight hours integrate to `24/π` hours of noon. The mean loss coefficient is `0.1/π` per hour, and the mean length is longer by `sqrt(π)`:

```text
λ_mean = 1.17 · sqrt(π) ≈ 2.08 m
```

On the shipped pool the shore is a circle of radius 1 m held at 65535. The steady field outside it is the modified Bessel function

```text
H(r) = 65535 · K₀(r / λ) / K₀(1 m / λ)
```

with `r` the distance from the pool centre. Reading that curve:

| Distance past the shore | Full sun | Daily mean |
| ----------------------- | -------- | ---------- |
| 1 m                     | 20600, about 31 % of saturation | 30400, about 46 % |
| 10 m                    | 4        | 180        |

So at noon the first metre is still about a third saturated, and 10 m still holds a few units. Across the day the same places sit near half saturation and near 180.

One wake shows the same arithmetic before it has settled into that curve. A dry soil cell with a single water neighbour and three dry ones receives `0.25 · 65535 = 16383.75`, which rounds to 16384. At full sun the same wake then removes `0.1 · 16384 · 65536/3600000 ≈ 29.83`, which rounds to 30, and the cell is left at 16354. A cell at 2000 whose four neighbours are also 2000 does not diffuse; the same sun removes `0.1 · 2000 · 65536/3600000 ≈ 3.64`, which rounds to 4, and the cell is left at 1996. Grass does not enter either result: the divisor is pending.

The rounding cuts the far tail. At full sun one `CLK_0016` wake removes a unit only when `0.1 · H · 65536/3600000` reaches 0.5, which is `H ≥ 275`. Below that the wake removes nothing, so the tail past 10 m lasts longer than the Bessel curve.

## Dependencies

| Other layer       | Role in this function                                      |
| ----------------- | ---------------------------------------------------------- |
| Static water      | Absolute source. Wet cells are set to 65535. Water is unchanged. |
| Diffuse daylight  | Published irradiance. Sampled, not pushed by the light function. |
| Grass             | None yet. The grass divisor is pending. Grass is unchanged. |
| Fertility         | None.                                                      |
| Heightmap         | None.                                                      |

Humidity writes no other layer.
