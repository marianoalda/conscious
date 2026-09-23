# Static water

`TYPE_STATICWATER`, name `LYR_STATICWATER`.

Depth of water that does not move, evaporate, drain, or fill.

```text
depth   = min_depth + stored_offset
surface = terrain_elevation + depth
```

`min_depth` is zero or positive. A stored depth of 0 is dry ground. The cell size is the heightmap's cell size. A water block without a heightmap is invalid. The clock on disk is `CLK_NOEV`.

On the pool world the floor of the pool is the heightmap minimum, the plain is 500 mm above that floor, and the water is 500 mm deep on the pool cells only.

## Evolution

The layer was added so a second grid could describe standing water without a new file version. The loader already read layer blocks until the end of the file. The first cut was a depth of 500 mm wherever the heightmap offset was 0.

It has no rules of flow. The name records that.

## Simulation

**Implemented.** The engine treats the layer as never due, including when the file carries a divisor clock. The cells stay as stored. There is no separate simulation body.

## Dependencies

It reads nothing and writes nothing. Humidity, in the designed function, reads the published depth and treats a cell with water at the surface as a saturated source. The water grid is not reduced when that happens.
