#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTileMapper.h" // EDungeonTileType
#include "DungeonTileModule.h" // UDungeonTileModule
#include "DungeonTypes.h" // EDungeonRoomType (room-type overrides)
#include "DungeonTileSet.generated.h"

/**
 * Everything one dungeon tile type needs, in one place: a single mesh (auto-fit to the cell), an
 * optional multi-mesh module that REPLACES the mesh (placed at one uniform cell scale — see
 * UDungeonTileModule), and orientation/scale corrections for a mesh imported non-canonically.
 *
 * Convention reminders (for the single-mesh path):
 *   Floor/Ceiling mesh local axes: X=width, Y=depth, Z=thickness (flat slab).
 *   Wall/Door/Entrance mesh local axes: X=depth(thin), Y=width, Z=height; finished face toward +X.
 */
/**
 * One alternative piece of geometry for a slot (E4 variety): a mesh or a module with the same
 * conventions as the slot itself, picked per placement by weight with the dungeon seed.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonTileVariant
{
	GENERATED_BODY()

	/** Single mesh, auto-fit like the slot's own mesh. Ignored when Module is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Module (uniform cell scale), replaces Mesh when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	TSoftObjectPtr<UDungeonTileModule> Module;

	/** Rotation offset composed with the placement rotation (this variant's own, e.g. a yaw-90 floor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Scale multiplier on top of the auto-fit scale (single-mesh path). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	FVector ScaleMultiplier = FVector::OneVector;

	/** Relative pick weight against the slot's own Weight and the other variants. <= 0 never places. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	bool IsActive() const { return !Mesh.IsNull() || !Module.IsNull(); }
};

USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonTileSlot
{
	GENERATED_BODY()

	/** Single mesh for this tile, auto-fit to the cell. Ignored when Module is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Multi-mesh module. When set, REPLACES Mesh: pieces are placed at one uniform cell scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TSoftObjectPtr<UDungeonTileModule> Module;

	/** Rotation offset composed with the placement rotation (e.g. Yaw=180 to flip a wall's face). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Scale multiplier applied on top of the auto-fit scale (single-mesh path). (1,1,1) = exact fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FVector ScaleMultiplier = FVector::OneVector;

	/**
	 * Pick weight of the slot's OWN geometry against its Variants (E4). 0 with live variants =
	 * the slot's mesh / module is never placed itself (a pure variant pool).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot|Variety", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/**
	 * Alternative pieces picked per placement by weight with the dungeon seed (E4): the same
	 * seed always yields the same dungeon, on every machine and every rebuild. Empty = no variety.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot|Variety")
	TArray<FDungeonTileVariant> Variants;

	bool HasMesh() const { return !Mesh.IsNull(); }
	bool HasModule() const { return !Module.IsNull(); }
	bool IsActive() const { return HasMesh() || HasModule(); }
};

/**
 * Per-room-type slot replacements (E4): a boss or treasure room swaps floor / wall / ceiling /
 * decor geometry without a second tileset. Only the listed types change; a type the base tileset
 * does not place at all stays absent (overrides swap geometry, they never add placements).
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonRoomTypeOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Override")
	TMap<EDungeonTileType, FDungeonTileSlot> Slots;
};

/**
 * Interior lighting control (E5, §3.5 part 4): the post-process ADungeonActor applies inside its
 * grid box. The point of it is exposure: without a clamp the auto exposure chases the dark
 * corridors down to the floor of Lumen's cached-lighting range (the "clipped" warning) and then
 * blows out the first torch-lit room. Values are EV100 (the project extends the default
 * luminance range).
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonInteriorLighting
{
	GENERATED_BODY()

	/** Apply an interior post-process volume over the dungeon grid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	bool bInteriorPostProcess = true;

	/** Darkest the interior auto exposure may adapt to (EV100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (UIMin = "-10.0", UIMax = "20.0"))
	float MinExposureEV100 = -3.0f;

	/** Brightest the interior auto exposure may adapt to (EV100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (UIMin = "-10.0", UIMax = "20.0"))
	float MaxExposureEV100 = 8.0f;

	/** Exposure compensation inside (stops). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (UIMin = "-5.0", UIMax = "5.0"))
	float ExposureCompensation = 0.0f;

	/** Adaptation speed when brightening / darkening (stops per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0.1"))
	float ExposureSpeedUp = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0.1"))
	float ExposureSpeedDown = 2.0f;

	/** Lumen ambient occlusion intensity inside (0 = off, 1 = full). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientOcclusionIntensity = 0.8f;

	/** Blend distance from the grid box edge (world units) so the shaft / tunnel crossfades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0.0"))
	float BlendRadius = 400.0f;

	/** Volume priority over the level's own post-process volumes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	float Priority = 10.0f;
};

/** Where decor goes (E4): densities are per candidate site, selection is seeded per cell / face. */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonDecorRules
{
	GENERATED_BODY()

	/** Fraction of rock-backed room / corridor wall faces that take a WallDecor piece. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallDecorDensity = 0.15f;

	/** Fraction of room cells that take a FloorDecor piece (never Door / Entrance / opening cells). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FloorDecorDensity = 0.08f;

	/** Fraction of hallway cells that take a HallwayDecor piece. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HallwayDecorDensity = 0.08f;

	/** Wall faces carrying a WallLight fixture take no wall decor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	bool bSkipFixtureFaces = true;
};

/**
 * The wall profile every wall-family piece of a tileset is authored to, so walls, partitions,
 * door and entrance frames and corner posts align by construction instead of by memory. All
 * distances are at ReferenceCellSize (like module authoring) and scale with the cell.
 *
 * Module frame reminder (see UDungeonModuleTools): the anchor is the face centre with local +X
 * OUTWARD across the face, so a piece's finished face toward the room lies at negative X.
 *
 *   WallSegment / DoorFrame / EntranceFrame body: finished face at X = -FaceInset; the body may
 *     extrude outward (into the rock) freely; decorative elements (frame jambs, pilasters) may
 *     stand proud of the face by at most MaxProtrusion.
 *   WallPartition: symmetric about the plane, X in [-PartitionThickness/2, +PartitionThickness/2]
 *     (plus MaxProtrusion for decoration on either side).
 *   Single-mesh slots are fitted by the mapper to the same numbers.
 *
 * FDungeonWallProfileConformance measures modules against this; the editor validator and
 * Dungeon.WallProfile.* tests report anything off.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonWallProfile
{
	GENERATED_BODY()

	/** Cell size the distances below are expressed at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "1.0"))
	float ReferenceCellSize = 400.0f;

	/** Finished face distance INSIDE the cell plane for rock-backed walls and frames. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "0.0"))
	float FaceInset = 40.0f;

	/** Full thickness of a partition (a wall shared by two open cells), centred on the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "1.0"))
	float PartitionThickness = 80.0f;

	/** Clear opening of a doorway (informational for authors and the door-leaf actor; not measured yet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "1.0"))
	float OpeningWidth = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "1.0"))
	float OpeningHeight = 320.0f;

	/** How far decoration (jambs, pilasters, trim) may stand proud of the finished face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "0.0"))
	float MaxProtrusion = 30.0f;

	/** Measurement tolerance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile", meta = (ClampMin = "0.0"))
	float Tolerance = 2.0f;

	/**
	 * Door leaf that seats in the DoorFrame slot's opening (E3): the leaf actor scales the tileset's
	 * DoorLeafMesh to this width x height. Defaults fit the pack's SM_Door_01 in SM_Doorway_Smooth_01.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile|Door", meta = (ClampMin = "1.0"))
	float DoorLeafWidth = 151.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile|Door", meta = (ClampMin = "1.0"))
	float DoorLeafHeight = 289.0f;

	/** Hinge line offset from the finished-face plane along the face normal (positive = outward, into the neighbour). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile|Door")
	float HingeInset = 0.0f;
};

/**
 * Where wall fixtures go (E3): the light-placement rule of the environment plan, as tileset data so
 * a themed set can be darker or brighter. The mapper emits FDungeonFixture placements from these;
 * gameplay (VoxelWorldPOI torches) spawns the actual lights and counts against the same cap.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonFixtureRules
{
	GENERATED_BODY()

	/** A wall light on each side wall of every Door cell (the jamb passage), at the room end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures")
	bool bLightDoorways = true;

	/** Every Nth rock-backed wall face of a room (in cell order) takes a wall light; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures", meta = (ClampMin = "0"))
	int32 RoomWallLightEvery = 3;

	/** Every Nth rock-backed wall face of a hallway (in cell order, after the room lights) takes a wall light; 0 = none (corridors stay dark). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures", meta = (ClampMin = "0"))
	int32 HallwayWallLightEvery = 0;

	/** Hard cap of wall lights per dungeon (doorway lights first, then room walls). 0 = no fixtures at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures", meta = (ClampMin = "0"))
	int32 MaxWallLights = 24;

	/** Mount height above the cell floor, at WallProfile.ReferenceCellSize (scales with the cell). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures", meta = (ClampMin = "0.0"))
	float WallLightHeight = 190.0f;

	/**
	 * Doorway lights sit this far from the door cell's centre toward the ROOM (away from the
	 * frame), as a fraction of the cell, so an open leaf swung into the cell never meets them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float DoorwayLightOffsetFraction = 0.25f;
};

/**
 * Maps each dungeon tile type to its geometry (mesh or module) + orientation, via one
 * FDungeonTileSlot per type (Slots). Assign this to ADungeonActor to control dungeon appearance.
 *
 * Legacy tilesets authored with the old parallel fields (RoomFloor, WallSegmentRotationOffset,
 * TileModules, …) auto-migrate into Slots on load — the old fields are kept hidden purely for that
 * migration and should not be authored against.
 */
UCLASS(BlueprintType)
class DUNGEONOUTPUT_API UDungeonTileSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UDungeonTileSet();

	/** Per-tile-type geometry + orientation. The single authoring surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	TMap<EDungeonTileType, FDungeonTileSlot> Slots;

	/** When true, only adjacent Hallway cells count as connected for hallway-variant selection;
	 *  doors, entrances, and staircases are treated as walls, producing end caps at transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	bool bHallwayVariantsHallwayOnly = false;

	/** The wall profile the wall-family slots are authored to (see FDungeonWallProfile). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FDungeonWallProfile WallProfile;

	/** Wall-fixture placement rules (see FDungeonFixtureRules); the mapper emits placements from these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FDungeonFixtureRules FixtureRules;

	/** Decor placement rules (E4, see FDungeonDecorRules) for the WallDecor / FloorDecor / HallwayDecor slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FDungeonDecorRules DecorRules;

	/** Per-room-type slot replacements (E4, see FDungeonRoomTypeOverride). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	TMap<EDungeonRoomType, FDungeonRoomTypeOverride> RoomTypeOverrides;

	/** Interior exposure / AO the tile actor applies over the grid (E5, see FDungeonInteriorLighting). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FDungeonInteriorLighting InteriorLighting;

	/**
	 * Door leaf mesh for Doorway openings (E3), hung by gameplay, never instanced. Leaf convention
	 * after DoorLeafRotationOffset: hinge line through the mesh origin along Z, the leaf extending
	 * along local +Y with its base at Z=0. Unset = no door leaves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Interactables")
	TSoftObjectPtr<UStaticMesh> DoorLeafMesh;

	/** Mesh-local rotation bringing DoorLeafMesh to the leaf convention (pack doors extend along -Y: yaw 180). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Interactables")
	FRotator DoorLeafRotationOffset = FRotator(0.0f, 180.0f, 0.0f);

	/**
	 * Wall light mesh for WallLight fixtures (E3), spawned as torch actors. Convention after
	 * WallLightRotationOffset: mount point at the mesh origin, the fixture extending along local
	 * +X off the wall. Unset = no wall lights.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Interactables")
	TSoftObjectPtr<UStaticMesh> WallLightMesh;

	/** Mesh-local rotation bringing WallLightMesh to the fixture convention (pack sconces extend along +Y: yaw -90). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Interactables")
	FRotator WallLightRotationOffset = FRotator(0.0f, -90.0f, 0.0f);

	/**
	 * Measure every wall-family module against WallProfile (face plane, partition faces, element
	 * height) and every wall-family mesh's collision (thin single-sided complex collision is
	 * passable from the back). One line per problem; empty = conforms. Loads the meshes. Also run
	 * by the editor asset validator on save.
	 */
	UFUNCTION(BlueprintCallable, Category = "TileSet")
	TArray<FString> CheckWallProfile() const;

	// --- Accessors (read Slots; safe for missing types) ---

	/** The slot for Type, or a default (empty) slot if none is configured. */
	const FDungeonTileSlot& GetSlot(EDungeonTileType Type) const;

	TSoftObjectPtr<UStaticMesh> GetMesh(EDungeonTileType Type) const { return GetSlot(Type).Mesh; }
	TSoftObjectPtr<UDungeonTileModule> GetModule(EDungeonTileType Type) const { return GetSlot(Type).Module; }
	FRotator GetRotationOffset(EDungeonTileType Type) const { return GetSlot(Type).RotationOffset; }
	FVector GetScaleMultiplier(EDungeonTileType Type) const { return GetSlot(Type).ScaleMultiplier; }

	/** Returns true if at least one slot has a mesh or module. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TileSet")
	bool IsValid() const;

	/** Collects all non-null slot meshes with their tile-type names (single-mesh slots only). */
	void GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const;

	// UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

private:
	/** Populate Slots with the base tile types pointing at the engine cube (fresh tilesets only). */
	void PopulateDefaultSlots();

	/** One-time upgrade of a legacy tileset: copy the deprecated parallel fields into Slots. */
	void MigrateLegacyFieldsToSlots();

	/** Null out every deprecated field so the next save persists Slots alone. */
	void ClearLegacyFields();

	/**
	 * Set once the legacy fields have been folded into Slots. Guards the migration so it can
	 * never run twice: an asset saved after the first migration carries this flag (and empty
	 * legacy fields), so its Slots are authoritative from then on. Without the guard a tileset
	 * saved with BOTH layouts re-migrated on every load and silently threw away every edit made
	 * to Slots (module assignments included).
	 */
	UPROPERTY() bool bMigratedToSlots = false;

	// ========================================================================
	// DEPRECATED — legacy parallel fields, kept ONLY so pre-Slots tilesets migrate on load.
	// Not editable; do not author against these. Removed once all assets are re-saved.
	// ========================================================================
	UPROPERTY() TSoftObjectPtr<UStaticMesh> RoomFloor;
	UPROPERTY() FRotator RoomFloorRotationOffset;
	UPROPERTY() FVector RoomFloorScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloor;
	UPROPERTY() FRotator HallwayFloorRotationOffset;
	UPROPERTY() FVector HallwayFloorScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> RoomCeiling;
	UPROPERTY() FRotator RoomCeilingRotationOffset;
	UPROPERTY() FVector RoomCeilingScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeiling;
	UPROPERTY() FRotator HallwayCeilingRotationOffset;
	UPROPERTY() FVector HallwayCeilingScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> WallSegment;
	UPROPERTY() FRotator WallSegmentRotationOffset;
	UPROPERTY() FVector WallSegmentScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> DoorFrame;
	UPROPERTY() FRotator DoorFrameRotationOffset;
	UPROPERTY() FVector DoorFrameScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> EntranceFrame;
	UPROPERTY() FRotator EntranceFrameRotationOffset;
	UPROPERTY() FVector EntranceFrameScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorStraight;
	UPROPERTY() FRotator HallwayFloorStraightRotationOffset;
	UPROPERTY() FVector HallwayFloorStraightScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorCorner;
	UPROPERTY() FRotator HallwayFloorCornerRotationOffset;
	UPROPERTY() FVector HallwayFloorCornerScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorTJunction;
	UPROPERTY() FRotator HallwayFloorTJunctionRotationOffset;
	UPROPERTY() FVector HallwayFloorTJunctionScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorCrossroad;
	UPROPERTY() FRotator HallwayFloorCrossroadRotationOffset;
	UPROPERTY() FVector HallwayFloorCrossroadScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorEndCap;
	UPROPERTY() FRotator HallwayFloorEndCapRotationOffset;
	UPROPERTY() FVector HallwayFloorEndCapScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingStraight;
	UPROPERTY() FRotator HallwayCeilingStraightRotationOffset;
	UPROPERTY() FVector HallwayCeilingStraightScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingCorner;
	UPROPERTY() FRotator HallwayCeilingCornerRotationOffset;
	UPROPERTY() FVector HallwayCeilingCornerScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingTJunction;
	UPROPERTY() FRotator HallwayCeilingTJunctionRotationOffset;
	UPROPERTY() FVector HallwayCeilingTJunctionScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingCrossroad;
	UPROPERTY() FRotator HallwayCeilingCrossroadRotationOffset;
	UPROPERTY() FVector HallwayCeilingCrossroadScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingEndCap;
	UPROPERTY() FRotator HallwayCeilingEndCapRotationOffset;
	UPROPERTY() FVector HallwayCeilingEndCapScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> StaircaseMesh;
	UPROPERTY() FRotator StaircaseMeshRotationOffset;
	UPROPERTY() TMap<EDungeonTileType, TSoftObjectPtr<UDungeonTileModule>> TileModules;
};
