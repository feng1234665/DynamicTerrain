// Copyright © 2019 Created by Brian Faubion

#include "Terrain.h"
#include "TerrainComponent.h"
#include "TerrainStat.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

DECLARE_CYCLE_STAT(TEXT("Dynamic Terrain - Rebuild Terrain"), STAT_DynamicTerrain_RebuildMesh, STATGROUP_DynamicTerrain);
DECLARE_CYCLE_STAT(TEXT("Dynamic Terrain - Reset Terrain Proxy"), STAT_DynamicTerrain_ResetProxy, STATGROUP_DynamicTerrain);

ATerrain::ATerrain()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create the root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));

	// Create the heightmap
	Map = CreateDefaultSubobject<UHeightMap>(TEXT("HeightMap"));

	// Scale the terrain so that 1 square = 1 units
	SetActorRelativeScale3D(FVector(100.0f, 100.0f, 100.0f));
}

/// Engine Functions ///

void ATerrain::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RebuildProxies();
}

void ATerrain::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update mesh sections that have been changed since the last tick
	Update();
}

void ATerrain::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		FString name = PropertyChangedEvent.MemberProperty->GetName();
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::White, "Property Changed: " + name);

		// If the mesh properties change, rebuild the heightmap and mesh
		if (name == "XWidth" || name == "YWidth" || name == "ComponentSize" || name == "Height" || name == "Border")
		{
			Rebuild();
		}
		else if (name == "TerrainMaterial")
		{
			ApplyMaterials();
		}
		else if (name == "UseAsyncCooking")
		{
			for (int32 i = 0; i < Components.Num(); ++i)
			{
				if (Components[i] != nullptr)
				{
					Components[i]->AsyncCooking = UseAsyncCooking;
				}
			}
		}
	}
}

void ATerrain::BeginPlay()
{
	Super::BeginPlay();

	UpdateAll();
}

/// Accessor Functions ///

void ATerrain::Resize(int32 SizeOfComponents, int32 ComponentWidthX, int32 ComponentWidthY)
{
	// Safety check for input parameters
	if (SizeOfComponents <= 1 || ComponentWidthX < 1 || ComponentWidthY < 1)
	{
		return;
	}

	ComponentSize = SizeOfComponents;
	XWidth = ComponentWidthX;
	YWidth = ComponentWidthY;

	// Rebuild everything to match the new map size
	Rebuild();
}

void ATerrain::SetMaterials(UMaterial* terrain_material, UMaterial* border_material)
{
	// Change materials
	if (terrain_material != nullptr)
	{
		TerrainMaterial = terrain_material;
	}

	// Set the mesh materials
	ApplyMaterials();
}

void ATerrain::SetLODs(int32 NumLODs, float DistanceScale)
{
	if (NumLODs < 1)
	{
		NumLODs = 1;
	}
	else if (NumLODs > ComponentSize)
	{
		NumLODs = ComponentSize;
	}
	if (DistanceScale < 0.0f)
	{
		DistanceScale = 0.0f;
	}
	else if (DistanceScale > 1.0f)
	{
		DistanceScale = 1.0f;
	}

	LODLevels = NumLODs;
	LODScale = DistanceScale;

	// Set the LODs of each component
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		Components[i]->SetLODs(LODLevels, LODScale);
	}
}

UMaterialInterface* ATerrain::GetMaterials()
{
	return TerrainMaterial;
}

void ATerrain::SetTiling(float Frequency)
{
	if (Frequency <= 0.0f)
	{
		Frequency = 0.01f;
	}

	Tiling = Frequency;

	// Set the tiling of each component
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		Components[i]->SetTiling(Tiling);
	}
}

void ATerrain::Rebuild()
{
	DirtyMesh = true;
	RebuildHeightmap();
	RebuildFoliage();
	RebuildMesh();
}

void ATerrain::Refresh()
{
	RebuildMesh();
}

void ATerrain::DeleteFoliage()
{
	for (int32 i = 0; i < FoliageComponents.Num(); ++i)
	{
		FoliageComponents[i]->ClearInstances();
	}
}

