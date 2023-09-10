#pragma once
#include "Engine/DeveloperSettings.h"
#include "PVGDeveloperSettings.generated.h"

UCLASS(defaultconfig)
class PRECOMPUTEDVISIBILITYGRID_API UPVGDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UPVGDeveloperSettings();
	
	static const UPVGDeveloperSettings* Get();

	static FIntVector GetBoxSize();
	
	static FBox GetBoxSizeAsFBox();
	
protected:
	UPROPERTY(EditDefaultsOnly, Category="Grid")
	FIntVector BoxSize;

	/* Minimum distance for Precomputed Visibility Culling to occur this can be tweaked to reduce shadow popping.*/
	UPROPERTY(EditDefaultsOnly, Category="Culling")
	float MinimumCullDistance = 100000; // 100m on default.

	UPROPERTY(EditDefaultsOnly, Category="Culling")
	bool bSupportDynamicBlockers = false;
	
};
