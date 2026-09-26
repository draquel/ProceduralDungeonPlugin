# Entrance Placement Control — Review & Design Plan

**Status:** P0 in progress on `fix/entrance-p0` (2026-09-26); P1–P3 proposed.
**Decisions taken:** vertical approaches prefer the **top floor** (`Floor = Top`; the demo POI
configs move to `TopFloor` in P0 so the column above the entrance is empty by construction).
**Scope:** `DungeonCore` (entrance spec, entrance-first placement, approach keep-out, validator),
small `DungeonOutput` and `DungeonVoxelIntegration` touches (opening cell, style/approach
agreement), and `VoxelWorldPOI` wiring (derive the approach from the POI's entrance style).
**Motivating context:** dungeon POIs in VoxelWorlds are entered through a vertical shaft carved
from the pad surface straight down onto the entrance cell. When the entrance room sits on a low
floor with rooms, hallways or staircases stacked above it, the shaft lands on them and the dungeon
is unenterable. See §1 for why this happens and §2 for the proposal.

---

## 1. How the entrance is placed today

### 1.1 Pipeline order

| Step | Where | What happens |
|------|-------|--------------|
| 3 | `FRoomPlacement::PlaceRooms` | Every room is sampled uniformly: random size in `[MinRoomSize, MaxRoomSize]`, random position with `X,Y ∈ [RoomBuffer, GridSize - Size - RoomBuffer]`, `Z ∈ [0, GridSize.Z - Size.Z]`. No room is special. |
| 4 | `FRoomSemantics::SelectEntranceRoom` | The entrance is chosen **after** placement by filtering the already-placed rooms with `Config.EntrancePlacement`, then a random draw among the survivors. `EntranceCell` = the room's ground-floor centre. |
| 6 | `FMinimumSpanningTree::Compute` | Prim's is rooted at the entrance (the one thing the architecture doc says about the entrance that is true). |
| 9 | `FHallwayPathfinder` | Hallways and staircases are carved through any `Empty` cell, including the column above the entrance. |
| 10 | generator | The entrance cell is retyped to `Entrance`. |

Consumers then treat `EntranceCell` as the place the world connects to:

- `UDungeonEntranceStitcher` carves a column from the terrain surface down to the entrance cell
  (its top plane in tile mode, its bottom in voxel-carved mode) — blindly, through whatever grid
  volume is in the way.
- `FDungeonTileMapper` opens the ceiling tile of `EntranceCell` only.
- `UPOIPlacementSubsystem::SetupDungeonPOI` anchors the whole grid so `EntranceCell` sits under
  the pad's `EntranceOffset`, with the grid top `DungeonDepth` below the pad.

### 1.2 Finding A — the default placement mode is dead code in practice

`BoundaryEdge` (the enum default, and what both POI demo configs use) tests
`Position.X == 0`, `Position.X + Size.X >= GridSize.X` (and the same for Y). Room placement
clamps positions to `[RoomBuffer, GridSize - Size - RoomBuffer]`, so with `RoomBuffer >= 1`
(default 1, and 1 in both POI configs) **no room can ever satisfy the test**. Every generation
logs `SelectEntranceRoom: No rooms match EntrancePlacement=0, falling back to all rooms` and picks
a uniformly random room.

The unit test `Dungeon.RoomSemantics.Entrance.BoundaryEdgePicksBoundaryRoom` passes because it
hand-builds a room at X=0 rather than running placement.

Live values read from the editor (2026-09-26):

| Asset | GridSize | MaxRoomSize | RoomBuffer | EntrancePlacement | Style |
|-------|----------|-------------|------------|-------------------|-------|
| `DA_DungeonConfig_POIDemo` (VoxelCarved) | 16×16×4 | 6×6×**2** | 1 | BoundaryEdge | VerticalShaft |
| `DA_DungeonConfig_POIDemoTiled` (TileMeshes) | 20×20×3 | 7×7×1 | 1 | BoundaryEdge | VerticalShaft |

So with 4 floors the entrance is on the bottom floor a quarter of the time, and `TopFloor` (which
would at least keep it high) is not even selected.

### 1.3 Finding B — nothing reserves the approach volume

Even with `TopFloor`, the entrance room's top face is only guaranteed to touch the grid top when
the room happens to reach `GridSize.Z`; and for `BottomFloor`/`Any` the column above the entrance
cell is ordinary grid volume:

- Step 3 places other rooms in it (rooms only avoid each other in XY-with-buffer and Z-overlap; a
  room directly above on another floor is legal).
- Step 9 routes hallways and staircases through it (`GetCellCost` returns 1.0 for `Empty`).

What the stitcher then does depends on the representation:

- **VoxelCarved:** `CarveColumn` sets the interior to air (harmless through a room, destructive
  through a hallway's stone lining) **and stamps a 2-voxel stone shell** on every layer below the
  surface — a stone ring appears inside any room or hallway the shaft crosses, and floors of
  crossed hallways/rooms are cut open.
- **TileMeshes:** the carve stops at the entrance cell's top, but the tile floor/ceiling slabs of
  every room, hallway or staircase in the column remain — the player drops onto the first slab and
  the entrance below is sealed.

`SlopedTunnel` has the same blindness with a diagonal shape (it steps outward and up from the
entrance cell inside the grid volume).

### 1.4 Finding C — multi-floor entrance rooms open the wrong ceiling (tile mode)

`EntranceCell.Z` is the room's bottom floor. For a 2-floor entrance room the mapper's
`bOpenEntranceCeiling` skips the ceiling between `EntranceCell` and the cell above — which is the
same room, so no tile exists there anyway — while the room's real ceiling at `Position.Z + 2`
stays closed; the stitcher stops at `EntranceCell` top, one floor below that lid. Not hit by the
tiled demo today (`MaxRoomSize.Z = 1`) and masked in voxel mode (the carve goes to the cell
bottom, through the stone lining), but it is a latent blocker for any tiled config that allows
tall rooms.

### 1.5 Finding D — architecture doc describes a design that was never built

`ARCHITECTURE.md` §4 step 3 and §5 say the entrance is "placed first during room generation" at
the boundary on the configured floor. The code selects it post-hoc. Whichever way this plan lands,
the doc should be corrected.

### 1.6 Root cause in one sentence

The entrance is a *label applied after layout*, and the world-side approach (shaft/tunnel) is
*decided after generation*, so nothing in the generator knows an approach volume must stay clear.

---

## 2. Proposal: the caller declares the approach; the generator reserves it

### 2.1 Goals

1. A caller states **how the dungeon will be entered** before generation (from above, from a
   side, from below, or no external approach).
2. The generator places the entrance room to suit that approach and **keeps the approach volume
   free of rooms, hallways and staircases**.
3. The result carries the approach geometry so every backend (tile mapper, voxel stitcher, POI
   subsystem) reads one description instead of re-deriving it.
4. The validator and a red-first test make an obstructed approach a hard failure.
5. Determinism and existing layouts for configs that do not opt in are unchanged.

Non-goals: multiple entrances, new stitcher styles, changing the POI anchoring convention.

### 2.2 Data: `FDungeonEntranceSpec` (DungeonCore)

```cpp
UENUM(BlueprintType)
enum class EDungeonEntranceApproach : uint8
{
    None,       // Standalone dungeon: no external approach, no reservation (legacy behaviour)
    FromAbove,  // Vertical shaft / trapdoor / cave mouth drops onto the entrance cell
    FromBelow,  // Rises into the entrance cell (e.g. dungeon above a cave system)
    FromSide,   // Horizontal tunnel enters through a grid-boundary face
};

UENUM(BlueprintType)
enum class EDungeonEntranceFloor : uint8 { Any, Top, Bottom, Explicit };

UENUM(BlueprintType)
enum class EDungeonGridFace : uint8 { Any, MinX, MaxX, MinY, MaxY };

USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonEntranceSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDungeonEntranceApproach Approach = EDungeonEntranceApproach::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDungeonEntranceFloor    Floor    = EDungeonEntranceFloor::Any;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="Floor==EDungeonEntranceFloor::Explicit")) int32 ExplicitFloor = 0;
    /** FromSide only: which boundary face the tunnel enters through. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDungeonGridFace Face = EDungeonGridFace::Any;
    /** Extra clear cells around the approach column/corridor (0 = the cell itself). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="2")) int32 Clearance = 0;
    /** FromAbove/FromBelow: force the entrance room to a single floor so the opening is the room's own lid/floor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bSingleFloorEntranceRoom = true;
};
```

`UDungeonConfiguration` gains `FDungeonEntranceSpec Entrance`. The existing
`EDungeonEntrancePlacement EntrancePlacement` is kept for one release as the legacy path
(`Approach == None` ⇒ the current post-hoc selection runs, with Finding A fixed), then deprecated
with a `PostLoad` migration: `BoundaryEdge → FromSide/Any`, `TopFloor → FromAbove/Top`,
`BottomFloor → FromBelow/Bottom`, `Any → None`.

`FDungeonResult` gains the resolved geometry so backends never recompute it:

```cpp
USTRUCT(BlueprintType)
struct FDungeonEntranceApproachInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EDungeonEntranceApproach Approach = EDungeonEntranceApproach::None;
    UPROPERTY(BlueprintReadOnly) bool bSatisfied = false;     // false = fell back to unconstrained placement
    /** Cell whose lid (FromAbove), floor (FromBelow) or boundary wall (FromSide) is the opening. Same as EntranceCell for single-floor rooms. */
    UPROPERTY(BlueprintReadOnly) FIntVector OpeningCell = FIntVector::ZeroValue;
    UPROPERTY(BlueprintReadOnly) EDungeonGridFace Face = EDungeonGridFace::Any;   // FromSide
    /** Inclusive cell box that is guaranteed Empty in the final grid. */
    UPROPERTY(BlueprintReadOnly) FIntVector KeepOutMin = FIntVector::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntVector KeepOutMax = FIntVector(-1);
};
```

`EntranceCell` keeps its meaning (walkable floor cell of the entrance room) because the POI
subsystem, `ADungeonActor::GetEntranceWorldPosition` and the loot/elevator code use it for the
floor height.

### 2.3 Generator changes

**Step 3a — place the entrance room first** (`FRoomPlacement::PlaceEntranceRoom`, new):

- Size sampled as today; `Size.Z` forced to 1 when `bSingleFloorEntranceRoom` and the approach is
  vertical.
- Position constrained by the spec:
  - `Floor`: `Top` ⇒ `Z = GridSize.Z - Size.Z`; `Bottom` ⇒ `Z = 0`; `Explicit` ⇒ that floor;
    `Any` ⇒ random.
  - `FromSide`: the room is placed **on the buffer line** of the chosen face
    (`Position.X == RoomBuffer` for `MinX`, etc.), so the reserved corridor is exactly the
    `RoomBuffer` cells between the room face and the grid edge. `Face == Any` picks a face from
    the seed.
- Uses a **new seed fork** (`MainSeed.Fork(4)`) so configs with `Approach == None` draw exactly
  the same numbers as today and keep their layouts.
- On failure after `MaxPlacementAttempts`: log Warning, fall back to unconstrained placement,
  `bSatisfied = false`.

**Step 3b — reserve the approach.** Stamp the keep-out box into the grid as a new, *transient*
cell type `EDungeonCellType::Reserved`:

| Approach | Keep-out (inclusive) |
|----------|----------------------|
| FromAbove | `(OpeningCell.XY ± Clearance, OpeningCell.Z + 1 … GridSize.Z - 1)` |
| FromBelow | `(OpeningCell.XY ± Clearance, 0 … OpeningCell.Z - 1)` |
| FromSide | The row/column from the room face to the grid edge on the entrance floor, `± Clearance` sideways and `+ Clearance` in Z |

Why a cell type rather than a side list: the pathfinder already blocks anything it does not
recognise (`GetCellCost` default `-1`) and staircase body/headroom/exit checks require `Empty`, so
**hallways and staircases avoid `Reserved` with zero pathfinder changes**. Room placement needs
one addition: `DoesRoomOverlap` (or a sibling `OverlapsReserved`) rejects candidates whose AABB
touches a `Reserved` cell.

**Step 3c — remaining rooms** as today.

**Step 4** — with a spec active, `EntranceRoomIndex = 0` (the pre-placed room) and no RNG draw.
The legacy path also gets Finding A fixed: `BoundaryEdge` tests `Position <= RoomBuffer` /
`Position + Size >= GridSize - RoomBuffer`.

**Step 10** — clear every `Reserved` cell back to `Empty`, fill `Result.EntranceApproach`
(`OpeningCell` = for FromAbove the *top* cell of the entrance column inside the room,
`Position.Z + Size.Z - 1`; FromBelow the bottom cell; FromSide the room cell on the face).
`Reserved` never reaches an output backend, but it is still added to
`FDungeonBoundaryRules::IsOpenCell`'s closed set with a rule test, per the "never re-implement the
predicates" lesson.

### 2.4 Validator and tests

- `FDungeonValidator::ValidateEntranceApproach` (new, in `ValidateAll`): every cell in the
  keep-out box is `Empty`; `OpeningCell` belongs to the entrance room; for `FromSide` the room
  face lies on the buffer line of `Face` and the corridor is `Empty`. Skipped when
  `Approach == None`.
- New tests (`Source/DungeonCore/Private/Tests/Test_DungeonEntranceApproach.cpp`):
  - `Dungeon.RoomSemantics.Entrance.BoundaryEdgeMatchesBufferedRooms` — runs real placement with
    `RoomBuffer = 1`; red today (Finding A).
  - `Dungeon.Generation.Entrance.FromAboveColumnClear` — 5 floors, `Floor = Bottom` (worst case),
    100 seeds: keep-out empty, opening cell in the entrance room, validator passes.
  - `…FromSideCorridorClear`, `…FromBelowColumnClear` — same shape.
  - `…FallbackReportsUnsatisfied` — grid too small for the constraint; `bSatisfied == false`,
    generation still valid.
  - `Dungeon.Generation.Determinism.LegacySpecUnchanged` — `Approach == None` with a fixed seed
    reproduces the pre-change room list (hard-code the current output for the default config as a
    golden).
- `Dungeon.BoundaryRules.ReservedIsClosed`.

### 2.5 Output backends

- **Tile mapper:** open the lid of `EntranceApproach.OpeningCell` (falls back to `EntranceCell`
  when `Approach == None`, preserving `bOpenEntranceCeiling` semantics) — fixes Finding C. For
  `FromSide`, open the wall tile on `Face` of the opening cell instead (new: the boundary rule
  sees `Reserved`-cleared `Empty` outside, so the mapper needs an explicit "entrance wall" skip,
  the same pattern as the ceiling skip).
- **Stitcher:** read `Result.EntranceApproach`. `VerticalShaft`, `CaveOpening` and `Trapdoor`
  require `FromAbove`; `SlopedTunnel` requires `FromSide` and is re-shaped to run horizontally
  through the reserved corridor to the grid edge and climb **outside** the grid footprint (its
  current diagonal-inside-the-grid path cannot be reserved without hollowing out a large wedge).
  A style/approach mismatch logs Error and returns `-1` instead of carving. `ComputeEntranceZ`
  uses the opening cell's plane. With `Approach == None` the stitcher behaves exactly as today.
- Integration test (`Test_DungeonVoxelStampPlan` sibling): the stitcher's carve box, converted to
  cells, is contained in `KeepOut ∪ OpeningCell` for each style.

### 2.6 VoxelWorldPOI wiring

- `UDungeonGenerator::Generate` gains an optional `const FDungeonEntranceSpec* Override`
  (Blueprint-exposed overload). `UPOIPlacementSubsystem::SetupDungeonPOI` derives the spec from
  `UPOITypeDefinition::EntranceStyle` (`VerticalShaft/CaveOpening/Trapdoor → FromAbove/Top`,
  `SlopedTunnel → FromSide`) and passes it, so a dungeon config can be shared by POI types with
  different entrance styles without duplicating assets. The config's own spec is the default for
  non-POI callers (`ADungeonActor`).
- `FStampedDungeon::Entrance.PassageBottomZ` and `EntranceFloorZ` derive from `OpeningCell` and
  `EntranceCell` respectively (today both use `EntranceCell`, which is wrong for tall rooms).
- Anchoring stays "entrance cell under pad + `EntranceOffset`" for vertical approaches. For
  `FromSide` the anchor should become the tunnel mouth (grid-edge cell of the corridor); that is a
  follow-up, since every current POI type is vertical.
- Optional editor validation on `UPOITypeDefinition`: warn when the referenced config's spec
  conflicts with the override (e.g. `Explicit` floor that the override ignores).

---

## 3. Phasing

| Phase | Content | Modules | Size |
|-------|---------|---------|------|
| **P0 — bug fixes, no new API** | Finding A buffer-aware `BoundaryEdge`; Finding C mapper opens the top cell of the entrance column; fix `ARCHITECTURE.md` §4/§5 to describe the real pipeline. Red tests first. | DungeonCore, DungeonOutput, docs | ½ day |
| **P1 — spec + reservation** | `FDungeonEntranceSpec`, `FDungeonEntranceApproachInfo`, entrance-first placement, `Reserved` keep-out, Step 10 clear + fill, validator, tests, legacy-golden determinism test. | DungeonCore | 1–2 days |
| **P2 — backends** | Mapper opening cell/wall; stitcher approach check + `FromSide` sloped tunnel; stamp-plan containment test. | DungeonOutput, DungeonVoxelIntegration | 1 day |
| **P3 — POI wiring + live verify** | `Generate` override, subsystem derives spec from style, stamp record uses opening cell, demo configs set `FromAbove/Top`; PIE check with `vox.TPPOI` + `pie_screenshot` on both demo POI types; run `Dungeon.*` in-editor. | VoxelWorldPOI, content | ½–1 day |

P0 is independently shippable and already removes the most common failure (random entrance room
because the default mode never matches). P1 is the real fix; P2/P3 make the guarantee reach the
world.

---

## 4. Risks and decisions

1. **Layout change for existing seeds.** Enabling a spec on a config (or the POI override)
   changes every dungeon generated from that config. POI dungeons are stamp-once per world
   lifetime with the tile actor rebuilt from the seed on each stream-in, so a config change while a
   world has already-carved dungeons desyncs the tile actor from the voxels. Fine for the demo
   world; for shipped saves this needs either a per-stamp spec snapshot in `FStampedDungeon` or a
   world-version gate. **Decision needed:** does stamp-once state persist across sessions today?
2. **Placement space.** A `FromAbove` column with `Clearance = 0` removes one cell per floor above
   the entrance; with the entrance on the top floor it removes nothing. `FromSide` reserves
   `RoomBuffer` cells. Negligible, but `Clearance = 2` on a 16×16 grid starts to bite; clamp it.
3. **Entrance room forced single-floor.** Default on for vertical approaches so the opening is the
   room's own lid. Off keeps today's behaviour (2-floor drop into the room in voxel mode) — the
   mapper fix in P0 makes that work in tile mode too.
4. **`SlopedTunnel` semantics change** (climb outside the footprint). No current POI type uses it;
   confirm nobody depends on the in-grid diagonal.
5. **Prim's root.** With the entrance always at index 0 the MST root is fixed; harmless, and the
   graph-distance rules already assume the entrance is the root.

## 5. Open questions

- ~~Prefer `Floor = Top` for vertical approaches or allow lower floors with a reserved column?~~
  **Decided: top floor.** The spec still supports lower floors for callers that want them.
- Should `FromSide` also be able to target a specific *world* heading (so the tunnel faces the POI
  structure), i.e. map `Face` from the POI's layout yaw? Natural follow-on once anchoring moves to
  the tunnel mouth.
