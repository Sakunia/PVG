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

	RegisterToManager();

}

// Called every frame
void APVGExampleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void APVGExampleActor::UpdateVisibility_Implementation(bool NewVisibility)
{
	for (UPrimitiveComponent* Prim : TInlineComponentArray<UPrimitiveComponent*>(this))
	{
		if (NewVisibility && Prim->IsRegistered() )
		{
			Prim->UnregisterComponent();
			continue;
		}
		if (!Prim->IsRegistered())
		{
			Prim->RegisterComponent();
			continue;
		}
		
		checkNoEntry();
	}
}

void APVGExampleActor::RegisterToManager()
{
	if (APVGManager::GetManager())
	{
		APVGManager::GetManager()->ReportActorToPVGManager(this);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick([=]()
		{
			if (IsValid(this))
			{
				// try again.
				RegisterToManager();
			}
		});
	}	
}
