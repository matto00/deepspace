#pragma once

#include "CoreMinimal.h"
#include "Surface/WorldRelief.h"
#include "Templates/SharedPointer.h"

/**
 * A world's solid ground as the flight and the terrain read it (landing
 * decisions 5 and 10): heights over the datum sphere by unit direction in
 * universe axes, and the analytic bounds the ray march and the LOD boxes rest
 * on. FReliefGround is the real one -- WorldRelief, the one height function --
 * and tests put cheap analytic fields behind the same interface.
 *
 * Const and thread-safe: terrain tiles build on worker threads from the same
 * object the flight reads on the game thread.
 */
class DEEPSPACE_API IGroundField
{
public:
    virtual ~IGroundField() = default;

    /** The datum's radius, cm: heights are measured from it. */
    virtual double RadiusCm() const = 0;

    /** cm above the datum at D (unit), with bands finer than the footprint
     *  faded as the material fades them. 0 footprint is every band. */
    virtual double Height(const FVector3d& D, double FootprintCm) const = 0;

    /** The same, and its gradient in cm per unit of D. */
    virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const = 0;

    /** An analytic bound on what a footprint's fade removed, cm. */
    virtual double OmittedBoundCm(double FootprintCm) const = 0;

    /** Analytic bounds, never sampled. */
    virtual double MaxHeightCm() const = 0;
    virtual double MinHeightCm() const = 0;

    /** Rise over run, the Lipschitz bound the ray march steps by. */
    virtual double MaxSlope() const = 0;
};

using FGroundFieldRef = TSharedPtr<const IGroundField, ESPMode::ThreadSafe>;

/** WorldRelief behind the interface: the ground procgen made. */
class DEEPSPACE_API FReliefGround final : public IGroundField
{
public:
    explicit FReliefGround(const FWorldReliefParams& Params) : Relief(Params) {}

    const FWorldRelief& GetRelief() const { return Relief; }

    virtual double RadiusCm() const override { return Relief.GetParams().RadiusCm; }
    virtual double Height(const FVector3d& D, double FootprintCm) const override { return Relief.Height(D, FootprintCm); }
    virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
    {
        return Relief.HeightAndGradient(D, Grad, FootprintCm);
    }
    virtual double OmittedBoundCm(double FootprintCm) const override { return Relief.OmittedBoundCm(FootprintCm); }
    virtual double MaxHeightCm() const override { return Relief.MaxHeightCm(); }
    virtual double MinHeightCm() const override { return Relief.MinHeightCm(); }
    virtual double MaxSlope() const override { return Relief.MaxSlope(); }

private:
    FWorldRelief Relief;
};

namespace ShipGround
{
    /** A shared ground for Params: what the subsystem hands the flight and
     *  the terrain. */
    DEEPSPACE_API FGroundFieldRef FromRelief(const FWorldReliefParams& Params);

    /**
     * The ground's unit normal at D: the sphere's own normal tilted by the
     * gradient's part along the surface over the datum radius,
     * normalize(D - Grad_t / R) -- the composition M_SkyBody's relief_normal
     * uses, so the tile's vertex normal and the orbit's pixel normal agree.
     */
    DEEPSPACE_API FVector3d NormalAt(const IGroundField& Ground, const FVector3d& D, double FootprintCm = 0.0);
}
