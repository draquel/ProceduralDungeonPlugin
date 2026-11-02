# ProceduralDungeon — Claude Code Instructions

> State as of 2026-11-02 (code review CR5). Terms: `Documentation/GLOSSARY.md` ("Dungeons and places").

A standalone Unreal Engine 5.8 plugin that generates 3D multi-floor dungeons (a TinyKeep-derived algorithm, after
Vazgriz), dresses them with instanced tiles and stamps them into VoxelWorlds terrain. These are the rules; the system
itself is described in `Documentation/ARCHITECTURE.md`.

## Reading order

1. `Documentation/ARCHITECTURE.md` — modules, data, the pipeline, the entrance approach, tiles and modules, the voxel
   bridge, determinism. Start here for any change.
2. The headers of the module you touch (`Source/<Module>/Public/`); each class carries its own doc comment.
3. For the voxel bridge: `Plugins/VoxelWorlds/Documentation/Features/EDIT_LAYER.md` and `WORLD_MODES.md` (frames).
4. For POI dungeons (stamp once, tile actor over the carve, interactables, saves): the VoxelWorldPOI docs,
   `Plugins/VoxelWorldPOI/Documentation/POI_DUNGEON_PLAN.md`.
5. History only: `Documentation/Archive/` (entrance placement, environment polish, tile module plans, all built).

## Modules and dependencies (strict)

| Module | Type | Depends on |
|--------|------|------------|
| `DungeonCore` | Runtime | Core, CoreUObject, Engine only |
| `DungeonOutput` | Runtime | `DungeonCore` (+ `UnrealEd` privately in editor builds) |
| `DungeonEditor` | Editor | `DungeonCore`, `DungeonOutput` |
| `DungeonVoxelIntegration` | Runtime | `DungeonCore`; VoxelWorlds' modules privately |

- Never add a VoxelWorlds dependency to `DungeonCore` or `DungeonOutput`, and nothing game-specific (interaction,
  inventory, saves) to any module: the game layer (VoxelWorldPOI) subclasses and hooks in.
- `DungeonVoxelIntegration` is loaded unconditionally by the `.uplugin` and links VoxelWorlds; it is not "disabled by
  default" (DNG-29).

## Rules

**Boundary rules.** `FDungeonBoundaryRules` (DungeonCore) is the one source of wall / floor / ceiling decisions:
`NeedsWall`, `NeedsVerticalBoundary`, `OwnsSharedFace`, `IsOpenCell`. The tile mapper and the voxel stamper call it;
never re-implement a predicate in a backend (copies drifted three times, each sealing a doorway or dropping a floor).
Every rule has a test under `Dungeon.BoundaryRules.*`; a new rule gets one.

**Determinism.** Identical seed + configuration + entrance spec must give an identical dungeon on every platform.
- All randomness through `FDungeonSeed`; no `FMath::Rand()` / `FMath::SRand()`.
- No dependence on `TMap` / `TSet` iteration order; sort by a deterministic key.
- One `Fork` per sub-system (rooms 1, edges 2, entrance selection 3, types 4, entrance room 5, Delaunay jitter 99); a
  new consumer takes a new fork number so existing layouts do not change.
- Integer math on the grid; float only in the Delaunay circumsphere tests.
- Tile choices (variants, decor) hash seed, cell, type and salt; they take no stream draws.
- Seed precedence: a non-zero `Seed` passed to `Generate` always wins; `bUseFixedSeed` / `FixedSeed` only replace the
  clock fallback for seed 0 (`UDungeonConfiguration::ResolveSeed`).

**One build path.** Anything that must agree with a tile actor's layout goes through `ADungeonActor::BuildDungeon` with
the same inputs (configuration, tileset, seed, entrance override, opening flag). The entrance spec changes room
placement, so a caller that drops it builds a different dungeon.

**The result is immutable.** Never modify an `FDungeonResult` after generation; derive what you need.

