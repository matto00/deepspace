#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Surface/SunShadow.h"

class IGroundField;

/**
 * A solid world's cast shadow for the orbit (the developer's ruling on
 * slice (b)'s build: the orbital proxy reads a per-world shadow texture
 * baked in C++ when the system loads). Pure data.
 *
 * Equirectangular in the star's own frame: FrameZ is the light
 * (SkyProjection::LightDirection), FrameX and FrameY square to it. Column c
 * is the azimuth -pi + (c + 0.5) Step about the light, row r the star's
 * elevation PsiLo + (r + 0.5) Step above the datum's level. Each texel is
 * Step square at the terminator, where the shadows are, and narrows toward
 * the point under the star, where the disc is whole. Under PsiLo no point of
 * the world, however high, sees any of the star, so the night side is not
 * stored. Levels[0] is row-major, Width x Rows, each texel
 * Quantise(visibility); level L + 1 is level L halved (BuildLevels), at the
 * GPU's own mip sizes.
 */
struct DEEPSPACE_API FSunShadowMap
{
    int32 Width = 0;
    int32 Rows = 0;
    double PsiLo = 0.0;
    double Step = 0.0;
    FVector3d FrameX = FVector3d::UnitX();
    FVector3d FrameY = FVector3d::UnitY();
    FVector3d FrameZ = FVector3d::UnitZ();
    TArray<TArray<uint16>> Levels;

    int32 WidthAt(int32 Level) const { return FMath::Max(1, Width >> Level); }
    int32 RowsAt(int32 Level) const { return FMath::Max(1, Rows >> Level); }
    int32 LevelCount() const { return Levels.Num(); }
    uint16 Texel(int32 Level, int32 X, int32 Y) const { return Levels[Level][Y * WidthAt(Level) + X]; }
    int64 Bytes() const;
};

namespace SunShadowMap
{
    /** Columns a world's map is baked with unless ds.Sky.ShadowMapWidth says
     *  otherwise: 8.8 km texels on Baemsekai IV, finer than a 4K pixel while
     *  the world is under 35 degrees across (the cast-shadow plan, Task 0). */
    inline constexpr int32 DefaultWidth = 4096;

    /** The map's frame, rows and step for Ground under Sun, and no texels;
     *  Width 0 where there is no star or no ground. */
    DEEPSPACE_API FSunShadowMap Shape(const IGroundField& Ground, const SunShadow::FSunLight& Sun, int32 Width);

    /** The unit direction, body axes, at the centre of texel (Column, Row). */
    DEEPSPACE_API FVector3d TexelDirection(const FSunShadowMap& Map, int32 Column, int32 Row);

    /** 0..1 as the 16 bits a texel holds. */
    DEEPSPACE_API uint16 Quantise(double Visible);

    /** Every texel: each column's heights read once, at the step times R as
     *  the footprint, then each row's AlongProfile over the rows ahead of it
     *  to the march's end. Rows over the day exit are whole and read nothing,
     *  and rows under the night exit are dark. Cancel, if given, is polled
     *  once a column; set, the bake stops and returns a map with no levels.
     *  Then BuildLevels. */
    DEEPSPACE_API FSunShadowMap Bake(const IGroundField& Ground, const SunShadow::FSunLight& Sun, double SteepestSlope, int32 Width,
                                     const std::atomic<bool>* Cancel = nullptr);

    /** Level L + 1 from level L until one texel is left: each texel the
     *  rounded mean of its two by two, the columns and rows past the last
     *  taken from the last, as the GPU sizes a mip (Width >> L). */
    DEEPSPACE_API void BuildLevels(FSunShadowMap& Map);

    /** What M_SkyBody and M_SkyGround read at D (body axes) for a pixel of
     *  this footprint -- one pixel's width, D units, not the faces'
     *  filter_pixels-wide footprint: the shared file's lookup and eight
     *  texel loads, trilinear, in double and in float. 0 under PsiLo, and 1
     *  for a map with no levels. */
    DEEPSPACE_API double Sample(const FSunShadowMap& Map, const FVector3d& D, double Footprint);
    DEEPSPACE_API float SampleF32(const FSunShadowMap& Map, const FVector3f& D, float Footprint);
}
