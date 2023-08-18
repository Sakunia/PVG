// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PVGPrecomputedGridDataAsset.generated.h"

/**
 * 
 */

USTRUCT()
struct FRawRegionVisibilityData16
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere)
	TArray<uint16> InvisibleRegions;
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere,SkipSerialization)
	TArray<uint16> VisibleRegions;
#endif
};

USTRUCT()
struct FPackedVisibilityData
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Location;

	UPROPERTY()
	uint16 SizeX;

	UPROPERTY()
	uint16 SizeY;

	UPROPERTY()
	uint16 SizeZ;

	FPackedVisibilityData(uint16 InLocation, uint16 X, uint16 Y, uint16 Z)
	{
		Location = InLocation;
		SizeX = X;
		SizeY = Y;
		SizeZ = Z;
	}
	FPackedVisibilityData()
	{
	}
#if WITH_EDITORONLY_DATA
	TArray<uint16> ReflectionData;
#endif
	
public:
	static TArray<uint16> Unpack(const FPackedVisibilityData& Entry, const UPVGPrecomputedGridDataAsset* Self);
};

USTRUCT()
struct FPackedCellData
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FPackedVisibilityData> CellData;
};

USTRUCT()
struct FRawRegionVisibilityCompressed
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<uint8> CompressedData;
	
	UPROPERTY()
	int32 NumEntries = 0;
};

UCLASS()
class PRECOMPUTEDVISIBILITYGRID_API UPVGPrecomputedGridDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	
	TArray<uint16> GetCellData(int32 CellId) const;

	int32 GetNumCells() const {return GridSizeX * GridSizeY * GridSizeZ; }
	
	FBox GetCellBox() const
	{
		return FBox(FVector::ZeroVector - CellExtents, FVector::ZeroVector + CellExtents);
	}

	int32 IsCellIndexValid(int32 Index) const
	{
		return GridData.IsValidIndex(Index);// || CompressedGridData.IsValidIndex(Index);
	}

	FBox GetGridBounds() const { return GridBounds;}
	int32 GetGridSizeX() const { return GridSizeX; }
	int32 GetGridSizeY() const { return GridSizeY; }
	int32 GetGridSizeZ() const { return GridSizeZ; }

protected:
	//static void CompressData(const FRawRegionVisibilityData16& Source, FRawRegionVisibilityCompressed& Target);
	//static TArray<uint16> UnCompressData(const FRawRegionVisibilityCompressed& Target);
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	
	void CubeCompress(const FRawRegionVisibilityData16& InData, TArray<FPackedVisibilityData>& Out);
	
#if WITH_EDITOR
	// Assign visibility data to the cell, will assign to both tested and testing cell.
	void SetDataCell(int32 Cell, int32 InvisibleRegion,bool bVisible);
#endif

protected:
	UPROPERTY(VisibleDefaultsOnly)
	FVector CellExtents;

	UPROPERTY(VisibleDefaultsOnly)
	FBox GridBounds;
	
private:
	UPROPERTY(VisibleDefaultsOnly)
	int32 GridSizeX;

	UPROPERTY(VisibleDefaultsOnly)
	int32 GridSizeY;
	
	UPROPERTY(VisibleDefaultsOnly)
	int32 GridSizeZ;

	/* Allow for runtime compression & decompression */
	UPROPERTY(EditDefaultsOnly)
	bool bAllowRuntimeCompression;

#if WITH_EDITORONLY_DATA
	// Transient data, either de-compressed on load or dynamically.
	UPROPERTY(VisibleAnywhere)
	TArray<FRawRegionVisibilityData16> GridData;
#endif
	
	UPROPERTY()
	TArray<FPackedCellData> GridCellData;

	friend class APVGBuilder;
};
