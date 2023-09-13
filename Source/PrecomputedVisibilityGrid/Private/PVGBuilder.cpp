// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGBuilder.h"

#include "EngineUtils.h"
#include "PrecomputedVisibilityGrid.h"
#include "PVGManager.h"
#include "PVGPrecomputedGridDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "InstancedFoliageActor.h"
#include "Async/ParallelFor.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "UObject/SavePackage.h"

#define BOX_SCENE 1

TAutoConsoleVariable<float> CVARPVGBuilderOcclusionTestMultiplier(TEXT("r.Culling.Builder.OcclusioCullSizeTestMultiplier"),1.01,TEXT("Scale adjustment for occlusion test."));

FIntVector IndexTo3D_(int32 Index, const UPVGPrecomputedGridDataAsset* Self)
{
	uint16 z = Index / (Self->GetGridSizeX() * Self->GetGridSizeY());
	Index -= (z * Self->GetGridSizeX() * Self->GetGridSizeY());
	uint16 y = Index / Self->GetGridSizeX();
	uint16 x = Index % Self->GetGridSizeX();

	return FIntVector(x,y,z);
};

// Sets default values
APVGBuilder::APVGBuilder()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	StreamingSourceComponent = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("StreamingSourceComp"));
}

void APVGBuilder::Initialize(const TArray<FVector>& InLocations, FIntVector GridSize)
{
	LocationsToBuild = InLocations;
	
	if (InLocations.Num() >= MAX_uint16)
	{
		UE_LOG(LogTemp,Warning,TEXT("Max num cells, please reduce resolution"));
		if ( APlayerController* TargetPC = UGameplayStatics::GetPlayerController(GetWorld(), 0) )
		{
			TargetPC->ConsoleCommand(TEXT("Exit"), true);
		}
		return;
	}
	

	UPVGPrecomputedGridDataAsset* Asset = GetOrCreateCellData(GridSize);
	
	bIsInitialized = true;
	BeginTime = FPlatformTime::Seconds();
}

// Called when the game starts or when spawned
void APVGBuilder::BeginPlay()
{
	Super::BeginPlay();
	
}

UPVGPrecomputedGridDataAsset* APVGBuilder::GetOrCreateCellData(FIntVector GridSize)
{
	APVGManager* Manager = APVGManager::GetManager();
	
	if (!Manager->GridDataAsset)
	{
		const FString LevelName = GWorld->PersistentLevel.GetName();
		FString GridName = FString::FromInt(GridSize.X) + "_";
		GridName += FString::FromInt(GridSize.Y) + "_";
		GridName += FString::FromInt(GridSize.Z);
		
		const FString Name = "PVGGrid_" + LevelName + GridName;
		const FString DefaultPath = "/Game/PrecomputedCulling/" + Name;
		UPackage* CellDataAsset = CreatePackage(*DefaultPath);
		UPVGPrecomputedGridDataAsset* Obj = NewObject<UPVGPrecomputedGridDataAsset>(CellDataAsset, UPVGPrecomputedGridDataAsset::StaticClass(), *Name, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
		FAssetRegistryModule::AssetCreated(Obj);

		if (Obj)
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;

			const FString PackageName = CellDataAsset->GetName();
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			
			const bool bSucceeded = UPackage::SavePackage(CellDataAsset, NULL,*PackageName,SaveArgs);
			// Save Package.
			//const bool bSucceeded = UPackage::SavePackage(CellDataAsset, NULL, RF_Public | RF_Standalone, *PackageName,GError,nullptr,false,true,SAVE_NoError);
			if (!bSucceeded)
			{
				UE_LOG(LogTemp, Error, TEXT("Package '%s' wasn't saved!"), *PackageName)
			}

			Obj->MarkPackageDirty();
			CellDataAsset->SetDirtyFlag(true);
		}

		Manager->GridDataAsset = Obj;
	}
	
	// Setup save data.
	Manager->GridDataAsset->GridSizeX = GridSize.X;
	Manager->GridDataAsset->GridSizeY = GridSize.Y;
	Manager->GridDataAsset->GridSizeZ = GridSize.Z;
	Manager->GridDataAsset->CellExtents = APVGManager::GetManager()->CellSize.GetExtent();
	Manager->GridDataAsset->GridBounds = APVGManager::GetManager()->GridBounds;

	Manager->GridDataAsset->Modify();
	
	// Setup default array.
	const int32 GridArraySize = GridSize.X * GridSize.Y * GridSize.Z;
	Manager->GridDataAsset->GridData.Empty();
	Manager->GridDataAsset->GridData.SetNumZeroed(GridArraySize);
		
	return Manager->GridDataAsset;
}

