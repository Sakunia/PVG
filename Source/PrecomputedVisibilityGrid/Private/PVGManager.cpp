// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGManager.h"

#include "PVGBuilder.h"
#include "PVGPrecomputedGridDataAsset.h"
#include "SWarningOrErrorBox.h"
#include "Components/BoxComponent.h"

APVGManager* APVGManager::Manager = nullptr;

TAutoConsoleVariable<int32> CVarPVGManagerDebugDrawMode( TEXT("r.culling.debug.DrawMode"),
															0,
															TEXT("Debug draw mode: \n "
															"0: None.\n"
															"1: Player cell & hidden cells."
															"2: 1 and hidden primitives"),
															ECVF_Cheat);

// Sets default values
APVGManager::APVGManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	RootComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	RootComponent->Mobility = EComponentMobility::Static;
	RootComponent->SetCanEverAffectNavigation(false);

}

// Called when the game starts or when spawned
void APVGManager::BeginPlay()
{
	Super::BeginPlay();

	// assign self as manager.
	Manager = this;
	
	if (!GridDataAsset)
	{
		UE_LOG(LogTemp,Error,TEXT("Failed to initalize PVG"));
		return;
	}
	
	// Assign player, TODO do this in a better way.
	Player = GetWorld()->GetFirstPlayerController()->GetPawn();

	// Initialize dynamic data. // TODO maybe we want to do this on demand?
	CellRuntimeData.SetNum(GridDataAsset->GetNumCells());

	// Setup grid snapped bounds
	GridBounds = GridDataAsset->GetGridBounds();
}

void APVGManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Manager = nullptr;
}

// Called every frame
void APVGManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	bool bIsEnabled = false;
	
	if (!GridDataAsset)
	{
		return;
	}
	
	if (!bIsEnabled)
	{
		// Disable all culling logic.
	}

	// No cell data.
	if (GridDataAsset && GridDataAsset->GetNumCells() == 0)
	{
		return;
	}

	const int32 PlayerGridIndex = GetPlayerGridIndex();

	if (CurrentIndex != PlayerGridIndex)
	{
		CurrentIndex = PlayerGridIndex;
		UpdateCellsVisibility(CurrentIndex);
	}
	
	UpdateCells();

	if (CurrentIndex >= 0 && CurrentIndex < GridDataAsset->GetNumCells() )
	{
		if (CVarPVGManagerDebugDrawMode.GetValueOnAnyThread() > 0 )
		{
			const int32 DebugValue = CVarPVGManagerDebugDrawMode.GetValueOnAnyThread();
			if (DebugValue >= 1)
			{
				TArray<uint16> CurrentCellData = GridDataAsset->GetCellData(CurrentIndex);
				for (int32 i = 0; i < CurrentCellData.Num(); i++)
				{
					FVector CellLocation = IndexToLocation(CurrentCellData[i]);
					DrawDebugBox(GetWorld(),CellLocation,CellSize.GetExtent(),FColor::Red,false,-1,255);
				}

				// Draw current celt.
				const FVector CellLocation = IndexToLocation(CurrentIndex);
				DrawDebugBox(GetWorld(),CellLocation,CellSize.GetExtent(),FColor::Green,false,-1,255);
			}
		}
	}
}


#if UE_BUILD_TEST || !UE_BUILD_SHIPPING
#define CheckForDupes(Arr) CheckForDupesInternal(Arr);
template<typename T>
void CheckForDupesInternal(const TArray<T>& Arr)
{
	bool bIsDupe = false;
	TSet<T> ReflectionSet;
	for (auto Entry : Arr)
	{
		ReflectionSet.Add(Entry,&bIsDupe);
		if (bIsDupe)
		{
			UE_LOG(LogTemp,Error,TEXT("Dupe found!"))
			break;
		}
	}
}
#else
#define CheckForDupes(Arr) {}
#endif

