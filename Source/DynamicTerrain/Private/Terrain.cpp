// Copyright © 2019 Created by Brian Faubion

#include "Terrain.h"
#include "TerrainComponent.h"

#include "Engine/Engine.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Components/DecalComponent.h"

DECLARE_STATS_GROUP(TEXT("Dynamic Terrain Plugin"), STATGROUP_DynamicTerrain, STATCAT_Advanced)
DECLARE_CYCLE_STAT(TEXT("Dynamic Terrain - Rebuild Terrain"), STAT_DynamicTerrain_RebuildMesh, STATGROUP_DynamicTerrain)

void ComponentData::Allocate(int32 Size)
{
	// Empty all of the arrays
	Vertices.Empty();
	UV0.Empty();
	Normals.Empty();
	Tangents.Empty();
	Triangles.Empty();

	// Preallocate the arrays
	Vertices.SetNum(Size * Size);
	UV0.SetNum(Size * Size);
	Normals.SetNum(Size * Size);
	Tangents.SetNum(Size * Size);
	Triangles.SetNum((Size - 1) * (Size - 1) * 6);
}

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

	// Allocate data for the component buffer
	ComponentBuffer.Allocate(ComponentSize);
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

	FString name = PropertyChangedEvent.MemberProperty->GetName();
	GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::White, "Property Changed: " + name);

	// If the mesh properties change, rebuild the heightmap and mesh
	if (name == "XWidth" || name == "YWidth" || name == "ComponentSize" || name == "Height" || name == "Border")
	{
		Rebuild();
	}
	else if (name == "TerrainMaterial" || name == "BorderMaterial")
	{
		ApplyMaterials();
	}
}

void ATerrain::BeginPlay()
{
	Super::BeginPlay();

	// Allocate data for the component buffer
	ComponentBuffer.Allocate(ComponentSize);

	// Refresh the terrain to update collision if anything was changed in the editor
	Refresh();
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
	if (border_material != nullptr)
	{
		BorderMaterial = border_material;
	}

	// Set the mesh materials
	ApplyMaterials();
}

UMaterialInterface* ATerrain::GetMaterials()
{
	return TerrainMaterial;
}

void ATerrain::SetTiling(float Frequency)
{
	if (Frequency > 0.0f)
	{
		Tiling = Frequency;
	}
}

void ATerrain::EnableBorder(bool Enable)
{
	Border = Enable;
}

void ATerrain::Rebuild()
{
	DirtyMesh = true;
	RebuildHeightmap();
	RebuildMesh();
}

void ATerrain::Refresh()
{
	RebuildMesh();
}

