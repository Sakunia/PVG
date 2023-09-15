#pragma once

class APVGManager;

#define PATCHED 1

class IPVGCulling : public ICustomCulling
{
public:
	explicit IPVGCulling(APVGManager* WorldPVGManager);
	virtual ~IPVGCulling() = default;
	virtual ICustomVisibilityQuery* CreateQuery (const FSceneView& View) override;

	TWeakObjectPtr<APVGManager> PVGManager;
};

class IPVGVisibilityQuery: public ICustomVisibilityQuery
{
public:
	explicit IPVGVisibilityQuery(APVGManager* WorldManager,const FSceneView &InView );
	virtual ~IPVGVisibilityQuery() override;
	
	/** prepares the query for visibility tests */
	virtual bool Prepare() override;

	/** test primitive visiblity */
#if PATCHED
	virtual bool IsVisible(int32 VisibilityId, const FBoxSphereBounds& Bounds) override;
#else
	virtual bool IsVisible(int32 VisibilityId, const FBoxSphereBounds& Bounds) override;
#endif
	
	/** return true if we can call IsVisible from a ParallelFor */
	virtual bool IsThreadsafe() override
	{
		return true;
	}

	virtual uint32 AddRef() const
	{
		return (uint32)NumRefs.Increment(); 
	}
	virtual uint32 Release() const override;
	uint32 GetRefCount() const final override
	{
		return (uint32)NumRefs.GetValue();
	}

	/** Ref couting **/
	mutable FThreadSafeCounter NumRefs;

	/*weak ptr to the world pre computed visibility manager. */
	TWeakObjectPtr<APVGManager> PVGManager;
	const FSceneView* View;

	TSet<int32> CachedHidden;
};