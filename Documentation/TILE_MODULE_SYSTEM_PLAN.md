# Tile Module System — Design Plan

**Status:** Proposed (not implemented)
**Scope:** `DungeonOutput` module only. No `DungeonCore` change; the generator and
`FDungeonResult` are untouched. The voxel-carve/stamp path (`DungeonVoxelIntegration`) is
independent and unaffected.
**Motivating context:** see §1. This plan is a separate effort from the accessibility/tile bug
fixes on `fix/dungeon-accessibility`.

---

## 1. Problem statement

The tile renderer (`FDungeonTileMapper` + `ADungeonActor`) maps each dungeon cell face to a
**single static mesh** per slot and **auto-fits** it to the cell: scale = `TargetDim / MeshExtent`
per axis, plus a pivot correction. This has three structural weaknesses, all hit in practice:

1. **Auto-fit distortion.** Dividing by the mesh's bounding extent per-axis stretches any mesh
   whose proportions or pivot don't match the hard-coded convention. A zero-thickness plane gets a
   ~8000× axis scale (clamped from a divide-by-zero); an oversized mesh gets squashed.
2. **The flat-vs-deep wall dilemma.** A flat single-quad wall tiles corners perfectly but z-fights
   with coplanar floor-slab edges and reads as a billboard. A deep wall (e.g. 110 cm crypt) looks
   correct but **interpenetrates at corners and collides with door frames**, because the mapper
   places a full-cell-width wall centred on each of the four faces — two perpendicular deep walls
   share the corner volume. No mesh swap or orientation offset fixes this; it is inherent to
   single-mesh, full-width, face-centred placement.
3. **No composition.** A "correct" dungeon wall is usually several pieces — wall panel + base trim +
   corner post + optional sconce. The current model can slot exactly one mesh per face.

The user's proposal — **slot pre-authored modules made of multiple meshes** — solves all three at
the *authoring* level: a module is built to spec at cell scale, so there is nothing to auto-fit,
corners are authored correctly, and composition is native.

### The one hard constraint

`ADungeonActor` renders a whole dungeon (1200+ tiles) as **one HISM per tile type** — a handful of
instanced draw calls — which is what makes a dungeon cheap enough to stream as a POI. **Spawning a
Blueprint actor per cell would produce ~1200 actors per dungeon**, losing instancing entirely
(draw calls, tick, memory, streaming churn) and violating the project's HISM-for-tiles /
multiplayer-first standards.

**Therefore the design rule for this system:** *modules must bake down to shared instances for the
common tiles; per-tile Blueprint actors are reserved for the few tiles that carry gameplay.*

---

## 2. Goals / non-goals

**Goals**
- Let a tileset slot resolve to a **multi-mesh module** authored at cell scale.
- Preserve HISM instancing: N modules of the same kind cost the same draw calls as N single meshes.
- Bypass per-axis auto-fit for modules (uniform cell-ratio scale only) — no distortion.
- Solve corner-clipping and z-fighting by *authoring*, not by more placement heuristics.
- Backward compatible: existing single-mesh tilesets render identically, unchanged.
- Keep `DungeonCore` untouched and `DungeonOutput`'s dependency surface unchanged.

**Non-goals**
- Not a per-tile-actor spawner for the bulk. (Hero/gameplay tiles keep the existing structure/prop
  actor path — see §8.)
- Not a change to generation, cell semantics, or the voxel carve.
- Not a new authoring DCC pipeline; modules are assembled from existing static meshes.

---

## 3. Core concept: the module descriptor

A **module** is a flat list of `(mesh, relative transform, optional material)` entries plus a
declared reference cell size. It is pure data — no actor, no components at runtime.

```cpp
// New: DungeonOutput/Public/DungeonTileModule.h

USTRUCT(BlueprintType)
struct FDungeonModuleElement
{
    GENERATED_BODY()

    /** One piece of the module. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UStaticMesh> Mesh;

    /** Transform of this piece RELATIVE to the module origin (the cell's placement anchor),
     *  expressed at ReferenceCellSize. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FTransform RelativeTransform;

    /** Optional material override. When set, the piece batches into a HISM keyed by
     *  (Mesh, MaterialOverride) rather than (Mesh, defaults). Null = mesh defaults. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UMaterialInterface> MaterialOverride;
};

UCLASS(BlueprintType)
class DUNGEONOUTPUT_API UDungeonTileModule : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Cell size (cm) the RelativeTransforms were authored at. Runtime scales by
     *  ActualCellWorldSize / ReferenceCellSize — a single UNIFORM factor, never per-axis
     *  auto-fit, so authored proportions are preserved exactly. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ReferenceCellSize = 400.0f;

    /** The pieces. Origin convention: module anchor = the cell's placement point (§6), so a piece
     *  at RelativeTransform identity sits exactly where a legacy single mesh would after pivot
     *  correction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDungeonModuleElement> Elements;
};
```

A tileset slot may reference **either** a mesh (legacy) **or** a module. See §5.

---

## 4. Instancing model — how modules stay cheap

