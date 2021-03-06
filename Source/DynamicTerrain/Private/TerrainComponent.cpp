#include "TerrainComponent.h"

#include "Terrain.h"
#include "TerrainRender.h"
#include "TerrainStat.h"

#include "Engine.h"
#include "PrimitiveSceneProxy.h"
#include "DynamicMeshBuilder.h"
#include "Materials/Material.h"
#include "Engine/CollisionProfile.h"

DECLARE_CYCLE_STAT(TEXT("Dynamic Terrain - Rebuild Collision"), STAT_DynamicTerrain_RebuildCollision, STATGROUP_DynamicTerrain)

/// Mesh Component Interface ///

UTerrainComponent::UTerrainComponent(const FObjectInitializer& ObjectInitializer)
{
	Size = 0;
	Tiling = 1.0f;
	LODs = 1;
	LODScale = 0.5;

	// Disable ticking for the component to save some CPU cycles
	PrimaryComponentTick.bCanEverTick = false;

	SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
}

FPrimitiveSceneProxy* UTerrainComponent::CreateSceneProxy()
{
	FPrimitiveSceneProxy* proxy = nullptr;
	VerifyMapProxy();

	if (Vertices.Num() > 0 && IndexBuffer.Num() > 0 && MapProxy.IsValid())
	{
		proxy = new FTerrainComponentSceneProxy(this);
	}

	return proxy;
}

UBodySetup* UTerrainComponent::GetBodySetup()
{
	if (BodySetup == nullptr)
	{
		BodySetup = CreateBodySetup();
	}
	return BodySetup;
}

int32 UTerrainComponent::GetNumMaterials() const
{
	return 1;
}

bool UTerrainComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	// Copy vertex and triangle data
	CollisionData->Vertices = Vertices;
	int32 num_triangles = IndexBuffer.Num() / 3;
	for (int32 i = 0; i < num_triangles; ++i)
	{
		FTriIndices tris;
		tris.v0 = IndexBuffer[i * 3];
		tris.v1 = IndexBuffer[i * 3 + 1];
		tris.v2 = IndexBuffer[i * 3 + 2];
		CollisionData->Indices.Add(tris);
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;
	CollisionData->bFastCook = true;

	return true;
}

FBoxSphereBounds UTerrainComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox bound(ForceInit);

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		bound += LocalToWorld.TransformPosition(Vertices[i]);
	}

	FBoxSphereBounds boxsphere;
	boxsphere.BoxExtent = bound.GetExtent();
	boxsphere.Origin = bound.GetCenter();
	boxsphere.SphereRadius = boxsphere.BoxExtent.Size();

	return boxsphere;
}

/// Terrain Interface ///

void UTerrainComponent::Initialize(ATerrain* Terrain, TSharedPtr<FMapSection, ESPMode::ThreadSafe> Proxy, int32 X, int32 Y)
{
	XOffset = X;
	YOffset = Y;
	LODs = Terrain->GetNumLODs();
	LODScale = Terrain->GetLODDistanceScale();
	Tiling = Terrain->GetTiling();
	AsyncCooking = Terrain->GetAsyncCookingEnabled();
	MapProxy = Proxy;

	SetMaterial(0, Terrain->GetMaterials());
	SetSize(Terrain->GetComponentSize());
}

void UTerrainComponent::CreateMeshData()
{
	// Create vertex data
	uint32 width = GetTerrainComponentWidth(Size);
	Vertices.Empty();
	Vertices.SetNumUninitialized(width * width);
	for (uint32 y = 0; y < width; ++y)
	{
		for (uint32 x = 0; x < width; ++x)
		{
			Vertices[y * width + x] = FVector(x, y, 0.0f);
		}
	}

	// Create triangles
	uint32 polygons = width - 1;
	IndexBuffer.Empty();
	IndexBuffer.SetNumUninitialized(polygons * polygons * 6);
	for (uint32 y = 0; y < polygons; ++y)
	{
		for (uint32 x = 0; x < polygons; ++x)
		{
			uint32 i = (y * polygons + x) * 6;

			IndexBuffer[i] = x + (y * width);
			IndexBuffer[i + 1] = 1 + x + (y + 1) * width;
			IndexBuffer[i + 2] = 1 + x + y * width;

			IndexBuffer[i + 3] = x + (y * width);
			IndexBuffer[i + 4] = x + (y + 1) * width;
			IndexBuffer[i + 5] = 1 + x + (y + 1) * width;
		}
	}
}

void UTerrainComponent::SetSize(uint32 NewSize)
{
	if (NewSize > 1 && NewSize != Size)
	{
		Size = NewSize;
		CreateMeshData();
		UpdateCollision();
		MarkRenderStateDirty();
	}
}

uint32 UTerrainComponent::GetSize()
{
	return Size;
}

