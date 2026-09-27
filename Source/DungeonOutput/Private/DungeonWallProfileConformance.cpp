// DungeonWallProfileConformance.cpp — Wall-family modules measured against the tileset's wall profile.
#include "DungeonWallProfileConformance.h"
#include "DungeonTileSet.h"
#include "DungeonTileModule.h"
#include "Engine/StaticMesh.h"

bool FDungeonWallProfileConformance::IsWallFamily(EDungeonTileType Type)
{
	return Type == EDungeonTileType::WallSegment
		|| Type == EDungeonTileType::WallPartition
		|| Type == EDungeonTileType::DoorFrame
		|| Type == EDungeonTileType::EntranceFrame;
}

bool FDungeonWallProfileConformance::MeasureModule(const UDungeonTileModule& Module, float ProfileCellSize, FDungeonWallProfileMeasure& Out)
{
	const float Rescale = ProfileCellSize / FMath::Max(Module.ReferenceCellSize, 1.0f);
	float BestArea = -1.0f;
	bool bAny = false;
	Out.InnerX = TNumericLimits<float>::Max();
	Out.OuterX = -TNumericLimits<float>::Max();
	Out.ElementCount = 0;

	for (const FDungeonModuleElement& E : Module.Elements)
	{
		if (E.Mesh.IsNull())
		{
			continue;
		}
		UStaticMesh* Mesh = E.Mesh.LoadSynchronous();
		if (!Mesh)
		{
			continue;
		}
		// Bounds in the module frame: the mesh box through the element transform (rotation
		// included: a pack wall yawed 180 finishes toward -X), then to the profile's cell size.
		const FBox Local = Mesh->GetBoundingBox();
		const FBox InFrame = Local.TransformBy(E.RelativeTransform);
		const float MinX = static_cast<float>(InFrame.Min.X) * Rescale;
		const float MaxX = static_cast<float>(InFrame.Max.X) * Rescale;
		const float Area = static_cast<float>((InFrame.Max.Y - InFrame.Min.Y) * (InFrame.Max.Z - InFrame.Min.Z)) * Rescale * Rescale;

		Out.InnerX = FMath::Min(Out.InnerX, MinX);
		Out.OuterX = FMath::Max(Out.OuterX, MaxX);
		if (Area > BestArea)
		{
			BestArea = Area;
			Out.BodyInnerX = MinX;
			Out.BodyOuterX = MaxX;
		}
		++Out.ElementCount;
		bAny = true;
	}
	return bAny;
}

void FDungeonWallProfileConformance::CheckMeasure(const FDungeonWallProfileMeasure& M, const FDungeonWallProfile& P, TArray<FString>& OutIssues)
{
	const UEnum* TypeEnum = StaticEnum<EDungeonTileType>();
	const FString TypeName = TypeEnum ? TypeEnum->GetNameStringByValue(static_cast<int64>(M.Type)) : FString::FromInt(static_cast<int32>(M.Type));
	const float Tol = P.Tolerance;

	if (M.Type == EDungeonTileType::WallPartition)
	{
		// A partition is seen from both sides, so the WHOLE module must reach both faces of the
		// profile slab (a pair of back-to-back quads is the natural authoring) and nothing may
		// stand further proud than MaxProtrusion on either side.
		const float Half = P.PartitionThickness * 0.5f;
		if (M.InnerX > -Half + Tol || M.OuterX < Half - Tol)
		{
			OutIssues.Add(FString::Printf(TEXT("%s: geometry spans X %.1f..%.1f, profile partition needs both faces at %.1f and %.1f (thickness %.0f)"),
				*TypeName, M.InnerX, M.OuterX, -Half, Half, P.PartitionThickness));
		}
		if (M.InnerX < -(Half + P.MaxProtrusion) - Tol || M.OuterX > (Half + P.MaxProtrusion) + Tol)
		{
			OutIssues.Add(FString::Printf(TEXT("%s: decoration reaches X %.1f..%.1f, beyond the partition face by more than MaxProtrusion %.0f"),
				*TypeName, M.InnerX, M.OuterX, P.MaxProtrusion));
		}
		return;
	}

	// Rock-backed walls and frames: the body's finished face at -FaceInset; decoration may stand
	// proud of it by MaxProtrusion; the outer side is free (it is inside the rock).
	if (!FMath::IsNearlyEqual(M.BodyInnerX, -P.FaceInset, Tol))
	{
		OutIssues.Add(FString::Printf(TEXT("%s: finished face at X %.1f, profile FaceInset puts it at %.1f (off by %.1f)"),
			*TypeName, M.BodyInnerX, -P.FaceInset, M.BodyInnerX + P.FaceInset));
	}
	if (M.InnerX < -(P.FaceInset + P.MaxProtrusion) - Tol)
	{
		OutIssues.Add(FString::Printf(TEXT("%s: decoration reaches X %.1f, more than MaxProtrusion %.0f proud of the face at %.1f"),
			*TypeName, M.InnerX, P.MaxProtrusion, -P.FaceInset));
	}
}

