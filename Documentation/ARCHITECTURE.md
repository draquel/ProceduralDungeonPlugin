# ProceduralDungeon Plugin — Architecture

> State as of 2026-11-02 (code review CR5). Terms: `Documentation/GLOSSARY.md` ("Dungeons and places").

A standalone Unreal Engine 5.8 plugin that generates 3D multi-floor dungeons on an integer grid, dresses them with
instanced tiles, and (optionally) stamps them into a VoxelWorlds world. The algorithm is Vazgriz's TinyKeep-derived
generator ([Procedurally Generated Dungeons](https://vazgriz.com/119/procedurally-generated-dungeons/)) extended to 3D
with semantic room types and an entrance approach. All four modules are built; the game layer that places dungeons in
the voxel world as points of interest lives in VoxelWorldPOI (`Plugins/VoxelWorldPOI/Documentation/POI_DUNGEON_PLAN.md`).

---

## Table of Contents

1. [Design Goals & Constraints](#1-design-goals--constraints)
2. [Plugin Structure & Modules](#2-plugin-structure--modules)
3. [Core Data Structures](#3-core-data-structures)
4. [Generation Pipeline](#4-generation-pipeline)
5. [Room Semantics & Entrance System](#5-room-semantics--entrance-system)
6. [Tile Output](#6-tile-output)
7. [VoxelWorlds Integration](#7-voxelworlds-integration)
8. [Determinism & Seed System](#8-determinism--seed-system)
9. [When Dungeons Are Generated](#9-when-dungeons-are-generated)
10. [Performance Considerations](#10-performance-considerations)
11. [Design Decisions & Rationale](#11-design-decisions--rationale)

---

## 1. Design Goals & Constraints

### Goals

- **Standalone generation**: `DungeonCore` produces pure data (`FDungeonResult`) with no dependency on VoxelWorlds or any
  renderer. `DungeonOutput` renders it as instanced tiles; `DungeonVoxelIntegration` stamps it into voxel terrain.
- **3D multi-floor dungeons**: staircases with a configurable rise:run ratio and headroom.
- **Semantic rooms**: room types (entrance, boss, treasure, spawn, ...) are assigned inside the pipeline, not as a
  post-process.
- **Deterministic reproduction**: identical seed + configuration (+ entrance spec) gives an identical layout, so a
  client, a reload or a stream-in rebuilds the same dungeon from a few bytes.
- **Configurable scale**: the cell size is a per-configuration value, not a constant.

### Constraints

- Generation runs on the CPU and is sequential: each A\* hallway run changes the grid the next one paths through.
- The 3D Delaunay (`FDungeonDelaunay3D`) is the plugin's own Bowyer-Watson, ported from the Vazgriz C# reference.
- The staircase-aware A\* is O(N²) worst case; keep grids at or under 50×50×10 for runtime generation.

---

## 2. Plugin Structure & Modules

```
ProceduralDungeonPlugin/
├── ProceduralDungeonPlugin.uplugin
├── Config/DefaultProceduralDungeonPlugin.ini   ← [CoreRedirects] for CR3's renames
├── Source/
│   ├── DungeonCore/              ← generator, data, boundary rules (Core, CoreUObject, Engine only)
│   │   DungeonTypes.h  DungeonConfiguration.h  DungeonGenerator.h  DungeonSeed.h
│   │   DungeonRoomPlacement.h  DungeonRoomSemantics.h  DungeonDelaunay3D.h  DungeonMST.h
│   │   DungeonPathfinder.h  DungeonBoundaryRules.h  DungeonValidator.h
│   ├── DungeonOutput/            ← tiles, the dungeon actor, gameplay actors (+ DungeonCore)
│   │   DungeonTileSet.h  DungeonTileModule.h  DungeonTileMapper.h  DungeonActor.h
│   │   DungeonCoverage.h  DungeonWallProfileConformance.h
│   │   DungeonInteractable.h  DungeonDoorActor.h  DungeonTorchActor.h
│   │   (DungeonActorEditor.cpp: the actor's editor-only half, CR4)
│   ├── DungeonEditor/            ← editor only (+ DungeonCore, DungeonOutput)
│   │   DungeonActorDetails.h  DungeonModuleTools.h  DungeonTileSetValidator.h
│   └── DungeonVoxelIntegration/  ← the VoxelWorlds bridge (+ DungeonCore; VoxelWorlds modules privately)
│       DungeonVoxelTypes.h  DungeonVoxelConfig.h  DungeonVoxelStamper.h  DungeonVoxelStampPlan.h
│       DungeonVoxelLattice.h  DungeonEntranceStitcher.h  DungeonEntrancePassagePlan.h
└── Documentation/
    ├── ARCHITECTURE.md           ← this document
    └── Archive/                  ← the finished plans (entrance placement, environment polish, tile modules)
```

Each module keeps its automation tests under `Private/Tests/` (`Dungeon.*`).

### Module dependencies

```
DungeonEditor (Editor)            → DungeonCore, DungeonOutput
DungeonOutput (Runtime)           → DungeonCore
DungeonVoxelIntegration (Runtime) → DungeonCore; privately VoxelCore, VoxelGeneration, VoxelStreaming, VoxelLOD, VoxelRendering
DungeonCore (Runtime)             → Core, CoreUObject, Engine
```

`DungeonCore` and `DungeonOutput` never depend on VoxelWorlds. The descriptor lists all four modules as `Runtime` /
`Editor` with `LoadingPhase` `Default` and no gate, and VoxelWorlds as an optional plugin; `DungeonVoxelIntegration`
links VoxelWorlds' modules, so it is a hard dependency in practice: a project without VoxelWorlds removes that module
from the descriptor (DNG-29).

---

## 3. Core Data Structures

All in `DungeonTypes.h` unless noted. The grid is Z-up: X and Y span a floor, Z counts floors.

### Cells and the grid

```cpp
enum class EDungeonCellType : uint8
{
    Empty, Room, RoomWall, Hallway, Staircase, StaircaseHead, Door, Entrance,
    Reserved,   // transient: the entrance approach's keep-out during generation, cleared to Empty before output
};

struct FDungeonCell            // 8 bytes (static_assert)
{
    EDungeonCellType CellType;
    uint8 RoomIndex;           // 0 = none
    uint8 HallwayIndex;        // 0 = none
    uint8 FloorIndex;
    uint8 StaircaseDirection;
    uint8 Reserved[3];         // padding (MaterialHint and Flags were deleted in CR1, DNG-06)
};

struct FDungeonGrid
{
    FIntVector GridSize;                    // e.g. (30, 30, 5): 30×30 cells on 5 floors
    TArray<FDungeonCell> Cells;             // [X + Y*SizeX + Z*SizeX*SizeY]
    static const FIntVector FaceDirections[6];  // +X, -X, +Y, -Y, +Z, -Z: the one table for stamper, validator, tests
    // GetCell, IsInBounds, CellIndex
};
```

### Rooms, hallways, staircases

```cpp
enum class EDungeonRoomType : uint8
{ Generic, Entrance, Boss, Treasure, Spawn, Rest, Secret, Corridor, Stairwell, Custom };

struct FDungeonRoom      { uint8 RoomIndex; EDungeonRoomType RoomType; FIntVector Position, Size, Center;
                           TArray<uint8> ConnectedRoomIndices; bool bOnMainPath; int32 GraphDistanceFromEntrance;
                           int32 FloorLevel; };
struct FDungeonHallway   { uint8 HallwayIndex, RoomA, RoomB; TArray<FIntVector> PathCells; bool bHasStaircase, bIsFromMST; };
struct FDungeonStaircase { FIntVector BottomCell, TopCell; uint8 Direction; int32 RiseRunRatio, HeadroomCells;
                           TArray<FIntVector> OccupiedCells; };
```

### The result

`FDungeonResult` is the generator's immutable output: the seed actually used, `GridSize`, `CellWorldSize`, the grid, the
rooms, hallways and staircases, the graph (`DelaunayEdges`, `MSTEdges`, `FinalEdges`), the entrance
(`EntranceRoomIndex`, `EntranceCell`, `EntranceApproach`), metrics (`GenerationTimeMs`, cell totals) and queries
(`FindRoomByType`, `GetEntranceRoom`, `GetEntranceOpeningCell`, `GridToWorld`, `WorldToGrid`).

### Configuration

`UDungeonConfiguration` (`DungeonConfiguration.h`), one data asset per dungeon template:

| Category | Property | Default | Meaning |
|----------|----------|---------|---------|
| Grid | `GridSize` | 30×30×5 | X × Y cells × floors |
| Grid | `CellWorldSize` | 400 | cm per cell (100-2000) |
| Rooms | `RoomCount`, `MinRoomSize`, `MaxRoomSize` | 8, 3×3×1, 7×7×2 | target rooms and their size bounds in cells |
| Rooms | `RoomBuffer`, `MaxPlacementAttempts` | 1, 100 | gap between rooms and from the grid edge; tries per room |
| Room types | `RoomTypeRules`, `bGuaranteeEntrance`, `bGuaranteeBossRoom` | —, true, true | `FDungeonRoomTypeRule` list (§5) |
| Hallways | `EdgeReadditionChance` | 0.125 | probability a non-MST Delaunay edge comes back (cycles) |
| Hallways | `HallwayMergeCostMultiplier`, `RoomPassthroughCostMultiplier` | 0.5, 3.0 | A\* costs of reusing a hallway / crossing a room |
| Staircases | `StaircaseRiseToRun`, `StaircaseHeadroom` | 2, 2 | run cells per floor; open cells above the body |
| Entrance | `Entrance` | Approach `None` | `FDungeonEntranceSpec` (§5) |
| Entrance | `EntrancePlacement` | `BoundaryEdge` | which placed room becomes the entrance when the approach is `None` |
| Seed | `bUseFixedSeed`, `FixedSeed` | false, 0 | the seed used for seed-0 generations (§8) |

---

## 4. Generation Pipeline

`UDungeonGenerator::Generate(Config, Seed)` and `GenerateWithEntrance(Config, Seed, EntranceOverride)` (a caller's spec
instead of the configuration's) run:

```
 1. Initialize grid (GridSize)
 2. Seed RNG (FDungeonSeed from the resolved seed)
 3. Place rooms            3a. the entrance room first when an approach is declared, 3b. reserve its approach
 4. Assign room types      (two passes: structural before the graph, graph-based after the MST)
 5. Delaunay tetrahedralization of the room centres (FDungeonDelaunay3D)
 6. Minimum spanning tree (Prim's, rooted at the entrance; FDungeonMST)
 7. Edge re-addition (EdgeReadditionChance)
 8. Mark the main path (entrance → boss over the MST)
 9. A* hallway carving with staircases (FDungeonPathfinder)
10. Entrances and doors; Reserved cells cleared to Empty; EntranceApproach filled
11. Validate (FDungeonValidator: overlap, buffer, reachability, headroom, entrance, metrics, the approach)
12. Return FDungeonResult
```

### Step 3: room placement

`FDungeonRoomPlacement::PlaceRooms` samples a size in `[MinRoomSize, MaxRoomSize]` and a position with
`X, Y ∈ [RoomBuffer, GridSize - Size - RoomBuffer]`, `Z ∈ [0, GridSize.Z - Size.Z]`, and keeps it when it overlaps no
room (with the buffer), retrying up to `MaxPlacementAttempts`. Rooms with `Size.Z > 1` span floors.

With approach `None` (the default, the standalone actor's case) no room is special during placement: the entrance is
selected afterwards (step 4) by `EntrancePlacement`, and nothing reserves the volume around it.

With an approach declared (`FDungeonEntranceSpec`, §5) the entrance room is placed first
(`FDungeonRoomPlacement::PlaceEntranceRoom`, seed fork 5): its floor follows `Floor`, a `FromSide` room sits on the
buffer line of the chosen face, and the approach volume (the column above the lid for `FromAbove`, the corridor from the
face to the grid edge for `FromSide`, widened by `Clearance`) is stamped `EDungeonCellType::Reserved`. The remaining
rooms reject any candidate touching a Reserved cell (`OverlapsReserved`), the pathfinder never enters one (it blocks
every type it does not cost, and staircase body, headroom and exit checks require `Empty`), and step 10 clears Reserved
back to `Empty`. If the constraints cannot be met the generator falls back to the unconstrained path and records
`bSatisfied = false` (a validation issue). Tests: `Dungeon.Generation.EntranceApproach.*`,
`Dungeon.RoomPlacement.RoomsAvoidReservedCells`.

### Step 4: room type assignment

`FDungeonRoomSemantics::AssignRoomTypes` applies the configuration's `FDungeonRoomTypeRule`s (type, `Count`, `Priority`,
normalised `Min/MaxGraphDistanceFromEntrance`, `bPreferLeafNodes`, `bPreferMainPath`, `bRequireMultiFloor`, `MinSize`).
Graph distances exist only after the MST, so structural constraints (size, floors) run first and graph-based ones after.

### Step 5: Delaunay tetrahedralization

3D Bowyer-Watson over the room centres: a super-tetrahedron around the grid bounds, circumsphere tests, and
deterministic jitter (seed fork 99) for degenerate (coplanar) inputs. The edges of the tetrahedra are the candidate
connections.

### Step 9: A\* hallway carving

Costs (`FDungeonPathfinder`): an empty cell 1.0; an existing hallway or door `HallwayMergeCostMultiplier`; a room's
ground floor 0 for the two rooms being joined, else `RoomPassthroughCostMultiplier`; a room wall 5.0; the upper cells
and walls of a tall room, staircases, headroom, entrances and Reserved cells are blocked.

Staircases move `StaircaseRiseToRun` cells horizontally per floor, are entered only along their axis, need their
headroom cells free, and reserve body and headroom while the search runs; their flanks are kept free of hallways
(`Dungeon.Generation.Staircase.FlanksHaveNoHallway`). Each hallway's run changes the grid, so later hallways merge into
earlier ones; MST edges are carved first, then the re-added ones.

---

## 5. Room Semantics & Entrance System

### Semantic placement

```
Before the graph:
  Entrance: selected from the placed rooms by EntrancePlacement (approach None), or placed first (an approach declared)
  Stairwell: Size.Z >= 2
  Corridor: one dimension >= 2x the others
After the MST:
  Boss: farthest graph distance from the entrance, largest available room
  Treasure: leaf or near-leaf nodes, small rooms preferred
  Secret: reachable only through re-added edges
  Spawn: on the main path, medium rooms
  Rest: moderate graph distance, off the critical path
  Generic: everything unassigned
```

### Entrance placement modes (approach `None`)

```cpp
enum class EDungeonEntrancePlacement : uint8
{
    BoundaryEdge,   // the room sits on the RoomBuffer line of a grid edge (placement never reaches coordinate 0)
    TopFloor,       // the room reaches the highest floor; the column above it is empty by construction
    BottomFloor,    // the room is on floor 0
    Any,            // no constraint
};
```

`FDungeonRoomSemantics::SelectEntranceRoom` filters the placed rooms by the mode and draws one (seed fork 3); with no
match it warns and draws from all rooms. The entrance room becomes `EDungeonRoomType::Entrance`, the MST's root and the
origin of graph distances.

Two cells describe the entrance:

- `FDungeonResult::EntranceCell`: the walkable ground-floor centre cell of the entrance room. Consumers use it for the
  floor height (spawn, loot, an elevator car, the POI stamp record).
- `FDungeonResult::GetEntranceOpeningCell()`: the top cell of the entrance room directly above `EntranceCell` (the same
  cell for a single-floor room). A passage from above stops at this cell's top plane and the tile mapper opens its
  ceiling; for a two-floor room the lid is a floor above the entrance cell
  (`Dungeon.RoomSemantics.Entrance.OpeningCellIsTopOfEntranceColumn`).

### Entrance approach (`FDungeonEntranceSpec`)

The placement modes only pick among placed rooms and reserve nothing, so a shaft could land on a hallway routed above
the entrance. `UDungeonConfiguration::Entrance`, or a caller's override (`GenerateWithEntrance`), declares the approach
up front:

```cpp
struct FDungeonEntranceSpec
{
    EDungeonEntranceApproach Approach;   // None (the default) | FromAbove | FromSide
    EDungeonEntranceFloor    Floor;      // Any | Top | Bottom | Explicit (+ ExplicitFloor)
    EDungeonGridFace         Face;       // FromSide: Any | MinX | MaxX | MinY | MaxY
    int32                    Clearance;  // extra clear cells around the column / corridor (0-2)
    bool                     bSingleFloorEntranceRoom; // vertical approach: the opening is the room's own lid
};

struct FDungeonEntranceApproachInfo   // FDungeonResult::EntranceApproach
{
    EDungeonEntranceApproach Approach;
    bool        bSatisfied;           // false = fell back to unconstrained placement
    FIntVector  OpeningCell;          // the lid (FromAbove) / the face cell (FromSide)
    EDungeonGridFace Face;            // FromSide, resolved
    FIntVector  KeepOutMin, KeepOutMax;  // inclusive box guaranteed Empty; an empty box = nothing needed
};
```

`None` is the standalone default, not a legacy value (D-CR18, DNG-09); the `FromBelow` approach was never used and was deleted in
CR1. For a vertical shaft use `FromAbove` with `Floor = Top`: nothing above the room to reserve, the shortest shaft.
Backends read `EntranceApproach` rather than re-deriving the geometry. VoxelWorldPOI derives the spec from the POI
type's entrance style (`VerticalShaft`, `CaveOpening`, `Trapdoor` → `FromAbove`/`Top`; `SlopedTunnel` → `FromSide`) and
passes it to `GenerateWithEntrance`, so one configuration serves several entrance styles; `ADungeonActor` takes the same
override (`bUseEntranceOverride`, `EntranceOverride`) so the tile actor dresses the layout the stamp carved.

---

## 6. Tile Output

`DungeonOutput` turns a result into instanced geometry. Tile vocabulary (glossary): a *tile type* is a cell-face role
(`EDungeonTileType`), a *slot* the tileset's entry for it, a *variant* a weighted alternative, a *piece* the resolved
geometry an instance renders, a *module* a multi-mesh composition, an *element* one mesh of a module, the *tile map* the
mapper's result.

### Boundary rules (shared by every backend)

Walls, floors and ceilings are not stored in the grid. Each backend derives them per cell face from one place,
`FDungeonBoundaryRules` in DungeonCore:

| Predicate | Face | Answers |
|-----------|------|---------|
| `NeedsWall(Grid, Cell, NX, NY, NZ)` | horizontal | does this face get a wall? |
| `NeedsVerticalBoundary(Grid, Cell, NX, NY, NZ)` | vertical | does this face get a floor / ceiling? |
| `OwnsSharedFace(...)` | horizontal, two open cells | which side dresses it (the −X / −Y cell) |
| `IsOpenCell(Type)` | — | is the type traversable (not Empty / RoomWall / Reserved)? |

Rules, in order: out-of-bounds or solid neighbour → boundary; Door / Entrance neighbour → open (it owns its frame); Door /
Entrance facing the hallway family → open (the doorway); same room → open; hallway family on both sides → open, except a
StaircaseHead opens only toward its own hallway; otherwise boundary. Vertically only the staircase shaft is open, so two
flat hallway cells stacked on different floors each keep their floor.

The tile mapper places meshes from the answers and the voxel stamper decides which faces of the void get a jacket.
**Never re-implement these in a backend**: they were duplicated once and drifted three times, and each drift sealed a
doorway or dropped a floor. Each rule has a test under `Dungeon.BoundaryRules.*`.

### The tileset and the mapper

`UDungeonTileSet` (`DungeonTileSet.h`) holds `Slots` (`TMap<EDungeonTileType, FDungeonTileSlot>`): per tile type a single
mesh or a module, a rotation offset and scale multiplier, weighted `Variants`, a `Weight` and a prop `Role`. Beside them:
`RoomTypeOverrides`, `WallProfile` (`FDungeonWallProfile`), `FixtureRules`, `DecorRules`, `InteriorLighting`, and the door
leaf and wall light meshes. The tile types are the room and hallway floors and ceilings (hallways with straight, corner,
T-junction, crossroad and end-cap variants), `WallSegment` (a rock-backed face), `WallPartition` (a face shared by two
open cells), `WallCornerInner` / `WallCornerOuter`, `DoorFrame`, `EntranceFrame`, `StaircaseMesh`, and the decor types
`WallDecor`, `FloorDecor`, `HallwayDecor`.

`FDungeonTileMapper::MapToTiles` is a pure function from result + tileset to an `FDungeonTileMapResult`: instance
transforms per tile type, each tagged with its piece, plus `Openings` (door and entrance sites), `Fixtures` (wall lights)
and `Props` (containers and traps). Floors' walkable top is `FDungeonTileMapper::TileThickness` (0.2 of the cell) above
the cell's bottom plane; the voxel carve and every floor module respect it.

### Tile modules

A `UDungeonTileModule` (`DungeonTileModule.h`) is a list of `FDungeonModuleElement`s (mesh, transform relative to the
module origin, optional material override) authored at `ReferenceCellSize`. A single mesh in a slot is auto-fit per axis
to the cell (and pivot-corrected); a module is placed with one uniform scale, `CellWorldSize / ReferenceCellSize`, at
the slot's anchor (face centre for walls, cell floor for floors, cell top for ceilings), so its authored proportions
survive and corners and z-fighting are solved by authoring rather than by placement rules. `ADungeonActor` expands every
piece to render batches keyed by (mesh, material) and creates one HISM per batch across the whole map, so N modules cost
the same draw calls as N single meshes and no tile is an actor. Modules are made in the editor from a selection of
actors in a scratch level (`UDungeonModuleTools::CreateModuleFromSelection`). Tests: `Dungeon.TileModule.*`. Authoring
rules: a wall piece's finished face sits at the profile's `FaceInset` inside the cell plane, a floor module's walkable
top at `TileThickness`, and nothing coplanar with a neighbour's face. History: `Archive/TILE_MODULE_SYSTEM_PLAN.md`.

### Environment polish

What the September polish (E1-E6) built into the mapper and the actor:

- **One owner per face.** A face between two open cells is dressed once, by its owner (`OwnsSharedFace`), with a
  two-faced `WallPartition`; rock-backed faces get `WallSegment` (`Dungeon.TileMapper.Walls.*`).
- **A declared wall profile.** `FDungeonWallProfile` (face inset 40, partition thickness 80, opening 240 × 320, door
  leaf 151 × 289 and hinge, at a 400 reference cell) is what every wall-family piece is authored to; a single wall mesh
  is fitted so its finished face lands on the profile. `UDungeonTileSet::CheckWallProfile()` (`FDungeonWallProfileConformance`)
  measures modules, variants and overrides against it, flags elements outside the cell height and thin single-sided
  collision meshes, and `UDungeonTileSetValidator` reports it on save (`Dungeon.WallProfile.*`).
- **Corners.** `WallCornerInner` posts where two walled faces of a cell meet, `WallCornerOuter` where a wall run ends
  at an unframed opening; one post per corner point, pulled onto the wall face, never beside a door or entrance frame
  (`Dungeon.TileMapper.Corners.InnerAndOuterPosts`).
- **Seeded variety.** A placement picks among a slot's piece and its `Variants` by weight from a hash of seed, cell, type
  and salt; `RoomTypeOverrides` swap a type's pieces in typed rooms (what, never where); decor is placed by
  `DecorRules` density on rock-backed faces, room cells and hallway cells (`Dungeon.Variety.*`).
- **Lighting.** Wall lights are fixtures: both side walls of every doorway, every Nth rock-backed room face, every Mth
  hallway face, capped (`FDungeonFixtureRules`: `RoomWallLightEvery` 3, `HallwayWallLightEvery` 0, `MaxWallLights` 24);
  never on a partition. `FDungeonInteriorLighting` adds a box-bounded post-process volume on the actor that clamps
  exposure (−3 to 8 EV100) and adds AO; its box needs a query-only collision body or the volume is inert.
- **Light-tightness.** `FDungeonCoverage::Analyse` matches every boundary the rules require against the placed pieces
  (`ADungeonActor::GetCoverageReport`; the debug overlay draws problem sites); generated dungeons are light-tight
  (`Dungeon.Coverage.GeneratedDungeonsLightTight`).

History: `Archive/ENVIRONMENT_POLISH_PLAN.md`.

### ADungeonActor

The actor that owns and displays a dungeon (`DungeonActor.h`): `DungeonConfig`, `TileSet`, `Seed`,
`bOpenEntranceOpening` (leave the entrance's lid, floor or face wall unbuilt), `bUseEntranceOverride` /
`EntranceOverride`, `bMapInActorFrame`; `GenerateDungeon()`, `ClearDungeon()`, `RandomizeSeed()` (all callable in the
editor), `GetDungeonResult()`, `GetTileMap()`, `GetEntranceWorldPosition()`, `HasDungeon()`, `GetTotalInstanceCount()`,
`GetCoverageReport()`. It generates nothing on its own at `BeginPlay`; a level script, a game system or the editor calls
`GenerateDungeon`.

`ADungeonActor::BuildDungeon` is the one generate-and-map entry point: the actor and every consumer that must agree with
its layout (VoxelWorldPOI's interactables) call it with the same inputs, so no second caller drifts.

**Tiles in the actor's frame.** By default the tile map is built at the actor's location in world space and the actor's
rotation is ignored. With `bMapInActorFrame` the tile map is built at a zero origin and every instance, door, torch and
the entrance position is relative to the actor, rotation included: a dungeon on a planet stands on local up this way (a
POI dungeon's tile actor spawns in its pad's tangent frame; `Dungeon.TileMapper.RotatedFrame`).

### Interactables and props

Doors and lights carry state, so they are actors, not tiles. After each build, in a game world on authority, the actor
spawns one `DoorActorClass` per `Doorway` opening (at its `LeafHinge`) and one `WallLightActorClass` per wall-light
fixture (`bSpawnInteractables`, `SpawnInteractables()`, `DestroyInteractables()` on clear and `EndPlay`,
`OnInteractableSpawned`), and tells each its record through `IDungeonInteractable`. `ADungeonDoorActor` and
`ADungeonTorchActor` replicate state and animate; they keep no persistence and no interaction prompt, because
DungeonOutput depends on nothing but DungeonCore. VoxelWorldPOI subclasses them (`APOIDungeonDoor`, `APOIDungeonTorch`:
the state record and `UInteractableComponent`) and spawns its own, with `bSpawnInteractables = false` on its tile actor.
Every interactable has a deterministic ID (`FDungeonTileMapper::MakeInteractableId`).

A decor slot or variant with `Role` `Container` or `Trap` is instanced like any decor and recorded as an `FDungeonProp`
(cell, room, kind, piece, transform, and the `TileType` + `InstanceIndex` of its instance) in the tile map's `Props`;
gameplay hangs an actor on the record. `SetTileInstanceHidden` collapses one instance to zero scale in every batch it
contributed to (a broken crate) without a rebuild. The plugin spawns nothing for props itself
(`Dungeon.Interactables.*`).

---

## 7. VoxelWorlds Integration

`DungeonVoxelIntegration` writes a dungeon into a VoxelWorlds world through `UVoxelEditManager` as system edits
(`EEditSource::System`), in one edit operation per stamp or stitch kept outside the undo history, so a player's undo
never takes back a dungeon (G68). The edits persist with the edit layer like any other. The separate instanced world
mode that the February design planned (`FVoxelDungeonWorldMode`) was never used and was deleted in CR1 (DNG-01).

### Stamp modes and the jacket

```cpp
enum class EDungeonStampMode : uint8
{
    CarveUnderground,   // voxel-lined: the void plus a stone jacket; the voxels are the walls
    CarveOnly,          // tile-dressed: a bare void for an ADungeonActor over the same grid; the tiles are the walls
};  // ReplaceRegion and MergeAsStructure were deleted in CR1 (DNG-07)
```

`UDungeonVoxelStamper::StampDungeonInFrame(Result, ChunkManager, WorldOffset, StampMode, Config, Frame)` carves every
open cell to air, then gives every closed face (`FDungeonBoundaryRules`) a *jacket*, `UDungeonVoxelConfig::WallThickness`
layers of the wall material. In `CarveUnderground` the jacket's first `InnerLiningLayers` (one) is the *lining*, inside
the cell: its surface is the floor, wall or ceiling. The rest is the *seal*, just outside the cell and never in another
open cell (the seal's sample planning, `FDungeonVoxelStampPlan`, excludes carved samples by lattice index). In
`CarveOnly` the whole jacket is seal, outside the void, which is carved `CarveMarginVoxels` past every cell plane so the
rock surface never stands inside a tile's face; size the margin to the deepest wall piece plus half a voxel (1.5 at
VoxelSize 100 for the crypt wall's niches), and CarveOnly skips the staircase step voxels (the tile ramp is the stair).
Lining the whole jacket from inside filled every one-storey cell once it reached half the cell (3 voxels of 75 on 400 uu
cells stamped the demo's rooms solid): content standing on a voxel-carved floor goes `GetInnerLiningThickness` above the
cell's bottom plane (G24; `Dungeon.Stamp.VoxelLining`, `Dungeon.Stamp.CarvedFloorCollision`).

The voxel lattice is the world's, corner-sampled: `FDungeonVoxelLattice` enumerates the samples inside a cell box, so the
meshed surface lies within half a voxel of each cell plane (`Dungeon.VoxelLattice.*`, `Dungeon.VoxelStampPlan.*`).

`UDungeonVoxelConfig` holds the material IDs (`WallMaterialID`, `FloorMaterialID`, `CeilingMaterialID`,
`StaircaseMaterialID`, `DoorFrameMaterialID`, all 2 by default), `RoomTypeMaterialOverrides`, `VoxelsPerCellOverride`
(0 = `CellWorldSize / VoxelSize`), `WallThickness` (1-8), `CarveMarginVoxels` (0-3) and `DungeonBiomeID`.

### Entrance stitching

`UDungeonEntranceStitcher::StitchEntranceInFrame(Result, ChunkManager, WorldOffset, Style, Config,
bStopAtEntranceCellTop, SideTunnelFloorLift, Frame)` carves the passage from the terrain surface to the entrance.

```cpp
enum class EDungeonEntranceStyle : uint8
{
    VerticalShaft,   // a column from the surface onto the entrance room's lid; needs FromAbove
    SlopedTunnel,    // through the reserved corridor out of the grid's side, then a walkable ramp up outside the footprint; needs FromSide
    CaveOpening,     // a noise-displaced vertical opening; needs FromAbove (Clearance 1 advised)
    Trapdoor,        // a 1×1 hole straight down, no jacket; needs FromAbove
};
```

The passage geometry is pure data (`FDungeonEntrancePassagePlan`, from `EntranceApproach`; `Dungeon.EntrancePassagePlan.*`).
A style that does not match the result's approach is refused (logged, −1, nothing carved); a result with approach `None`
takes any style best-effort. The carve is two-pass: every segment's interior is set to air first, then the jackets go
only on samples that are neither passage interior nor inside an open dungeon cell, because a ramp's overlapping columns
re-solidified each other in one pass; a side tunnel also gets a floor and ceiling jacket. A jacket is written only
`SurfaceSkinVoxels` (2) or more below the local terrain surface, so the natural top layers (the skin) keep their
material and no stone apron rings the mouth. For a tile-dressed dungeon `bStopAtEntranceCellTop` stops a vertical carve
at the entrance room's lid and a side tunnel at the room face (the mapper opens them), and `SideTunnelFloorLift` raises
the tunnel floor to the room's walkable floor. History: `Archive/ENTRANCE_PLACEMENT_PLAN.md`.

### Dungeons in a frame

Both entry points lay the dungeon out in a frame (an `FTransform`, frame space to world): `WorldOffset`, every cell,
slab, seal and stair, and the passage plan are in frame space. The identity frame is the world (a heightmap world's
dungeons); on a planet it is a POI's pad space, the tangent frame at its centre, so the floors lie across local up. The
voxel lattice stays the world's: `FDungeonVoxelLattice::SetFrame` / `ForEachSampleInBox` enumerate a frame-space box over
the samples of its world bounds and keep those whose frame position lies in it (new stamp code enumerates through
`ForEachSampleInBox`, never `RangeForBox` directly). The stitcher's surface is the frame-space height where the frame's
vertical line meets the generated terrain (`DetectSurfaceHeight`, the one sampler for every world after CR2), so a shaft
stops at the surface of a planet. A rotated `CarveOnly` stamp carves half a voxel more margin and seals one voxel thicker
(an oblique plane steps by up to √3/2 voxel on the lattice); the lining keeps its thickness. The identity frame writes
the same samples in the same order as the world-space stamp did (`Dungeon.Stamp.RotatedFrame`,
`Dungeon.Stitch.PlanetSurface`).

---

## 8. Determinism & Seed System

All randomness flows through `FDungeonSeed` (`DungeonSeed.h`), a wrapper over `FRandomStream`: `RandRange`, `FRand`,
`RandBool`, and `Fork(SubsystemID)` for an independent child stream.

### Determinism rules

1. No `FMath::Rand()` / `FMath::SRand()`: all randomness via `FDungeonSeed`.
2. No dependence on hash-map iteration order: sorted arrays or a deterministic key where order matters.
3. Integer math for grid operations; float only in the Delaunay circumsphere tests (deterministic for identical inputs).
4. One fork per sub-system, so a change in one step's draw count does not cascade: room placement `Fork(1)`, edge
   re-addition `Fork(2)`, entrance selection `Fork(3)`, type assignment `Fork(4)`, the entrance room `Fork(5)`, Delaunay
   jitter `Fork(99)`. Configurations with approach `None` never draw from fork 5.
5. Tile choices (variants, decor) hash seed, cell, type and salt; they take no stream draws.

### Seed resolution

`UDungeonGenerator::Generate(Config, Seed)` resolves the seed through `UDungeonConfiguration::ResolveSeed`:

1. A non-zero `Seed` argument is always used as is. A configuration never overrides a caller's seed (`ADungeonActor::Seed`,
   `RandomizeSeed()`, POI placement seeds).
2. `Seed == 0` with `bUseFixedSeed` on and a non-zero `FixedSeed` uses `FixedSeed`.
3. Otherwise a clock-derived seed (non-deterministic; a warning when `bUseFixedSeed` is on with `FixedSeed` 0).

`FDungeonResult::Seed` records the seed actually used. Reproducing a layout needs that seed and the same configuration
values and entrance spec: every value feeds the draw order, so a cached result made under an earlier configuration does
not match a regeneration with the same seed. Tests: `Dungeon.Seed.*`, `Dungeon.Generation.Determinism.*`.

### Multiplayer and save/load

Only the seed, the configuration reference and the entrance spec need to reach another machine or a save;
`ADungeonActor::BuildDungeon` regenerates the same result and tile map from them. Voxel edits a stamp made are saved
with the edit layer and re-applied on load; gameplay state (doors, torches, containers) lives in the game layer's records
keyed by the interactable IDs.

---

## 9. When Dungeons Are Generated

The plugin provides synchronous generation (`UDungeonGenerator::Generate` / `GenerateWithEntrance`) and the actor; when
to call them is the caller's choice:

- **In the editor.** Place an `ADungeonActor`, assign a configuration and a tileset, and press Generate (the actor's
  `CallInEditor` functions); with `bAutoRegenerate` a change of configuration, tileset or seed rebuilds it. The detail
  panel (`FDungeonActorDetails`) groups generation, stats and debug visualization; the debug overlay
  (`bShowDebugVisualization`: rooms, hallways, graph edges, labels, entrance, staircases, grid bounds, coverage problems)
  is drawn by the actor's editor half (`DungeonActorEditor.cpp`).
- **At runtime.** Game code calls `GenerateDungeon` on an actor or `BuildDungeon` directly. In this project
  VoxelWorldPOI places dungeon POIs as their chunks stream in: it generates with the type's entrance spec, stamps once
  per world (`StampDungeonInFrame`, `StitchEntranceInFrame`), and for tile-dressed types spawns a tile actor over the
  carve that rebuilds from the seed on every stream-in.

There is no async wrapper and no pre-placed dungeon record in the plugin. The February design's preview actor, pre-placed
record, output interface and async subsystem were never built (`ADungeonPreviewActor`, `FPreplacedDungeon`, `IDungeonOutputHandler`);
the actor, the tile mapper and the stamper took their places.

---

## 10. Performance Considerations

### Generation time

Design budgets (not a recent measurement): 30×30×3 with 6-8 rooms under 5 ms; 30×30×5 with 8-12 rooms 5-15 ms; 50×50×8
with 15-20 rooms 15-50 ms; 50×50×10 with 20-25 rooms 50-150 ms. The demo's tiled POI configuration (20×20×3, 8 rooms)
regenerates in about 1.6 ms. The A\* pathfinder is the bottleneck.

### Memory

`FDungeonGrid` at 30×30×5 is 4,500 cells × 8 bytes = 36 KB; at 50×50×10, 200 KB. Room and hallway metadata adds a few
KB; a dungeon is well under 1 MB.

### Tile output

One HISM per (mesh, material) batch across the whole map, never a component or actor per tile; the demo's tiled dungeon
is about 1,200 tiles in a few dozen batches. Interactables are counted in the tens (doors, capped torches).

### Voxel stamping

At `CellWorldSize` 400 and `VoxelSize` 100 a cell is 4×4×4 = 64 voxels, so 2,000 open cells write about 128,000 edits
plus the jacket, across 20-50 chunks. The stamp runs on the calling thread as one edit operation (`FDungeonStampResult`
reports `VoxelsModified`, `ChunksAffected` and `StampTimeMs`); VoxelWorlds remeshes the touched chunks on its workers.

---

## 11. Design Decisions & Rationale

### Why a standalone plugin with optional integration?

The generator is useful without VoxelWorlds (tile-based dungeons). Standalone means broader use, simpler tests, and the
voxel bridge a clean layer rather than a tangled dependency.

### Why CPU-only generation?

Each A\* hallway run changes the grid the next one reads; Delaunay and MST are hard to parallelise at 8-25 rooms. A GPU
port would add complexity for no gain at these sizes.

### Why semantic rooms in the core, not a decorator?

Room types constrain placement (the entrance's approach, the boss far away, secrets off the MST). Applying them after
generation would mean re-running placement or accepting worse layouts.

### Why an 8-byte cell?

Room index, hallway index, floor and staircase direction in each cell spare the tile mapper and the stamper repeated
spatial lookups; three bytes are padding since CR1 removed the material hint and flags.

### Why a configurable cell size?

Different games need different scales (tight 200 cm corridors, 600 cm halls). The algorithm works on abstract cells, so
the world-unit mapping costs nothing to parameterise.

### Why the edit layer for voxel stamping, not a generation pass?

Edits integrate with VoxelWorlds' persistence and streaming without changing its generators: the dungeon is written over
procedural terrain like a player's edit (as a system edit, outside the undo history). A generation pass would couple
the plugin to VoxelWorlds' density pipeline. The cost is that a stamp happens after the terrain generates; POIs stamp
once per world, which makes that cheap.

---

## References

- [Vazgriz: Procedurally Generated Dungeons](https://vazgriz.com/119/procedurally-generated-dungeons/)
- [TinyKeep Dungeon Generation Algorithm (Reddit)](https://www.reddit.com/r/gamedev/comments/1dlwc4/procedural_dungeon_generation_algorithm_explained/)
- [Bowyer-Watson Algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Bowyer%E2%80%93Watson_algorithm)
- [Circumsphere (Wolfram MathWorld)](http://mathworld.wolfram.com/Circumsphere.html)
- [Prim's Algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Prim%27s_algorithm)
