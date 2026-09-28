#pragma once

#include "CoreMinimal.h"

/**
 * The air's optics as the game draws them (atmospheres decision 1):
 * Shaders/Private/Atmosphere.ush, the one law, compiled here twice -- in
 * double, what the game computes, and in float, the GPU's mirror, which the
 * rendered probe (orbital slice 1) holds the GPU to and the pure tests hold
 * to the double. AtmosphereReference is what "right" means; this is what
 * draws.
 */
namespace AtmosphereLaw
{
    /** ln of the Chapman function, the .ush's AT_LogChapman: X = R / H,
     *  CosZenith the ray against the local vertical. */
    DEEPSPACE_API double LogChapmanF64(double X, double CosZenith);
    DEEPSPACE_API float LogChapmanF32(float X, float CosZenith);
}
