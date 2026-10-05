# Dungeon Environment Polish — Design Plan (final generation phase)

**Status:** EPIC COMPLETE — E6 sweep + sign-off done 2026-09-27 (see §8). E5 merged (PDP#20 / parent #53). E4 merged (PDP#19 / parent #52). E3 merged (PDP#18 / parent #51). E2 merged (PDP#17 / parent #50). E1 merged (PDP#16 / parent #49): `OwnsSharedFace`,
`WallPartition` slot + mapper ownership (not a tileset default: map properties serialize as a
delta against the class default, so a new default key would appear as an engine cube in every
existing tileset), coverage tests; demo tileset carries
crypt on `WallSegment` and the thin stone wall on `WallPartition`. E2–E6 proposed. Follows the
entrance-placement epic (P0–P3 merged,
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

Two optional slots (as built): `WallCornerInner`, a post where two walled faces of one open cell
meet (a room or corridor corner), and `WallCornerOuter`, a post where a wall run ends at an
unframed opening (the mouth of a side corridor); never beside a door or entrance frame, whose
jambs cover that corner. The mapper places each once per corner lattice point (doubled cell
coords + face offsets) and pulls the anchor onto the wall face(s): in by `FaceInset` from a
rock-backed face, by half `PartitionThickness` from a partition, so a post centred on its anchor
straddles the face, half proud. Where two open cells share a corner point across a partition only
the first cell's post is placed (one post per point; the other side sees its back). Empty slot =
no corner geometry. A partition module must reach both faces of the profile slab (the demo one is
two single-sided pack quads back to back).

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

**As built (E4, 2026-09-27):**

- Pieces. The mapper flattens every slot's own mesh / module plus its `Variants` (each with its own
  rotation offset, scale multiplier and `Weight`) into a piece table, per tile type, and again per
  `RoomTypeOverrides` entry. A placement calls `Pick(type, cell, salt)`: the cell's room type selects
  the override list when one exists for the type, else the base list; a weighted draw from
  `Hash01(seed, cell, type, salt)` (FNV-1a + murmur fmix — FNV alone left nearby salts correlated,
  so a density roll and the pick that followed it were not independent) chooses the piece. Every
  instance carries its piece id; `ADungeonActor` groups instances by piece and expands modules per
  element. The fit helpers (floor / wall scale, pivot, slab lift, rotation compose) read the PIECE,
  so a single-mesh variant is auto-fit exactly like the slot's own mesh, and a module variant is
  placed at the uniform cell scale.
- A type renders only if its BASE slot has geometry; an override swaps what, never where or whether.
  Hallway-variant base pieces now compose their rotation offset like everything else (before, the
  base hallway floor / ceiling ignored it).
- Decor: `WallDecor` on rock-backed room / corridor faces (fixture faces skipped by default),
  `FloorDecor` on room cells, `HallwayDecor` on hallway cells; never the entrance opening cell.
  Wall decor anchors on the finished face at floor level (+X into the cell — the fixture convention),
  cell decor at the cell centre with a seeded quarter-turn yaw; uniform profile scale, no per-axis
  fit, so author decor as modules or base-pivot meshes at reference scale.
- Content lessons: a floor VARIANT must match its base's convention — a single-mesh variant beside a
  module base gets the slab fit (the flagstone stretched 4.3x in Z and stepped above the module's
  slab), so the turned flagstone is a module (`TM_FloorFlagstoneTurned`). Wall variants are modules
  authored to the profile (`TM_WallPlain`, one pack quad on the face); the conformance check now
  covers variant and override modules. Demo: crypt 3 : plain 1 walls, flagstone 2 : turned 1
  floors, wall decor shield / shelf / web, floor decor barrel / crate / rubble / bricks / wood,
  boss room = plain ceiling + coffins, treasure room = crates.

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

**As built (E5, 2026-09-27):**

- Part 1 (light-tight geometry) became a REPORT rather than new overlap rules: `FDungeonCoverage::Analyse`
  matches every boundary the shared rules require (walled faces once post-ownership, floor under, lid over;
  the entrance opening excepted) against the mapped pieces, bucketed on the doubled cell lattice — walls by
  their cell (they anchor at mid-height), slabs by their plane. Generated dungeons (10 seeds x open/closed
  entrance) are light-tight; `ADungeonActor::DescribeCoverage` prints the summary and the debug overlay
  draws problem sites red and fixtures yellow. The border seams of the original report were the E1 double
  dressing and the E2 profile, both gone.
- Part 2 (emissive fixtures) and part 3 (budgeted real lights) were delivered by E3's torches; E5 adds the
  hallway rule (`HallwayWallLightEvery`, after the room lights, default 0). Demo: every 5th corridor face,
  cap 96 → 61 torches.
- Part 4 (interior post-process): `FDungeonInteriorLighting` → a `UBoxComponent` + `UPostProcessComponent`
  on the tile actor, sized to the grid on build, disabled on clear. GOTCHA: `UPostProcessComponent` answers
  "is the view inside?" through its parent shape's PHYSICS body, so the box must have a query-only,
  ignore-all collision body — with `NoCollision` the volume is silently inert (the HDR readout still showed
  the project range). Defaults min -3 / max 8 EV100, AO 0.8, blend 400, priority 10. The project-level
  `r.EyeAdaptation.CachedLightingPreExposure` was left alone: with the clamp the exposure never leaves the
  safe range, so the warning does not fire.
- Torch defaults stay 1000 lm / 800 radius; with the clamp the torch-lit walls read correctly and the
  entrance room is a stop under the shaft — the intended mood. Tune per tileset via the struct.

### 3.6 Interactables: doors and lights are actors, on the gameplay path

Decision (2026-09-27): doors and lights must be interactable. Interactable things carry state and
replicate, so they are **actors on the POI structure path** (the elevator / loot-chest pattern:
authority-spawned, replicated, `UInteractableComponent` under `WITH_INTERACTION_PLUGIN`), never
tile instances. The tile actor stays visual-only and spawns on every client; a new
`IPOIStructure` implementer (`APOIDungeonInteractables`) regenerates the dungeon from seed + spec
(1.6 ms) and spawns the actors server-side from the mapper's placements.

- **Placements are a second mapper output.** `FDungeonTileMapResult` gains
  `TArray<FDungeonOpening> Openings` (cell, face, `EDungeonOpeningKind { Doorway, StairEntry,
  EntranceOpening }`) and `TArray<FDungeonFixture> Fixtures` (position, face, kind). Frames stay
  instanced; only `Doorway` openings get a leaf — a leaf on a stair entry blocks the ramp, and one
  on the entrance opening blocks the shaft drop or the tunnel.
- **Door leaf and frame align through the profile.** `FDungeonWallProfile` gains the hinge line
  (offset from the face plane, jamb side) and leaf size, so `SM_Door_01..03`, `SM_Door_Large_01`
  and `SM_Door_MetalGate_01` seat in the instanced `DM_Door` frame by data. The leaf actor
  replicates STATE (`Closed / Open / Locked`, locked reserved for keys later) and animates locally;
  its collision matches the opening while closed.
- **Torches are the lights.** A `APOIDungeonTorch` actor carries the sconce mesh (`SM_Torch_Sconce_01`
  / `SM_WallSconce_01`), an emissive material and one shadowless point light; `Lit` is replicated
  state (unlit = emissive off, light off). The light budget of §3.5 becomes the torch placement
  rule: both sides of every doorway, then every 3rd rock-backed room wall face (never a partition —
  it is seen from both sides), capped per dungeon. Standing braziers can ride the existing prop path.
- **Stable identity and state.** Every interactable has a deterministic ID
  (`Hash(POI cell, grid cell, face)`); the POI subsystem keeps a per-dungeon state record
  (`TMap<ID, state>`) that survives stream-out / stream-in the way `StampedDungeons` does today, and
  is the unit a save system persists later. Without this, a door re-closes and a torch relights on
  every stream-in.

**As built (E3, 2026-09-27):**

- `FDungeonTileMapResult` carries `Openings` and `Fixtures` next to the instance transforms. An
  opening is emitted at every frame site whether or not the frame slot has geometry; its
  `LeafHinge` sits on the finished-face plane (`FaceInset` in, `HingeInset` out) at the -Y jamb,
  +Y running toward the opening centre, so a leaf of `DoorLeafWidth` x `DoorLeafHeight` (profile
  data; 151 x 289 fits the pack door in the smooth doorway) closes the opening and swings toward
  -X into the Door cell's jamb passage, never into the corridor.
- `FDungeonFixtureRules` (tileset): doorway lights on both side walls of every Door cell, offset
  toward the room by `DoorwayLightOffsetFraction` so the open leaf clears them; then every Nth
  rock-backed room wall face; hard cap `MaxWallLights`. Partitions never carry fixtures (seen from
  both sides, owned by either cell). Lesson from the demo: 19 doorways x 2 exhaust a cap of 24
  before any room wall lights — the demo tileset runs at 64; the C++ default stays 24.
- `FDungeonTileMapper::MakeInteractableId` (FNV-1a over cell, face, kind namespace) keys the state
  record; `ADungeonActor::BuildDungeon` is the single generate + map entry point and
  `UPOIPlacementSubsystem::RebuildStampedDungeon` feeds it the stamp's exact inputs (concern §7.1).
- Actors follow the loot-chest pattern: replicated state + `OnRep`, `UInteractableComponent` under
  `WITH_INTERACTION_PLUGIN`, leaf / sconce mesh and offsets replicated so clients build the same
  actor. A torch's point light is created when lit and destroyed when put out (concern §7.5).
  Door collision follows the leaf (a closed door blocks; `SetCanEverAffectNavigation` on).
- Content: `DoorLeafMesh = SM_Door_01` (yaw 180), `WallLightMesh = SM_Torch_Sconce_01` (yaw -90);
  `APOIDungeonInteractables` is a structure entry of `DA_POIType_DungeonTiled`. Python gotcha:
  `unreal.Rotator(a, b, c)` is (roll, pitch, yaw) — author offsets with keyword arguments.
- Open for E5: the entrance room over-exposes with shaft daylight plus a dozen torches (the
  Lumen cached-lighting exposure warning); torch intensity / radius are actor defaults to tune
  with the project exposure fix. Hallways stay unlit by rule (no hallway fixtures yet).

### 3.7 Validation and tooling

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
| **E3 — Interactables** | Mapper emits openings + fixtures; `APOIDungeonInteractables` structure; door leaf actor (state replicated, collision while closed, hinge from the profile); torch actor (emissive + shadowless light, `Lit` state); per-dungeon state record with stable IDs; placement rule + cap. | DungeonOutput, VoxelWorldPOI | 2–3 days |
| **E4 — Variety** | Slot variants + seeded pick, room-type overrides, decor slots + density, hallway ceiling fudges → modules, determinism test. | DungeonOutput, content | 1–2 days |
| **E5 — Lighting polish** | Light-tight overlap rules + coverage report, interior post-process (exposure clamp, fog, AO), project exposure settings, torch budget tuning per representation. | DungeonOutput, VoxelWorldPOI, content | 1 day |
| **E6 — Sweep + sign-off** | Author the demo tileset through all of it (crypt for `WallSegment`, thin symmetric for `WallPartition`), PIE pass on both demo POI types (shaft and side tunnel) with a light-leak checklist, screenshots, doc updates. | content | 1 day |

E1 is independently shippable and removes the clipping class outright. E2 makes the door problem
impossible to reintroduce and is the prerequisite for door leaves. E3 comes before variety because
doors and torches exercise the profile and the actor path early; E4 and E5 are what turn the shell
into a place.

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

## 6. Decisions (taken 2026-09-27)

1. **Crypt wall for rock-backed faces** (`WallSegment` returns to `TM_WallCrypt`), **thin symmetric
   piece for partitions** (`WallPartition`, from `SM_Wall_Stone_01_Sml`).
2. **Wall sconces / torches** from the pack (`SM_Torch_Sconce_01`, `SM_WallSconce_01`,
   `SM_Torch_01`), placed both sides of every doorway plus every 3rd rock-backed room wall face.
3. **Lights and doors are interactable** → actors on the structure path (§3.6), with a per-dungeon
   state record from the start.

## 7. Concerns to settle before E1 starts

1. **Two spawn paths must agree on the layout.** The tile actor (every client) and the
   interactables structure (server) each regenerate from seed + entrance spec. Both must use
   `GenerateWithEntrance` with the spec from `MakeEntranceSpecForStyle` — the P3 lesson. Put the
   regeneration in one shared helper on the POI subsystem so a third caller cannot drift.
2. **State persistence has no home yet.** `StampedDungeons` lives in memory for the world's
   lifetime; nothing persists across sessions (open question since P0). E3 defines the state record
   and the stable IDs; hooking it to a save system is a later feature but the record shape should
   be save-ready (POD, keyed by deterministic IDs, no actor references).
3. **Doors and pathing.** A closed door blocks the player by collision; it must also block AI
   later (dynamic nav obstacle) and must never be placed where the generator's connectivity
   guarantee is the only route to a room that a *locked* door would sever. Locked stays reserved
   until keys are designed; the connectivity validator can later check that every room is
   reachable through unlocked doors.
4. **Partition thickness changes clearances.** A partition centred on the plane takes 40 from each
   side; the crypt face inset takes 40 from a room. Corridor floor variants (auto-fit to 400) run
   under the partition base — acceptable, but the profile must state it and the coverage test must
   not count the overlap as a gap. Door opening width = 400 − 2 × inset by construction.
5. **Light cost lives with torches now.** Every torch is a replicated actor with a light; the cap
   per dungeon (start at 24) bounds Lumen cost and replication, and unlit torches must drop their
   light component, not just dim it. Standing braziers via the prop path count against the same cap.
6. **The uncommitted tileset asset.** `Content/PluginTesting/Config/DungeonTileSet.uasset` carries
   the thin-wall swap in the working tree; it should land with E1, where the wall slot becomes
   `WallSegment = crypt module` again and the thin mesh moves to the new `WallPartition` slot.
7. **Tests.** E1–E2 stay pure (mapper + validator tests). E3's actors need a functional PIE check
   (open a door, light a torch, stream out and back in, state kept) — manual in `VoxelDemo` with the
   `vox.*` tooling until a functional-test harness for POI structures exists.

## 8. E6 sweep + sign-off (2026-09-27)

PIE pass on the tiled demo POI (`DA_POIType_DungeonTiled`, vertical shaft, 8 rooms / 3 floors) with
the light-leak checklist; the voxel-carved demo type is untouched by E1–E5 (tile-only work; the
interactables structure is on the tiled type alone) and was visited for regression only.

| Check | Result |
|-------|--------|
| Live coverage report (`ADungeonActor::DescribeCoverage`) | walls 223/223 (0 doubled), floors 241/241, ceilings 240/240 — light-tight (the entrance lid is the one open ceiling, by design) |
| Generated-dungeon coverage test | 10 seeds × open/closed entrance light-tight (137/137 suite) |
| Wall profile conformance (`CheckWallProfile`) | 0 issues incl. variant / override modules |
| Doors | 19 leaves in 19 doorways, seated in the smooth-doorway frames, swing 90° into the jamb passage; none clip a torch (0 torches within 120 of a hinge) |
| Torches | 61 (doorway jambs, every 3rd room face, every 5th corridor face), all lit, cap 96; none on a partition |
| Partitions | two-sided, textured from both cells; posts at partition corners pulled to the half-thickness face |
| Corners | inner posts on both faces in room corners, outer posts flanking corridor mouths, none beside frames |
| Variety | crypt / plain walls mixed, turned flagstones flush, wall shields / shelves / webs, floor barrels / crates / rubble, boss room plain ceiling + coffins, treasure crates |
| Exposure | interior range −3..8 in the HDR readout, no Lumen cached-lighting warning, torch walls read correctly, corridor at −0.7 EV100 |
| State | door open / torch doused survives stream-out (0 actors) and stream-in |

Gotcha found in the sweep: the coverage report must bucket a single-mesh instance by its mesh
BOUNDS centre, not the instance location — the pack's corner-pivot floors and ceilings (and the
plain boss ceiling) put the pivot on the cell corner, so the first live report showed 40 floors
and 59 ceilings "missing" that were plainly there. Modules anchor on the cell / face centre and
need no correction.