bool APVGBuilder::BoxOcclusion(uint16 Location, uint16 Target, FBox BaseBox)
{
	TArray<FBox> Blockers;
	
	const FVector CameraLocation =		LocationsToBuild[Location];
	const FRotator CameraViewRotator =	UKismetMathLibrary::FindLookAtRotation(LocationsToBuild[Location],LocationsToBuild[Target]);
	const FVector TargetLocation =		LocationsToBuild[Target];
	
	for (int32 i = 0; i < BoxScene.Num(); i++)
	{
		FVector Hit, Normal;
		float Time;
		if (FMath::LineExtentBoxIntersection(BoxScene[i],CameraLocation,TargetLocation,BaseBox.GetExtent(),Hit,Normal,Time))
		{
			Blockers.Add(BoxScene[i]);
		}
	}

	if (Blockers.Num() == 0)
	{
		return false;
	}
	
	constexpr float HalfFOVRadians = FMath::DegreesToRadians<float>(50) * 0.5f;
	FMatrix const ViewRotationMatrix = FInverseRotationMatrix(CameraViewRotator) * FMatrix( FPlane(0,	0,	1,	0), FPlane(1,	0,	0,	0), FPlane(0,	1,	0,	0), FPlane(0,	0,	0,	1));
	FMatrix const ProjectionMatrix = FReversedZPerspectiveMatrix( HalfFOVRadians, HalfFOVRadians, 1, 1, GNearClippingPlane, GNearClippingPlane );
	FMatrix const ViewProjectionMatrix = FTranslationMatrix(-CameraLocation) * ViewRotationMatrix * ProjectionMatrix;
	
	// Project out target points
	FVector TargetVerts3D[8];
	TArray<FVector2d> TargetVertsInScreenSpace;
		
	const FBox TargetBox = BaseBox.MoveTo(LocationsToBuild[Target]);
	TargetBox.GetVertices(TargetVerts3D);

	for (int32 i = 0; i < 8; i++)
	{
		FVector2D ScreenPosition;
		// TODO check if we need to respect the outcome of this function	
		FSceneView::ProjectWorldToScreen(TargetVerts3D[i], FIntRect(0,0,1000,1000), ViewProjectionMatrix, ScreenPosition);
		TargetVertsInScreenSpace.Add(ScreenPosition);
	}
	constexpr uint8 QuadFaces[6][4] {{0,1,6,2},{0,1,5,3},{2,6,7,4},{0,2,4,3},{1,6,7,5},{3,5,7,4}};
	
	constexpr int32 NumTasks =	24;
	int32 NumPerTask = FMath::DivideAndRoundUp(Blockers.Num(), NumTasks);
	TArray<bool> Results;
	Results.SetNumZeroed(NumTasks);

	ParallelFor(NumTasks,[&](int32 Task)
	{
		const int32 Begin = NumPerTask * Task;
		const int32 End = FMath::Min(Begin + NumPerTask,Blockers.Num());
		
		for (int32 i = Begin; i < End; i++)
		{
			// check if we can grow the box.
			FVector BlockerVerts3D[8];
			TArray<FVector2d> BlockerVertsInScreenSpace;
			const FBox& BlockerBox = BoxScene[i];
			BlockerBox.GetVertices(BlockerVerts3D);
			
			for (int32 j = 0; j < 8; j++)
			{
				FVector2D ScreenPosition;
				// TODO check if we need to respect the outcome of this function	
				FSceneView::ProjectWorldToScreen(BlockerVerts3D[j], FIntRect(0,0,1000,1000), ViewProjectionMatrix, ScreenPosition);

				BlockerVertsInScreenSpace.Add(ScreenPosition);
			}
						
			// compare points
			int32 PointsInside = 0;

			for (int32 Point = 0; Point < 8; Point++)
			{
				// For each shape
				for (int32 Face = 0; Face < 6; Face++)
				{
					int32 Intersections = 0;
					for (int32 Edge = 0; Edge < 4; Edge++)
					{
						FVector IntersectionPoint;
						if (FMath::SegmentIntersection2D(
							FVector(BlockerVertsInScreenSpace[QuadFaces[Face][Edge]],0),
							FVector(BlockerVertsInScreenSpace[QuadFaces[Face][(Edge + 1) % 4]],0),
							FVector(TargetVertsInScreenSpace[Point],0),
							FVector(TargetVertsInScreenSpace[Point] + FVector2d(200000,0),0),IntersectionPoint))
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

			if (PointsInside == 8)
			{
				Results[Task] = true;
				break;
			}
		}
	});

	for (bool Result : Results)
	{
		if (Result)
			return true;
	}
	
	return false;
}

bool APVGBuilder::BoxCornerTraceCheck(FVector A, FVector B, FBox Base)
{
	FWorldContext& EditorWorldContext = GEditor->GetEditorWorldContext();
	
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECollisionResponse::ECR_Block);
	
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;
	QueryParams.AddIgnoredActor(APVGManager::GetManager()->Player);
	
	FBox ABox = Base.MoveTo(A);
	FBox BBox = Base.MoveTo(B);

	FVector AVertices[8];
	FVector BVertices[8];
	ABox.GetVertices(AVertices);
	BBox.GetVertices(BVertices);

	for (int32 i = 0; i < 8; i++)
	{
		const FVector& AVert = AVertices[i];
					
		for (int32 j = 0; j < 8; j++)
		{
			const FVector& BVert = BVertices[j];
						
			if(!EditorWorldContext.World()->LineTraceTestByChannel(AVert,BVert,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
			{
				return true;
			}
		}
	}
	return false;
}

void APVGBuilder::UpdateBoxScene()
{
	
	const UPVGPrecomputedGridDataAsset* Asset = APVGManager::GetManager()->GridDataAsset;
	const uint16 MaxX  = Asset->GetGridSizeX();
	const uint16 MaxY = Asset->GetGridSizeY();
	const FBox SourceBox = Asset->GetCellBox();
	TSet<uint16> Processed;

	BoxScene.Reset();
	
	int32 CurrentBox = 0;
	TArray<FBox> Boxes;

	constexpr bool MinimalMemoryOptimization = true;

	// Build biggest boxes.
	while ( CurrentBox <  ViewBlockers.Num())
	{
		bool bCanGrowX = true;
		bool bCanGrowY = true;
		bool bCanGrowZ = true;
		
		uint16 X = 0;
		uint16 Y = 0;
		uint16 Z = 0;

		const uint16 Origin = ViewBlockers[CurrentBox];

		// Add origin.
		TSet<uint16> Entries;
		Entries.Add(Origin);
		
		while (bCanGrowX || bCanGrowY || bCanGrowZ)
		{
			if (bCanGrowX)
			{
				TSet<uint16> TempEntriesX;
				
				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,MaxX,MaxY);
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(1,0,0), MaxX,MaxY);

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (ViewBlockers.Contains(TestLocation) && (MinimalMemoryOptimization || !Processed.Contains(TestLocation)))
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
					Entries.Append(TempEntriesX);
				}
			}
			
			if (bCanGrowY)
			{
				TSet<uint16> TempEntriesY;

				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,MaxX,MaxY);
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(0,1,0), MaxX,MaxY);

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (ViewBlockers.Contains(TestLocation) && (MinimalMemoryOptimization || !Processed.Contains(TestLocation)))
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
					Y++;
				}
			}
			
			if (bCanGrowZ)
			{
				TSet<uint16> TempEntriesZ;

				for (const uint16& Entry : Entries)
				{
					FIntVector Loc = IndexTo3D(Entry,MaxX, MaxY);
					const uint16 TestLocation = XYZToIndex(Loc + FIntVector(0,0,1), MaxX, MaxY);

					if (Entries.Contains(TestLocation))
					{
						continue;	
					}
					
					if (ViewBlockers.Contains(TestLocation) && (MinimalMemoryOptimization || !Processed.Contains(TestLocation)))
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
					Z++;
				}
			}
		}
		
		Processed.Append(Entries);

		{	// Build box.
			FBox Base = Asset->GetCellBox();
			Base = Base.MoveTo(LocationsToBuild[Origin]);
			Base += Asset->GetCellBox().MoveTo(LocationsToBuild[Origin] + (FVector(X,Y,Z) * Asset->GetCellBox().GetSize()) );
		
			BoxScene.Add(Base);
		}
		// Skip cells we have processed already.
		CurrentBox++;
		while ( CurrentBox < ViewBlockers.Num() && Processed.Contains(ViewBlockers[CurrentBox]))
		{
			CurrentBox++;
		}
	}
}

