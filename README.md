# steading

A small colony/settlement sim where **OpenUSD is the world, save and mod format** and the simulation runs on flat
arrays. It is built on **Strata**, a shared C++ engine.

Status: early development. The colony rules, seeded world generation and mod manifests are being built first; saves
go through Strata's save core once it lands.

## Design in one paragraph

A seed generates a **base layer**. Prefabs (settler, hut, tree, …) live in a content library that entities
reference. **Mods are USD sublayers** stacked in an explicit load order, **saves are thin delta sublayers** over
base + mods, and seasons are **variants**. The simulation never touches USD per tick: it ticks plain arrays and meets
USD only when loading, saving and recording history. Save safety comes first: a save is checksummed before USD ever
parses it, a damaged or missing layer is refused rather than silently skipped, and the previous save is always kept.

## License

MIT, see [LICENSE](LICENSE).
