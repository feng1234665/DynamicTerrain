#pragma once

#include "CoreMinimal.h"

// Linear interpolation
inline float lerp(float t, float a, float b);
// Cosine interpolation
inline float corp(float t, float a, float b);
// Cubic interpolation
inline float curp(float t, float a[4]);
// Perlin smoothstep function
inline float fade(float t);

// The square of the distance between two vectors in the x and y dimensions
float distance2D(const FVector2D& a, const FVector2D& b)
{
	float dx = b.X - a.X;
	float dy = b.Y - a.Y;

	return dx * dx + dy * dy;
}

// The base class for noise data
class Noise
{
public:
	unsigned getWidth() const;
	unsigned getHeight() const;

	virtual void scale(unsigned sample_width, unsigned sample_height) = 0;

protected:
	unsigned width = 0;
	unsigned height = 0;

	float scale_x = 0.0f;
	float scale_y = 0.0f;
};

// Noise generated by creating a grid of gradient vectors
class GradientNoise : public Noise
{
public:
	GradientNoise(unsigned _width, unsigned _height, unsigned seed);
	GradientNoise(const GradientNoise& copy);
	virtual ~GradientNoise();

	virtual void scale(unsigned sample_width, unsigned sample_height) override;

	// Get the gradient at a given grid point
	FVector2D getGradient(unsigned x, unsigned y) const;

	// Get Perlin noise at the specified coordinate
	float perlin(float x, float y) const;

protected:
	FVector2D* gradient = nullptr;
};

// Noise generated by creating a grid of random values
class ValueNoise : public Noise
{
public:
	ValueNoise() {};
	ValueNoise(unsigned _width, unsigned _height, unsigned seed);
	ValueNoise(const ValueNoise& copy);
	virtual ~ValueNoise();

	virtual void scale(unsigned sample_width, unsigned sample_height) override;

	// Get the value at a given grid point
	float getValue(unsigned x, unsigned y) const;

	// Bilinear interpolated noise
	virtual float linear(float x, float y) const;
	// Cosine interpolated noise
	virtual float cosine(float x, float y) const;
	// Cubic interpolated noise
	virtual float cubic(float x, float y) const;

protected:
	float* value = nullptr;
};

// Value noise generated using the diamond square algorithm
class PlasmaNoise : public ValueNoise
{
public:
	PlasmaNoise(unsigned size, unsigned seed);
	PlasmaNoise(const PlasmaNoise& copy);
};

// Noise generated by choosing random points in a given area
class PointNoise : public Noise
{
public:
	PointNoise() {};
	PointNoise(unsigned x_bias, unsigned y_bias, unsigned points, unsigned seed);
	PointNoise(const PointNoise& copy);
	virtual ~PointNoise();

	virtual void scale(unsigned sample_width, unsigned sample_height) override;

	// Get the nearest point to a given location
	virtual inline FVector2D getNearest(FVector2D location) const;

	// Sample point noise at the given coordinates
	virtual float dot(float x, float y) const;
	// Sample raw Worley noise at the given coordinates
	virtual float worley(float x, float y) const;

	virtual const TArray<FVector2D>& getPoints();

private:
	TArray<FVector2D> Points;

	// A uniform grid used for nearest neighbor searches
	std::vector<FVector2D*>* point_grid = nullptr;
};

// Noise generated by plotting random points within each cell of a unit grid
class GridNoise : public PointNoise
{
public:
	GridNoise(unsigned _width, unsigned _height, unsigned seed);
	GridNoise(const GridNoise& copy);

	// Get the point in the provided grid cell
	inline FVector2D getPoint(unsigned x, unsigned y) const;
	// Get the nearest point to the provided coordinates
	virtual inline FVector2D getNearest(FVector2D location) const override;

	// Sample point noise at the given coordinates
	virtual float dot(float x, float y) const override;
	// Sample raw Worley noise at the given coordinates
	virtual float worley(float x, float y) const override;

	virtual const TArray<FVector2D>& getPoints();

private:
	TArray<FVector2D> Points;
};