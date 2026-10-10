# ProceduralDungeon — Claude Code Instructions

## Project Overview

ProceduralDungeon is a **standalone Unreal Engine 5.7 plugin** that generates 3D procedural dungeons using a TinyKeep-derived algorithm (Vazgriz). It operates independently but has an optional integration layer for the VoxelWorlds voxel terrain plugin.

**Reference Algorithm**: https://vazgriz.com/119/procedurally-generated-dungeons/

## Architecture Principles

1. **Standalone Core**: `DungeonCore` has zero plugin dependencies — only UE Core/CoreUObject/Engine. All generation logic lives here.
2. **Optional VoxelWorlds Integration**: `DungeonVoxelIntegration` module is disabled by default. It bridges dungeon data to VoxelWorlds via edit layer stamping or a custom `IVoxelWorldMode`.
3. **Deterministic Generation**: ALL randomness flows through `FDungeonSeed` (wraps `FRandomStream`). No `FMath::Rand()`, no hash map iteration order dependence. Forked sub-seeds per pipeline stage.
4. **Semantic Rooms**: Room types (Entrance, Boss, Treasure, Secret, etc.) are first-class concepts that affect placement rules during generation, not decorators applied after.
5. **Abstract Output**: The generator produces `FDungeonResult` (pure data). Rendering is handled by output backends (tile-based actors, voxel stamping, etc.) via `IDungeonOutputHandler`.

## Module Structure

```
DungeonCore         → Core algorithm, data structures, no plugin deps
DungeonOutput       → Tile-based static mesh output, ADungeonActor
DungeonEditor       → Editor-only tools, preview actor, debug viz
DungeonVoxelIntegration → Optional VoxelWorlds bridge (disabled by default)
```

### Dependency Rules (STRICT)

- `DungeonCore`: Depends on NOTHING except UE Core modules
- `DungeonOutput`: Depends on `DungeonCore` only
- `DungeonEditor`: Depends on `DungeonCore` + `DungeonOutput`
- `DungeonVoxelIntegration`: Depends on `DungeonCore` + `VoxelCore` (from VoxelWorlds)
- **NEVER** add VoxelWorlds dependencies to DungeonCore or DungeonOutput

## Key Interfaces & Classes

### Interfaces
- `IDungeonOutputHandler` — Abstract output consumer (tile actors, voxel stamper, etc.)

### Core Classes
- `UDungeonGenerator` — Main generation orchestrator, runs the full pipeline
- `FDungeonSeed` — Deterministic RNG wrapper with fork support
- `FDungeonGrid` — 3D integer grid holding all cell data
- `UDungeonConfiguration` — Data asset with all generation parameters
- `FDungeonBoundaryRules` — THE wall / floor / ceiling predicates (`NeedsWall`, `NeedsVerticalBoundary`, `IsOpenCell`). Every output backend (tile mapper, voxel stamper, world mode) must call these; never re-implement them in a backend — they drifted three times and each drift sealed a doorway or dropped a floor. Rule-per-test coverage in `Dungeon.BoundaryRules.*`

