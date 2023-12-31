// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGPrecomputedGridDataAsset.h"
#include "PrecomputedVisibilityGrid.h"
#include "UObject/ObjectSaveContext.h"

TArray<uint16> FPackedVisibilityData::Unpack(const FPackedVisibilityData& Entry, const UPVGPrecomputedGridDataAsset* Self)
{
	TArray<uint16> OutArray;
	const FIntVector OriginXYZ = IndexTo3D(Entry.Location, Self->GetGridSizeX(),Self->GetGridSizeY());
	
	for (int32 x = 0; x <= Entry.SizeX; x++)
	{
		for (int32 y = 0; y <= Entry.SizeY; y++)
		{
			for (int32 z = 0; z <= Entry.SizeZ; z++)
			{
				const FIntVector Location = OriginXYZ + FIntVector(x,y,z);
				OutArray.Add(XYZToIndex(Location,Self->GetGridSizeX(),Self->GetGridSizeY()));
			}
		}
	}
	
	check(OutArray.Num() == ((Entry.SizeX + 1) * (Entry.SizeY + 1) * (Entry.SizeZ + 1)))
	return OutArray;
}

TArray<uint16> UPVGPrecomputedGridDataAsset::GetCellData(int32 CellId) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GetCellData)
	TArray<uint16> Data;
	for (int32 i = 0; i < GridCellData[CellId].CellData.Num(); i++)
	{
		Data.Append(FPackedVisibilityData::Unpack(GridCellData[CellId].CellData[i],this));
	}
	
	return Data;
}
#if WITH_EDITORONLY_DATA

void UPVGPrecomputedGridDataAsset::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	if (GridData.Num() > 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("Compressing"));
		// Cube compression.
		GridCellData.SetNumZeroed(GridData.Num());

		constexpr int32 NumTasks = 32;
		const int32 NumPerTask = FMath::DivideAndRoundUp(GridData.Num(), NumTasks);
	
		ParallelFor(NumTasks,[&](int32 TaskID)
		{
			const int32 Start = TaskID * NumPerTask;
			const int32 End = FMath::Min(Start + NumPerTask,GridData.Num());

			for (int32 i = Start; i < End; i++)
			{
				CubeCompress(GridData[i],GridCellData[i].CellData);
			}
		});
	}
}
#endif	

void UPVGPrecomputedGridDataAsset::CubeCompress(const FRawRegionVisibilityData16& InData, TArray<FPackedVisibilityData>& Out)
{
	double StartTime = FPlatformTime::Seconds();
	TArray<FPackedVisibilityData> BoxedEntries;
	
	// Entries to ignore.
	TArray<uint16> SingleEntries;
	
	int32 CurrentCell = 0;
	int32 NumEntriesBatched = 1; /* self is 1*/
	TSet<uint16> Processed;

	constexpr bool MinimalMemoryOptimization = true;
	
	while ( CurrentCell <  InData.InvisibleRegions.Num())
	{
		bool bCanGrowX = true;
		bool bCanGrowY = true;
		bool bCanGrowZ = true;
		
		uint16 Origin = InData.InvisibleRegions[CurrentCell];
		
		uint16 X = 0;
		uint16 Y = 0;
		uint16 Z = 0;

		// Add origin.
		TSet<uint16> Entries;
		
		Entries.Add(Origin);

		/* TODO, we can isolate the known new entries instead of just testing all of them. */
		while (bCanGrowX || bCanGrowY || bCanGrowZ)
		{
			if (bCanGrowX)
			{
				TSet<uint16> TempEntriesX;
				
				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,GetGridSizeX(),GetGridSizeY());
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(1,0,0), GetGridSizeX(),GetGridSizeY());

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (InData.InvisibleRegions.Contains(TestLocation) && (!Processed.Contains(TestLocation) || MinimalMemoryOptimization))
					{
						TempEntriesX.Add(TestLocation);
					}
					else
					{
						bCanGrowX = false;
						break;
					}
				}
				
				if (bCanGrowX)
				{
					X++;
					NumEntriesBatched++;
					Entries.Append(TempEntriesX);
				}
			}
			
			if (bCanGrowY)
			{
				TSet<uint16> TempEntriesY;

				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,GetGridSizeX(),GetGridSizeY());
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(0,1,0), GetGridSizeX(),GetGridSizeY());

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (InData.InvisibleRegions.Contains(TestLocation) && (!Processed.Contains(TestLocation) || MinimalMemoryOptimization))
					{
						TempEntriesY.Add(TestLocation);
					}
					else
					{
						bCanGrowY = false;
						break;
					}
				}

				if (bCanGrowY)
				{
					Entries.Append(TempEntriesY);
					NumEntriesBatched++;
					Y++;
				}
			}
			
			if (bCanGrowZ)
			{
				TSet<uint16> TempEntriesZ;

				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,GetGridSizeX(),GetGridSizeY());
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(0,0,1), GetGridSizeX(),GetGridSizeY());

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (InData.InvisibleRegions.Contains(TestLocation) && (!Processed.Contains(TestLocation) || MinimalMemoryOptimization))
					{
						TempEntriesZ.Add(TestLocation);
					}
					else
					{
						bCanGrowZ = false;
						break;
					}
				}

				if (bCanGrowZ)
				{
					Entries.Append(TempEntriesZ);
					NumEntriesBatched++;
					Z++;
				}
			}
		}

		
		Processed.Append(Entries);
		
		int32 Entry = BoxedEntries.Add(FPackedVisibilityData(Origin,X,Y,Z));
