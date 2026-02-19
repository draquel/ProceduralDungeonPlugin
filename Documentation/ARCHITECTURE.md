# ProceduralDungeon Plugin — Architecture Document

**Version**: 1.0  
**Date**: 2026-02-19  
**Target Engine**: Unreal Engine 5.7  
**Relationship**: Standalone plugin with optional VoxelWorlds integration  
**Reference Algorithm**: [Vazgriz — Procedurally Generated Dungeons](https://vazgriz.com/119/procedurally-generated-dungeons/) (TinyKeep-derived, extended to 3D)

---

## Table of Contents

1. [Design Goals & Constraints](#1-design-goals--constraints)
2. [Plugin Structure & Modules](#2-plugin-structure--modules)
3. [Core Data Structures](#3-core-data-structures)
4. [Generation Pipeline](#4-generation-pipeline)
5. [Room Semantics & Entrance System](#5-room-semantics--entrance-system)
6. [Output Backends](#6-output-backends)
7. [VoxelWorlds Integration Layer](#7-voxelworlds-integration-layer)
8. [Determinism & Seed System](#8-determinism--seed-system)
9. [Generation Timing Modes](#9-generation-timing-modes)
10. [Performance Considerations](#10-performance-considerations)
11. [Implementation Phases](#11-implementation-phases)
12. [Design Decisions & Rationale](#12-design-decisions--rationale)

---

## 1. Design Goals & Constraints

### Goals

- **Standalone operation**: The plugin generates dungeon data structures that can be consumed by any rendering system. No hard dependency on VoxelWorlds or any specific renderer.
- **Optional VoxelWorlds integration**: A dedicated integration module provides dungeon-to-voxel stamping, embedded carving, and instanced dungeon levels using VoxelWorlds' existing infrastructure.
- **3D multi-floor dungeons**: Full support for vertical connectivity via staircases, with configurable rise:run ratio and headroom constraints.
- **Semantic rooms**: The generator natively understands room types (entrance, boss, treasure, spawn, etc.) and applies placement rules during generation, not as a post-process.
- **Deterministic reproduction**: Given identical seed + configuration, the generator produces byte-identical dungeon layouts across platforms and sessions. Critical for multiplayer synchronization and save/load.
- **Three generation timing modes**: Pre-placed at world generation, on-demand at runtime, and interactive editor tool.
- **Configurable scale**: Dungeon grid cell size is a per-dungeon parameter, not a compile-time constant.

### Constraints

- All generation runs on CPU (the algorithm is inherently sequential — A\* pathfinding depends on prior hallway state).
- The Delaunay tetrahedralization must be implemented from scratch (no MIT-licensed C++ implementations exist; will port/adapt from the Vazgriz C# reference).
- Staircase pathfinding uses a modified A\* that is O(N²) worst-case. Grid sizes should be kept reasonable (≤ 50×10×50 for runtime generation).

---

## 2. Plugin Structure & Modules

```
ProceduralDungeon/
├── ProceduralDungeon.uplugin
├── Source/
│   ├── DungeonCore/              ← Core data structures, algorithm, no UE dependencies beyond Core
│   │   ├── DungeonCore.Build.cs
│   │   ├── Public/
│   │   │   ├── DungeonTypes.h              // All core data structures
│   │   │   ├── DungeonConfig.h             // UDungeonConfiguration data asset
│   │   │   ├── DungeonGenerator.h          // Main generation orchestrator
│   │   │   ├── DungeonSeed.h               // Deterministic RNG wrapper
│   │   │   ├── RoomPlacement.h             // Room placement strategies
│   │   │   ├── DelaunayTetrahedralization.h // 3D Delaunay (Bowyer-Watson)
│   │   │   ├── MinimumSpanningTree.h       // Prim's algorithm
│   │   │   ├── HallwayPathfinder.h         // Modified A* with staircase support
│   │   │   └── RoomSemantics.h             // Room type definitions & placement rules
│   │   └── Private/
│   │       └── (implementations)
│   │
│   ├── DungeonOutput/            ← Output backends (tile-based actors, abstract data)
│   │   ├── DungeonOutput.Build.cs
│   │   ├── Public/
│   │   │   ├── IDungeonOutputHandler.h     // Abstract output interface
│   │   │   ├── DungeonTileMapper.h         // Grid cell → static mesh actor mapping
│   │   │   ├── DungeonTileSet.h            // Data asset: mesh assignments per cell type
│   │   │   └── ADungeonActor.h             // Runtime actor that owns a generated dungeon
│   │   └── Private/
│   │       └── (implementations)
│   │
│   ├── DungeonEditor/            ← Editor tools (optional, Editor-only module)
│   │   ├── DungeonEditor.Build.cs
│   │   ├── Public/
│   │   │   ├── DungeonEditorMode.h         // Custom editor mode
│   │   │   ├── DungeonPreviewActor.h       // In-editor preview with regeneration
│   │   │   └── DungeonEditorToolkit.h      // Detail panel customizations
│   │   └── Private/
│   │       └── (implementations)
│   │
│   └── DungeonVoxelIntegration/  ← VoxelWorlds bridge (optional module)
│       ├── DungeonVoxelIntegration.Build.cs
│       ├── Public/
│       │   ├── DungeonVoxelStamper.h       // Stamps dungeon data into voxel chunks
│       │   ├── DungeonVoxelConfig.h        // Voxel-specific config (material mapping, stamp mode)
│       │   ├── DungeonEntranceStitcher.h   // Connects dungeon entrance to terrain surface
│       │   └── VoxelDungeonWorldMode.h     // IVoxelWorldMode for instanced dungeon levels
│       └── Private/
│           └── (implementations)
│
├── Content/
│   ├── DefaultTileSet.uasset     // Starter tile set for standalone use
│   └── ExampleDungeonConfig.uasset
│
└── Documentation/
    ├── ARCHITECTURE.md           // This document
    ├── QUICK_START.md
    └── ALGORITHM_REFERENCE.md    // Detailed algorithm walkthrough
```

### Module Dependency Graph

```
DungeonEditor (Editor only)
    ├── depends on → DungeonCore
    └── depends on → DungeonOutput

DungeonOutput (Runtime)
    └── depends on → DungeonCore

DungeonVoxelIntegration (Runtime, optional)
    ├── depends on → DungeonCore
    └── depends on → VoxelCore (from VoxelWorlds plugin)

DungeonCore (Runtime)
    └── (no plugin dependencies — only Core, CoreUObject, Engine)
```

### Plugin Descriptor

```jsonc
// ProceduralDungeon.uplugin
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "FriendlyName": "Procedural Dungeon Generator",
    "Description": "3D procedural dungeon generation with semantic rooms, deterministic seeding, and optional VoxelWorlds integration",
    "Category": "Procedural Generation",
    "Modules": [
        {
            "Name": "DungeonCore",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        },
        {
            "Name": "DungeonOutput",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        },
        {
            "Name": "DungeonEditor",
            "Type": "Editor",
            "LoadingPhase": "Default"
        },
        {
            "Name": "DungeonVoxelIntegration",
            "Type": "Runtime",
            "LoadingPhase": "Default",
            "Enabled": false  // User enables when VoxelWorlds is present
        }
    ],
    "Plugins": [
        {
            "Name": "VoxelWorlds",
            "Enabled": false  // Soft dependency — only required if DungeonVoxelIntegration is enabled
        }
    ]
}
```

---

## 3. Core Data Structures

### Dungeon Grid

The dungeon exists on a 3D integer grid. Each cell has a type and metadata.

```cpp
/**
 * EDungeonCellType
 * What occupies a single grid cell.
 */
UENUM(BlueprintType)
enum class EDungeonCellType : uint8
{
    Empty,          // Solid / unused space
    Room,           // Part of a room interior
    RoomWall,       // Room boundary (generated, not user-placed)
    Hallway,        // Carved corridor
    Staircase,      // Staircase body (occupies multiple cells)
    StaircaseHead,  // Headroom above a staircase (must remain open)
    Door,           // Transition between room and hallway
    Entrance,       // Dungeon entry/exit point
};

/**
 * FDungeonCell
 * Single grid cell. 8 bytes.
 */
struct FDungeonCell
{
    EDungeonCellType CellType = EDungeonCellType::Empty;
    uint8 RoomIndex = 0;           // Which room this cell belongs to (0 = none)
    uint8 HallwayIndex = 0;        // Which hallway carved this cell (0 = none)
    uint8 FloorIndex = 0;          // Which floor/level (Y coordinate)
    uint8 MaterialHint = 0;        // Material suggestion (maps to tile set variant or voxel MaterialID)
    uint8 StaircaseDirection = 0;  // Packed direction for staircase cells (NESW + Up/Down)
    uint8 Flags = 0;               // Bitfield: bIsEntrance, bIsDoorway, bIsLit, etc.
    uint8 Reserved = 0;
};

/**
 * FDungeonGrid
 * The 3D grid that holds the entire dungeon layout.
 */
struct FDungeonGrid
{
    FIntVector GridSize;           // e.g., (30, 5, 30) for 30×30 with 5 floors
    TArray<FDungeonCell> Cells;    // Flat array, indexed as [X + Y*SizeX + Z*SizeX*SizeY]

    FDungeonCell& GetCell(int32 X, int32 Y, int32 Z);
    const FDungeonCell& GetCell(int32 X, int32 Y, int32 Z) const;
    bool IsInBounds(int32 X, int32 Y, int32 Z) const;
    FDungeonCell& GetCell(const FIntVector& Coord);
};
```

### Room Definition

```cpp
/**
 * EDungeonRoomType
 * Semantic meaning of a room. Affects placement rules and connectivity.
 */
UENUM(BlueprintType)
enum class EDungeonRoomType : uint8
{
    Generic,        // No special rules
    Entrance,       // Must be reachable from dungeon boundary; spawn point
    Boss,           // Largest room, placed far from entrance (graph distance)
    Treasure,       // Small, off main path (leaf node or near-leaf in MST)
    Spawn,          // Monster spawn room, placed along main paths
    Rest,           // Safe room, moderate distance from entrance
    Secret,         // Not on MST — only reachable via re-added edges
    Corridor,       // Extra-long narrow room (functions as wide hallway)
    Stairwell,      // Tall room spanning multiple floors (vertical hub)
    Custom,         // User-defined via subclass or data asset
};

/**
 * FDungeonRoom
 * A placed room in the dungeon.
 */
struct FDungeonRoom
{
    uint8 RoomIndex;               // Unique ID within the dungeon (1-255, 0 reserved)
    EDungeonRoomType RoomType;
    FIntVector Position;           // Grid-space origin (min corner)
    FIntVector Size;               // Grid-space dimensions (width, height, depth)
    FIntVector Center;             // Cached center point (for graph algorithms)
    
    // Connectivity
    TArray<uint8> ConnectedRoomIndices;
    bool bOnMainPath = false;      // Is this room on the MST?
    int32 GraphDistanceFromEntrance = -1;
    
    // Metadata
    int32 FloorLevel = 0;         // Primary floor this room is on
    uint8 MaterialHint = 0;       // Suggested material theme
    FString CustomTag;             // User-defined tag for gameplay hooks
};
```

### Hallway & Staircase

```cpp
/**
 * FDungeonHallway
 * A carved path between two rooms.
 */
struct FDungeonHallway
{
    uint8 HallwayIndex;
    uint8 RoomA;                   // Starting room index
    uint8 RoomB;                   // Ending room index
    TArray<FIntVector> PathCells;  // Ordered cells along the path
    bool bHasStaircase = false;
    bool bIsFromMST = true;        // MST edge vs. re-added edge
};

/**
 * FDungeonStaircase
 * Vertical connection carved by the pathfinder.
 */
struct FDungeonStaircase
{
    FIntVector BottomCell;         // Entry cell (lower floor)
    FIntVector TopCell;            // Exit cell (upper floor)
    uint8 Direction;               // NESW — horizontal direction of the staircase run
    int32 RiseRunRatio = 2;        // Run cells per 1 rise cell (default 1:2)
    int32 HeadroomCells = 2;       // Open cells above staircase body
    TArray<FIntVector> OccupiedCells;  // All cells this staircase claims
};
```

### Dungeon Result

```cpp
/**
 * FDungeonResult
 * Complete output of the dungeon generator. Immutable after generation.
 */
struct FDungeonResult
{
    // Configuration snapshot
    int64 Seed;
    FIntVector GridSize;
    float CellWorldSize;          // World units per grid cell (configurable per-dungeon)

    // Grid data
    FDungeonGrid Grid;

    // Structural elements
    TArray<FDungeonRoom> Rooms;
    TArray<FDungeonHallway> Hallways;
    TArray<FDungeonStaircase> Staircases;

    // Graph data (preserved for gameplay queries)
    TArray<TPair<uint8, uint8>> DelaunayEdges;
    TArray<TPair<uint8, uint8>> MSTEdges;
    TArray<TPair<uint8, uint8>> FinalEdges;   // MST + re-added

    // Entrance
    int32 EntranceRoomIndex = -1;
    FIntVector EntranceCell;       // Grid cell designated as entry point

    // Metrics
    double GenerationTimeMs = 0.0;
    int32 TotalRoomCells = 0;
    int32 TotalHallwayCells = 0;
    int32 TotalStaircaseCells = 0;

    // Queries
    const FDungeonRoom* FindRoomByType(EDungeonRoomType Type) const;
    const FDungeonRoom* GetEntranceRoom() const;
    FVector GridToWorld(const FIntVector& GridCoord) const;
    FIntVector WorldToGrid(const FVector& WorldPos) const;
};
```

### Configuration

```cpp
/**
 * UDungeonConfiguration
 * Data asset defining generation parameters. One per dungeon template.
 */
UCLASS(BlueprintType)
class UDungeonConfiguration : public UDataAsset
{
    GENERATED_BODY()
public:
    // --- Grid ---
    UPROPERTY(EditAnywhere, Category="Grid")
    FIntVector GridSize = FIntVector(30, 5, 30);  // X=width, Y=floors, Z=depth

    UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin="100.0", ClampMax="2000.0"))
    float CellWorldSize = 400.0f;  // World units per cell (cm). 400 = comfortable rooms.

    // --- Rooms ---
    UPROPERTY(EditAnywhere, Category="Rooms")
    int32 RoomCount = 8;

    UPROPERTY(EditAnywhere, Category="Rooms")
    FIntVector MinRoomSize = FIntVector(3, 1, 3);  // Min room dimensions in cells

    UPROPERTY(EditAnywhere, Category="Rooms")
    FIntVector MaxRoomSize = FIntVector(7, 2, 7);  // Max room dimensions

    UPROPERTY(EditAnywhere, Category="Rooms")
    int32 RoomBuffer = 1;  // Minimum gap between rooms (cells)

    UPROPERTY(EditAnywhere, Category="Rooms")
    int32 MaxPlacementAttempts = 100;  // Attempts before giving up on a room

    // --- Room Semantics ---
    UPROPERTY(EditAnywhere, Category="Room Types")
    TArray<FDungeonRoomTypeRule> RoomTypeRules;  // See RoomSemantics.h

    UPROPERTY(EditAnywhere, Category="Room Types")
    bool bGuaranteeEntrance = true;

    UPROPERTY(EditAnywhere, Category="Room Types")
    bool bGuaranteeBossRoom = true;

    // --- Hallways ---
    UPROPERTY(EditAnywhere, Category="Hallways", meta=(ClampMin="0.0", ClampMax="1.0"))
    float EdgeReadditionChance = 0.125f;  // Probability of re-adding Delaunay edges (cycles)

    UPROPERTY(EditAnywhere, Category="Hallways")
    float HallwayMergeCostMultiplier = 0.5f;  // Cost reduction when reusing existing hallway

    UPROPERTY(EditAnywhere, Category="Hallways")
    float RoomPassthroughCostMultiplier = 3.0f;  // Cost increase for pathing through rooms

    // --- Staircases (3D) ---
    UPROPERTY(EditAnywhere, Category="Staircases")
    int32 StaircaseRiseToRun = 2;  // Horizontal cells per vertical cell

    UPROPERTY(EditAnywhere, Category="Staircases")
    int32 StaircaseHeadroom = 2;  // Open cells above staircase body

    // --- Entrance ---
    UPROPERTY(EditAnywhere, Category="Entrance")
    EDungeonEntrancePlacement EntrancePlacement = EDungeonEntrancePlacement::BoundaryEdge;
    // BoundaryEdge: entrance room must touch grid boundary
    // TopFloor: entrance on highest floor (descending dungeon)
    // BottomFloor: entrance on lowest floor (ascending dungeon)
    // Any: no constraint on entrance position

    // --- Seed ---
    UPROPERTY(EditAnywhere, Category="Seed")
    bool bUseFixedSeed = false;

    UPROPERTY(EditAnywhere, Category="Seed", meta=(EditCondition="bUseFixedSeed"))
    int64 FixedSeed = 0;
};
```

---

## 4. Generation Pipeline

The generator follows the Vazgriz/TinyKeep algorithm adapted for 3D with semantic room assignment integrated into the flow.

```
┌─────────────────────────────────────────────────────────┐
│                  UDungeonGenerator                       │
│                                                         │
│  1. Initialize Grid (GridSize from config)              │
│         ↓                                               │
│  2. Seed RNG (deterministic FDungeonSeed)               │
│         ↓                                               │
│  3. Place Rooms (random position/size, no overlap)      │
│         ↓                                               │
│  4. Assign Room Types (semantic rules)      ◄── NEW     │
│         ↓                                               │
│  5. Delaunay Tetrahedralization (room centers)          │
│         ↓                                               │
│  6. Minimum Spanning Tree (Prim's, weighted by dist)    │
│         ↓                                               │
│  7. Edge Re-addition (12.5% default, bias for secrets)  │
│         ↓                                               │
│  8. Mark Main Path (BFS entrance → boss via MST)        │
│         ↓                                               │
│  9. A* Hallway Carving (with staircase constraints)     │
│         ↓                                               │
│  10. Place Entrances & Doors                            │
│         ↓                                               │
│  11. Validate (all rooms reachable, entrance exists)    │
│         ↓                                               │
│  12. Output FDungeonResult                              │
└─────────────────────────────────────────────────────────┘
```

### Step Details

#### Step 3: Room Placement

Rooms are placed by sampling random positions and sizes within the grid bounds. The algorithm:

1. For each room to place (up to `RoomCount`):
   - Sample random size within `[MinRoomSize, MaxRoomSize]`
   - Sample random position within grid bounds (respecting `RoomBuffer` from edges)
   - Check overlap with all existing rooms (including buffer zone)
   - If no overlap, place the room. Otherwise retry up to `MaxPlacementAttempts`
2. Rooms that span multiple floors have `Size.Y > 1`

Rooms are placed in priority order: Entrance first (if `bGuaranteeEntrance`), then Boss (if `bGuaranteeBossRoom`), then remaining rooms. This ensures critical rooms get favorable placement before the grid fills up.

#### Step 4: Room Type Assignment

After placement, rooms that weren't pre-assigned a type during placement are assigned based on `FDungeonRoomTypeRule`:

```cpp
/**
 * FDungeonRoomTypeRule
 * Declarative rule for assigning room types.
 */
struct FDungeonRoomTypeRule
{
    EDungeonRoomType RoomType;
    int32 Count = 1;                    // How many rooms of this type
    int32 Priority = 0;                 // Higher = assigned first

    // Placement constraints
    float MinGraphDistanceFromEntrance = 0.0f;  // Normalized 0-1 (0=adjacent, 1=farthest)
    float MaxGraphDistanceFromEntrance = 1.0f;
    bool bPreferLeafNodes = false;       // For treasure/secret rooms
    bool bPreferMainPath = false;        // For spawn rooms
    bool bRequireMultiFloor = false;     // For stairwell rooms
    FIntVector MinSize = FIntVector(0);  // Override minimum size for this type
};
```

Graph distance is computed after Step 6 (MST), so type assignment runs in two passes: structural constraints first (size, floor), graph-based constraints after MST.

#### Step 5: Delaunay Tetrahedralization

Implements 3D Bowyer-Watson algorithm:

- Input: room center points (FVector3f)
- Output: set of tetrahedra, from which edges are extracted
- Uses circumsphere tests (4×4 matrix determinant via FMatrix)
- Super-tetrahedron encompasses entire grid bounds
- Degenerate cases (coplanar rooms) handled by jittering points with deterministic RNG

This is ported from the Vazgriz C# implementation, adapted to UE C++ types.

#### Step 9: A\* Hallway Carving (Modified)

The most complex step. Key differences from standard A\*:

**Cost Function:**
- Empty cell → base cost (1.0)
- Existing hallway → reduced cost (`HallwayMergeCostMultiplier`, default 0.5)
- Room interior → high cost (`RoomPassthroughCostMultiplier`, default 3.0)
- Room wall → very high cost (5.0)
- Occupied staircase → blocked (except from valid approach direction)

**Staircase Handling:**
- Staircases move `StaircaseRiseToRun` cells horizontally per 1 cell vertically
- Cannot enter staircase from side or opposite direction
- Must verify headroom cells above staircase are claimable
- Path node tracks all previous nodes via HashSet for self-intersection avoidance (O(1) lookup, O(N) copy on extension)
- Hallways placed at both ends of staircase guaranteed

**Iteration:**
- Each hallway's A\* run modifies the grid state
- Later hallways path around earlier ones, naturally creating merging corridors
- Hallway carving order: MST edges first (guaranteed paths), then re-added edges

---

## 5. Room Semantics & Entrance System

### Semantic Placement Logic

Room type assignment integrates into the generation pipeline:

```
Placement Pass 1 (before graph):
  - Entrance: placed at grid boundary, on configured floor
  - Stairwell: requires Size.Y >= 2
  - Corridor: requires one dimension >= 2× others

Placement Pass 2 (after MST):
  - Boss: farthest graph distance from entrance, largest available room
  - Treasure: leaf nodes or near-leaf in MST, small rooms preferred
  - Secret: rooms only reachable via re-added edges (not on MST)
  - Spawn: on main path (entrance → boss), medium rooms
  - Rest: moderate graph distance, not on critical path
  - Generic: all unassigned rooms
```

### Entrance System

The entrance is a first-class concept in the generator:

```cpp
/**
 * EDungeonEntrancePlacement
 */
UENUM(BlueprintType)
enum class EDungeonEntrancePlacement : uint8
{
    BoundaryEdge,   // Room touches grid edge — natural cave/door entrance
    TopFloor,       // Entrance on highest floor — descending dungeon
    BottomFloor,    // Entrance on lowest floor — ascending dungeon
    Any,            // No positional constraint
};
```

The entrance room is always:

- Assigned `EDungeonRoomType::Entrance`
- Placed first during room generation (best chance at valid position)
- The root node for MST construction (Prim's starts here → shortest paths radiate outward)
- The origin for graph distance calculations

The entrance cell itself (`FDungeonResult::EntranceCell`) is the specific grid cell within the entrance room that faces outward (toward grid boundary) or upward/downward depending on placement mode. This cell is what output backends use to position transition triggers, doors, or terrain openings.

---

## 6. Output Backends

### Abstract Output Interface

```cpp
/**
 * IDungeonOutputHandler
 * Consumes a FDungeonResult and produces renderable/playable content.
 */
class IDungeonOutputHandler
{
public:
    virtual ~IDungeonOutputHandler() = default;

    /**
     * Process a completed dungeon result.
     * @param Result       The generated dungeon data.
     * @param WorldOffset  World-space origin for the dungeon.
     * @param Owner        The actor that will own spawned geometry (if applicable).
     */
    virtual void ProcessDungeon(
        const FDungeonResult& Result,
        const FVector& WorldOffset,
        AActor* Owner) = 0;

    /** Tear down any spawned content. */
    virtual void ClearDungeon() = 0;
};
```

### Tile-Based Static Mesh Output (DungeonOutput Module)

The default standalone output. Maps grid cells to static mesh actors using a tile set data asset.

```cpp
/**
 * UDungeonTileSet
 * Maps cell types and neighbor configurations to static meshes.
 */
UCLASS(BlueprintType)
class UDungeonTileSet : public UDataAsset
{
    GENERATED_BODY()
public:
    // Floor tiles (placed under rooms and hallways)
    UPROPERTY(EditAnywhere, Category="Floors")
    UStaticMesh* RoomFloor;

    UPROPERTY(EditAnywhere, Category="Floors")
    UStaticMesh* HallwayFloor;

    // Wall tiles (placed at room/hallway boundaries adjacent to Empty cells)
    UPROPERTY(EditAnywhere, Category="Walls")
    UStaticMesh* WallStraight;

    UPROPERTY(EditAnywhere, Category="Walls")
    UStaticMesh* WallCornerInner;

    UPROPERTY(EditAnywhere, Category="Walls")
    UStaticMesh* WallCornerOuter;

    // Ceiling tiles
    UPROPERTY(EditAnywhere, Category="Ceilings")
    UStaticMesh* RoomCeiling;

    UPROPERTY(EditAnywhere, Category="Ceilings")
    UStaticMesh* HallwayCeiling;

    // Vertical
    UPROPERTY(EditAnywhere, Category="Stairs")
    UStaticMesh* StaircaseMesh;

    UPROPERTY(EditAnywhere, Category="Doors")
    UStaticMesh* DoorFrame;

    UPROPERTY(EditAnywhere, Category="Entrance")
    UStaticMesh* EntranceMesh;

    // Per-room-type overrides (optional)
    UPROPERTY(EditAnywhere, Category="Room Overrides")
    TMap<EDungeonRoomType, UDungeonTileSet*> RoomTypeOverrides;
};
```

The `FDungeonTileMapper` iterates the grid, examines each cell and its neighbors, selects the appropriate mesh, computes rotation, and spawns `UStaticMeshComponent` instances (or `UInstancedStaticMeshComponent` for repeated tiles like floors).

### ADungeonActor

The runtime actor that owns and displays a dungeon:

```cpp
/**
 * ADungeonActor
 * Place in a level or spawn at runtime. Generates and displays a dungeon.
 */
UCLASS(BlueprintType, Blueprintable)
class ADungeonActor : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Generation")
    UDungeonConfiguration* DungeonConfig;

    UPROPERTY(EditAnywhere, Category="Rendering")
    UDungeonTileSet* TileSet;

    UPROPERTY(EditAnywhere, Category="Seed")
    int64 Seed = 0;

    /** Generate and display the dungeon. Callable from Blueprint. */
    UFUNCTION(BlueprintCallable, Category="Dungeon")
    void GenerateDungeon();

    /** Clear all spawned geometry. */
    UFUNCTION(BlueprintCallable, Category="Dungeon")
    void ClearDungeon();

    /** Access the raw dungeon data after generation. */
    UFUNCTION(BlueprintCallable, Category="Dungeon")
    const FDungeonResult& GetDungeonResult() const;

    /** Get the world-space entrance position. */
    UFUNCTION(BlueprintCallable, Category="Dungeon")
    FVector GetEntranceWorldPosition() const;

private:
    FDungeonResult CachedResult;
    TUniquePtr<IDungeonOutputHandler> OutputHandler;
};
```

---

## 7. VoxelWorlds Integration Layer

The `DungeonVoxelIntegration` module bridges dungeon data with VoxelWorlds. It provides two integration modes:

### Mode A: Embedded (Dungeon Carved into Voxel Terrain)

The dungeon is stamped into an existing VoxelWorlds world, appearing as an underground structure the player can discover.

```cpp
/**
 * EDungeonStampMode
 * How the dungeon interacts with existing voxel terrain.
 */
UENUM(BlueprintType)
enum class EDungeonStampMode : uint8
{
    CarveUnderground,   // Dungeon carved below terrain surface; entrance is a surface opening
    ReplaceRegion,      // Dungeon replaces a terrain volume entirely (void + dungeon walls)
    MergeAsStructure,   // Dungeon data merged into generation pass (respects terrain shape)
};
```

```cpp
/**
 * UDungeonVoxelStamper
 * Converts FDungeonResult into voxel edit operations on a VoxelWorlds world.
 */
UCLASS()
class UDungeonVoxelStamper : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Stamp dungeon into a voxel world.
     * @param Result       Generated dungeon data.
     * @param ChunkManager Target voxel world's chunk manager.
     * @param WorldOffset  World-space origin for the dungeon grid.
     * @param StampMode    How to interact with existing terrain.
     * @param MaterialMap  Maps DungeonCell::MaterialHint → VoxelData::MaterialID.
     */
    void StampDungeon(
        const FDungeonResult& Result,
        UVoxelChunkManager* ChunkManager,
        const FVector& WorldOffset,
        EDungeonStampMode StampMode,
        const TMap<uint8, uint8>& MaterialMap);

private:
    /** Carve a single dungeon cell as air in voxel data. */
    void CarveCell(const FIntVector& GridCoord, const FDungeonResult& Result,
                   UVoxelChunkManager* ChunkManager, const FVector& WorldOffset);

    /** Build walls: set voxels adjacent to carved cells as solid with dungeon material. */
    void BuildWalls(const FIntVector& GridCoord, const FDungeonResult& Result,
                    UVoxelChunkManager* ChunkManager, const FVector& WorldOffset,
                    const TMap<uint8, uint8>& MaterialMap);
};
```

**How carving works with VoxelWorlds:**

The stamper maps each dungeon grid cell to a region of voxels (determined by `CellWorldSize / VoxelSize`). For each non-Empty dungeon cell:

1. **Room/Hallway cells** → Set voxel density to 0 (air) in the corresponding volume
2. **Adjacent Empty cells** → Set voxel density to 255 (solid) with the dungeon wall MaterialID
3. **Floor/ceiling boundaries** → Maintain solid voxels with appropriate material

These edits are written via VoxelWorlds' `UVoxelEditManager` as overlay edits, meaning the dungeon integrates with the existing edit layer system (undo/redo, serialization, persistence).

### Entrance Stitching (Embedded Mode)

```cpp
/**
 * UDungeonEntranceStitcher
 * Connects the dungeon's entrance to the terrain surface.
 */
UCLASS()
class UDungeonEntranceStitcher : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Create a surface opening that connects terrain to the dungeon entrance.
     * Traces downward from terrain surface to find connection point,
     * then carves a vertical shaft or sloped tunnel.
     */
    void StitchEntrance(
        const FDungeonResult& Result,
        UVoxelChunkManager* ChunkManager,
        const FVector& DungeonWorldOffset,
        EDungeonEntranceStyle Style);
};

UENUM(BlueprintType)
enum class EDungeonEntranceStyle : uint8
{
    VerticalShaft,    // Straight down from surface
    SlopedTunnel,     // Gradual descent (uses staircase logic)
    CaveOpening,      // Organic-looking opening carved into hillside
    Trapdoor,         // Small surface opening (1×1 cell)
};
```

The stitcher:

1. Finds the terrain surface height above the entrance room (raycast or density sampling)
2. Carves a connection from surface to entrance cell using the selected style
3. For `CaveOpening`, applies noise-based displacement to the carved tunnel for organic feel
4. Marks surrounding surface voxels with a distinct MaterialID (e.g., "dungeon stone") to signal the entrance visually

### Mode B: Instanced (Separate Dungeon Level)

The dungeon is a standalone voxel world in its own sublevel.

```cpp
/**
 * FVoxelDungeonWorldMode : public IVoxelWorldMode
 * Custom world mode for dungeon instances.
 * Generates a bounded box of solid voxels, then applies dungeon carving.
 */
class FVoxelDungeonWorldMode : public IVoxelWorldMode
{
public:
    void SetDungeonResult(const FDungeonResult& InResult);

    // IVoxelWorldMode interface
    virtual float GetDensityAt(const FVector& WorldPos,
                               const FVoxelNoiseParams& NoiseParams) const override;
    virtual bool ShouldGenerateChunk(const FIntVector& ChunkCoord) const override;
    virtual FVector GetSpawnPosition() const override;

private:
    FDungeonResult DungeonResult;
};
```

This world mode:

- Returns solid density everywhere except inside dungeon rooms/hallways/staircases
- Only generates chunks that overlap the dungeon bounding box
- Uses the dungeon entrance cell as the spawn position
- No LOD needed (bounded, small world)
- Player transitions via level streaming or seamless travel

### Voxel Material Mapping

```cpp
/**
 * UDungeonVoxelConfig
 * Maps dungeon concepts to voxel materials.
 */
UCLASS(BlueprintType)
class UDungeonVoxelConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // Cell type → MaterialID mapping
    UPROPERTY(EditAnywhere, Category="Materials")
    uint8 WallMaterialID = 2;     // Stone

    UPROPERTY(EditAnywhere, Category="Materials")
    uint8 FloorMaterialID = 1;    // Dirt

    UPROPERTY(EditAnywhere, Category="Materials")
    uint8 CeilingMaterialID = 2;  // Stone

    UPROPERTY(EditAnywhere, Category="Materials")
    uint8 StaircaseMaterialID = 2;

    UPROPERTY(EditAnywhere, Category="Materials")
    uint8 DoorFrameMaterialID = 6; // Custom dungeon material

    // Per-room-type material overrides
    UPROPERTY(EditAnywhere, Category="Materials")
    TMap<EDungeonRoomType, uint8> RoomTypeMaterials;

    // Scale
    UPROPERTY(EditAnywhere, Category="Scale")
    int32 VoxelsPerCell = 8;       // How many voxels per dungeon grid cell
    // CellWorldSize / VoxelSize should equal this
};
```

---

## 8. Determinism & Seed System

Deterministic generation is critical for multiplayer and save/load. The seed system wraps UE's random streams:

```cpp
/**
 * FDungeonSeed
 * Deterministic RNG wrapper. All randomness in the generator flows through this.
 */
struct FDungeonSeed
{
    explicit FDungeonSeed(int64 InSeed);

    /** Get next random int in [Min, Max] inclusive. */
    int32 RandRange(int32 Min, int32 Max);

    /** Get next random float in [0, 1). */
    float FRand();

    /** Get next random bool with given probability. */
    bool RandBool(float Probability = 0.5f);

    /** Fork a child seed for a sub-system (deterministic from parent state). */
    FDungeonSeed Fork(int32 SubsystemID);

private:
    FRandomStream Stream;
};
```

### Determinism Rules

Every step in the pipeline must obey:

1. **No `FMath::Rand()`** — all randomness via `FDungeonSeed`
2. **No hash map iteration order dependence** — use sorted arrays or deterministic iteration where order matters
3. **No floating-point instability** — use integer math for grid operations; float only for Delaunay circumsphere tests (which are deterministic given identical inputs)
4. **Fork seeds per sub-system** — room placement uses `Seed.Fork(1)`, edge re-addition uses `Seed.Fork(2)`, etc. This ensures changing one system's iteration count doesn't cascade to others.
5. **Platform-identical results** — `FRandomStream` is deterministic cross-platform in UE. Avoid platform-specific float rounding.

### Seed in Multiplayer

- Server generates dungeon with seed S, sends S + config to clients
- Clients regenerate locally → identical `FDungeonResult`
- Only the seed (8 bytes) + config reference need to be replicated, not the entire grid

### Seed in Save/Load

- Save: store seed + config asset reference
- Load: regenerate dungeon from seed → identical layout
- If player edits exist (via VoxelWorlds edit layer), those are saved separately and re-applied on top

---

## 9. Generation Timing Modes

### Pre-Placed (World Generation)

Dungeon locations and seeds baked into the world at generation time:

```cpp
/**
 * FPreplacedDungeon
 * Serialized with the world save. Dungeon generates when chunks around it load.
 */
struct FPreplacedDungeon
{
    FVector WorldPosition;        // Where the dungeon is placed
    int64 Seed;
    TSoftObjectPtr<UDungeonConfiguration> Config;
    EDungeonStampMode StampMode;  // For VoxelWorlds embedded mode
    bool bGenerated = false;      // Has this dungeon been materialized?
};
```

For VoxelWorlds embedded mode, the world generator places `FPreplacedDungeon` entries at valid underground locations. When the chunk streaming system loads chunks overlapping a pre-placed dungeon that hasn't been generated yet, it triggers generation and stamping.

### On-Demand Runtime

Dungeon generates when the player triggers it (finds an entrance, enters a portal, etc.):

```cpp
// Blueprint-callable function on ADungeonActor or via subsystem
void UDungeonSubsystem::GenerateDungeonAsync(
    UDungeonConfiguration* Config,
    int64 Seed,
    const FVector& WorldPosition,
    FOnDungeonGenerated OnComplete);  // Delegate fires when done
```

Generation runs on a background thread (the algorithm is CPU-bound but self-contained). The `FDungeonResult` is returned on the game thread via delegate.

For VoxelWorlds instanced mode, this spawns a sublevel with its own `AVoxelWorld` configured with `FVoxelDungeonWorldMode`.

### Editor Tool

An editor mode that allows designers to:

1. Place a `ADungeonPreviewActor` in the level
2. Assign a `UDungeonConfiguration` and seed
3. Click "Generate" to see the dungeon in-editor with debug visualization (room colors, hallway paths, staircase markers)
4. Tweak parameters and regenerate
5. "Bake" the dungeon into the level (spawns tile actors or stamps voxels)

The editor module provides:

- Custom detail panel for `ADungeonPreviewActor`
- Viewport overlay showing grid, room types, graph edges
- Seed randomization button
- Per-room type override widget
- Export to `FPreplacedDungeon` for world-gen integration

---

## 10. Performance Considerations

### Generation Time Budgets

| Grid Size | Rooms | Expected Time | Use Case |
|-----------|-------|---------------|----------|
| 30×3×30 | 6-8 | < 5ms | Small runtime dungeon |
| 30×5×30 | 8-12 | 5-15ms | Standard dungeon |
| 50×8×50 | 15-20 | 15-50ms | Large dungeon |
| 50×10×50 | 20-25 | 50-150ms | Maximum (async only) |

The A\* pathfinder is the bottleneck. Its O(N²) staircase path-tracking dominates for larger grids.

### Memory

- `FDungeonGrid` at 30×5×30 = 4,500 cells × 8 bytes = 36 KB
- `FDungeonGrid` at 50×10×50 = 25,000 cells × 8 bytes = 200 KB
- Room/hallway metadata adds ~1-5 KB
- Total per dungeon: well under 1 MB

### Tile Output Performance

For tile-based rendering, use `UInstancedStaticMeshComponent` (ISM) per unique mesh type rather than individual `UStaticMeshComponent` per cell. A 30×5×30 dungeon might have ~2,000 non-empty cells × 3 elements (floor + wall + ceiling) = ~6,000 mesh instances, which ISM handles efficiently.

### VoxelWorlds Stamping Performance

Stamping into voxel terrain requires modifying voxels across multiple chunks. For a dungeon at `CellWorldSize=400`, `VoxelSize=100`:

- Each dungeon cell = 4×4×4 = 64 voxels
- 2,000 non-empty cells = ~128,000 voxel edits
- Spread across perhaps 20-50 chunks

This should be done async, queuing chunk remeshes as edits complete. The existing VoxelWorlds async meshing pipeline handles this naturally.

---

## 11. Implementation Phases

### Phase 1: Core Algorithm (Week 1-2)

- [ ] `DungeonCore` module setup
- [ ] `FDungeonGrid`, `FDungeonCell`, `FDungeonRoom` data structures
- [ ] `FDungeonSeed` deterministic RNG
- [ ] Room placement (random, non-overlapping, with buffer)
- [ ] 2D Delaunay triangulation (Bowyer-Watson) — start in 2D for validation
- [ ] Prim's MST
- [ ] Edge re-addition
- [ ] 2D A\* hallway pathfinding (no staircases yet)
- [ ] `FDungeonResult` assembly
- [ ] Unit tests: determinism, reachability, no overlapping rooms

### Phase 2: 3D Extension (Week 3)

- [ ] Delaunay tetrahedralization (extend Bowyer-Watson to 3D)
- [ ] 3D room placement across floors
- [ ] Staircase-aware A\* pathfinder
- [ ] Staircase data structures and headroom validation
- [ ] 3D grid validation and tests

### Phase 3: Room Semantics (Week 4)

- [ ] `EDungeonRoomType` and `FDungeonRoomTypeRule`
- [ ] Two-pass type assignment (structural + graph-based)
- [ ] Entrance placement modes
- [ ] Main path computation (BFS entrance → boss)
- [ ] Secret room detection (non-MST reachability)
- [ ] Graph distance calculations

### Phase 4: Tile Output (Week 5)

- [ ] `DungeonOutput` module setup
- [ ] `IDungeonOutputHandler` interface
- [ ] `UDungeonTileSet` data asset
- [ ] `FDungeonTileMapper` — cell-to-mesh mapping with neighbor analysis
- [ ] `ADungeonActor` runtime actor
- [ ] ISM batching for performance
- [ ] Basic starter tile set (BSP geometry or simple meshes)

### Phase 5: Editor Tools (Week 6)

- [ ] `DungeonEditor` module setup
- [ ] `ADungeonPreviewActor` with debug visualization
- [ ] Custom detail panel
- [ ] Viewport overlay (grid, rooms, graph edges)
- [ ] Regeneration and seed controls
- [ ] Bake-to-level functionality

### Phase 6: VoxelWorlds Integration (Week 7-8)

- [ ] `DungeonVoxelIntegration` module setup
- [ ] `UDungeonVoxelStamper` — grid-to-voxel edit conversion
- [ ] `EDungeonStampMode` implementations (Carve, Replace, Merge)
- [ ] `UDungeonEntranceStitcher` — surface connection
- [ ] `FVoxelDungeonWorldMode` — instanced dungeon world mode
- [ ] `UDungeonVoxelConfig` material mapping
- [ ] Integration testing with VoxelWorlds (all 3 world modes)
- [ ] Pre-placed dungeon support in chunk streaming

### Phase 7: Polish & Optimization (Week 9)

- [ ] A\* pathfinder optimization (profile and reduce allocations)
- [ ] Async generation wrapper
- [ ] Blueprint API exposure
- [ ] Comprehensive determinism test suite (cross-platform seed verification)
- [ ] Documentation and example content

---

## 12. Design Decisions & Rationale

### Why Standalone Plugin with Optional Integration?

The dungeon generator is useful without VoxelWorlds (tile-based dungeons for traditional games). Making it standalone means broader utility, simpler testing, and the VoxelWorlds integration is a clean layer rather than a tangled dependency.

### Why CPU-Only Generation?

The Vazgriz algorithm is inherently sequential: each A\* hallway run modifies the grid, and subsequent runs depend on that state. The Delaunay and MST steps are also difficult to parallelize at the scale of 8-25 rooms. GPU compute would add complexity without meaningful speedup for these problem sizes.

### Why Semantic Rooms in Core (Not a Decorator)?

Room types affect placement constraints (entrance must be at boundary, boss must be largest, secret rooms only via non-MST edges). Bolting this on after generation would require re-running placement or accepting suboptimal layouts. Integrating it into the pipeline produces better dungeons.

### Why 8-Byte Cell (Not 4)?

The extra bytes (vs. a minimal 4-byte cell) allow storing room index, hallway index, floor level, and material hint directly in each cell. This eliminates repeated spatial lookups during output generation and makes the tile mapper and voxel stamper much simpler.

### Why Configurable Cell Size (Not Fixed)?

Different games need different scales. A horror game might want tight 200cm corridors while an action RPG needs 600cm halls for combat. Since the algorithm operates on abstract grid cells, the world-unit mapping is purely an output concern and costs nothing to parameterize.

### Why Edit Layer for Voxel Stamping (Not Generation Pass)?

Using VoxelWorlds' edit layer means dungeons integrate with the existing persistence, undo/redo, and streaming systems without modification. The dungeon is "painted" on top of procedural terrain just like player edits. The alternative (a custom generation pass) would require modifying VoxelWorlds' noise pipeline, creating a tighter coupling.

The tradeoff is that edit-layer dungeons don't benefit from GPU-accelerated generation and must be stamped after terrain generates. For pre-placed dungeons this is fine (stamp once, persist). For on-demand dungeons, the stamping cost (~128K voxel edits) is acceptable on a background thread.

---

## References

- [Vazgriz: Procedurally Generated Dungeons](https://vazgriz.com/119/procedurally-generated-dungeons/)
- [TinyKeep Dungeon Generation Algorithm (Reddit)](https://www.reddit.com/r/gamedev/comments/1dlwc4/procedural_dungeon_generation_algorithm_explained/)
- [Bowyer-Watson Algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Bowyer%E2%80%93Watson_algorithm)
- [Circumsphere (Wolfram MathWorld)](http://mathworld.wolfram.com/Circumsphere.html)
- [Prim's Algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Prim%27s_algorithm)
- VoxelWorlds Plugin Documentation (see project knowledge)
