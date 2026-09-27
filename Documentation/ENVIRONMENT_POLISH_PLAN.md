# Dungeon Environment Polish — Design Plan (final generation phase)

**Status:** Proposed (not implemented). Follows the entrance-placement epic (P0–P3 merged,
`ENTRANCE_PLACEMENT_PLAN.md`) and the tile module system (`TILE_MODULE_SYSTEM_PLAN.md`, P1–P4 done).
**Scope:** `DungeonOutput` (tile mapper, tile set, dungeon actor), `DungeonEditor` (validation +
authoring helpers), demo content (tileset + modules), and a small `VoxelWorldPOI` touch for the
interior post-process / light budget. `DungeonCore` is untouched except, if chosen, one pure
face-ownership predicate beside the boundary rules.
**Goal of the phase:** a tile-dressed dungeon that is an unbroken, lit environment — no clipping,
no gaps at corners or doors, no light leaks, with enough variety not to read as a grid — so the
next planning round can be about gameplay features rather than the shell.

---

## 1. Where things stand (measured 2026-09-27, tiled demo POI in PIE)

### 1.1 The tileset today (`Content/PluginTesting/Config/DungeonTileSet`)

| Slot | Resolves to | Notes |
|------|-------------|-------|
| RoomFloor | module `TM_FloorFlagstone` | walkable top at TileThickness (80) — correct |
| RoomCeiling | module `TM_CeilingArch` | flat slab + 4 arch borders, hung 40 below the plane |
| WallSegment | **single mesh** `SM_Wall_Stone_01_Sml` (thin) | your change; replaced module `TM_WallCrypt` (deep body, face 40 inside the plane, body outward) |
| DoorFrame | module `DM_Door` | doorway quad at −40, frame at −61, arch at −40: **authored against the crypt wall** |
| EntranceFrame | single mesh pillar | |
| Hallway floor/ceiling variants | single meshes | ceiling straight carries a `0.85` Y scale fudge |

### 1.2 Three defects, one root cause each

