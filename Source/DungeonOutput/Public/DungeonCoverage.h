#pragma once

#include "CoreMinimal.h"
#include "DungeonCoverage.generated.h"

struct FDungeonResult;
struct FDungeonTileMapResult;

/**
 * Light-tightness report of a mapped dungeon (environment plan E5, §3.5 part 1): every open cell's
 * boundary that the shared FDungeonBoundaryRules say needs geometry — a wall on a walled face, a
 * slab under a floor, a lid over a ceiling — matched against the mapper's instances. A face with
 * no piece is a hole light and sight pass through; a face with two is the double-dressing E1 removed.
 *
 * Produced by FDungeonCoverage::Analyse; ADungeonActor::GetCoverageReport exposes it in the editor
 * and the Dungeon.Coverage.* tests keep it green on generated dungeons.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonCoverageReport
{
	GENERATED_BODY()

	/** Walled faces (post-ownership: a shared face counts once). */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 WallFacesNeeded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 WallFacesUncovered = 0;

	/** Walled faces carrying MORE than one wall-family piece (double dressing). */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 WallFacesDoubled = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 FloorsNeeded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 FloorsUncovered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 CeilingsNeeded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	int32 CeilingsUncovered = 0;

	/** One line per problem: "wall (x,y,z) +X: 0 pieces", "floor (x,y,z): 0 pieces", ... */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	TArray<FString> Problems;

	/** World-space centre of each problem site (parallel to Problems), for debug drawing. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Coverage")
	TArray<FVector> ProblemLocations;

	/** No holes and no double dressing. */
	bool IsLightTight() const { return WallFacesUncovered == 0 && WallFacesDoubled == 0 && FloorsUncovered == 0 && CeilingsUncovered == 0; }

	/** Multi-line summary + problems. */
	FString Describe() const;
};

/** Coverage analysis of a mapped dungeon (see FDungeonCoverageReport). */
struct DUNGEONOUTPUT_API FDungeonCoverage
{
	/**
	 * Match the mapper's instances against every boundary the grid needs.
	 * @param Result      The generated dungeon.
	 * @param TileMap     Its tile map (world space, at WorldOffset).
	 * @param WorldOffset The offset the map was built with.
	 * @param bOpenEntranceCeiling Whether the entrance opening was left unbuilt (that boundary is
	 *        then expected open and not reported).
	 * @return The report.
	 */
	static FDungeonCoverageReport Analyse(const FDungeonResult& Result, const FDungeonTileMapResult& TileMap,
		const FVector& WorldOffset, bool bOpenEntranceCeiling);
};
