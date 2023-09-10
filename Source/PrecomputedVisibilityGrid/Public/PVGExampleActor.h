// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PVGInterface.h"
#include "GameFramework/Actor.h"
#include "PVGExampleActor.generated.h"

UCLASS()
class PRECOMPUTEDVISIBILITYGRID_API APVGExampleActor : public AActor, public IPVGInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APVGExampleActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/* Begin PVGInterface*/
	virtual void UpdateVisibility_Implementation(bool NewVisibility) override;
	/* End PVGInterface */

	void RegisterToManager();
};