void ATerrain::Update()
{
	bool update_border = true;

	for (int32 x = 0; x < XWidth; ++x)
	{
		for (int32 y = 0; y < YWidth; ++y)
		{
			int32 i = y * XWidth + x;

			// Check each section for updates
			if (UpdateMesh[i])
			{
				// Update the section
				GenerateMeshSection(x, y, ComponentBuffer, false);
				Meshes[i]->UpdateMeshSection(0, ComponentBuffer.Vertices, ComponentBuffer.Normals, ComponentBuffer.UV0, TArray<FColor>(), ComponentBuffer.Tangents);

				// Update the border if a component on the edge is updating
				if (update_border)
				{
					if (x == 0 || y == 0 || x == XWidth - 1 || y == YWidth - 1)
					{
						ComponentData BorderBuffer;
						GenerateBorderSection(BorderBuffer, true);

						Meshes.Last()->UpdateMeshSection(0, BorderBuffer.Vertices, BorderBuffer.Normals, BorderBuffer.UV0, TArray<FColor>(), BorderBuffer.Tangents);

						update_border = false;
					}
				}

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
	int32 polygons = ComponentSize - 1;
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

UHeightMap* ATerrain::GetMap() const
{
	return Map;
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

bool ATerrain::GetBorderEnabled() const
{
	return Border;
}

/// Map Rebuilding ///

void ATerrain::RebuildHeightmap()
{
	// Determine how much memory to allocate for the heightmap
	uint16 heightmap_x = (ComponentSize - 1) * XWidth + 3;
	uint16 heightmap_y = (ComponentSize - 1) * YWidth + 3;

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
	SCOPE_CYCLE_COUNTER(STAT_DynamicTerrain_RebuildMesh)

	// Destroy the mesh and start over
	if (DirtyMesh)
	{
		// Delete old components
		while (Meshes.Num() > 0)
		{
			Meshes.Last()->DestroyComponent();
			Meshes.Pop();

			UpdateMesh.Pop();
		}

		// Determine how many components are needed
		uint16 component_count = XWidth * YWidth;
		if (Border)
		{
			++component_count;
		}

		// Create a new set of components
		for (int32 i = 0; i < component_count; ++i)
		{
			// Name the component
			FString name;
			if (i == (component_count - 1) && Border)
			{
				name = "TerrainBorder";
			}
			else
			{
				name = "TerrainSection";
				name.AppendInt(i);
			}

			// Create the component
			Meshes.Add(NewObject<UProceduralMeshComponent>(this, UProceduralMeshComponent::StaticClass(), FName(*name)));
			Meshes.Last()->RegisterComponentWithWorld(GetWorld());
			Meshes.Last()->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

			UpdateMesh.Add(false);
		}

		// Resize the component buffer
		ComponentBuffer.Allocate(ComponentSize);

		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}

		// Attach a component mesh
		Component = NewObject<UTerrainComponent>(this, UTerrainComponent::StaticClass(), FName("TestComponent"));
		Component->RegisterComponentWithWorld(GetWorld());
		Component->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		Component->GenerateVertices(1, 1, this);
	}

	// Determine the number of threads to use
	uint16 num_threads = 4;
	if (XWidth * YWidth < num_threads)
	{
		num_threads = XWidth * YWidth;
	}

	uint16 section_x = 0;
	uint16 section_y = 0;

	// Start the worker threads and add an inital workload
	TArray<ComponentBuilder> builders;
	for (uint16 i = 0; i < num_threads; ++i)
	{
		if (i > 0)
		{
			// Switch to the next section
			++section_x;
			if (section_x >= XWidth)
			{
				section_x = 0;
				++section_y;
			}
		}

		builders.Emplace(this);
		builders.Last().Build(section_x, section_y);
	}

	// Run until all threads are closed
	while (builders.Num() > 0)
	{
		for (uint16 i = 0; i < builders.Num(); ++i)
		{
			// Check to see if the thread is idling
			if (builders[i].IsIdle())
			{
				ComponentData* data = builders[i].GetData();
				if (data != nullptr)
				{
					int32 section = builders[i].GetSection();
					// Add the thread data to a mesh
					if (DirtyMesh)
					{
						Meshes[section]->CreateMeshSection(0, data->Vertices, data->Triangles, data->Normals, data->UV0, TArray<FColor>(), data->Tangents, true);
					}
					else
					{
						Meshes[section]->UpdateMeshSection(0, data->Vertices, data->Normals, data->UV0, TArray<FColor>(), data->Tangents);
					}

					// Change sections
					++section_x;
					if (section_x >= XWidth)
					{
						section_x = 0;
						++section_y;
					}

					if (section_y < YWidth)
					{
						// Queue another component
						builders[i].Build(section_x, section_y);
					}
					else
					{
						// Close the thread if all components have been built
						builders.RemoveAt(i);
						break;
					}
				}
			}
		}

		// Sleep between checking threads to avoid eating resources
		FPlatformProcess::Sleep(0.01);
	}

	// Build the border mesh
	if (Border)
	{
		ComponentData BorderBuffer;
		GenerateBorderSection(BorderBuffer, true);

		// Add or update the border component
		if (DirtyMesh)
		{
			Meshes.Last()->CreateMeshSection(0, BorderBuffer.Vertices, BorderBuffer.Triangles, BorderBuffer.Normals, BorderBuffer.UV0, TArray<FColor>(), BorderBuffer.Tangents, true);
		}
		else
		{
			Meshes.Last()->UpdateMeshSection(0, BorderBuffer.Vertices, BorderBuffer.Normals, BorderBuffer.UV0, TArray<FColor>(), BorderBuffer.Tangents);
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

void ATerrain::ApplyMaterials()
{
	uint32 mesh_count = Meshes.Num();

	// Set the material for the border mesh
	if (Border)
	{
		--mesh_count;
		Meshes.Last()->SetMaterial(0, BorderMaterial);
	}

	// Set materials for terrain sections
	for (uint32 i = 0; i < mesh_count; ++i)
	{
		Meshes[i]->SetMaterial(0, TerrainMaterial);
	}
}

/// Generator Functions ///

void ATerrain::GenerateMeshSection(int32 X, int32 Y, ComponentData& Data, bool CreateTriangles) const
{
	int32 polygons = ComponentSize - 1;

	// The location of the component in the world
	float world_offset_x = (float)(Map->GetWidthX() - 2 - 1) / 2.0f - polygons * X;
	float world_offset_y = (float)(Map->GetWidthY() - 2 - 1) / 2.0f - polygons * Y;

	// The location of the component on the heightmap
	int32 heightmap_offset_x = X * polygons;
	int32 heightmap_offset_y = Y * polygons;

	// Create vertices and UVs
	for (int32 y = 0; y < ComponentSize; ++y)
	{
		for (int32 x = 0; x < ComponentSize; ++x)
		{
			int32 i = y * ComponentSize + x;

			Data.Vertices[i] = FVector(x - world_offset_x, world_offset_y - y, Map->GetHeight(heightmap_offset_x + x + 1, heightmap_offset_y + y + 1));
			Data.UV0[i] = FVector2D((x + heightmap_offset_x) * Tiling, (y + heightmap_offset_y) * Tiling);
		}
	}

	// Create normals and tangents
	for (int32 y = 0; y < ComponentSize; ++y)
	{
		for (int32 x = 0; x < ComponentSize; ++x)
		{
			int32 map_offset_x = heightmap_offset_x + x + 1;
			int32 map_offset_y = heightmap_offset_y + y + 1;
			float s01 = Map->GetHeight(map_offset_x - 1, map_offset_y);
			float s21 = Map->GetHeight(map_offset_x + 1, map_offset_y);
			float s10 = Map->GetHeight(map_offset_x, map_offset_y - 1);
			float s12 = Map->GetHeight(map_offset_x, map_offset_y + 1);

			// Get tangents in the x and y directions
			FVector vx(2.0f, 0, s21 - s01);
			FVector vy(0, 2.0f, s10 - s12);

			// Calculate the cross product of the two tangents
			vx.Normalize();
			vy.Normalize();
			Data.Normals[y * ComponentSize + x] = FVector::CrossProduct(vx, vy);
			Data.Tangents[y * ComponentSize + x] = FProcMeshTangent(vx.X, vx.Y, vx.Z);
		}
	}

	// Create triangles
	if (CreateTriangles)
	{
		for (int32 y = 0; y < polygons; ++y)
		{
			for (int32 x = 0; x < polygons; ++x)
			{
				int32 i = (y * polygons + x) * 6;

				Data.Triangles[i] = x + (y * ComponentSize);
				Data.Triangles[i+1] = 1 + x + y * ComponentSize;
				Data.Triangles[i+2] = 1 + x + (y + 1) * ComponentSize;

				Data.Triangles[i+3] = x + (y * ComponentSize);
				Data.Triangles[i+4] = 1 + x + (y + 1) * ComponentSize;
				Data.Triangles[i+5] = x + (y + 1) * ComponentSize;
			}
		}
	}
}

void ATerrain::GenerateMeshSection(int32 X, int32 Y, TArray<FTerrainVertex>& Vertices, TArray<uint32>& Indices) const
{
	int32 polygons = ComponentSize - 1;

	// The location of the component in the world
	float world_offset_x = (float)(Map->GetWidthX() - 2 - 1) / 2.0f - polygons * X;
	float world_offset_y = (float)(Map->GetWidthY() - 2 - 1) / 2.0f - polygons * Y;

	// The location of the component on the heightmap
	int32 heightmap_offset_x = X * polygons;
	int32 heightmap_offset_y = Y * polygons;

	// Allocate vertices
	Vertices.SetNumUninitialized(ComponentSize * ComponentSize);

	// Create vertices and UVs
	for (int32 y = 0; y < ComponentSize; ++y)
	{
		for (int32 x = 0; x < ComponentSize; ++x)
		{
			int32 i = y * ComponentSize + x;

			Vertices[i].Position = FVector(x - world_offset_x, world_offset_y - y, Map->GetHeight(heightmap_offset_x + x + 1, heightmap_offset_y + y + 1) + 10.0f);
			Vertices[i].UV = FVector2D((x + heightmap_offset_x) * Tiling, (y + heightmap_offset_y) * Tiling);
		}
	}

	// Create normals and tangents
	for (int32 y = 0; y < ComponentSize; ++y)
	{
		for (int32 x = 0; x < ComponentSize; ++x)
		{
			int32 map_offset_x = heightmap_offset_x + x + 1;
			int32 map_offset_y = heightmap_offset_y + y + 1;
			float s01 = Map->GetHeight(map_offset_x - 1, map_offset_y);
			float s21 = Map->GetHeight(map_offset_x + 1, map_offset_y);
			float s10 = Map->GetHeight(map_offset_x, map_offset_y - 1);
			float s12 = Map->GetHeight(map_offset_x, map_offset_y + 1);

			// Get tangents in the x and y directions
			FVector vx(2.0f, 0, s21 - s01);
			FVector vy(0, 2.0f, s10 - s12);

			// Calculate the cross product of the two tangents
			vx.Normalize();
			vy.Normalize();
			Vertices[y * ComponentSize + x].Normal = FVector::CrossProduct(vx, vy);
			Vertices[y * ComponentSize + x].Tangent = FVector(vx.X, vx.Y, vx.Z);
		}
	}

	// Create triangles
	Indices.SetNumUninitialized((ComponentSize - 1) * (ComponentSize - 1) * 6);
	for (int32 y = 0; y < polygons; ++y)
	{
		for (int32 x = 0; x < polygons; ++x)
		{
			int32 i = (y * polygons + x) * 6;

			Indices[i] = x + (y * ComponentSize);
			Indices[i + 1] = 1 + x + y * ComponentSize;
			Indices[i + 2] = 1 + x + (y + 1) * ComponentSize;

			Indices[i + 3] = x + (y * ComponentSize);
			Indices[i + 4] = 1 + x + (y + 1) * ComponentSize;
			Indices[i + 5] = x + (y + 1) * ComponentSize;
		}
	}
}

void ATerrain::GenerateBorderSection(ComponentData& Data, bool CreateTriangles) const
{
	int32 width_x = Map->GetWidthX() - 2;
	int32 width_y = Map->GetWidthY() - 2;

	float world_x = (float)(width_x - 1) / 2.0f;
	float world_y = (float)(width_y - 1) / 2.0f;

	// -X side
	for (uint16 y = 0; y < width_y; ++y)
	{
		Data.Vertices.Add(FVector(-world_x, world_y - y, -200.0f));
		Data.Vertices.Add(FVector(-world_x, world_y - y, Map->GetHeight(1, y + 1)));
		Data.UV0.Add(FVector2D(-200.0f, y));
		Data.UV0.Add(FVector2D(Map->GetHeight(1, y + 1), y));
		Data.Normals.Add(FVector(-1.0f, 0.0f, 0.0f));
		Data.Normals.Add(FVector(-1.0f, 0.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));
		Data.Tangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));
	}

	if (CreateTriangles)
	{
		for (uint16 y = 0; y < width_y - 1; ++y)
		{
			Data.Triangles.Add(y * 2);
			Data.Triangles.Add(1 + y * 2);
			Data.Triangles.Add(1 + (y + 1) * 2);

			Data.Triangles.Add(y * 2);
			Data.Triangles.Add(1 + (y + 1) * 2);
			Data.Triangles.Add((y + 1) * 2);
		}
	}

	uint16 size = Data.Vertices.Num();

	// +X side
	for (uint16 y = 0; y < width_y; ++y)
	{
		Data.Vertices.Add(FVector(world_x, world_y - y, -200.0f));
		Data.Vertices.Add(FVector(world_x, world_y - y, Map->GetHeight(width_x, y + 1)));
		Data.Normals.Add(FVector(1.0f, 0.0f, 0.0f));
		Data.Normals.Add(FVector(1.0f, 0.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));
		Data.Tangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));
		Data.UV0.Add(FVector2D(-200.0f, y));
		Data.UV0.Add(FVector2D(Map->GetHeight(width_x, y + 1), y));
	}

	if (CreateTriangles)
	{
		for (uint16 y = 0; y < width_y - 1; ++y)
		{
			Data.Triangles.Add(size + 1 + (y + 1) * 2);
			Data.Triangles.Add(size + 1 + y * 2);
			Data.Triangles.Add(size + y * 2);

			Data.Triangles.Add(size + (y + 1) * 2);
			Data.Triangles.Add(size + 1 + (y + 1) * 2);
			Data.Triangles.Add(size + y * 2);
		}
	}

	size = Data.Vertices.Num();

	// +Y side
	for (uint16 x = 0; x < width_x; ++x)
	{
		Data.Vertices.Add(FVector(-world_x + x, world_y, -200.0f));
		Data.Vertices.Add(FVector(-world_x + x, world_y, Map->GetHeight(x + 1, 1)));
		Data.Normals.Add(FVector(0.0f, 1.0f, 0.0f));
		Data.Normals.Add(FVector(0.0f, 1.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		Data.UV0.Add(FVector2D(x, -200.0f));
		Data.UV0.Add(FVector2D(x, Map->GetHeight(x + 1, 1)));
	}

	if (CreateTriangles)
	{
		for (uint16 x = 0; x < width_x - 1; ++x)
		{
			Data.Triangles.Add(size + 1 + (x + 1) * 2);
			Data.Triangles.Add(size + 1 + x * 2);
			Data.Triangles.Add(size + x * 2);

			Data.Triangles.Add(size + (x + 1) * 2);
			Data.Triangles.Add(size + 1 + (x + 1) * 2);
			Data.Triangles.Add(size + x * 2);
		}
	}

	size = Data.Vertices.Num();

	// -Y side
	for (uint16 x = 0; x < width_x; ++x)
	{
		Data.Vertices.Add(FVector(-world_x + x, -world_y, -200.0f));
		Data.Vertices.Add(FVector(-world_x + x, -world_y, Map->GetHeight(x + 1, width_y)));
		Data.Normals.Add(FVector(0.0f, -1.0f, 0.0f));
		Data.Normals.Add(FVector(0.0f, -1.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		Data.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		Data.UV0.Add(FVector2D(x, -200.0f));
		Data.UV0.Add(FVector2D(x, Map->GetHeight(x + 1, width_y)));
	}

	if (CreateTriangles)
	{
		for (uint16 x = 0; x < width_x - 1; ++x)
		{
			Data.Triangles.Add(size + x * 2);
			Data.Triangles.Add(size + 1 + x * 2);
			Data.Triangles.Add(size + 1 + (x + 1) * 2);

			Data.Triangles.Add(size + x * 2);
			Data.Triangles.Add(size + 1 + (x + 1) * 2);
			Data.Triangles.Add(size + (x + 1) * 2);
		}
	}
}