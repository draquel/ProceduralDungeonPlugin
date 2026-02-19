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

### Algorithm Classes
- `FDelaunayTetrahedralization` — 3D Bowyer-Watson (ported from Vazgriz C#)
- `FMinimumSpanningTree` — Prim's algorithm on room graph
- `FHallwayPathfinder` — Modified A* with staircase constraints
- `FRoomPlacement` — Room placement with semantic type awareness

### Output Classes
- `ADungeonActor` — Runtime actor owning a generated dungeon
- `FDungeonTileMapper` — Grid cell → static mesh mapping with ISM batching
- `UDungeonTileSet` — Data asset mapping cell types to meshes

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
