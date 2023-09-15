#include "PVGCulling.h"

#include "PrimitiveSceneInfo.h"
#include "PVGManager.h"
#include "Renderer/Private/ScenePrivate.h"

IPVGCulling::IPVGCulling(APVGManager* WorldPVGManager)
{
	PVGManager = WorldPVGManager;
}

ICustomVisibilityQuery* IPVGCulling::CreateQuery(const FSceneView& View)
{
	return new IPVGVisibilityQuery(PVGManager.Get(), View);
}

IPVGVisibilityQuery::IPVGVisibilityQuery(APVGManager* WorldManager,const FSceneView &InView)
{
	PVGManager = WorldManager;
	View = &InView;
}

IPVGVisibilityQuery::~IPVGVisibilityQuery()
{
}

bool IPVGVisibilityQuery::Prepare()
{
	return true;
}

bool IPVGVisibilityQuery::IsVisible(int32 VisibilityId, const FBoxSphereBounds& Bounds)
{
	// frustum cull first.
	if (!View->ViewFrustum.IntersectSphere(Bounds.Origin, Bounds.SphereRadius))
	{
		return false;
	}
	
	if (APVGManager* Manager = APVGManager::GetManager())
	{
		if (Manager->IsInsideOccludedArea(Bounds))
		{
			return false;	
		}
	}
	
	return true;
}

uint32 IPVGVisibilityQuery::Release() const
{
	uint32 NewValue = (uint32)NumRefs.Decrement();
	return NewValue;
}
