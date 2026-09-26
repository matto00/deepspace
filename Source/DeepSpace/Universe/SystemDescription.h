#pragma once

#include "CoreMinimal.h"
#include "Universe/StarSystem.h"

/**
 * A system as readable text: the star, then every planet's designation,
 * kind, orbit and temperature, and who lives there. Pure -- it returns the
 * text and whoever calls it decides where the text goes (the console, a log,
 * a corpus file).
 *
 * What it is for is reading: whether a system reads as a *place* or as a
 * roll is a question statistics cannot answer.
 */
namespace SystemDescription
{
    DEEPSPACE_API FString Describe(const FStarSystem& System);

    /** One line: name, class, luminosity, temperature, the distance if one
     *  is given, and the id. For ds.Universe.Near and anything else that
     *  lists stubs. */
    DEEPSPACE_API FString DescribeStub(const FStarSystemStub& Stub, double DistanceCm = -1.0);

    DEEPSPACE_API const TCHAR* ClassName(EStarClass Class);
    DEEPSPACE_API const TCHAR* KindName(EPlanetKind Kind);
}