Today HISMs are keyed by `EDungeonTileType` (18 buckets). Modules shift the key from *tile type* to
**render batch** = `(StaticMesh, MaterialOverride)`.

At bake time (`MapToTiles`), for each placed tile:
- **Legacy mesh slot:** emit one instance into the batch `(SlotMesh, none)` — same as today.
- **Module slot:** for each `Element`, emit one instance into batch `(Element.Mesh,
  Element.MaterialOverride)` at `TilePlacement ∘ Element.RelativeTransform` (see §6).

`ADungeonActor` then creates **one HISM per unique render batch** and `AddInstances` per batch.
A module with 3 meshes contributes to 3 batches; identical meshes across different modules/slots
**share** a batch. Total HISM count = number of unique `(mesh, material)` pairs across the whole
tileset — bounded, small, fully instanced.

**Cost comparison** (1215-tile demo dungeon):

| Approach | Draw batches | Actors | Verdict |
|---|---|---|---|
| Today (mesh/type) | ~18 HISMs | 1 | baseline |
| **Module → instances (this plan)** | ~ unique sub-meshes (tens) | 1 | ✅ preserves instancing |
| BP actor per tile | ~1200+ | ~1215 | ❌ perf cliff — rejected |

The output container changes from `Transforms[TypeCount]` to a map keyed by render batch:

```cpp
// DungeonTileMapper.h — replaces the fixed per-type array
struct FDungeonBatchKey { TSoftObjectPtr<UStaticMesh> Mesh; TSoftObjectPtr<UMaterialInterface> Material; };
struct FDungeonTileMapResult
{
    TMap<FDungeonBatchKey, TArray<FTransform>> Batches; // keyed by render batch
    int32 GetTotalInstanceCount() const;
};
```

---

## 5. Tileset slot model — mesh OR module, backward compatible

Rather than fork every slot, introduce a small resolver so a slot can hold a mesh (existing
`TSoftObjectPtr<UStaticMesh>`) *or* a module, without breaking serialized tilesets.

Two options, pick at implementation:

- **5a (minimal, recommended first):** add a parallel `TSoftObjectPtr<UDungeonTileModule>` beside
  each existing mesh slot (e.g. `WallSegmentModule`). If the module is set it wins; else the mesh
  slot is used. Zero migration — existing tilesets ignore the null module fields.
- **5b (cleaner, later):** a `FDungeonTileSlot { EMode Mode; TSoftObjectPtr<UStaticMesh> Mesh;
  TSoftObjectPtr<UDungeonTileModule> Module; FRotator RotationOffset; FVector ScaleMultiplier; }`
  struct, one per slot. Consolidates the mesh + the rotation/scale offsets already added on
  `fix/dungeon-accessibility`. Requires a one-time data migration of existing tilesets.

Connectivity variants (`HallwayFloorCorner`, etc.) are just more slots — each can independently be a
mesh or a module. The variant *selection* logic in `MapToTiles` is unchanged; only what a selected
slot *resolves to* changes.

---

## 6. Placement math — modules skip auto-fit

For a legacy mesh, the mapper computes `Scale = TargetDim / Extent` (per-axis) + a pivot offset +
the slot rotation offset, then `Emplace(FTransform(Rot, Pos, Scale))`.

For a module, the tile still computes a **single placement transform** `TilePlacement` for the cell
face (position at the face, base rotation from `WC.Yaw`/floor/ceiling, plus the slot's rotation
offset), but **no per-axis fit**. Instead:

```
UniformScale = ActualCellWorldSize / Module.ReferenceCellSize      // one scalar
InstanceXf   = TilePlacement ∘ (Scale(UniformScale) · Element.RelativeTransform)
```

Because the module was authored at `ReferenceCellSize`, uniform scaling preserves its proportions
exactly — the distortion class from §1.1 disappears. The module author owns the pivot (the origin
convention in §3), so the mapper's pivot-correction step is skipped for modules.

This means **the corner-clip and z-fight problems become authoring choices**: the module author
insets the wall band, adds corner posts, and lifts the wall a hair off the floor plane so nothing
is coplanar. The renderer just places what was authored.

---

## 7. Authoring workflow

Modules are flat data, but nobody wants to type transforms. Provide an **editor utility** (in
`DungeonEditor`) to build them visually:

1. Designer drags the wall panel + base trim + corner post into a scratch level, arranging them
   inside one 400 cm reference cell (a gizmo/box actor marks the cell + the anchor origin).
2. Select the pieces → **"Create Dungeon Module from Selection"** (an `AssetActionUtility` or
   editor-utility action).
3. The tool captures each piece's mesh + transform *relative to the cell anchor* + any material
   override into a new `UDungeonTileModule` asset, stamping `ReferenceCellSize`.
4. Assign the module to a tileset slot; regenerate to preview.

This keeps runtime pure-data (no BP introspection at bake time) while giving designers full WYSIWYG
authoring. A later "edit module" round-trip (spawn pieces from a module back into a scratch level)
is a nice-to-have, not required for v1.

---