void ATerrain::Update()
{
	bool update_border = true;

	int32 width = GetTerrainComponentWidth(ComponentSize);
	for (int32 x = 0; x < XWidth; ++x)
	{
		for (int32 y = 0; y < YWidth; ++y)
		{
			int32 i = y * XWidth + x;

			// Check each section for updates
			if (UpdateMesh[i])
			{
				// Get a copy of the heightmap covered by the component
				FIntPoint min;
				min.X = x * (width - 1);
				min.Y = y * (width - 1);

				TSharedPtr<FMapSection, ESPMode::ThreadSafe> section = MakeShareable(new FMapSection(width + 2, width + 2));
				Map->GetMapSection(section.Get(), min);

				// Update the component
				Components[i]->Update(section);

				UpdateMesh[i] = false;
			}
		}
	}
}

void ATerrain::UpdateSection(int32 X, int32 Y)
{
	UpdateMesh[Y * XWidth + X] = true;
}

void ATerrain::UpdateRange(FIntRect Range)
{
	int32 polygons = GetTerrainComponentWidth(ComponentSize) - 1;
	int32 width_x = Map->GetWidthX() - 3;
	int32 width_y = Map->GetWidthY() - 3;

	// Cap the min and max values
	if (Range.Min.X < 1)
	{
		Range.Min.X = 1;
	}
	if (Range.Min.Y < 1)
	{
		Range.Min.Y = 1;
	}
	if (Range.Max.X > width_x)
	{
		Range.Max.X = width_x;
	}
	if (Range.Max.Y > width_y)
	{
		Range.Max.Y = width_y;
	}

	--Range.Min.X; --Range.Min.Y; --Range.Max.X; --Range.Max.Y;

	// Get the positions of the components to update by dividing and flooring the min and max values
	FIntRect component;
	component.Min.X = Range.Min.X / polygons;
	component.Min.Y = Range.Min.Y / polygons;
	component.Max.X = Range.Max.X / polygons;
	component.Max.Y = Range.Max.Y / polygons;

	// Mark the necessary sections for updating
	for (int32 x = component.Min.X; x <= component.Max.X; ++x)
	{
		for (int32 y = component.Min.Y; y <= component.Max.Y; ++y)
		{
			UpdateMesh[y * XWidth + x] = true;
		}
	}
}

void ATerrain::UpdateAll()
{
	for (int32 i = 0; i < UpdateMesh.Num(); ++i)
	{
		UpdateMesh[i] = true;
	}
}

FVector2D ATerrain::GetMapVector(FVector WorldPosition) const
{
	WorldPosition -= GetActorLocation();
	WorldPosition /= GetActorScale3D();

	WorldPosition.X += Map->GetWidthX() / 2;
	WorldPosition.Y += Map->GetWidthY() / 2;

	return FVector2D(WorldPosition.X, WorldPosition.Y);
}

float ATerrain::GetHeight(FVector WorldLocation) const
{
	// Convert the vector to heightmap space
	FVector2D Location = GetMapVector(WorldLocation);

	// Verify that the coordinates are within the bounds of the heightmap, use the nearest safe coordinate if not
	if (Location.X < 0.0f)
	{
		Location.X = 0.0f;
	}
	else if (Location.X >= Map->GetWidthX() - 2)
	{
		Location.X = Map->GetWidthX() - 2;
	}
	if (Location.Y < 0.0f)
	{
		Location.Y = 0.0f;
	}
	else if (Location.Y >= Map->GetWidthY() - 2)
	{
		Location.Y = Map->GetWidthY() - 2;
	}

	// Get the height and convert it to world space
	return Map->GetLinearHeight(Location.X, Location.Y) * GetActorScale3D().Z + GetActorLocation().Z;
}

FVector ATerrain::GetNormal(FVector WorldLocation) const
{
	// Convert the vector to heightmap space
	FVector2D Location = GetMapVector(WorldLocation);

	// Verify that the coordinates are within the bounds of the heightmap, use the nearest safe coordinate if not
	if (Location.X < 0.0f)
	{
		Location.X = 0.0f;
	}
	else if (Location.X >= Map->GetWidthX() - 2)
	{
		Location.X = Map->GetWidthX() - 2;
	}
	if (Location.Y < 0.0f)
	{
		Location.Y = 0.0f;
	}
	else if (Location.Y >= Map->GetWidthY() - 2)
	{
		Location.Y = Map->GetWidthY() - 2;
	}

	// Get the map normal
	return Map->GetLinearNormal(Location.X, Location.Y);
}

