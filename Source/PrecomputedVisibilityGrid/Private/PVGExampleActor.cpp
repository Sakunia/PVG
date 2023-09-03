// Fill out your copyright notice in the Description page of Project Settings.


#include "PVGExampleActor.h"

#include "PVGManager.h"

// Sets default values
APVGExampleActor::APVGExampleActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void APVGExampleActor::BeginPlay()
{
	Super::BeginPlay();

	APVGManager::GetManager()->ReportActorToPVGManager(this);
}

// Called every frame
void APVGExampleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void APVGExampleActor::UpdateVisibility_Implementation(bool NewVisibility)
{
	for (auto Prim : TInlineComponentArray<UPrimitiveComponent*>(this))
	{
		if (NewVisibility && !Prim->IsRenderStateCreated() )
		{
			Prim->CreateRenderState_Concurrent(nullptr);
			continue;
		}
		if (Prim->IsRenderStateCreated())
		{
			Prim->DestroyRenderState_Concurrent();
			continue;
		}
		
		checkNoEntry();
	}
}