## 8. Hero / gameplay tiles — the actor escape hatch (already exists)

A handful of tiles per dungeon legitimately want a full actor: the entrance, a boss-room
centrepiece, a trapped tile, a lever. These are **not** part of this instancing system — they ride
the **existing POI structure/prop path** (`IPOIStructure`, `APOIElevatorEntrance`, the demo
braziers/pillars/chest), spawned at a deterministic offset. This plan explicitly keeps that split:

> Instanced modules for the bulk (walls/floors/ceilings/doors); actors only for tiles that carry
> behaviour, counted in the ones, not the thousands.

If a per-cell gameplay hook is ever needed at scale, that is a separate "tile actor budget"
discussion — out of scope here.

---

## 9. Lifecycle, streaming, determinism

Unchanged from today. `ADungeonActor::GenerateDungeon` re-runs on stream-in and rebuilds the HISMs
deterministically (same config + seed → same modules → same instances). Modules add a
`LoadSynchronous` per unique sub-mesh (soft refs) at bake — same pattern as the current per-slot
load, just more refs; pre-warm via the tileset's `GetAllUniqueMeshes` equivalent extended to walk
modules. Cleanup (`ClearDungeon`) clears the batch HISMs exactly as it clears per-type HISMs now.

The POI streaming lifecycle (stamp-once carve + streamed tile actor over it) is untouched — modules
change only what the tile actor renders, not when it lives.

---

## 10. Voxel-carve parity — nothing to do

The voxel side (`UDungeonVoxelStamper`, `CarveOnly` + outer seal) works off the **grid and the
boundary predicates**, never the meshes. Modules are visual dressing over the same carved voids.
The stamper and the module renderer stay independent; the only shared contract is
`FDungeonTileMapper::TileThickness` (the walkable-floor-top metric), which modules should respect so
collision and the carved void agree — document this as a module-authoring constraint.

---

## 11. Testing strategy

`FDungeonTileMapper` is a pure function today and stays testable:
- **Module expansion:** a 3-element module on a single-cell grid → 3 instances in the right 3
  batches at the expected transforms (compose `TilePlacement ∘ RelativeTransform`).
- **Instancing/dedup:** the same mesh in two modules + a legacy slot → one shared batch.
- **Uniform scale:** author at 400, place at 600 → every element scaled ×1.5, proportions intact.
- **Backward compat:** a mesh-only tileset produces byte-identical batches to the pre-change
  per-type output (golden test).
- **Determinism:** same seed + tileset → identical batch map.

Follow the existing `Dungeon.TileMapper.*` automation-test conventions.

---

## 12. Implementation phases

1. **P1 — Data + expansion (no authoring UI).** `UDungeonTileModule`, batch-keyed
   `FDungeonTileMapResult`, `MapToTiles` module expansion, `ADungeonActor` HISM-per-batch. Slot
   model 5a (parallel module fields). Hand-author one module asset in the editor by filling the
   array to validate. Unit tests. **This is the shippable core.**
2. **P2 — Authoring utility.** The "Create Module from Selection" editor action (§7).
3. **P3 — Slot consolidation (5b)** + migration of the demo tilesets, folding in the rotation/scale
   offsets from `fix/dungeon-accessibility`.
4. **P4 — Content.** Author correct wall / corner / floor / ceiling modules for the demo tileset;
   retire the flat-quad walls. Validate corners, doors, z-fighting all resolved.

P1 alone unblocks the wall problem (author a wall+corner module); P2–P4 are polish/scale.

---

## 13. Open questions / decisions

1. **Batch key granularity.** `(Mesh, Material)` is proposed. If per-instance custom data (e.g.
   per-tile tint) is ever wanted, HISM custom data floats cover it without splitting batches —
   defer.
2. **Nanite.** Module sub-meshes can be Nanite; HISM + Nanite is fine and would relax the tri-count
   pressure that pushed the pack toward flat quads. Worth noting to content authors.
3. **Collision.** Modules likely carry the collision (crypt wall has depth). Confirm the carved
   void + module collision + `TileThickness` walkable metric all agree (§10).
4. **Overlapping modules at shared faces.** Single-owner wall placement already prevents two walls
   on one face (verified: zero coincident wall instances). Modules inherit that — but corner posts
   authored into *both* a wall module and a corner module could double up; prefer corner geometry in
   a dedicated corner slot, not baked into every wall.
5. **Reference-cell mismatch.** If `ReferenceCellSize` ≠ the tileset's working cell and the module
   has non-uniform authored intent, uniform scale is still correct; but warn if the ratio is far
   from 1 (authored-at-100 placed-at-600 magnifies any authoring error 6×).

---

## 14. Recommendation

Worthy — with the framing that the value is **composed, correctly-authored, cell-scale modules that
bake to shared instances**, not "Blueprints per tile." Build **P1** as the core (data + expansion +
batch HISMs, backward compatible), keep the actor path for the handful of gameplay tiles, and treat
authoring UI and content as follow-on phases. The single rule to hold the line on: *the common
tiles must remain instanced; actors are for behaviour, counted in the ones.*