#if WITH_EDITORONLY_DATA && PVG_DEBUG
		BoxedEntries[Entry].ReflectionData = Entries.Array();
#endif
		
		// Skip cells we have processed already.
		CurrentCell++;
		while ( CurrentCell < InData.InvisibleRegions.Num() && Processed.Contains(InData.InvisibleRegions[CurrentCell]))
		{
			CurrentCell++;
		}
	}

	// check boxes
	int32 NumUnpacked = 0;
#if 0
	
	TArray<uint16> unPacked;
	if (!MinimalMemoryOptimization)
	{
		for (int32 i = 0; i < BoxedEntries.Num(); i++)
		{
			TArray<uint16> t = FPackedVisibilityData::Unpack(BoxedEntries[i],this);
			unPacked.Append(t);
			NumUnpacked += t.Num();
		}
	}
	else
	{
		TSet<uint16> UnpackedSet;
		for (int32 i = 0; i < BoxedEntries.Num(); i++)
		{
			TArray<uint16> t = FPackedVisibilityData::Unpack(BoxedEntries[i],this);
			UnpackedSet.Append(t);
		}
		
		unPacked = UnpackedSet.Array();
		NumUnpacked = unPacked.Num();
	}
#endif
	
	UE_LOG(LogTemp,Log,TEXT("[%f MS]Boxes: %d ( %.4f kb)vs %d ( %.4f kb )entries, Batched: %d == %d Num Unpacked: %d(debug only) "),
		(FPlatformTime::Seconds() - StartTime) / 1000.f,	
		BoxedEntries.Num(),				float(float(BoxedEntries.Num() * sizeof(uint16)) * 4.f / 1000.f),
		InData.InvisibleRegions.Num(),	float(float(InData.InvisibleRegions.Num() * sizeof(uint16)) / 1000.f),
		Processed.Num(), InData.InvisibleRegions.Num(),NumUnpacked);

#if 0
	// find dupes
	if (InData.InvisibleRegions.Num() != NumUnpacked)
	{
		TSet<uint16> Relf;
		bool bDupe = false;
		for (int32 i = 0 ; i < unPacked.Num(); i++)
		{
			Relf.Add(unPacked[i],&bDupe);
			if (bDupe)
			{
				UE_LOG(LogTemp,Warning,TEXT("Dupe!"));
			}
		}
	}
#endif
	Out = BoxedEntries;
}

#if WITH_EDITOR
void UPVGPrecomputedGridDataAsset::SetDataCell(int32 Cell, int32 InvisibleRegion, bool bVisible)
{
	if (Cell == InvisibleRegion)
	{
		UE_LOG(LogTemp,Warning,TEXT("Cannot add self!"));
		return;
	}
	
	if (bVisible)
	{
		GridData[Cell].VisibleRegions.Add(InvisibleRegion);
		GridData[InvisibleRegion].VisibleRegions.Add(Cell);
	}
	else
	{
		GridData[Cell].InvisibleRegions.Add(InvisibleRegion);
		GridData[InvisibleRegion].InvisibleRegions.Add(Cell);
	}
}
#endif
