# ProceduralDungeon — 3D Procedural Dungeon Generator

> State as of 2026-11-02 (code review CR5).

An Unreal Engine 5.8 plugin that generates 3D multi-floor dungeons on an integer grid: rooms placed and given semantic
types (entrance, boss, treasure, ...), connected through a 3D Delaunay graph, a minimum spanning tree and re-added
cycles, and joined by A\* hallways with staircases, deterministically from a seed (after
[Vazgriz's TinyKeep-derived algorithm](https://vazgriz.com/119/procedurally-generated-dungeons/)). `DungeonCore`
generates pure data with no plugin dependency; `DungeonOutput` dresses it with instanced tiles and multi-mesh modules
(`ADungeonActor`, `UDungeonTileSet`, `UDungeonTileModule`) and hangs doors and torches on it; `DungeonEditor` adds the
actor's detail panel, the module tool and the tileset validator; `DungeonVoxelIntegration` stamps a dungeon into a
VoxelWorlds world and stitches its entrance to the surface. To try it, place an `ADungeonActor`, assign a
`UDungeonConfiguration` and a `UDungeonTileSet`, and press Generate.

- [Architecture](Documentation/ARCHITECTURE.md): modules, data, the pipeline, the entrance approach, tiles and modules,
  the voxel bridge, determinism.
- [Claude Code instructions](.claude/instructions.md): the rules for changing the plugin.
- [Archive](Documentation/Archive/): the built plans (entrance placement, environment polish, tile modules), as history.
- Vocabulary: `Documentation/GLOSSARY.md` at the project root.
