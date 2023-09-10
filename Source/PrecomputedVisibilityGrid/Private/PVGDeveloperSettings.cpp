#include "PVGDeveloperSettings.h"

UPVGDeveloperSettings::UPVGDeveloperSettings()
{
	CategoryName = FName(TEXT("Precomputed visiblity grid"));
	
}

const UPVGDeveloperSettings* UPVGDeveloperSettings::Get()
{
	return GetDefault<UPVGDeveloperSettings>();
}

FIntVector UPVGDeveloperSettings::GetBoxSize()
{
	return Get()->BoxSize;
}

FBox UPVGDeveloperSettings::GetBoxSizeAsFBox()
{
	const FVector Bounds = FVector(Get()->BoxSize);
	return FBox(-Bounds / 2.f,Bounds / 2.f);
}
