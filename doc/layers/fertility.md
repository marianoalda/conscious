# Fertility

`TYPE_FERTILITY`, name `LYR_FERTILITY`.

Soil fertility. On disk, 0 is sterile and 255 is the maximum. Storage is `ST_8`, the cell size is 100 mm, and the minimum field is 0. The shipped world is an empty grid on `CLK_0016` (65536 ms).

## Evolution

The layer was added with humidity and grass so the three could feed one another. No transfer rule was written. The engine calls the same placeholder as humidity and grass.

A later sketch, not a function definition, says the stored range is too coarse for slow diffusion: a 16-bit cell, 0 sterile and 65535 maximum, would be the working scale. Grass growth would subtract fertility and grass death would add more biomass than growth had taken. Those contributions would sit in a signed accumulator, wider than the cell, until fertility's own tick folds them into the grid. The diffusion kernel, the clock, and the width of the cell are not decided. The file is still one byte per cell.

## Simulation

**Placeholder.** `simulate_coupled_u8` does not change a cell. There is no designed step sequence.

## Dependencies

No implemented or designed function reads or writes this layer yet.

The sketch says grass will push a signed delta here, and will read the published fertility plus that unposted delta before growing again. Fertility would not reach into the grass grid to discover a death after the grass height had already become 0.
