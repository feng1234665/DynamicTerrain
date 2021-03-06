// Copyright © 2019 Created by Brian Faubion

#pragma once

#include "TerrainHeightMap.h"
#include "TerrainFoliage.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Terrain.generated.h"

class UTerrainComponent;
class UHierarchicalInstancedStaticMeshComponent;

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
	// Change LOD settings for the terrain
	UFUNCTION(BlueprintCallable)
		void SetLODs(int32 NumLODs, float DistanceScale);
	// Get the terrain material
	UFUNCTION(BlueprintCallable)
		UMaterialInterface* GetMaterials();
	// Set the tiling frequency for the terrain
	UFUNCTION(BlueprintCallable)
		void SetTiling(float Frequency);

	// Reset the heightmap and rebuild the terrain completely
	UFUNCTION(BlueprintCallable)
		void Rebuild();
	// Rebuild the terrain mesh to reflect changes made to the heightmap
	UFUNCTION(BlueprintCallable)
		void Refresh();
	// Delete all the foliage on the terrain
	UFUNCTION(BlueprintCallable)
		void DeleteFoliage();

	// Rebuild sections that were marked with Update and UpdateRange
	UFUNCTION(BlueprintCallable)
		void Update();
	// Mark a mesh section for updating
	UFUNCTION(BlueprintCallable)
		void UpdateSection(int32 X, int32 Y);
	// Mark all sections for updating
	UFUNCTION(BlueprintCallable)
		void UpdateAll();
	// Mark sections overlapping a region of the heightmap
	void UpdateRange(FIntRect Range);

	// Convert a vector in world space into a map coordinate
	UFUNCTION(BlueprintPure)
		FVector2D GetMapVector(FVector WorldPosition) const;
	// Get the height of the heightmap at a given point in world space
	UFUNCTION(BlueprintPure)
		float GetHeight(FVector WorldLocation) const;
	// Get the surface normal of the heightmap at a given point in world space
	UFUNCTION(BlueprintPure)
		FVector GetNormal(FVector WorldLocation) const;
	// Get the X tangent of the heightmap at a given point in world space
	UFUNCTION(BlueprintPure)
		FVector GetTangent(FVector WorldLocation);

	// Get the heightmap used to store map data
	UFUNCTION(BlueprintPure)
		inline UHeightMap* GetMap() const;
	// Get the foliage components
	UFUNCTION(BlueprintPure)
		inline TArray<UHierarchicalInstancedStaticMeshComponent*> GetInstancedMeshComponents() const;
	// Get foliage groups for the terrain
	
		inline void GetFoliageGroups(TArray<UTerrainFoliageSpawner*>& FoliageList) const;
	// Change foliage groups for the terrain
	
		inline void SetFoliageGroups(TArray<UTerrainFoliageSpawner*>& FoliageList);
	// Find a foliage component matching the provided mesh
	UFUNCTION(BlueprintPure)
		inline UHierarchicalInstancedStaticMeshComponent* FindInstancedMesh(UStaticMesh* Mesh) const;
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
		inline int32 GetNumLODs() const;
	UFUNCTION(BlueprintPure)
		inline float GetLODDistanceScale() const;
	UFUNCTION(BlueprintPure)
		inline bool GetAsyncCookingEnabled() const;

protected:
	/// Map Rebuilding ///

	// Create a blank heightmap big enough to accomodate every component
	void RebuildHeightmap();
	// Rebuild or reload the procedural mesh component
	void RebuildMesh();
	// Copy map data to components
	void RebuildProxies();
	// Rebuild instanced meshes
	void RebuildFoliage();

	// Refresh the materials on component meshes
	void ApplyMaterials();
	// Refresh meshes on foliage components
	//void ApplyFoliageMeshes();

	/// Properties ///

	// Set to true when a corresponding section in Meshes needs to update
	UPROPERTY()
		TArray<bool> UpdateMesh;
	// Terrain components
	UPROPERTY(VisibleAnywhere)
		TArray<UTerrainComponent*> Components;
	// Foliage components
	UPROPERTY(VisibleAnywhere)
		TArray<UHierarchicalInstancedStaticMeshComponent*> FoliageComponents;
	// Foliage meshes
	UPROPERTY()
		TArray<UTerrainFoliageSpawner*> FoliageGroups;

	// Set to true to cook collision asynchronously
	UPROPERTY(EditAnywhere)
		bool UseAsyncCooking = true;

	// The material to apply to the terrain
	UPROPERTY(EditAnywhere)
		UMaterialInterface* TerrainMaterial = nullptr;

	// The number of vertices per component section
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 ComponentSize = 6;
	// The number of component sections in the mesh
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 XWidth = 1;
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		int32 YWidth = 1;
	// The scaling factor for UV coordinates for mesh materials
	UPROPERTY(VisibleAnywhere, Category = "Attributes")
		float Tiling = 1.0f;

	// The number of LOD levels to generate
	UPROPERTY(VisibleAnywhere, Category = "LOD")
		int32 LODLevels = 5;
	// The scaling to use when switching between LODs
	UPROPERTY(VisibleAnywhere, Category = "LOD")
		float LODScale = 0.5;

	// Set to true when the map mesh needs to be rebuilt
	UPROPERTY(VisibleAnywhere)
		bool DirtyMesh = true;

	// The heightmap used to store terrain data
	UPROPERTY(VisibleAnywhere)
		UHeightMap* Map;
};

// Get the vertex width of a component from a given size
inline uint32 DYNAMICTERRAIN_API GetTerrainComponentWidth(uint32 Size);