// Called every frame
void APVGBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

 	if (!bIsInitialized)
	{
		return;
	}

	if (CurrentCell >= LocationsToBuild.Num())
	{
		// We are done.
		SetActorTickEnabled(false);

		// Save package.
		UPackage* Package = APVGManager::GetManager()->GridDataAsset->GetPackage();
		const FString PackageName = Package->GetName();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
	
		// This is specified just for example
		{
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
		}
	
		const bool bSucceeded = UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);

		if (!bSucceeded)
		{
			UE_LOG(LogTemp, Error, TEXT("Package '%s' wasn't saved!"), *PackageName)
		}

		UE_LOG(LogTemp, Warning, TEXT("Package '%s' was successfully saved"), *PackageName)
		UE_LOG(LogTemp,Warning,TEXT("Finished grid in %.f2 hour /(%.2f min)"),((FPlatformTime::Seconds() - BeginTime) / 60)/60, (FPlatformTime::Seconds() - BeginTime) / 60);
		return;
	}

	if (!MoveToLocation(CurrentCell))
	{
		// we are going to start tracing next frame.
		bTraceCheckStage = true;
		return;
	}

	// Recaptured every frame to ensure we never get them in the test scene.
	TArray<AActor*> FoliageActors;
	for (TActorIterator<AActor> It(GetWorld(), AInstancedFoliageActor::StaticClass()); It; ++It)
	{
		if (auto FIA = *It)
		{
			FoliageActors.Add(FIA);
		}
	}

	
	// Setup default params.
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;
	QueryParams.AddIgnoredActors(FoliageActors);
	
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECollisionResponse::ECR_Block);
	
	// Ignore all removables in the world.
	ResponseParams.CollisionResponse.SetResponse(ECC_Destructible,ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic,ECR_Ignore);

	FWorldContext& EditorWorldContext = GEditor->GetEditorWorldContext();
	UWorld* World = EditorWorldContext.World();
	
	if (bTraceCheckStage)
	{
		// DEBUG STATS:
		double StartTime = FPlatformTime::Seconds();
		double TimeSimpleTest = 0;
		int32 NumOccludedBoxScene = 0;
		double TimeOccludedBoxScene = 0;
		int32 ShotgunTest = 0;
		double TimeShotgunTest = 0;
		bool bIsBoxSceneDirty = true;
		
		
		TArray<FRawRegionVisibilityData16>& GridData = APVGManager::GetManager()->GridDataAsset->GridData;
		UPVGPrecomputedGridDataAsset* GridDataAsset = APVGManager::GetManager()->GridDataAsset;
		const int32 MaxX = GridDataAsset->GetGridSizeX();
		const int32 MaxY = GridDataAsset->GetGridSizeY();
		const int32 MaxZ = GridDataAsset->GetGridSizeZ();
		
		const FIntVector Origin = IndexTo3D(CurrentCell,MaxX,MaxY);

		TSet<uint16> HandledPoints;

		int32 Iteration = 1;
		while (true)
		{
			bool bShouldBreak = false;
			
			TSet<uint16> PointsToProcess;
			
			// Calculate corner points.
			FIntVector MinLocation = Origin - FIntVector(Iteration,Iteration,Iteration);
			FIntVector MaxLocation = Origin + FIntVector(Iteration,Iteration,Iteration);

			if ( MinLocation.X <= 0 && MinLocation.Y <= 0 && MinLocation.Z <= 0 && MaxLocation.X >= MaxX && MaxLocation.Y >= MaxY && MaxLocation.Z >= MaxZ )
			{
				bShouldBreak = true;
			}
			
			// Clamp
			MinLocation.X = MinLocation.X < 0 ? 0 : MinLocation.X; 
			MinLocation.Y = MinLocation.Y < 0 ? 0 : MinLocation.Y; 
			MinLocation.Z = MinLocation.Z < 0 ? 0 : MinLocation.Z; 
			MaxLocation.X = MaxLocation.X < MaxX ? MaxLocation.X : MaxX - 1;
			MaxLocation.Y = MaxLocation.Y < MaxY ? MaxLocation.Y : MaxY - 1;
			MaxLocation.Z = MaxLocation.Z < MaxZ ? MaxLocation.Z : MaxZ - 1;
			
			// Build Points.
			
			// Build XPlanes first.
			for (int32 y = MinLocation.Y; y <= MaxLocation.Y ; y++)
			{
				for (int32 z = MinLocation.Z; z <= MaxLocation.Z ; z++)
				{
					PointsToProcess.Add(XYZToIndex(FIntVector(MinLocation.X,y,z),MaxX,MaxY));
					PointsToProcess.Add(XYZToIndex(FIntVector(MaxLocation.X,y,z),MaxX,MaxY));
				}
			}
			
			// YPlane.
			for (int32 x = MinLocation.X; x <= MaxLocation.X ; x++)
			{
				for (int32 z = MinLocation.Z; z <= MaxLocation.Z ; z++)
				{
					PointsToProcess.Add(XYZToIndex(FIntVector(x,MinLocation.Y,z),MaxX,MaxY));
					PointsToProcess.Add(XYZToIndex(FIntVector(x,MaxLocation.Y,z),MaxX,MaxY));
				}
			}

			// Build top and bottom.
			for (int32 x = MinLocation.X; x <= MaxLocation.X ; x++)
			{
				for (int32 y = MinLocation.Y; y <= MaxLocation.Y ; y++)
				{				
					PointsToProcess.Add(XYZToIndex(FIntVector(x,y,MinLocation.Z),MaxX,MaxY));
					PointsToProcess.Add(XYZToIndex(FIntVector(x,y,MaxLocation.Z),MaxX,MaxY));
				}
			}
		
			Iteration++;

			constexpr int32 NumTasks = 24;
			int32 NumPerTask = FMath::DivideAndRoundUp( PointsToProcess.Num(), NumTasks);
			auto PointsToProcessArray = PointsToProcess.Array();
			TArray<uint16> ViewBlockerArr[NumTasks];
			TArray<uint16> CanSee[NumTasks];
			TArray<uint16> Unresolved[NumTasks];

			{
				double Start = FPlatformTime::Seconds();
				
				ParallelFor(NumTasks,[&](int32 TaskID)
				{
					int32 Begin = TaskID * NumPerTask;
					int32 End = FMath::Min(Begin + NumPerTask,PointsToProcess.Num());
					for (int32 i = Begin; i < End; i++)
					{
						const auto Point = PointsToProcessArray[i];

						if (Point == CurrentCell)
						{
							continue;
						}
					
						const bool bIsKnownVisible =	GridData[Point].VisibleRegions.Contains(CurrentCell);
						const bool bIsKnownInvisible =	GridData[Point].InvisibleRegions.Contains(CurrentCell);
						if (bIsKnownVisible)
						{
							continue;
						}
						if (bIsKnownInvisible)
						{
							ViewBlockerArr[TaskID].Add(Point);
							continue;
						}
						const bool CanNotReachPoint = !World->LineTraceTestByChannel(LocationsToBuild[Point],LocationsToBuild[CurrentCell],ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams);
						if (CanNotReachPoint)
						{
							CanSee[TaskID].Add(Point);
							continue;
						}
					
						if (BoxCornerTraceCheck(LocationsToBuild[Point],LocationsToBuild[CurrentCell],APVGManager::GetManager()->CellSize))
						{
							CanSee[TaskID].Add(Point);
							continue;
						}

						// Unresolved.
						Unresolved[TaskID].Add(Point);
					}
				});
				
				TimeSimpleTest += FPlatformTime::Seconds() - Start;
			}

			// Resolve parallel work
			for (int32 i = 0; i < NumTasks; i++)
			{
				for (int32 j = 0; j < CanSee[i].Num(); j++)
				{
					GridDataAsset->SetDataCell(CurrentCell,CanSee[i][j],true);
				}

				ViewBlockers.Append(ViewBlockerArr[i]);
			}
			
			// Process unprocessed points.
			for	(int32 i = 0; i < NumTasks; i++)
			{
				for (int32 j = 0; j < Unresolved[i].Num(); j++)
				{
					const auto Point = Unresolved[i][j];
#if BOX_SCENE
					{	// Box test
						double Start = FPlatformTime::Seconds();

						if (bIsBoxSceneDirty)
						{
							UpdateBoxScene();
							bIsBoxSceneDirty = false;
						}
					
						if (BoxOcclusion(CurrentCell,Point,APVGManager::GetManager()->CellSize))
						{
							NumOccludedBoxScene++;
							GridDataAsset->SetDataCell(CurrentCell,Point,false);
							
							TimeOccludedBoxScene += FPlatformTime::Seconds() - Start;
							continue;
						}
						TimeOccludedBoxScene += FPlatformTime::Seconds() - Start;
					}
#endif
					// Shogun
					{
						ShotgunTest++;
						double Start = FPlatformTime::Seconds();
						FBox Current = APVGManager::GetManager()->GridDataAsset->GetCellBox().MoveTo(LocationsToBuild[CurrentCell]);
						FBox Target = APVGManager::GetManager()->GridDataAsset->GetCellBox().MoveTo(LocationsToBuild[Point]);

						const int32 NumRays = 5000;
						const int32 NumRaysPerTask = FMath::DivideAndRoundUp(NumRays,NumTasks);
						std::atomic<bool> bDidHit = false;

						ParallelFor(NumTasks,[&](int32 Task )
						{
							for (int32 iray = 0; iray < NumRaysPerTask; iray++)
							{
								const FVector A = FMath::RandPointInBox(Current);
								const FVector B = FMath::RandPointInBox(Target);

								// We are checking here if one of the rays does hit the target, since it shouldn't hit!
								const bool WasBlocked = World->LineTraceTestByChannel(A,B,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams);
								if (!WasBlocked)
								{
									break;
								}

								// Failed case.
								if (bDidHit)
								{
									break;
								}
							}
						});

						if (bDidHit)
						{
							GridDataAsset->SetDataCell(CurrentCell,Point,true);
						}
						else
						{
							GridDataAsset->SetDataCell(CurrentCell,Point,false);
							ViewBlockers.Add(Point);
							bIsBoxSceneDirty = true;
						}
						TimeShotgunTest += FPlatformTime::Seconds() - Start;
					}
				}
			}
			
			if (bShouldBreak)
			{
				break;
			}
		}

		UE_LOG(LogTemp,Warning,TEXT("Finished %d,Simple Tests %.3f.\tBox Scene Occluded: %d (%.3f sec) SceneSize %d\t Shotgun Trace: %d %.3f."),
			CurrentCell,TimeSimpleTest,
			NumOccludedBoxScene,TimeOccludedBoxScene,
			ViewBlockers.Num(),
			ShotgunTest,TimeShotgunTest);
		
		UE_LOG(LogTemp,Warning,TEXT("%d visible %d occluded. Computed %.3f"),
			GridData[CurrentCell].VisibleRegions.Num(),
			GridData[CurrentCell].InvisibleRegions.Num(),
			FPlatformTime::Seconds() - StartTime);
		
		// check missing one.
		bTraceCheckStage = true;
		CurrentCell++;

		ViewBlockers.Reset();
		BoxScene.Reset();
 	}
}

bool APVGBuilder::MoveToLocation(int32 Index)
{
	if(!LocationsToBuild[Index].Equals(GetActorLocation()))
	{
		// Update location.
		SetActorLocation(LocationsToBuild[Index]);
		GetWorld()->FlushLevelStreaming();
		
		return false;
	}
	
	return true;
}