void APVGManager::UpdateCellsVisibility(int32 PlayerCellLocation)
{
	if (!GridDataAsset->IsCellIndexValid(PlayerCellLocation))
	{
		return;
	}

	const TArray<uint16> Region = GridDataAsset->GetCellData(PlayerCellLocation);
	
	// Remove hidden cells.
	for (int32 i = 0; i < HiddenCells.Num(); i++)
	{
		const uint16 ID = HiddenCells[i];
		if(!Region.Contains(ID))
		{
			CellsToUnHide.AddUnique(ID);
		}
	}

	CheckForDupes(CellsToUnHide);

	// Remove the cells from the hidden list.
	for (int32 i = 0; i < CellsToUnHide.Num(); i++)
	{
		HiddenCells.Remove(CellsToUnHide[i]);
	}

	HiddenCells.Shrink();
	
	// Find cells to hide.
	for (int32 i = 0; i < Region.Num(); i++)
	{
		const uint16 CellToHide = Region[i];
		if (!HiddenCells.Contains(CellToHide))
		{
			CellsToHide.AddUnique(CellToHide);
		}
	}
}

#pragma optimize("",off)

FVector APVGManager::GetLocationInGridSpace(const FVector& Location) const
{
	FVector OffsetLocation = GetActorTransform().InverseTransformPositionNoScale(Location);
	OffsetLocation += GetStreamingBounds().GetExtent();
	return OffsetLocation;
}

int32 APVGManager::GetPlayerGridIndex() const
{
	const FVector PlayerLocationInGridSpace = GetLocationInGridSpace(Player->GetActorLocation());

	const int32 X = FMath::FloorToInt(float(PlayerLocationInGridSpace.X / CellSize.GetSize().X));
	const int32 Y = FMath::FloorToInt(float(PlayerLocationInGridSpace.Y / CellSize.GetSize().Y));
	const int32 Z = FMath::FloorToInt(float(PlayerLocationInGridSpace.Z / CellSize.GetSize().Z));

	return XYZToIndex(X,Y,Z);
}

FVector APVGManager::IndexToLocation(int32 Index) const
{
	uint16 z = Index / (GridDataAsset->GetGridSizeX() * GridDataAsset->GetGridSizeY());
	Index -= (z * GridDataAsset->GetGridSizeX() * GridDataAsset->GetGridSizeY());
	uint16 y = Index / GridDataAsset->GetGridSizeX();
	uint16 x = Index % GridDataAsset->GetGridSizeX();

	
	FVector LocalLocation = FVector(x, y, z) * CellSize.GetSize() + (CellSize.GetSize() / 2);
	LocalLocation += GetActorLocation() - GetStreamingBounds().GetExtent();
	
	return LocalLocation;
}

FVector APVGManager::IndexToLocation(int32 x, int32 y, int32 z) const
{
	return IndexToLocation((z * GridDataAsset->GetGridSizeX() * GridDataAsset->GetGridSizeY()) + (y * GridDataAsset->GetGridSizeX()) + x);
}

int32 APVGManager::XYZToIndex(int32 x, int32 y, int32 z) const
{
	return (z * GridDataAsset->GetGridSizeX() * GridDataAsset->GetGridSizeY()) + (y * GridDataAsset->GetGridSizeX()) + x;
}
#pragma optimize("",on)
void APVGManager::UpdateCells()
{
	// Un hide.
	for (int32 i = 0; i < CellsToUnHide.Num(); i++)
	{
		const int32 Entry = CellsToUnHide[i];
		UpdateCellVisibility(Entry,false);
	}
	
	CellsToUnHide.Empty();
	
	// To Hide.
	for (int32 i = 0; i < CellsToHide.Num(); i++)
	{
		const int32 Entry = CellsToHide[i];
		UpdateCellVisibility(Entry,true);

		HiddenCells.Add(Entry);
	}

	CellsToHide.Empty();
}

