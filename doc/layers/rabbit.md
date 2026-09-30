# Rabbit (individual species)

Static species `SPECIES_RABBIT_FUNCTIONAL` on a [TYPE_INDIVIDUAL](../world-format/v4.md)
layer: one pose record per being (id, x/y/z mm, orientation milliradians).
Load and save live in `world_species_rabbit.*`. Further record-layout
changes for this species stay in that serializer/deserializer, not in a
new world-format version.

## Simulation

| Status      | Meaning |
| ----------- | ------- |
| Provisional | Calendar-day step of 1 m along orientation, only to check that a rabbit moves through the world. |

Not the real hunger/thirst/flee behaviour for 0.2. The engine wakes
individual layers on calendar midnights (`86400000` ms), independent of
`CLK_NOEV` on the layer block.