FVector ATerrain::GetTangent(FVector WorldLocation)
{
	// Convert the vector to heightmap space
	FVector2D Location = GetMapVector(WorldLocation);

	// Verify that the coordinates are within the bounds of the heightmap, use the nearest safe coordinate if not
	if (Location.X < 0.0f)
	{
		Location.X = 0.0f;
	}
	else if (Location.X >= Map->GetWidthX() - 2)
	{
		Location.X = Map->GetWidthX() - 2;
	}
	if (Location.Y < 0.0f)
	{
		Location.Y = 0.0f;
	}
	else if (Location.Y >= Map->GetWidthY() - 2)
	{
		Location.Y = Map->GetWidthY() - 2;
	}

	// Get the map normal
	return Map->GetLinearTangent(Location.X, Location.Y);
}

UHeightMap* ATerrain::GetMap() const
{
	return Map;
}

TArray<UHierarchicalInstancedStaticMeshComponent*> ATerrain::GetInstancedMeshComponents() const
{
	return FoliageComponents;
}

void ATerrain::GetFoliageGroups(TArray<UTerrainFoliageSpawner*>& FoliageList) const
{
	FoliageList = FoliageGroups;
}

void ATerrain::SetFoliageGroups(TArray<UTerrainFoliageSpawner*>& FoliageList)
{
	FoliageGroups = FoliageList;
	RebuildFoliage();
}

inline UHierarchicalInstancedStaticMeshComponent* ATerrain::FindInstancedMesh(UStaticMesh* Mesh) const
{
	for (int32 i = 0; i < FoliageComponents.Num(); ++i)
	{
		if (FoliageComponents[i]->GetStaticMesh() == Mesh)
		{
			return FoliageComponents[i];
		}
	}

	return nullptr;
}

int32 ATerrain::GetComponentSize() const
{
	return ComponentSize;
}

int32 ATerrain::GetXWidth() const
{
	return XWidth;
}

int32 ATerrain::GetYWidth() const
{
	return YWidth;
}

float ATerrain::GetTiling() const
{
	return Tiling;
}

int32 ATerrain::GetNumLODs() const
{
	return LODLevels;
}

float ATerrain::GetLODDistanceScale() const
{
	return LODScale;
}

bool ATerrain::GetAsyncCookingEnabled() const
{
	return UseAsyncCooking;
}

/// Map Rebuilding ///

void ATerrain::RebuildHeightmap()
{
	// Determine how much memory to allocate for the heightmap
	uint32 width = GetTerrainComponentWidth(ComponentSize) - 1;
	uint16 heightmap_x = width * XWidth + 3;
	uint16 heightmap_y = width * YWidth + 3;

	if (heightmap_x * heightmap_y == 0)
	{
		return;
	}

	// Resize the heightmap
	Map->Resize(heightmap_x, heightmap_y);

	GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::White, "Heightmap rebuilt");
}

void ATerrain::RebuildMesh()
{
	SCOPE_CYCLE_COUNTER(STAT_DynamicTerrain_RebuildMesh);

	// Destroy the mesh and start over
	uint32 width = GetTerrainComponentWidth(ComponentSize);
	uint32 polygons = width - 1;
	if (DirtyMesh)
	{
		// Delete old components
		while (Components.Num() > 0)
		{
			Components.Last()->DestroyComponent();
			Components.Pop();

			UpdateMesh.Pop();
		}

		// Create a new set of components
		for (int32 y = 0; y < YWidth; ++y)
		{
			for (int32 x = 0; x < XWidth; ++x)
			{
				// Name the component
				FString name;
				name = "TerrainSection";
				name.AppendInt(y * XWidth + x);

				// Create a section proxy
				TSharedPtr<FMapSection, ESPMode::ThreadSafe> proxy = MakeShareable(new FMapSection(width + 2, width + 2));
				FIntPoint min;
				min.X = x * polygons;
				min.Y = y * polygons;
				Map->GetMapSection(proxy.Get(), min);

				// Create the component
				Components.Add(NewObject<UTerrainComponent>(this, UTerrainComponent::StaticClass(), FName(*name)));
				Components.Last()->RegisterComponentWithWorld(GetWorld());
				Components.Last()->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
				Components.Last()->Initialize(this, proxy, x, y);

				// Move the component
				float world_offset_x = -(float)(Map->GetWidthX() - 2 - 1) / 2.0f + polygons * x;
				float world_offset_y = -(float)(Map->GetWidthY() - 2 - 1) / 2.0f + polygons * y;
				Components.Last()->SetRelativeLocation(FVector(world_offset_x, world_offset_y, 0.0f));

				UpdateMesh.Add(false);
			}
		}
	}
	else
	{
		// Update map data
		for (int32 x = 0; x < XWidth; ++x)
		{
			for (int32 y = 0; y < YWidth; ++y)
			{
				// Create a section proxy
				TSharedPtr<FMapSection, ESPMode::ThreadSafe> proxy = MakeShareable(new FMapSection(width + 2, width + 2));
				FIntPoint min;
				min.X = x * polygons;
				min.Y = y * polygons;
				Map->GetMapSection(proxy.Get(), min);

				// Pass the proxy to the components
				Components[y * XWidth + x]->Update(proxy);
			}
		}
	}

	FString output;
	if (DirtyMesh)
	{
		ApplyMaterials();
		DirtyMesh = false;

		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::White, "Terrain mesh rebuilt");
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::White, "Terrain mesh updated");
	}
}

