// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGBuilder.h"

#include "PVGManager.h"
#include "PVGPrecomputedGridDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "UObject/SavePackage.h"


TAutoConsoleVariable<float> CVARPVGBuilderOcclusionTestMultiplier(TEXT("r.Culling.Builder.OcclusioCullSizeTestMultiplier"),1.01,TEXT("Scale adjustment for occlusion test."));

// Sets default values
APVGBuilder::APVGBuilder()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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
		const FString LevelName = GetWorld()->PersistentLevel.GetName();
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
			const FString PackageName = CellDataAsset->GetName();
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

			// Save Package.
			UPackage::SavePackage(CellDataAsset, NULL, RF_Public | RF_Standalone, *PackageName,GError,nullptr,false,true,SAVE_NoError);
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


// Called every frame
void APVGBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsInitialized)
	{
		return;
	}
	
	// Setup default params.
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;
	QueryParams.AddIgnoredActor(APVGManager::GetManager()->Player);

	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECollisionResponse::ECR_Block);

	auto FindAnyPoint = [this,QueryParams,ResponseParams](FVector A, FVector B, FBox Base)
	{
		// A is target,
		// B is self.
		const FBox ABox = Base.ShiftBy(A);
		const FBox BBox = Base.ShiftBy(B);

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
						
				if(!GetWorld()->LineTraceTestByChannel(AVert,BVert,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
				{
					return true;
				}
			}
		}
		
		// Centre to any point of B
		const FVector OriginA = ABox.GetCenter();
		for (int32 i = 0; i < 8; i++)
		{
			const FVector& BVert = BVertices[i];
			if(!GetWorld()->LineTraceTestByChannel(BVert,OriginA,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
			{
				return true;
			}
		}
		// Centre to any point of A
		const FVector OriginB = BBox.GetCenter();
		for (int32 i = 0; i < 8; i++)
		{
			const FVector& AVert = AVertices[i];
			if(!GetWorld()->LineTraceTestByChannel(AVert,OriginB,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
			{
				return true;
			}
		}
		FRandomStream Rand;
		Rand.Initialize(FMath::Rand());
		// try to trace against the box it self..
		for (int32 i = 0; i < 500; i++)
		{
			FVector RandomPointInABox = UKismetMathLibrary::RandomPointInBoundingBoxFromStream(Rand,ABox.GetCenter(),ABox.GetExtent());
			FVector RandomPointInBBox = UKismetMathLibrary::RandomPointInBoundingBoxFromStream(Rand,BBox.GetCenter(),ABox.GetExtent());
			// Line trace.
			if(!GetWorld()->LineTraceTestByChannel(RandomPointInABox,RandomPointInBBox,ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
			{
				// When failed, we know we can see it.
				return true;
			}
		}
		
		// Nothing found.
		return false;
	};
	
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
		return;
	}
	
	if (!MoveToLocation(CurrentCell))
	{
		// we are going to start tracing next frame.
		bTraceCheckStage = true;
		return;
	}

	if (bTraceCheckStage)
	{
		double T = FPlatformTime::Seconds();
		TArray<FRawRegionVisibilityData16>& GridData = APVGManager::GetManager()->GridDataAsset->GridData;
		UPVGPrecomputedGridDataAsset* GridDataAsset = APVGManager::GetManager()->GridDataAsset;

		int32 BasicTrace = 0;
		int32 AdvancedTrace = 0;
		
		for (int32 i = 0; i < APVGManager::GetManager()->GridDataAsset->GridData.Num(); i++)
		{
			if (i == CurrentCell)
			{	
				continue;
			}
			
			const bool bIsKnownVisible =	GridData[i].InvisibleRegions.Contains(CurrentCell);
			const bool bIsKnownInvisible =	GridData[i].VisibleRegions.Contains(CurrentCell);

			// Early out if this info has been assigned already.
			if (bIsKnownVisible || bIsKnownInvisible)
			{
				continue;
			}
			
			if (!GetWorld()->LineTraceTestByChannel(LocationsToBuild[i],LocationsToBuild[CurrentCell],ECollisionChannel::ECC_Visibility,QueryParams,ResponseParams))
			{
				//DrawDebugLine(GetWorld(),LocationsToBuild[i],LocationsToBuild[CurrentCell],FColor::Red,false,30.f);
				GridDataAsset->SetDataCell(CurrentCell,i,true);
				BasicTrace++;
			}
			else if (FindAnyPoint(LocationsToBuild[i],LocationsToBuild[CurrentCell],APVGManager::GetManager()->CellSize))
			{
				GridDataAsset->SetDataCell(CurrentCell,i,true);
				AdvancedTrace++;
			}
			else // we can assume its invisible.
			{
				// we can assume invisible.
				GridDataAsset->SetDataCell(CurrentCell,i,false);
			}
		}
		
		// Nothing to test, we can move to the next cell to test.
		bTraceCheckStage = true;
		CurrentCell++;

		const float Pct = (float(CurrentCell) / float(LocationsToBuild.Num())) * 100;
		UE_LOG(LogTemp,Warning,TEXT("cell %d/%d (%.2f%%)"), CurrentCell , LocationsToBuild.Num(), Pct );
		UE_LOG(LogTemp,Warning,TEXT("Finished trace test. Trace tested %d, %.2fMS"), LocationsToBuild.Num(), (FPlatformTime::Seconds() - T) * 1000);
	}
}

bool APVGBuilder::MoveToLocation(int32 Index)
{
	if(!LocationsToBuild[Index].Equals(APVGManager::GetManager()->Player->GetActorLocation()))
	{
		// Update location.
		APVGManager::GetManager()->Player->SetActorLocation(LocationsToBuild[Index]);
		GetWorld()->FlushLevelStreaming();
		
		return false;
	}
	
	return true;
}