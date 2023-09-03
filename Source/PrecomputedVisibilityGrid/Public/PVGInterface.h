#pragma once
#include "PVGInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UPVGInterface : public UInterface
{
	GENERATED_BODY()
};

class IPVGInterface
{    
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent)
	void UpdateVisibility(bool NewVisibility);
};