#pragma once
#include "Commandlets/Commandlet.h"
#include "PVGBuilderCommandlet.generated.h"

UCLASS()
class UPVGBuilderCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()
	virtual int32 Main(const FString& Params) override;
};

UCLASS()
class UPVGPrecomputedGridBuilder : public UObject
{
	GENERATED_UCLASS_BODY()

	bool RunBuilder(UWorld* World);
	bool Run(UWorld* World);//, FPackageSourceControlHelper& PackageHelper);
};