void ATerrain::RebuildProxies()
{
	if (Components.Num() < XWidth * YWidth)
		return;

	uint32 width = GetTerrainComponentWidth(ComponentSize);
	int32 polygons = width - 1;
	for (int32 y = 0; y < YWidth; ++y)
	{
		for (int32 x = 0; x < XWidth; ++x)
		{
			// Create a section proxy
			TSharedPtr<FMapSection, ESPMode::ThreadSafe> proxy = MakeShareable(new FMapSection(width + 2, width + 2));
			FIntPoint min;
			min.X = x * polygons;
			min.Y = y * polygons;
			Map->GetMapSection(proxy.Get(), min);

			// Pass the proxy to the components
			Components[y * XWidth + x]->SetMapProxy(proxy);
		}
	}
}

void ATerrain::RebuildFoliage()
{
	// Delete instance mesh components
	while (FoliageComponents.Num() > 0)
	{
		FoliageComponents.Last()->DestroyComponent();
		FoliageComponents.Pop();
	}

	// Add mesh components for each static mesh in each spawner
	for (int32 i = 0; i < FoliageGroups.Num(); ++i)
	{
		UTerrainFoliageSpawner* spawner = FoliageGroups[i];
		for (int32 j = 0; j < spawner->Foliage.Num(); ++j)
		{
			// Check to see if there is already a instanced mesh component using the current foliage mesh
			bool has_component = false;
			for (int32 s = 0; s < FoliageComponents.Num(); ++s)
			{
				if (FoliageComponents[s]->GetStaticMesh() == spawner->Foliage[j].Asset->Mesh)
				{
					has_component = true;
					break;
				}
			}

			// Create a new component for the foliage mesh
			if (!has_component)
			{
				FString name;
				name = "TerrainInstanceComponent";
				name.AppendInt(FoliageComponents.Num());

				FoliageComponents.Add(NewObject<UHierarchicalInstancedStaticMeshComponent>(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), FName(*name)));
				FoliageComponents.Last()->RegisterComponentWithWorld(GetWorld());
				FoliageComponents.Last()->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
				FoliageComponents.Last()->SetAbsolute(true, true, true);
				FoliageComponents.Last()->SetStaticMesh(spawner->Foliage[j].Asset->Mesh);
			}
		}
	}
}

void ATerrain::ApplyMaterials()
{
	uint32 mesh_count = Components.Num();

	// Set materials for terrain sections
	for (uint32 i = 0; i < mesh_count; ++i)
	{
		Components[i]->SetMaterial(0, TerrainMaterial);
	}
}

/*void ATerrain::ApplyFoliageMeshes()
{
	// Set static meshes for each foliage component
	for (int32 i = 0; i < FoliageComponents.Num(); ++i)
	{
		if (i < FoliageMeshes.Num())
		{
			FoliageComponents[i]->SetStaticMesh(FoliageMeshes[i]);
		}
		else
		{
			break;
		}
	}
}*/

uint32 GetTerrainComponentWidth(uint32 Size)
{
	return FMath::Exp2(Size) + 1;
}