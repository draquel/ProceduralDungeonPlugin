# ProceduralDungeon — 3D Procedural Dungeon Generator

A standalone Unreal Engine 5.7 plugin for generating 3D multi-floor procedural dungeons with semantic room types, deterministic seeding, and optional VoxelWorlds integration.

## Key Features

- **3D Multi-Floor Dungeons**: Rooms span multiple floors connected by staircases with configurable rise:run ratios and headroom constraints
- **Semantic Room Types**: Entrance, Boss, Treasure, Secret, Spawn, Rest — placement rules built into the generation pipeline, not bolted on after
- **Deterministic Seeding**: Identical seed + configuration produces byte-identical dungeons across platforms and sessions. Critical for multiplayer and save/load
- **Standalone Operation**: Core generator has no external plugin dependencies. Outputs abstract data consumable by any rendering system
- **Tile-Based Output**: Default renderer maps grid cells to static meshes via configurable tile set data assets with ISM batching
- **Optional VoxelWorlds Integration**: Stamp dungeons into voxel terrain (carved underground, region replacement, structure merge) or run as instanced voxel dungeon levels
- **Three Generation Modes**: Pre-placed at world generation, on-demand at runtime, interactive editor tool
- **Configurable Scale**: Per-dungeon grid cell size (200-800+ cm) — tight horror corridors or grand halls from the same algorithm

## Algorithm Overview