**A. Faces between two open cells are dressed from both sides.** `FDungeonBoundaryRules::NeedsWall`
answers per cell, and the mapper places a wall for every open cell whose face needs one. A face
between a room cell and a hallway cell that is not a door (a corridor running along a room's side),
and every staircase flank, therefore gets **two walls, back to back**. With the crypt module, whose
body extrudes *outward* "into the rock", each wall's body lands inside the neighbour's open cell —
the self-clipping you saw. With the thin wall the two slabs are coplanar and merely stack. The
mapper has no notion of a face *owner*; the module plan's "single-owner placement" note referred to
identical transforms, not to shared faces.

**B. Modules and single-mesh slots follow different placement conventions.** A single mesh is
auto-fit (thickness scaled to `TileThickness`, centred on the face plane, pivot-corrected), so the
thin wall's finished face sits 40 inside the plane by accident of the 80 thickness. A module is
placed at the authored anchor with no fit and no pivot fix. `DM_Door` was authored to the crypt
wall's profile; nothing ties it to whatever the wall slot resolves to now, so swapping the wall
mesh moved the door out of alignment. There is no declared **wall profile** (face inset,
thickness) that every wall-family piece must conform to, and no check.

**C. No lighting, and light leaks.** The only light is daylight down the entrance shaft; rooms
away from it are fog-grey (screenshots). Bright seams show where the shaft light reaches the joins
between the ceiling slab and its arch borders. PIE also prints the Lumen "cached lighting will be
clipped / adjust EyeAdaptation" warning, so exposure inside is uncontrolled. Nothing in
`DungeonOutput` knows about lights at all.

**Also:** there is no corner tile — a wall run that ends against a door frame or a stair leaves the
corner to whichever piece happens to cover it (the "openings at the corners with non-wall tiles");
hallway ceiling pieces carry per-slot scale fudges; every wall on every face is the same piece.

---

## 2. Goals / non-goals

**Goals**
1. Every face of every open cell is dressed exactly once, by geometry authored for that face kind.
2. Wall, door, entrance and corner pieces align by construction, from one declared profile, with an
   automated conformance check.
3. Deterministic variety: per-cell variant selection, per-room-type overrides, optional decor.
4. Lit interiors within a fixed cost budget, light-tight geometry, controlled exposure.
5. Keep the HISM instancing model: bulk tiles stay instanced; lights are budgeted, not per tile.
6. Keep `DungeonCore` generation and the voxel carve untouched.

**Non-goals**
- New gameplay content (traps, spawners, loot rules) — the next planning round.
- A new mesh pack; this phase works with `Dungeon_Modular_V1`.
- Baked lighting (streamed POIs cannot bake); Lumen-only.

---

## 3. Design

### 3.1 Face ownership and two wall kinds

Introduce a pure predicate beside the boundary rules:

```cpp
// A face between two OPEN cells that needs a wall from both sides is dressed ONCE, by its owner.
static bool OwnsSharedFace(const FDungeonGrid& Grid, const FIntVector& Cell, int32 DX, int32 DY);
```

Owner rule (deterministic, no RNG): the cell on the −X / −Y side owns the face; ties cannot occur.
The mapper skips the wall for the non-owner side. This alone removes the double wall and the
crypt-into-hallway clipping.

Then split the wall slot by what backs the face:

| New tile type | Placed when | Authoring contract |
|---------------|-------------|--------------------|
| `WallSegment` (kept) | face backed by solid rock / out of bounds / RoomWall | may be deep outward (crypt body into rock) |
| `WallPartition` (new) | face shared with another open cell (room/hallway, stair flank) | **two-faced, symmetric about the plane**, thickness ≤ `PartitionThickness`; falls back to `WallSegment` when the slot is empty |

A partition is authored once and seen from both sides, so it must be finished on both faces —
that is why it needs its own slot rather than a rule on the existing one.

### 3.2 A declared wall profile, and conformance checking

`UDungeonTileSet` gains:

```cpp
USTRUCT() struct FDungeonWallProfile
{
    /** Finished face distance INSIDE the cell plane, at ReferenceCellSize. */
    float FaceInset = 40.0f;
    /** Partition wall full thickness (symmetric about the plane). */
    float PartitionThickness = 80.0f;
    /** Door / entrance clear opening width and height. */
    float OpeningWidth = 240.0f, OpeningHeight = 320.0f;
};
```

Every wall-family module (`WallSegment`, `WallPartition`, `DoorFrame`, `EntranceFrame`, corners)
is authored to it. A `DungeonEditor` validator (`Dungeon.TileSet.WallProfileConformance`, also an
asset-validation warning on save) computes each module's inner face plane from its element mesh
bounds and flags any piece whose face is off the profile by more than a tolerance. `DM_Door` gets
re-authored against the profile the thin wall actually has (or the wall becomes a module again with
a symmetric partition variant) — either way the door and wall are then held together by data, not
by memory.

The single-mesh path is brought under the same profile: a single wall mesh is fitted so its
*finished face* lands at `FaceInset`, not its centre on the plane (today's accident).

### 3.3 Corners

Two optional slots, `WallCornerOuter` (convex post where two owned walls meet at a cell corner)
and `WallCornerInner` (concave, where a wall turns around solid). The mapper derives them from the
placed wall faces after ownership, per cell corner, so a corner post is placed once even where four
cells meet. Empty slot = no corner geometry (today's behaviour). This is the fix for the openings
where a wall run meets a door frame or a stair.

### 3.4 Deterministic variety

- `FDungeonTileSlot` gains `TArray<FDungeonTileVariant> Variants` (mesh-or-module + weight). The
  mapper picks per placement with `Hash(DungeonSeed, Cell, Face)` — same seed, same dungeon, and
  a POI's tile actor rebuilds identically on every stream-in.
- `UDungeonTileSet::RoomTypeOverrides` (`TMap<EDungeonRoomType, FDungeonTileSlotOverrides>`) so a
  boss or treasure room can swap floor/wall/ceiling without a second tileset.
- Decor slots placed by rule and density: `WallDecor` (banners, chains, niches on some owned wall
  faces), `FloorDecor` (rubble, puddles on some room cells), `HallwayDecor`. Decor is instanced
  like everything else; density is a tileset number, selection is seeded.
- Fold the hallway ceiling scale fudges into modules so no slot carries a per-axis multiplier.

### 3.5 Lighting

Four parts, in cost order:

1. **Light-tight geometry rules** (authoring + check): floor and ceiling slabs overlap their
   neighbours by a small margin or carry a border that does; walls extend from floor-top to
   ceiling-bottom with overlap; the coverage test (§3.6) reports any face with no geometry. The
   ceiling border seams that leak today are an overlap problem, not a light problem.
2. **Emissive fixtures first.** A `WallLight` decor slot (sconce / torch module with an emissive
   material) placed by rule: both sides of every door, every Nth owned wall face in rooms, every
   Mth in hallways. Lumen turns emissive surfaces into ambient light at no per-light cost; this is
   the baseline that makes rooms readable.
3. **A budgeted set of real lights.** The mapper emits `FDungeonLightPlacement { Position, Kind }`
   for room centres and fixture positions; `ADungeonActor` spawns at most `LightBudget` (tileset
   number, e.g. 32) `UPointLightComponent`s, shadows off, small attenuation, prioritising rooms by
   size and the entrance room, then hallway junctions. Components on the actor, never per-tile
   actors. When a tile actor streams out its lights go with it.
4. **Interior post-process.** A `UPostProcessComponent` on the dungeon actor bounded to the grid box:
   exposure min/max clamp so the shaft's daylight does not black out the interior, mild fog, AO. Fix
   the Lumen cached-lighting exposure warning at project level (the `r.EyeAdaptation.*` settings it
   names) rather than per dungeon.

Colour, fixture style and density are tileset data so the demo and a future themed set can differ.

### 3.6 Validation and tooling

- `Dungeon.TileMapper.Coverage`: generate several seeds, map to tiles, and assert for every open
  cell: each face that needs a wall has exactly one wall-family instance (post-ownership), each
  shared face exactly one partition, each floor/ceiling boundary one slab, each corner at most one
  post. Red before §3.1 (double walls), green after.
- `Dungeon.TileSet.WallProfileConformance` (§3.2), run as an automation test and as an editor
  asset-validation warning.
- `Dungeon.TileMapper.VariantsDeterministic`: same seed twice → identical batches; different seed →
  different variant picks.
- A `DungeonTest` map debug mode on `ADungeonActor` that draws uncovered faces and light
  placements, for eyeballing what the tests count.

---

## 4. Phases

| Phase | Content | Modules | Size |
|-------|---------|---------|------|
| **E1 — Ownership + partition** | `OwnsSharedFace`, `WallPartition` slot, mapper skips non-owner faces, coverage test (red → green), profile-correct fit for single wall meshes. | DungeonOutput (+ one predicate) | 1 day |
| **E2 — Profile + door + corners** | `FDungeonWallProfile`, conformance test + asset validation, re-author `DM_Door` (+ entrance frame) to the profile, corner slots + placement, demo corner module. | DungeonOutput, DungeonEditor, content | 1–2 days |
| **E3 — Variety** | Slot variants + seeded pick, room-type overrides, decor slots + density, hallway ceiling fudges → modules, determinism test. | DungeonOutput, content | 1–2 days |
| **E4 — Lighting** | Light-tight overlap rules + coverage report, emissive `WallLight` slot + placement rule, light placements + budgeted point lights on the actor, interior post-process, project exposure settings. | DungeonOutput, VoxelWorldPOI (budget/PPV hook), content | 2 days |
| **E5 — Sweep + sign-off** | Author the demo tileset through all of it, PIE pass on both demo POI types (shaft and side tunnel) with a light-leak checklist, screenshots, doc updates. | content | 1 day |

E1 is independently shippable and removes the clipping class outright. E2 makes the door problem
impossible to reintroduce. E3 and E4 are what turn the shell into a place.

---

## 5. Risks

1. **HISM batch growth.** Variants and decor add unique (mesh, material) batches. Bounded by the
   tileset's content, not by dungeon size; keep an eye on the batch count log line.
2. **Light budget vs. Lumen cost.** Point lights with shadows off are cheap, but 32 per dungeon
   times several streamed dungeons adds up; the budget is per tile actor and lights stream with it.
   Emissive fixtures carry most of the look.
3. **Ownership changes what a hallway sees.** A corridor along a room now shows the partition's
   hallway face instead of its own wall piece. That is the point, but the partition module must look
   right from both sides.
4. **Authoring cost.** Partition, corner, sconce and door modules are new assets; the module tool
   (`UDungeonModuleTools::CreateModuleFromSelection`) exists, so this is arrangement work in
   `DungeonTest`, not tooling work.
5. **Tile / voxel agreement.** Modules must keep the `TileThickness` walkable top and the
   half-voxel carve band (`CarveMarginVoxels`) rules from the module plan §10; corners and
   partitions do not change the carved void.

## 6. Decisions needed

1. Keep the deep crypt wall for rock-backed faces (`WallSegment`) and use a thin symmetric piece
   only for partitions, or go thin everywhere? The two-slot design supports either; the crypt look
   is worth keeping where it cannot clip.
2. Fixture style and colour for the demo (torches, braziers, sconces), and density (every door plus
   every 3rd room wall face is the suggested starting point).
3. Should lights be gameplay-relevant later (extinguishable, lit by the player)? If yes, fixtures
   become the POI structure/prop path (actors, counted in the ones) rather than tileset decor; the
   emissive-plus-budgeted-lights baseline stays either way.
