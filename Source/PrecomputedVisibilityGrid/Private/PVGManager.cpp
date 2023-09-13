// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGManager.h"

#include "Editor.h"
#include "PrecomputedVisibilityGrid.h"
#include "PVGBuilder.h"
#include "PVGInterface.h"
#include "PVGPrecomputedGridDataAsset.h"
#include "Selection.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

APVGManager* APVGManager::Manager = nullptr;

TAutoConsoleVariable<int32> CVarPVGManagerDebugDrawMode(
	TEXT("r.culling.debug.DrawMode"),
	0,
	TEXT("Debug draw mode: \n "
	"0: None.\n"
	"1: Player cell & hidden cells."
	"2: 1 and hidden primitives"),
	ECVF_Cheat
);

// TODO
static float GPVGMinDistanceForCulling = 0;//50 * 100;
static FAutoConsoleVariableRef CVarPVGMinDistanceForCulling(
	TEXT("r.PVG.Culling.MinDistance"),
	GPVGMinDistanceForCulling,
	TEXT("Minimum distance for pre computed culling.\n")
	TEXT("Default: 50000 units"),
	ECVF_Default
);

// TODO
static int32 GPVGIgnoreDistanceOnLowerCells = 3;//2;
static FAutoConsoleVariableRef CVarPVGIgnoreDistanceOnLowerCells(
	TEXT("r.PVG.Culling.IgnoreLowerCellsForDistanceCheck"),
	GPVGIgnoreDistanceOnLowerCells,
	TEXT("Ignore cells below current cell, this could result in minor shadow popping.\n")
	TEXT("0: Off,\tRespect minimum distance \n")
	TEXT("1: On,\tIgnore after 50% distance \n")
	TEXT("2: On,\tIgnore after 25% distance.\n")
	TEXT("3: On,\tIgnore distance.\n"),
	ECVF_Default
);

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

// Sets default values
APVGManager::APVGManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	RootComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	RootComponent->Mobility = EComponentMobility::Static;
	RootComponent->SetCanEverAffectNavigation(false);
}

void APVGManager::ReportActorToPVGManager(AActor* Actor)
{
	APVGManager* PVGManager = GetManager();
	if (!PVGManager)
	{
		UE_LOG(LogTemp,Error,TEXT("NO PVG FOUND"));
		return;
	}
	
	FVector Origin;
	FVector Extent;

	Actor->GetActorBounds(false,Origin,Extent,false);

	const FVector MinInGridSpace = PVGManager->GetLocationInGridSpace(Origin - Extent);
	const FVector MaxInGridSpace = PVGManager->GetLocationInGridSpace(Origin + Extent);

	const int32 MinX = FMath::FloorToInt(float(MinInGridSpace.X / PVGManager->CellSize.GetSize().X));
	const int32 MinY = FMath::FloorToInt(float(MinInGridSpace.Y / PVGManager->CellSize.GetSize().Y));
	const int32 MinZ = FMath::FloorToInt(float(MinInGridSpace.Z / PVGManager->CellSize.GetSize().Z));
	
	const int32 MaxX = FMath::FloorToInt(float(MaxInGridSpace.X / PVGManager->CellSize.GetSize().X));
	const int32 MaxY = FMath::FloorToInt(float(MaxInGridSpace.Y / PVGManager->CellSize.GetSize().Y));
	const int32 MaxZ = FMath::FloorToInt(float(MaxInGridSpace.Z / PVGManager->CellSize.GetSize().Z));

	int32 MinId = PVGManager->XYZToIndex(MinX,MinY,MinZ);
	int32 MaxId = PVGManager->XYZToIndex(MaxX,MaxY,MaxZ);

	// Same cell.
	if (MinId == MaxId)
	{
		if (PVGManager->CellRuntimeData.IsValidIndex(MinId))
		{
			PVGManager->CellRuntimeData[MinId].Actors.Add(Actor);

			if(Manager->IsCellHidden(MinId))
			{
				Manager->SetHidden(Actor,true);
			}
		}
	}
	else // Multi cell actor. figure out which cells.
	{
		TArray<int32> Ids; 
		FCellActorContainer* Entry = new FCellActorContainer();
			
		for (int32 IdX = MinX; IdX < MaxX + 1; IdX++)
		{
			for (int32 IdY = MinY; IdY < MaxY + 1; IdY++)
			{
				for (int32 IdZ = MinZ; IdZ < MaxZ + 1; IdZ++)
				{
					const int32 Index = Manager->XYZToIndex(IdX,IdY,IdZ);
					Ids.Add(Index);
				}
			}
		}

		// Determine initial state.
		bool bIsHidden = true;
		for (int32 i = 0; i< Ids.Num(); i++)
		{
			PVGManager->CellRuntimeData[Ids[i]].MultiCellActors.Add(Entry);
			
			if(!Manager->IsCellHidden(Ids[i]))
			{
				bIsHidden = false;
			}
		}

		Entry->Actor = Actor;
		Entry->VisibleCounter = !bIsHidden ? Ids.Num() : 0;

		if (bIsHidden)
		{
			Manager->SetHidden(Actor,true);
		}
	}
}

