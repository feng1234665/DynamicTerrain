// Copyright © 2019 Created by Brian Faubion

#pragma once

#include "HeightMap.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Terrain.generated.h"

struct ComponentData
{
	void Allocate(int32 Size);

	TArray<FVector> Vertices;
	TArray<FVector2D> UV0;
	TArray<FVector> Normals;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
};

UCLASS()
class DYNAMICTERRAIN_API ATerrain : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATerrain();

	/// Engine Functions ///

	// Called after construction
	virtual void OnConstruction(const FTransform& Transform) override;
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	// Called when a property is changed in the editor
	virtual void PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	/// Accessor Functions ///

	// Change the size of the terrain
	UFUNCTION(BlueprintCallable)
		void Resize(int32 SizeOfComponents, int32 ComponentWidthX, int32 ComponentWidthY);
	// Set the materials for the terrain
	UFUNCTION(BlueprintCallable)
		void SetMaterials(UMaterial* terrain_material = nullptr, UMaterial* border_material = nullptr);
	// Set the tiling frequency for the terrain
	UFUNCTION(BlueprintCallable)
		void SetTiling(float Frequency);
	// Enable or disable the mesh border
	UFUNCTION(BlueprintCallable)
		void EnableBorder(bool Enable);

	// Reset the heightmap and rebuild the terrain completely
	UFUNCTION(BlueprintCallable)
		void Rebuild();
	// Rebuild the terrain mesh to reflect changes made to the heightmap
	UFUNCTION(BlueprintCallable)
		void Refresh();

	// Rebuild sections that were marked with Update and UpdateRange
	UFUNCTION(BlueprintCallable)
		void Update();
	// Mark a mesh section for updating
	UFUNCTION(BlueprintCallable)
		void UpdateSection(int32 X, int32 Y);
	// Mark sections overlapping a region of the heightmap
		void UpdateRange(FIntRect Range);

	// Get the heightmap used to store map data
	UFUNCTION(BlueprintPure)
		inline UHeightMap* GetMap() const;
	// Get the width of each mesh component
	UFUNCTION(BlueprintPure)
		inline int32 GetComponentSize() const;
	// Get number of components along the x axis
	UFUNCTION(BlueprintPure)
		inline int32 GetXWidth() const;
	// Get number of components along the y axis
	UFUNCTION(BlueprintPure)
		inline int32 GetYWidth() const;
	UFUNCTION(BlueprintPure)
		inline float GetTiling() const;
	UFUNCTION(BlueprintPure)
		inline bool GetBorderEnabled() const;

	/// Generator Functions ///

	// Create a mesh from a subsection of the heightmap
	void GenerateMeshSection(int32 X, int32 Y, ComponentData& Data, bool CreateTriangles) const;
	// Create the border mesh
	void GenerateBorderSection(ComponentData& Data, bool CreateTriangles) const;

protected:
	/// Map Rebuilding ///

	// Create a blank heightmap big enough to accomodate every component
	void RebuildHeightmap();
	// Rebuild or reload the procedural mesh component
	void RebuildMesh();

	// Refresh the materials on component meshes
	void ApplyMaterials();

	/// Properties ///

	// The procedural meshes used for the terrain mesh
	UPROPERTY(EditAnywhere)
		TArray<UProceduralMeshComponent*> Meshes;
	// Set to true when a corresponding section in Meshes needs to update
	UPROPERTY(EditAnywhere)
		TArray<bool> UpdateMesh;

	// The material to apply to the terrain
	UPROPERTY(EditAnywhere)
		UMaterial* TerrainMaterial = nullptr;
	// The material for the outer border
	UPROPERTY(EditAnywhere)
		UMaterial* BorderMaterial = nullptr;

	// The number of vertices per component section
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 ComponentSize = 64;
	// The number of component sections in the mesh
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 XWidth = 1;
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 YWidth = 1;
	// The scaling factor for UV coordinates for mesh materials
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		float Tiling = 1.0f;

	// Set to true to create borders on the map mesh
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		bool Border = true;

	// The number of threads to use when generating a new map
	UPROPERTY(EditAnywhere)
		int32 WorkerThreads = 4;

	// Set to true when the map mesh needs to be rebuilt
	UPROPERTY(VisibleAnywhere)
		bool DirtyMesh = true;

	// The heightmap used to store terrain data
	UPROPERTY(VisibleAnywhere)
		UHeightMap* Map;

	// A buffer to store data for generated components
	ComponentData ComponentBuffer;
};