Based on the [Vazgriz/TinyKeep algorithm](https://vazgriz.com/119/procedurally-generated-dungeons/), extended with semantic room awareness:

```
Place Rooms → Assign Types → Delaunay Tetrahedralization → MST (Prim's)
→ Edge Re-addition (cycles) → Mark Main Path → A* Hallway Carving → Entrances
```

The Delaunay tetrahedralization creates a graph of potential room connections in 3D. The minimum spanning tree guarantees all rooms are reachable. Random edge re-addition creates loops for interesting exploration. Modified A\* carves hallways with staircase support, merging corridors that pass through the same area.

## Plugin Structure

```
ProceduralDungeon/
├── Source/
│   ├── DungeonCore/              ← Algorithm & data structures (no plugin deps)
│   ├── DungeonOutput/            ← Tile-based static mesh rendering
│   ├── DungeonEditor/            ← Editor tools & preview (Editor-only)
│   └── DungeonVoxelIntegration/  ← VoxelWorlds bridge (optional)
├── Content/
│   ├── DefaultTileSet.uasset
│   └── ExampleDungeonConfig.uasset
└── Documentation/
    ├── ARCHITECTURE.md           ← Complete system design
    ├── ALGORITHM_REFERENCE.md    ← Detailed algorithm walkthrough
    └── QUICK_START.md
```

### Module Dependencies

```
DungeonEditor (Editor only)
    ├── → DungeonCore
    └── → DungeonOutput

DungeonOutput (Runtime)
    └── → DungeonCore

DungeonVoxelIntegration (Runtime, optional)
    ├── → DungeonCore
    └── → VoxelCore (VoxelWorlds plugin)

DungeonCore (Runtime)
    └── (no plugin dependencies)
```

## Quick Start

### Prerequisites

- Unreal Engine 5.7
- Visual Studio 2022 or Rider
- (Optional) VoxelWorlds plugin for voxel integration

### Installation

1. Copy the `ProceduralDungeon` folder into your project's `Plugins/` directory
2. Regenerate project files
3. Build the project

If using VoxelWorlds integration, enable both plugins and set `DungeonVoxelIntegration` to `Enabled: true` in the `.uplugin`.

### Basic Usage (Blueprint)

1. Place an `ADungeonActor` in your level
2. Assign a `UDungeonConfiguration` data asset
3. Assign a `UDungeonTileSet` data asset
4. Set a seed (or leave 0 for random)
5. Call `GenerateDungeon()` — from Blueprint, on BeginPlay, or from the editor

### Basic Usage (C++)

```cpp
#include "DungeonGenerator.h"
#include "DungeonConfig.h"

// Generate a dungeon
UDungeonConfiguration* Config = LoadObject<UDungeonConfiguration>(...);
UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();

FDungeonResult Result = Generator->Generate(Config, /*Seed=*/ 42);

// Query the result
const FDungeonRoom* BossRoom = Result.FindRoomByType(EDungeonRoomType::Boss);
FVector EntranceWorldPos = Result.GridToWorld(Result.EntranceCell);

// The result contains the full grid, rooms, hallways, staircases, and graph data
```

### VoxelWorlds Integration

```cpp
#include "DungeonVoxelStamper.h"

// Stamp dungeon into voxel terrain
UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
Stamper->StampDungeon(
    Result,
    VoxelChunkManager,
    FVector(0, 0, -5000),              // Underground offset
    EDungeonStampMode::CarveUnderground,
    MaterialMap);

// Stitch entrance to terrain surface
UDungeonEntranceStitcher* Stitcher = NewObject<UDungeonEntranceStitcher>();
Stitcher->StitchEntrance(
    Result,
    VoxelChunkManager,
    FVector(0, 0, -5000),
    EDungeonEntranceStyle::CaveOpening);
```

## Configuration Reference

`UDungeonConfiguration` exposes all generation parameters as a data asset:

| Category | Parameter | Default | Description |
|----------|-----------|---------|-------------|
| Grid | `GridSize` | 30×5×30 | Width × Floors × Depth in cells |
| Grid | `CellWorldSize` | 400.0 | World units (cm) per grid cell |
| Rooms | `RoomCount` | 8 | Target number of rooms |
| Rooms | `MinRoomSize` | 3×1×3 | Minimum room dimensions (cells) |
| Rooms | `MaxRoomSize` | 7×2×7 | Maximum room dimensions (cells) |
| Rooms | `RoomBuffer` | 1 | Minimum gap between rooms (cells) |
| Hallways | `EdgeReadditionChance` | 0.125 | Probability of adding cycles |
| Hallways | `HallwayMergeCostMultiplier` | 0.5 | Cost reduction for reusing hallways |
| Staircases | `StaircaseRiseToRun` | 2 | Horizontal cells per vertical cell |
| Staircases | `StaircaseHeadroom` | 2 | Open cells above staircase body |
| Entrance | `EntrancePlacement` | BoundaryEdge | Where the entrance room is placed |
| Seed | `bUseFixedSeed` | false | Use deterministic seed |
| Seed | `FixedSeed` | 0 | Seed value when fixed |

### Room Type Rules

Room types are configured via `TArray<FDungeonRoomTypeRule>` on the configuration asset. Each rule specifies:

- Room type and count
- Min/max graph distance from entrance (normalized 0-1)
- Preference for leaf nodes (treasure), main path (spawn), multi-floor (stairwell)
- Minimum size override

## Room Types

| Type | Placement Logic | Purpose |
|------|----------------|---------|
| **Entrance** | Grid boundary, placed first | Player spawn / dungeon entry |
| **Boss** | Farthest from entrance, largest room | End-game encounter |
| **Treasure** | Leaf nodes in MST, small rooms | Reward rooms |
| **Secret** | Only reachable via non-MST edges | Hidden content |
| **Spawn** | On main path, medium rooms | Combat encounters |
| **Rest** | Moderate distance, off critical path | Safe areas |
| **Stairwell** | Multi-floor rooms (Size.Y ≥ 2) | Vertical hubs |
| **Corridor** | One dimension ≥ 2× others | Wide hallway rooms |
| **Generic** | Unassigned rooms | Filler / gameplay content |
| **Custom** | User-defined via subclass or data asset | Extensibility |

## VoxelWorlds Integration Modes

### Embedded (Carved into Terrain)

The dungeon is stamped into an existing VoxelWorlds world as edit layer data. Three stamp modes:

- **CarveUnderground**: Dungeon exists below terrain surface with an entrance opening
- **ReplaceRegion**: Dungeon replaces a volume of terrain entirely
- **MergeAsStructure**: Dungeon data merged respecting terrain shape

Entrance stitching connects the dungeon to the surface via vertical shafts, sloped tunnels, cave openings, or trapdoors.

### Instanced (Separate Level)

The dungeon is a standalone sublevel with its own `AVoxelWorld` using `FVoxelDungeonWorldMode`. The player transitions via level streaming or seamless travel. No LOD needed — bounded, small world.

## Key Data Structures

| Structure | Size | Description |
|-----------|------|-------------|
| `FDungeonCell` | 8 bytes | Per-cell: type, room index, floor, material hint, flags |
| `FDungeonRoom` | ~128 bytes | Room metadata, connectivity, semantic type |
| `FDungeonHallway` | Variable | Path cells between two rooms |
| `FDungeonStaircase` | ~64 bytes | Vertical connection with occupied cells |
| `FDungeonResult` | Variable | Complete immutable generation output |
| `FDungeonGrid` | GridSize × 8B | Full 3D grid (36 KB for 30×5×30) |

## Performance

| Grid Size | Rooms | Generation Time | Memory |
|-----------|-------|----------------|--------|
| 30×3×30 | 6-8 | < 5ms | ~36 KB |
| 30×5×30 | 8-12 | 5-15ms | ~36 KB |
| 50×8×50 | 15-20 | 15-50ms | ~200 KB |
| 50×10×50 | 20-25 | 50-150ms | ~200 KB |

Generation is CPU-only (inherently sequential algorithm). Large dungeons should use async generation.

## Development Roadmap

| Phase | Scope | Duration |
|-------|-------|----------|
| 1 | Core algorithm (2D first, then 3D) | Weeks 1-2 |
| 2 | 3D extension (tetrahedralization, staircases) | Week 3 |
| 3 | Room semantics & entrance system | Week 4 |
| 4 | Tile-based output & ADungeonActor | Week 5 |
| 5 | Editor tools & preview | Week 6 |
| 6 | VoxelWorlds integration | Weeks 7-8 |
| 7 | Polish, optimization, async, Blueprint API | Week 9 |

See [IMPLEMENTATION_PHASES in ARCHITECTURE.md](Documentation/ARCHITECTURE.md) for detailed task breakdowns.

## Documentation

- **[Architecture](Documentation/ARCHITECTURE.md)** — Complete system design, data structures, pipeline, integration layer, design decisions
- **[Algorithm Reference](Documentation/ALGORITHM_REFERENCE.md)** — Step-by-step algorithm walkthrough with diagrams
- **[Quick Start](Documentation/QUICK_START.md)** — Getting started guide

For Claude Code assistance, project context is in `.claude/instructions.md`.

## References

- [Vazgriz: Procedurally Generated Dungeons](https://vazgriz.com/119/procedurally-generated-dungeons/)
- [TinyKeep Algorithm (Reddit)](https://www.reddit.com/r/gamedev/comments/1dlwc4/procedural_dungeon_generation_algorithm_explained/)
- [Bowyer-Watson Algorithm](https://en.wikipedia.org/wiki/Bowyer%E2%80%93Watson_algorithm)
- [Circumsphere — Wolfram MathWorld](http://mathworld.wolfram.com/Circumsphere.html)

---

**Target Engine**: Unreal Engine 5.7  
**Language**: C++17  
**Status**: Architecture Complete, Implementation Pending
