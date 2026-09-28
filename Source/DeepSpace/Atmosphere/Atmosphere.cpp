#include "Atmosphere/Atmosphere.h"

#include <cmath>

// The shared file, twice. AT_CPP selects its C++ halves and AT_REAL its
// precision; each copy lives in its own namespace, so the two sets of AT_
// symbols never meet. The relative path is the file the GPU will include as
// /Project/Private/Atmosphere.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define AT_CPP 1
namespace AtmosphereF64
{
#define AT_REAL double
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
namespace AtmosphereF32
{
#define AT_REAL float
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
#undef AT_CPP

double AtmosphereLaw::LogChapmanF64(double X, double CosZenith)
{
    return AtmosphereF64::AT_LogChapman(X, CosZenith);
}

float AtmosphereLaw::LogChapmanF32(float X, float CosZenith)
{
    return AtmosphereF32::AT_LogChapman(X, CosZenith);
}