void FDungeonWallProfileConformance::Check(const UDungeonTileSet& TileSet, TArray<FString>& OutIssues)
{
	static const EDungeonTileType Family[] = {
		EDungeonTileType::WallSegment, EDungeonTileType::WallPartition,
		EDungeonTileType::DoorFrame, EDungeonTileType::EntranceFrame };
	const UEnum* TypeEnum = StaticEnum<EDungeonTileType>();

	// Every module a wall-family slot can resolve to: the slot's own, each variant's, and the
	// same for every room-type override. Single meshes are skipped (the mapper fits them).
	auto CheckModule = [&](EDungeonTileType Type, const TSoftObjectPtr<UDungeonTileModule>& ModulePtr, const TCHAR* Where)
	{
		if (ModulePtr.IsNull())
		{
			return;
		}
		const FString Name = FString::Printf(TEXT("%s%s"),
			TypeEnum ? *TypeEnum->GetNameStringByValue(static_cast<int64>(Type)) : TEXT("?"), Where);
		const UDungeonTileModule* Module = ModulePtr.LoadSynchronous();
		if (!Module)
		{
			OutIssues.Add(FString::Printf(TEXT("%s: module failed to load"), *Name));
			return;
		}
		FDungeonWallProfileMeasure M;
		M.Type = Type;
		if (!MeasureModule(*Module, TileSet.WallProfile.ReferenceCellSize, M))
		{
			return; // no geometry: nothing to measure
		}
		TArray<FString> Issues;
		CheckMeasure(M, TileSet.WallProfile, Issues);
		for (const FString& Issue : Issues)
		{
			// The measure names the type; add where the module came from when it is not the base slot.
			OutIssues.Add(Where[0] ? Issue.Replace(*TypeEnum->GetNameStringByValue(static_cast<int64>(Type)), *Name) : Issue);
		}
	};
	auto CheckSlot = [&](EDungeonTileType Type, const FDungeonTileSlot& Slot, const FString& Where)
	{
		CheckModule(Type, Slot.Module, *Where);
		for (int32 i = 0; i < Slot.Variants.Num(); ++i)
		{
			CheckModule(Type, Slot.Variants[i].Module, *FString::Printf(TEXT("%s variant %d"), *Where, i));
		}
	};
	for (EDungeonTileType Type : Family)
	{
		CheckSlot(Type, TileSet.GetSlot(Type), FString());
		for (const TPair<EDungeonRoomType, FDungeonRoomTypeOverride>& OPair : TileSet.RoomTypeOverrides)
		{
			if (const FDungeonTileSlot* OSlot = OPair.Value.Slots.Find(Type))
			{
				const UEnum* RoomEnum = StaticEnum<EDungeonRoomType>();
				CheckSlot(Type, *OSlot, FString::Printf(TEXT(" (%s override)"),
					RoomEnum ? *RoomEnum->GetNameStringByValue(static_cast<int64>(OPair.Key)) : TEXT("?")));
			}
		}
	}
}
