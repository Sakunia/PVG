// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PVGBuilder.generated.h"


class UPVGPrecomputedGridDataAsset;

UCLASS()
class PRECOMPUTEDVISIBILITYGRID_API APVGBuilder : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APVGBuilder();

	void Initialize(const TArray<FVector>& InLocations, FIntVector GridSize);
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual UPVGPrecomputedGridDataAsset* GetOrCreateCellData(FIntVector GridSize);

public:
	// @Returns "true" when we are already at the location
	bool MoveToLocation(int32 Index);
	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	/* Did we run initialize ?*/
	bool bIsInitialized = false;

	/* Locations to build, assigned by "initialize" function called from the PVGManager*/
	TArray<FVector> LocationsToBuild;

	/* Are we in the trace stage? */
	bool bTraceCheckStage = true;

	/* At what cell are we currently?*/
	int32 CurrentCell = 0;
	
	/* Are the scene captures rotation setup?
	 * if not, we rotate them and test their result next frame. */
	bool bAreLocationUpToDate = false;
};