void APVGManager::UpdateCellVisibility(int32 Cell, bool bHide)
{
	const auto& Actors = CellRuntimeData[Cell].Actors;
	const auto& MultiCellActors = CellRuntimeData[Cell].MultiCellActors;
	
	for(int32 i = 0; i < MultiCellActors.Num(); i++)
	{
		if (MultiCellActors.IsValidIndex(i) && MultiCellActors[i])	
		{
			if (bHide == true)
			{
				MultiCellActors[i]->VisibleCounter--;
			}
			else
			{
				MultiCellActors[i]->VisibleCounter++;

			}

			// Can we hide it?
			if ( MultiCellActors[i]->VisibleCounter == 0 )
			{
				MultiCellActors[i]->Actor->SetActorHiddenInGame(true);
			}
			else
			{
				if (MultiCellActors[i]->Actor->IsHidden())
				{
					MultiCellActors[i]->Actor->SetActorHiddenInGame(false);
				}
			}
		}
	}

	for (int32 i = 0; i < Actors.Num(); i++)
	{
		Actors[i]->SetActorHiddenInGame(bHide);
	}
}
	
#if WITH_EDITOR
void APVGManager::StartBuild()
{
	SetActorTickEnabled(false);
	
	const FBox Bounds = RootComponent->GetStreamingBounds();
	const int32 SizeX = FMath::DivideAndRoundUp(Bounds.GetSize().X, CellSize.GetSize().X);
	const int32 SizeY = FMath::DivideAndRoundUp(Bounds.GetSize().Y, CellSize.GetSize().Y);
	const int32 SizeZ = FMath::DivideAndRoundUp(Bounds.GetSize().Z, CellSize.GetSize().Z);

	const float CellSizeX = CellSize.GetSize().X;
	const float CellSizeY = CellSize.GetSize().Y;
	const float CellSizeZ = CellSize.GetSize().Z;

	TArray<FVector> Locations;
	for (int32 z = 0; z < SizeZ; z++)
	{
		for (int32 y= 0; y < SizeY; y++)
		{
			for (int32 x = 0; x < SizeX; x++)
			{
				FVector Location = FVector(CellSizeX * x,CellSizeY * y,CellSizeZ * z) + FVector(CellSizeX/2,CellSizeY/2,CellSizeZ/2);
				Location += GetActorLocation() - Bounds.GetExtent();
				Locations.Add(Location);

				// Debug draw
				// DrawDebugBox(GetWorld(),Location,CellSize.GetExtent(),FColor::Blue,false,10.f,255,10);
			}
		}
	}

	UE_LOG(LogTemp,Warning,TEXT("GridBounds %s"),*GridBounds.ToString());

	APVGBuilder* Builder = GetWorld()->SpawnActor<APVGBuilder>();
	Builder->Initialize(Locations,FIntVector(SizeX,SizeY,SizeZ));
}

void APVGManager::DebugDrawCells()
{
	const FBox Bounds = RootComponent->GetStreamingBounds();
	const int32 SizeX = FMath::DivideAndRoundUp(Bounds.GetSize().X, CellSize.GetSize().X);
	const int32 SizeY = FMath::DivideAndRoundUp(Bounds.GetSize().Y, CellSize.GetSize().Y);
	const int32 SizeZ = FMath::DivideAndRoundUp(Bounds.GetSize().Z, CellSize.GetSize().Z);
	
	const float CellSizeX = CellSize.GetSize().X;
	const float CellSizeY = CellSize.GetSize().Y;
	const float CellSizeZ = CellSize.GetSize().Z;
	
	for (int32 z = 0; z < SizeZ; z++)
	{
		for (int32 y= 0; y < SizeY; y++)
		{
			for (int32 x = 0; x < SizeX; x++)
			{
				FVector Location = FVector(CellSizeX * x,CellSizeY * y,CellSizeZ * z) + FVector(CellSizeX/2,CellSizeY/2,CellSizeZ/2);
				Location += GetActorLocation() - Bounds.GetExtent();
				
				DrawDebugBox(GetWorld(),Location,CellSize.GetExtent(),FColor::Blue,false,10.f,255,10);
			}
		}
	}
}
#endif


