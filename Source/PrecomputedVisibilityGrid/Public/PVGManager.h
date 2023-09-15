// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PVGCulling.h"
#include "GameFramework/Actor.h"
#include "PVGManager.generated.h"

class UPVGPrecomputedGridDataAsset;

USTRUCT()
struct FCellActorContainer
{
	GENERATED_BODY()

	UPROPERTY()
	AActor* Actor;

	uint8 VisibleCounter;
};

USTRUCT()
struct FCellContainer
{
	GENERATED_BODY()

	/* Shared pointers with other cells.*/
	TArray<FCellActorContainer*> MultiCellActors;
	
	UPROPERTY()
	TArray<AActor*> Actors;		
	
	~FCellContainer()
	{
		Actors.Empty();
	}
};

UCLASS()
class PRECOMPUTEDVISIBILITYGRID_API APVGManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APVGManager();

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	static APVGManager* GetManager() { return Manager; }
	
	UFUNCTION(BlueprintCallable)
	static void ReportActorToPVGManager(AActor* Actor);

	bool IsCellHidden(int32 Index) const;

	virtual bool IsInsideOccludedArea(const FBoxSphereBounds& Box) const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetHidden(AActor* Actor, bool bState);
	
	virtual void DrawDebugHUDInfo();
	virtual void DrawDebugOcclusionScene();

	void DebugDrawSelected(TArray<FString>& OutPrints);
private:
	void UpdateCellsVisibility(int32 PlayerCellLocation);
	
	FVector GetLocationInGridSpace(const FVector& Location) const;
	int32 GetPlayerGridIndex() const;
	int32 GetIndexFromLocation(const FVector& Location) const;
	
	FVector IndexToLocation(int32 Index) const;
	
	FVector IndexToLocation(int32 x, int32 y, int32 z) const;

	int32 XYZToIndex(int32 x, int32 y, int32 z) const;

	void UpdateCells();

	void UpdateOcclusionScene();
	
	void UpdateCellVisibility(int32 Cell,bool bHide);
	
protected:
	UPROPERTY(EditInstanceOnly)
	UPVGPrecomputedGridDataAsset* GridDataAsset;
	TSharedPtr<IPVGCulling> CullingPass;

private:
	UPROPERTY()
	AActor* Player;
	
	/* static pointer to the manager.*/
	static APVGManager* Manager;

	TArray<FCellContainer> CellRuntimeData;

	int32 CurrentIndex = -1;
	
	
	FVector CentreOffset;
	
	TArray<int32> CellsToHide;
	TArray<int32> CellsToUnHide;
	TArray<int32> HiddenCells;

	TArray<FBox> OcclusionScene;
	
	//TSet<int32> HiddenPrimitives;
	bool bIsEnabled = true;

public:
	friend class APVGBuilder;
	
#if WITH_EDITORONLY_DATA
#if WITH_EDITOR
	UFUNCTION(CallInEditor)
	APVGBuilder* StartBuild();

	UFUNCTION(CallInEditor)
	void DebugDrawCells();

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	FBox CellSize = FBox(FVector(-250,-250,-250),FVector(250,250,250));;

	/*Saved on asset data.*/
	FBox GridBounds;

#endif
#endif
};
