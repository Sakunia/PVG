#include "PVGBuilderCommandlet.h"

#include "EditorLevelLibrary.h"
#include "EditorWorldUtils.h"
#include "EngineUtils.h"
#include "PackageSourceControlHelper.h"
#include "UObject/GCObjectScopeGuard.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "PVGBuilder.h"
#include "PVGManager.h"
#include "WorldPartition/WorldPartitionHelpers.h"

UPVGBuilderCommandlet::UPVGBuilderCommandlet(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{}

int32 UPVGBuilderCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	if (Tokens.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Missing world name"));
		return 1;
	}

	// TODO
	//if (Switches.Contains(TEXT("Verbose")))
	//{
	//	LogWorldPartitionBuilderCommandlet.SetVerbosity(ELogVerbosity::Verbose);
	//}

	// This will convert incomplete package name to a fully qualified path
	FString WorldFilename;
	if (!FPackageName::SearchForPackageOnDisk(Tokens[0], &Tokens[0], &WorldFilename))
	{
		UE_LOG(LogTemp, Error, TEXT("Unknown world '%s'"), *Tokens[0]);
		return 1;
	}

	// Load the world package
	UPackage* WorldPackage = LoadWorldPackageForEditor(Tokens[0]);
	if (!WorldPackage)
	{
		UE_LOG(LogTemp, Error, TEXT("Couldn't load package %s."), *Tokens[0]);
		return 1;
	}

	// Find the world in the given package
	UWorld* World = UWorld::FindWorldInPackage(WorldPackage);
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("No world in specified package %s."), *Tokens[0]);
		return 1;
	}
	
	// Create builder instance
	UPVGPrecomputedGridBuilder* Builder = NewObject<UPVGPrecomputedGridBuilder>(GetTransientPackage());
	if (!Builder)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create builder."));
		return false;
	}

	bool bResult;
	{
		FGCObjectScopeGuard BuilderGuard(Builder);
		bResult = Builder->RunBuilder(World);
	}

	return bResult ? 0 : 1;
}

UPVGPrecomputedGridBuilder::UPVGPrecomputedGridBuilder(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{}

bool UPVGPrecomputedGridBuilder::RunBuilder(UWorld* World)
{
	bool bResult = true;
	
	{	// Setup world.
		TUniquePtr<FScopedEditorWorld> EditorWorld;
		if (World->bIsWorldInitialized)
		{
			// Skip initialization, but ensure we're dealing with an editor world.
			check(World->WorldType == EWorldType::Editor);
		}
		else
		{
			UWorld::InitializationValues IVS;
			IVS.RequiresHitProxies(false);
            IVS.ShouldSimulatePhysics(false);
            IVS.EnableTraceCollision(true);
            IVS.CreateNavigation(false);
            IVS.CreateAISystem(false);
            IVS.AllowAudioPlayback(false);
            IVS.CreatePhysicsScene(true);
			IVS.CreateWorldPartition(true);
			EditorWorld = MakeUnique<FScopedEditorWorld>(World, IVS);
		}

		// TODO support for non WP.
		// Make sure the world is partitioned
		if (UWorld::IsPartitionedWorld(World))
		{
			// Ensure the world has a valid world partition.
			UWorldPartition* WorldPartition = World->GetWorldPartition();
			check(WorldPartition);
			
			FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true /*bEnsureIsGWorld*/);
			WorldContext.SetCurrentWorld(World);
			UWorld* PrevGWorld = GWorld;
			GWorld = World;

			// Tick the engine.
			CommandletHelpers::TickEngine(World);

			// your work here
			bResult = Run(World);

			// Tick the engine.
			CommandletHelpers::TickEngine(World);

			// Restore previous world
			WorldContext.SetCurrentWorld(PrevGWorld);
			GWorld = PrevGWorld;
		}
	}

	return true;
}

bool UPVGPrecomputedGridBuilder::Run(UWorld* World)
{
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition)
			
	FBox Bound = FBox(FVector(-HALF_WORLD_MAX, -HALF_WORLD_MAX, -HALF_WORLD_MAX), FVector(HALF_WORLD_MAX, HALF_WORLD_MAX, HALF_WORLD_MAX));
	TUniquePtr<FLoaderAdapterShape> LoaderAdapterShape = MakeUnique<FLoaderAdapterShape>(World, Bound, TEXT("Loaded Region"));
	LoaderAdapterShape->Load();

	// Make sure we run begin play otherwise the phys scene etc wont be there.
	World->BeginPlay();
	
	// Force load
	World->FlushLevelStreaming();

	// Try find manager.
	APVGManager* Manager = nullptr;
	for (TActorIterator<APVGManager> It(World); It; ++It)
	{
		if (APVGManager* FoundManager = *It)
		{
			Manager = FoundManager;
			break;
		}
	}
	
	// Otherwise spawn one.
	if (!Manager)
	{
		// TODO To handle.
	}

	Manager->SetActorTickEnabled(false);
	APVGBuilder* BuilderActor = Manager->StartBuild();

	// "tick" the builder
	while (BuilderActor->IsActorTickEnabled())
	{
		World->Tick(ELevelTick::LEVELTICK_All,FApp::GetDeltaTime());
		CommandletHelpers::TickEngine(World);
	}
		
	return true;	
}