Sign-off montage: `Documentation/Images/environment_polish_signoff.jpg` (entrance room both diagonals, door cell toward the room, corridor, boss room, HDR readout).

**Remaining, outside this epic:** a side-tunnel (`SlopedTunnel`) tiled demo type does not exist
yet, so that entrance style is covered by the mapper / coverage tests only; the project-level
`r.EyeAdaptation.CachedLightingPreExposure` is untouched (the interior clamp keeps the exposure in
range); torch intensity / AO are tileset data to tune per theme; locked doors wait for keys.

## 9. Post-sign-off fixes (2026-10-05)

Three reports after E6, each pinned to a cause before fixing:

1. **Rock through the back of the crypt wall (niches).** `CarveMarginVoxels` 0.5 puts the meshed
   rock in [plane, plane + VoxelSize); the demo runs VoxelSize 100 and the crypt body extrudes 70
   past the plane, so the rock surface landed inside the niche recesses. Fix: the margin's clamp is
   0..3 voxels and its doc gives the rule (surface in (Margin - V/2, Margin + V/2]; size it to the
   deepest piece, keep 2 x Margin + V <= CellWorldSize so a one-cell buffer keeps a solid sample);
   demo voxel config 1.5. Verified: niches clean.
2. **Thin walls passable from one side; stair flanks and "walls inside rooms" see-through.** The
   demo partition module (`TM_WallPartitionThin`) was authored by the E2 script with
   `unreal.Rotator(0, yaw, 0)` — positional args are (roll, pitch, yaw) — so its room-side quad got
   PITCH 180: right face direction in plan, but it ran down into the floor below. Every partition
   was therefore the far-side quad alone, one-sided for collision (walk through from the owner's
   room) and transparent to the room (the stair's ramp and rock showed). Fix: module re-authored
   with keyword rotators; conformance now reports any wall-family element outside the cell height
   (`Dungeon.WallProfile.ElementHeightWithinCell`) and any thin single-sided complex-collision mesh
   (`CheckCollision`, in `CheckWallProfile()`), which would have flagged both the pitched quad and
   the door body quad (walkable from the hallway side beside the leaf). The pack's thin meshes
   (wall quad, doorway wall, arch wall, ceiling plane) are set Double Sided Geometry locally —
   that content is outside the repo (`Content/Dungeon_Modular_V1` is ignored), so a fresh checkout
   must redo it or the validator will say so.
3. **Stair step voxels in the room alongside.** Pass 3 of the stamper wrote solid step voxels over
   the exact stair box, refilling the open neighbour's carve margin, so the rock surface stood up to
   V/2 inside the room through its partition. CarveOnly dungeons now skip Pass 3: the tile ramp is
   the staircase (collision included), the cell stays void behind it.

Door-frame gaps reported at the same time were the partition's missing room-side quad beside door
bodies (an 80 step between the frame at -40 and the far quad at +40); re-check after the fix. If a
gap remains at a corridor MOUTH, the candidate is the outer-corner post rule skipping frame cells
(`DungeonTileMapper.cpp`, "jambs cover the corner"): the 40-deep inset of the side wall is exposed
there and the post should plug it.