**The entrance.** Declare the approach (`FDungeonEntranceSpec`) before generation; backends read
`FDungeonResult::EntranceApproach` and `GetEntranceOpeningCell()`, never re-derive them. Approach `None` is the
standalone default, not a legacy value. A stitcher style must match the approach (`VerticalShaft`, `CaveOpening`,
`Trapdoor` → `FromAbove`; `SlopedTunnel` → `FromSide`).

**Tiles.** Bulk tiles are HISM instances, one HISM per (mesh, material) batch; never a component or actor per tile.
Actors are for what carries state (doors, torches; props hang actors on `FDungeonProp` records), counted in the tens.
Wall-family pieces are authored to the tileset's `FDungeonWallProfile`; run `UDungeonTileSet::CheckWallProfile()` (the
editor validator does on save) after changing one. A floor's walkable top is `FDungeonTileMapper::TileThickness`.
Never hard-code the cell size: read `UDungeonConfiguration::CellWorldSize`.

**Interactables.** `ADungeonDoorActor` / `ADungeonTorchActor` replicate state and animate; persistence and prompts
belong to game-layer subclasses through the virtual hooks and `IDungeonInteractable`. IDs come from
`FDungeonTileMapper::MakeInteractableId` and must stay stable.

**The voxel bridge.**
- Stamp and stitch through `StampDungeonInFrame` / `StitchEntranceInFrame` (the one entry point each): system edits
  through `UVoxelEditManager`, one operation, outside the undo history.
- The jacket is `UDungeonVoxelConfig::WallThickness` layers around every closed face: in `CarveUnderground` the lining
  (`InnerLiningLayers`, one) inside the cell and the seal outside, never into another open cell; in `CarveOnly` all of it
  outside, past `CarveMarginVoxels`. Never line more than one layer inside a cell: a jacket lined from inside filled
  one-storey rooms solid (G24). Content on a voxel-carved floor stands `GetInnerLiningThickness` over the cell's bottom
  plane (`Dungeon.Stamp.VoxelLining`, `Dungeon.Stamp.CarvedFloorCollision`).
- Frames: enumerate samples through `FDungeonVoxelLattice::ForEachSampleInBox` (never `RangeForBox` directly in new stamp
  code) so a rotated frame works, and keep the identity frame bit for bit (`Dungeon.Stamp.RotatedFrame` compares the
  edits). Tiles in a frame: `ADungeonActor::bMapInActorFrame`.
- Scale: `CellWorldSize / VoxelSize` voxels per cell (`UDungeonVoxelConfig::GetEffectiveVoxelsPerCell`).

**Performance.** The A\* pathfinder is O(N²) worst case: keep runtime grids at or under 50×50×10. Generation is
synchronous and CPU-only.

## Naming

Every type carries `Dungeon` after the UE letter: `FDungeonRoom`, `UDungeonTileSet`, `ADungeonActor`,
`IDungeonInteractable`, `EDungeonRoomType`. One primary class per file; headers in `Public/`, implementations in `Private/`, tests in
`Private/Tests/Test_*.cpp`. Function categories follow `Dungeon|Sub` (`Dungeon|Actor`, `Dungeon|Voxel|Stamper`). A
renamed reflected name gets a `[CoreRedirects]` line in `Config/DefaultProceduralDungeonPlugin.ini`.

## Tests

Automation tests are `Dungeon.*` (`Dungeon.BoundaryRules`, `Generation`, `GridValidation`, `RoomSemantics`, `Seed`,
`TileMapper`, `TileModule`, `WallProfile`, `Variety`, `Interactables`, `Coverage`, `Stamp`, `Stitch`, `VoxelLattice`,
`VoxelStampPlan`, `EntrancePassagePlan`). Run them in the editor (`Automation RunTests Dungeon`, or unreal-mcp's
automation test toolset) or headless with the project's usual `UnrealEditor-Cmd.exe` automation run. A change to the
generator also runs `Dungeon.Generation.Determinism.*`; a change to the stamper or stitcher, `Dungeon.Stamp.*` and
`Dungeon.Stitch.*`.