void UTerrainComponent::SetTiling(float NewTiling)
{
	Tiling = NewTiling;
	float x = XOffset;
	float y = YOffset;

	// Update UV data in the proxy
	FTerrainComponentSceneProxy* proxy = (FTerrainComponentSceneProxy*)SceneProxy;
	ENQUEUE_RENDER_COMMAND(FComponentUpdate)([proxy, x, y, NewTiling](FRHICommandListImmediate& RHICmdList) {
		proxy->UpdateUVs(x, y, NewTiling);
		});
}

void UTerrainComponent::SetLODs(int32 NumLODs, float DistanceScale)
{
	if ((uint32)NumLODs < Size)
	{
		LODs = NumLODs;
	}
	else
	{
		LODs = Size;
	}
	LODScale = DistanceScale;

	// Reset the proxy to regenerate LODs
	MarkRenderStateDirty();
}

void UTerrainComponent::Update(TSharedPtr<FMapSection, ESPMode::ThreadSafe> NewSection)
{
	MapProxy = NewSection;

	// Update collision data and bounds
	uint32 width = GetTerrainComponentWidth(Size);
	for (uint32 y = 0; y < width; ++y)
	{
		for (uint32 x = 0; x < width; ++x)
		{
			Vertices[y * width + x].Z = MapProxy->Data[(y + 1) * NewSection->X + x + 1];
		}
	}
	BodyInstance.UpdateTriMeshVertices(Vertices);
	UpdateBounds();

	// Update the scene proxy
	FTerrainComponentSceneProxy* proxy = (FTerrainComponentSceneProxy*)SceneProxy;
	ENQUEUE_RENDER_COMMAND(FComponentUpdate)([proxy, NewSection](FRHICommandListImmediate& RHICmdList) {
		proxy->UpdateMap(NewSection);
		});
	MarkRenderTransformDirty();
}

void UTerrainComponent::UpdateCollision()
{
	SCOPE_CYCLE_COUNTER(STAT_DynamicTerrain_RebuildCollision);

	if (AsyncCooking)
	{
		// Abort previous cooks
		for (UBodySetup* body : BodySetupQueue)
		{
			body->AbortPhysicsMeshAsyncCreation();
		}

		// Start cooking a new body
		BodySetupQueue.Add(CreateBodySetup());
		BodySetupQueue.Last()->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateUObject(this, &UTerrainComponent::FinishCollision, BodySetupQueue.Last()));
	}
	else
	{
		// Create a new body setup and clean out the async queue
		BodySetupQueue.Empty();
		GetBodySetup();

		// Change GUID for new collision data
		BodySetup->BodySetupGuid = FGuid::NewGuid();

		// Cook collision data
		BodySetup->bHasCookedCollisionData = true;
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();
	}
}

void UTerrainComponent::FinishCollision(bool Success, UBodySetup* NewBodySetup)
{
	// Create a new queue for async cooking
	TArray<UBodySetup*> new_queue;
	new_queue.Reserve(BodySetupQueue.Num());

	// Find the body setup
	int32 location;
	if (BodySetupQueue.Find(NewBodySetup, location))
	{
		if (Success)
		{
			// Use the new body setup
			BodySetup = NewBodySetup;
			RecreatePhysicsState();

			// Remove any earlier requests
			for (int32 i = location + 1; i < BodySetupQueue.Num(); ++i)
			{
				new_queue.Add(BodySetupQueue[i]);
			}
			BodySetupQueue = new_queue;
		}
		else
		{
			// Remove failed bake
			BodySetupQueue.RemoveAt(location);
		}
	}
}

UBodySetup* UTerrainComponent::CreateBodySetup()
{
	UBodySetup* newbody = NewObject<UBodySetup>(this, NAME_None, IsTemplate() ? RF_Public : RF_NoFlags);
	newbody->BodySetupGuid = FGuid::NewGuid();

	newbody->bGenerateMirroredCollision = false;
	newbody->bDoubleSidedGeometry = true;
	newbody->CollisionTraceFlag = CTF_UseComplexAsSimple;

	return newbody;
}

TSharedPtr<FMapSection, ESPMode::ThreadSafe> UTerrainComponent::GetMapProxy()
{
	VerifyMapProxy();
	return MapProxy;
}

void UTerrainComponent::SetMapProxy(TSharedPtr<FMapSection, ESPMode::ThreadSafe> Proxy)
{
	MapProxy = Proxy;
	MarkRenderStateDirty();
}

void UTerrainComponent::VerifyMapProxy()
{
	uint32 width = GetTerrainComponentWidth(Size) + 2;
	if (!MapProxy.IsValid())
	{
		if (Size > 1)
		{
			MapProxy = MakeShareable(new FMapSection(width, width));
		}
	}
	else
	{
		if (MapProxy->X != width || MapProxy->Y != width)
		{
			MapProxy = MakeShareable(new FMapSection(width, width));
		}
	}
}