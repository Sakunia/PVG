// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#define PVG_DEBUG 0

inline FIntVector IndexTo3D(uint16 Index, uint16 MaxX, uint16 MaxY)
{
	uint16 z = Index / (MaxX * MaxY);
	Index -= (z * MaxX * MaxY);
	uint16 y = Index / MaxX;
	uint16 x = Index % MaxX;
	
	return FIntVector(x,y,z);
}

inline int32 XYZToIndex(int32 x, int32 y, int32 z, uint16 MaxX, uint16 MaxY)
{
	return (z * MaxX * MaxY) + (y * MaxX) + x;
}

inline uint16 XYZToIndex(FIntVector XYZ, uint16 MaxX, uint16 MaxY)
{
	return (XYZ.Z * MaxX * MaxY) + (XYZ.Y * MaxX) + XYZ.X;
}

class FPrecomputedVisibilityGridModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