bool APVGManager::IsCellHidden(int32 Index) const
{
	return HiddenCells.Contains(Index);
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

void APVGManager::SetHidden(AActor* Actor, bool bState)
{
	//if (Actors[i]->GetClass()->ImplementsInterface(UPVGInterface::StaticClass()))
	//{
	//	IPVGInterface::Execute_UpdateVisibility(Actors[i],bHide);
	//}
	//else
	{
		Actor->SetActorHiddenInGame(bState);
		//DrawDebugBox(GetWorld(),Actor->GetActorLocation(),FVector(200.f),FColor::Red,true,10.f );
	} 
}

void APVGManager::DrawDebugHUDInfo()
{
	if (GEngine)
	{
		TArray<FString> ToPrint;
		const int32 CurrentCell = GetPlayerGridIndex();
		// Header
		ToPrint.Add(FString("Precomputed Debug View"));

		// Current cell.
		ToPrint.Add(FString("Current Cell: ") + FString::FromInt(CurrentCell));

		// Cell Data.
		if (CellRuntimeData.IsValidIndex(CurrentCell))
		{
			const int32 NumActorsInCell = CellRuntimeData[CurrentCell].Actors.Num();
			const int32 NumMultiActorsInCell = CellRuntimeData[CurrentCell].MultiCellActors.Num();

			ToPrint.Add(FString("Num Simple Actors in cell: ") + FString::FromInt(NumActorsInCell));
			ToPrint.Add(FString("Num Multi cell Actors in cell: ") + FString::FromInt(NumMultiActorsInCell));
		}

		// Check if we have something selected
#if WITH_EDITOR
		if (GEditor )
		{
			bool bGotAnActor = false;
			TArray<AActor*> SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
			
			for ( auto Actor : SelectedActors)
			{
				if (IsValid(Actor) && Actor->GetClass()->ImplementsInterface(UPVGInterface::StaticClass()))
				{
					bGotAnActor = true;
					break;
				}
			}

			if (bGotAnActor)
			{
				ToPrint.Add("\tSelectedActor(s):");
				
				for (const AActor* Actor : SelectedActors)
				{
					if (IsValid(Actor) && Actor->GetClass()->ImplementsInterface(UPVGInterface::StaticClass()))
					{
						ToPrint.Add("\t\t" + Actor->GetName());
						int32 GridIndex = GetIndexFromLocation(Actor->GetActorLocation());
						
						if (CellRuntimeData.IsValidIndex(GridIndex))
						{
							for (const FCellActorContainer* Entry : CellRuntimeData[GridIndex].MultiCellActors)
							{
								if (Entry->Actor == Actor)
								{
									ToPrint.Add("\t\tVisCounter: " + FString::FromInt(Entry->VisibleCounter));

									// Find cells.
									const FIntVector Index3D = IndexTo3D(GridIndex, GridDataAsset->GetGridSizeX(), GridDataAsset->GetGridSizeY());

									for (int32 x = -1; x <= 1; x++)
									{
										for (int32 y = -1; y <= 1; y++)
										{
											for (int32 z = -1; z <= 1; z++)
											{
												if (Index3D.X + x < GridDataAsset->GetGridSizeX() && Index3D.X + x >= 0 &&
													Index3D.Y + y < GridDataAsset->GetGridSizeY() && Index3D.Y + y >= 0 &&
													Index3D.Z + z < GridDataAsset->GetGridSizeZ() && Index3D.Z + z >= 0 )
												{
													const int32 OffsetId = XYZToIndex(Index3D.X + x,Index3D.Y + y,Index3D.Z + z);
													if (CellRuntimeData.IsValidIndex(OffsetId))
													{
														auto TestEntry = CellRuntimeData[OffsetId].MultiCellActors.FindByPredicate([TestActor = Entry->Actor](const FCellActorContainer* A)
														{
															return A->Actor == TestActor;
														});
																												
														if (TestEntry != nullptr)
														{
															DrawDebugSphere(GWorld,IndexToLocation(OffsetId),16,6,FColor::Purple,false,-1,255);

															if (!HiddenCells.Contains(OffsetId))
															{
																DrawDebugBox(GWorld,IndexToLocation(OffsetId),CellSize.GetExtent(),FColor::Orange,false,-1,255);
															}
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
#endif
		for (int32 i = ToPrint.Num() - 1; i >= 0; --i)
		{
			GEngine->AddOnScreenDebugMessage(i,-1,FColor::Blue, ToPrint[i],false);
		}
	}
}

void APVGManager::DebugDrawSelected(TArray<FString>& OutPrints)
{
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
				// Draw current celt.
				const FVector CurrentCellLocation = IndexToLocation(CurrentIndex);

				TArray<uint16> CurrentCellData = GridDataAsset->GetCellData(CurrentIndex);
				for (int32 i = 0; i < CurrentCellData.Num(); i++)
				{
					FVector CellLocation = IndexToLocation(CurrentCellData[i]);
					FColor DebugColor = FColor::Red;
					
					if (GPVGIgnoreDistanceOnLowerCells > 0)
					{
						if ( CellLocation.Z < CurrentCellLocation.Z )
						{
							// 50% dist
							if ((GPVGIgnoreDistanceOnLowerCells == 1 && FVector::Distance(CurrentCellLocation,CellLocation) < GPVGMinDistanceForCulling * 0.5) ||
								(GPVGIgnoreDistanceOnLowerCells == 2 && FVector::Distance(CurrentCellLocation,CellLocation) < GPVGMinDistanceForCulling * 0.25))
							{
								DebugColor = FColor::Blue;
							}
							else if (GPVGIgnoreDistanceOnLowerCells > 2)
							{
								DebugColor = FColor::Red;
							}
						}
						else if(FVector::Distance(CurrentCellLocation,CellLocation) < GPVGMinDistanceForCulling)
						{
							DebugColor = FColor::Blue;
						}
					}
					else if (FVector::Distance(CurrentCellLocation,CellLocation) < GPVGMinDistanceForCulling )
					{
						DebugColor = FColor::Blue;
					}
					
					DrawDebugBox(GetWorld(),CellLocation,CellSize.GetExtent(),DebugColor,false,-1,255);
					DrawDebugPoint(GetWorld(),CellLocation,5.f,DebugColor,false,-1,255);

				}

				DrawDebugBox(GetWorld(),CurrentCellLocation,CellSize.GetExtent(),FColor::Green,false,-1,255);
			}
		}
	}

	// Debug
	DrawDebugHUDInfo();
}

void APVGManager::UpdateCellsVisibility(int32 PlayerCellLocation)
{
	// TODO refactor this.
	const FVector CurrentCellLocation = IndexToLocation(PlayerCellLocation);
	
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
		const FVector CurrentCell = IndexToLocation(CellToHide);

		// distance check.
		bool bShouldHide = true;

#if 0
		if (GPVGIgnoreDistanceOnLowerCells > 2)
		{
			if ( CurrentCell.Z < CurrentCellLocation.Z)
			{
				bShouldHide = true;
			}
		}
		
		if (!bShouldHide)
		{
			float Distance = FVector::Distance(CurrentCell,CurrentCellLocation);
			if ( GPVGIgnoreDistanceOnLowerCells < 2 && GPVGIgnoreDistanceOnLowerCells != 0)
			{
				if (GPVGIgnoreDistanceOnLowerCells == 1 && Distance > GPVGMinDistanceForCulling * 0.5)
				{
					bShouldHide = true;
				}
				else if (GPVGIgnoreDistanceOnLowerCells == 2 && Distance > GPVGMinDistanceForCulling * 0.25)
				{
					bShouldHide = true;
				}
			}

			// Didn't we figure it out yet and we are out of the min distance? hide it!
			if (!bShouldHide && Distance > GPVGMinDistanceForCulling)
			{
				bShouldHide = true;
			}
		}
#endif
		
		if (bShouldHide && !HiddenCells.Contains(CellToHide))
		{
			CellsToHide.AddUnique(CellToHide);
		}
	}
}

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
			UE_LOG(LogTemp,Warning,TEXT("Mutli actor cell %d"),Cell);
			if (bHide == true)
			{
				MultiCellActors[i]->VisibleCounter--;
			}
			else
			{
				MultiCellActors[i]->VisibleCounter++;
			}
			UE_LOG(LogTemp,Warning,TEXT("Vis counter: %d"),MultiCellActors[i]->VisibleCounter);

			// Can we hide it?
			if ( MultiCellActors[i]->VisibleCounter == 0 )
			{
				SetHidden(MultiCellActors[i]->Actor, true);
			}
			else
			{
				if (MultiCellActors[i]->Actor->IsHidden())
				{
					SetHidden(MultiCellActors[i]->Actor, false);
				}
			}
		}
	}

	for (int32 i = 0; i < Actors.Num(); i++)
	{
		SetHidden(Actors[i], bHide);
	}
}
	
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

int32 APVGManager::GetIndexFromLocation(const FVector& Location) const
{
	const FVector PlayerLocationInGridSpace = GetLocationInGridSpace(Location);

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

#if WITH_EDITOR
APVGBuilder* APVGManager::StartBuild()
{
	Manager = this;
	
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
			}
		}
	}

	UE_LOG(LogTemp,Warning,TEXT("GridBounds %s"),*GridBounds.ToString());

	APVGBuilder* Builder = GetWorld()->SpawnActor<APVGBuilder>();
	Builder->Initialize(Locations,FIntVector(SizeX,SizeY,SizeZ));

	return Builder;
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

void APVGManager::Test()
{
	FVector Vert[8];
	FBox TestBoxA = CellSize.MoveTo(LocationA);
	TestBoxA.GetVertices(Vert);

	for (int32 i = 0; i < 8; i++)
	{
		DrawDebugPoint(GWorld,Vert[i],5.f,FColor::Red,false,10.f);
		DrawDebugString(GWorld,Vert[i],FString::FromInt(i),nullptr,FColor::White,10.f,false,2);
	}
	
	DrawDebugBox(GetWorld(),LocationA,CellSize.GetExtent(),FColor::Red,false,10.f);
	DrawDebugBox(GetWorld(),LocationB,CellSize.GetExtent(),FColor::Green,false,10.f);

	FRotator ViewRotation = UKismetMathLibrary::FindLookAtRotation(Vieuw,LocationB);
	FVector CameraViewVector = ViewRotation.Vector();

	// Calculate the camera's right vector
	FVector CameraRightVector = FVector::CrossProduct(CameraViewVector, FVector::UpVector);
	CameraRightVector.Normalize();

	// Calculate the camera's up vector
	FVector CameraUpVector = FVector::CrossProduct(CameraRightVector, CameraViewVector);
	CameraUpVector.Normalize();

	// Calculate the view matrix
	FMatrix ViewMatrix = FMatrix(
		FPlane(CameraRightVector.X, CameraUpVector.X, -CameraViewVector.X, 0.0f),
		FPlane(CameraRightVector.Y, CameraUpVector.Y, -CameraViewVector.Y, 0.0f),
		FPlane(CameraRightVector.Z, CameraUpVector.Z, -CameraViewVector.Z, 0.0f),
		FPlane(-FVector::DotProduct(CameraRightVector, Vieuw),
			   -FVector::DotProduct(CameraUpVector, Vieuw),
			   FVector::DotProduct(CameraViewVector, Vieuw), 1.0f)
	);

	FVector2d ViewportSize(1000);

	// Calculate the projection matrix
	float FOV = 90;
	const float HalfXFOV = FMath::DegreesToRadians(FOV) * 0.5f;
	const float HalfYFOV = FMath::Atan(FMath::Tan(HalfXFOV) / 1);

	FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(HalfXFOV,HalfYFOV, 1, 1,0, 10);

	// Calculate the View-Projection matrix
	FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

	DrawDebugPoint(GetWorld(),Vieuw,10,FColor::Purple,false,10.f);
	DrawDebugLine(GetWorld(),Vieuw,Vieuw + (ViewRotation.Vector() * 1000),FColor::Purple,false,10.f);

	//FVector Vert[8];
	//FBox TestBoxA = CellSize.MoveTo(LocationA);
	TestBoxA.GetVertices(Vert);
	for (int32 i = 0; i < 8; i++)	
	{
		// Transform vertex by view matrix
		FVector TransformedVertex = ViewProjectionMatrix.TransformFVector4(FVector4(Vert[i],1));

		{
			// Project the vertex onto screen space
			FVector2D ScreenPosition = FVector2D(TransformedVertex.X,TransformedVertex.Y);

			UE_LOG(LogTemp,Warning,TEXT("%s - > %s"),*Vert[i].ToString(),*ScreenPosition.ToString());
			DrawDebugPoint(GetWorld(),FVector(ScreenPosition,0),8,FColor::Red, false,10.f,255);
		}
	}
	FBox TestBoxB = CellSize.MoveTo(LocationB);
	TestBoxB.GetVertices(Vert);
	
	UE_LOG(LogTemp,Warning,TEXT("BOX2"));
	for (int32 i = 0; i < 8; i++)	
	{
		FVector TransformedVertex = ViewProjectionMatrix.TransformFVector4(FVector4(Vert[i],1));
		{
			// Project the vertex onto screen space
			FVector2D ScreenPosition = FVector2D(TransformedVertex.X,TransformedVertex.Y);

			
			UE_LOG(LogTemp,Warning,TEXT("%s - > %s"),*Vert[i].ToString(),*ScreenPosition.ToString());
			DrawDebugPoint(GetWorld(),FVector(ScreenPosition,0),8,FColor::Green, false,10.f,255);
		}
	}
}

TArray<FVector> APVGManager::boxverts(FBox box)
{
	FVector Loc[8];
	box.GetVertices(Loc);

	TArray<FVector> locs;
	for (int32 i = 0; i < 8; i++)
	{
		locs.Add(Loc[i]);
	}

	return locs;
}

#pragma optimize("",off)
TArray<FVector2D> APVGManager::Testlala(FBox A, FBox B, FVector ViewLocation, FRotator ViewRotation, UObject* worldcontext)
{
	DrawDebugBox(worldcontext->GetWorld(),A.GetCenter(),A.GetExtent(),FColor::Red,false,10,255);
	
	TArray<FVector2D> Out;
	FVector Verts[8];
	A.GetVertices(Verts);
	
	const float HalfFOVRadians = FMath::DegreesToRadians<float>(40) * 0.5f;
	FMatrix const ViewRotationMatrix = FInverseRotationMatrix(ViewRotation) * FMatrix( FPlane(0,	0,	1,	0), FPlane(1,	0,	0,	0), FPlane(0,	1,	0,	0), FPlane(0,	0,	0,	1));
	FMatrix const ProjectionMatrix = FReversedZPerspectiveMatrix( HalfFOVRadians, HalfFOVRadians, 1, 1, GNearClippingPlane, GNearClippingPlane );
	FMatrix const ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;

	for (int32 i = 0; i < 8; i++)
	{
		FVector2D Pos;
		bool bResult = FSceneView::ProjectWorldToScreen(Verts[i], FIntRect(0,0,1000,1000), ViewProjectionMatrix, Pos);
		Out.Add(Pos);
	}

	FVector BVerts[8];
	B.GetVertices(BVerts);
	TArray<FVector2d> TestBox;

	for (int32 i = 0; i < 8; i++)
	{
		FVector2D Pos;
		bool bResult = FSceneView::ProjectWorldToScreen(BVerts[i], FIntRect(0,0,1000,1000), ViewProjectionMatrix, Pos);
		TestBox.Add(Pos);
	}

	if (TestBox.Num() != 8 || Out.Num() != 8)
	{
		UE_LOG(LogTemp,Warning,TEXT("failed"));
		return Out;
	}
	
	// compare.
	constexpr uint8 QuadFaces[6][4] {{0,1,6,2},{0,1,5,3},{2,6,7,4},{0,2,4,3},{1,6,7,5},{3,5,7,4}};

	// Check if the point is in any of the shapes.
	int32 PointsInside = 0;
	for (int32 i = 0; i < 8; i++)
	{
		// For each shape
		for (int32 Face = 0; Face < 6; Face++)
		{
			int32 Intersections = 0;
			for (int32 Edge = 0; Edge < 4; Edge++)
			{
				FVector IntersectionPoint;
				if (FMath::SegmentIntersection2D(
					FVector(Out[QuadFaces[Face][Edge]],0),
					FVector(Out[QuadFaces[Face][(Edge + 1) % 4]],0),
					FVector(TestBox[i],0),
					FVector(TestBox[i] + FVector2d(200000,0),0),IntersectionPoint))
				{
					Intersections++;
				}
			}
			
			const bool IsEven = Intersections % 2 == 0;
			
			if (!IsEven)
			{
				PointsInside++;
				break;
			}
		}
	}

	UE_LOG(LogTemp,Warning,TEXT("%d inside the shape which means: %s"),PointsInside, PointsInside == 8 ? TEXT("Inside!") : TEXT("Failed!"));
	return Out;
}
#pragma optimize("",on)

#endif