### Algorithm Classes
- `FDelaunayTetrahedralization` — 3D Bowyer-Watson (ported from Vazgriz C#)
- `FMinimumSpanningTree` — Prim's algorithm on room graph
- `FHallwayPathfinder` — Modified A* with staircase constraints
- `FRoomPlacement` — Room placement with semantic type awareness

### Output Classes
- `ADungeonActor` — Runtime actor owning a generated dungeon
- `FDungeonTileMapper` — Grid cell → tile instances (per piece), plus `Openings` (door leaves), `Fixtures` (wall lights); the wall profile fit and corner posts live here
- `UDungeonTileSet` — Data asset: per-type `FDungeonTileSlot` (mesh or module, weighted `Variants`), `RoomTypeOverrides`, `WallProfile`, `FixtureRules`, `DecorRules`, `InteriorLighting`, door-leaf / wall-light meshes
- `FDungeonBoundaryRules` (DungeonCore) — the ONE source of wall / floor / ceiling decisions, incl. `OwnsSharedFace`; the tile mapper and voxel stamper both call it
- `FDungeonWallProfileConformance` — measures wall-family modules (variants and overrides included) against the tileset profile; `UDungeonTileSetValidator` (DungeonEditor) surfaces it as asset validation
- `FDungeonCoverage` — light-tightness report of a tile map (needed boundaries vs placed pieces); `ADungeonActor::DescribeCoverage`
- `ADungeonActor::BuildDungeon` — the single generate + map entry point every consumer (the actor, POI gameplay) goes through
- `IDungeonInteractable`, `ADungeonDoorActor`, `ADungeonTorchActor` — the gameplay half of a tiled dungeon: the tile actor spawns one `DoorActorClass` per Doorway opening (at `LeafHinge`) and one `WallLightActorClass` per WallLight fixture (at `Anchor`) in game worlds on authority (`bSpawnInteractables`, `SpawnInteractables()`, `GetOpenings()` / `GetFixtures()` for Blueprint), and tells each actor its record through the interface. The plugin actors replicate state and animate; they keep NO persistence and NO interaction prompt (no game-framework dependency) — game layers subclass them (VoxelWorldPOI's `APOIDungeonDoor` / `APOIDungeonTorch` add the state record and `UInteractableComponent`) and set `bSpawnInteractables = false` when they hang their own
- Props (feature 3 loot, feature 5 hazards): a FloorDecor / HallwayDecor slot or variant with `Role = EDungeonPropRole::Container` or `Trap` is instanced like any decor AND recorded as an `FDungeonProp` (cell, room index, kind Container / Trap, piece, transform, plus `TileType` + `InstanceIndex` addressing its instance) in `FDungeonTileMapResult::Props` (`ADungeonActor::GetProps()`); gameplay hangs an actor on the record (VoxelWorldPOI's `APOIDungeonContainer` / `APOIDungeonTrap`) keyed by `MakeInteractableId(cell, 0, 0, kind)`. `ADungeonActor::SetTileInstanceHidden(TileType, InstanceIndex, bHidden)` collapses one instance to zero scale (a broken crate) in every render batch it contributed to, without rebuilding; the actor keeps an `InstanceLookup` from (type, index) to (batch key, batch index). The plugin spawns nothing for props itself. Test: `Dungeon.Interactables.ContainerPropsFromDecor`
- See `Documentation/ENVIRONMENT_POLISH_PLAN.md` (E1–E6 as built) and `TILE_MODULE_SYSTEM_PLAN.md`

### VoxelWorlds Integration Classes
- `UDungeonVoxelStamper` — Converts dungeon cells to voxel edit operations
- `UDungeonEntranceStitcher` — Connects dungeon entrance to terrain surface
- `FVoxelDungeonWorldMode` — IVoxelWorldMode for instanced dungeon levels
- `UDungeonVoxelConfig` — Material mapping (dungeon cells → voxel MaterialIDs)

### Core Data Structures
- `FDungeonCell` — 8 bytes per grid cell (type, room index, floor, material hint, flags)
- `FDungeonRoom` — Room with semantic type, position, size, connectivity
- `FDungeonHallway` — Carved path between two rooms
- `FDungeonStaircase` — Vertical connection with rise:run and headroom
- `FDungeonResult` — Complete immutable output of the generator

## Generation Pipeline (10 Steps)

```
1. Initialize Grid → 2. Seed RNG → 3. Place Rooms → 4. Assign Room Types
→ 5. Delaunay Tetrahedralization → 6. MST (Prim's) → 7. Edge Re-addition
→ 8. Mark Main Path → 9. A* Hallway Carving → 10. Place Entrances & Doors
```

Steps 3-4 run in two passes: structural constraints before graph, graph-based constraints after MST.

## Naming Conventions

Follow UE conventions with dungeon-specific prefixes:
- `FDungeon*` — Dungeon-specific structs
- `UDungeon*` — Dungeon UObject classes
- `ADungeon*` — Dungeon actors
- `IDungeon*` — Dungeon interfaces (currently only `IDungeonOutputHandler`)
- `EDungeon*` — Dungeon enums

## File Organization

```cpp
// 1. #pragma once
// 2. CoreMinimal.h
// 3. Engine includes
// 4. DungeonCore includes (for other modules)
// 5. Forward declarations
// 6. Class/struct definitions
```

Headers in `Public/`, implementations in `Private/`. One primary class per file.

## Determinism Rules (CRITICAL)

These rules ensure identical seed + config → identical dungeon across platforms:

1. **No `FMath::Rand()` or `FMath::SRand()`** — ALL randomness via `FDungeonSeed`
2. **No hash map iteration** where order matters — use `TArray` sorted by deterministic key
3. **Fork seeds per sub-system** — `Seed.Fork(1)` for rooms, `Seed.Fork(2)` for edges, etc.
4. **Integer math for grid ops** — float only for Delaunay circumsphere (deterministic given identical inputs)
5. **No platform-specific float rounding** — `FRandomStream` is cross-platform deterministic in UE
6. **Seed precedence** — an explicit non-zero `Seed` passed to `Generate` always wins; `bUseFixedSeed`/`FixedSeed` only replace the clock fallback when the caller passes 0 (`UDungeonConfiguration::ResolveSeed`). A config must never silently override a caller's seed.

## VoxelWorlds Integration Notes

When working on `DungeonVoxelIntegration`:
- Dungeon stamping uses VoxelWorlds' `UVoxelEditManager` (edit layer overlay)
- Material mapping: `FDungeonCell::MaterialHint` → `FVoxelData::MaterialID` via `UDungeonVoxelConfig`
- Entrance stitching traces from terrain surface down to dungeon entrance cell
- Instanced mode uses `FVoxelDungeonWorldMode` implementing `IVoxelWorldMode`
- Scale bridging: `CellWorldSize / VoxelSize` = voxels per dungeon cell

## Performance Constraints

- A* pathfinder is O(N²) worst-case — keep grids ≤ 50×10×50
- Generation is CPU-only (algorithm is inherently sequential)
- Large dungeons (50×10×50) should use async generation
- Tile output should use ISM batching, not individual StaticMeshComponents
- Voxel stamping may touch 20-50 chunks — queue remeshes, don't block

## Common Development Commands

```bash
# Build plugin
UE5Editor-Cmd.exe YourProject.uproject -run=UnrealBuildTool -mode=build

# Run tests
UE5Editor-Cmd.exe YourProject.uproject -run=AutomationTest -filter="Dungeon"
```

## Documentation

All documentation lives in `Documentation/`:
- `ARCHITECTURE.md` — Complete system design (START HERE)
- `ALGORITHM_REFERENCE.md` — Detailed algorithm walkthrough
- `QUICK_START.md` — Getting started

## Anti-Patterns (AVOID)

- ❌ Adding VoxelWorlds deps to DungeonCore or DungeonOutput
- ❌ Using `FMath::Rand()` anywhere in generation code
- ❌ Iterating `TMap` where order affects output
- ❌ Individual `UStaticMeshComponent` per tile (use ISM)
- ❌ Synchronous voxel stamping on game thread
- ❌ Hardcoded cell size (always use `UDungeonConfiguration::CellWorldSize`)
- ❌ Modifying `FDungeonResult` after generation (it's immutable output)
