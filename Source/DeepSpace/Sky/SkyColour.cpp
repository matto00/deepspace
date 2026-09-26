#include "Sky/SkyColour.h"

FLinearColor SkyColour::Blackbody(double TemperatureK)
{
    // The engine's Planckian-locus fit (Krystek), which returns linear sRGB
    // at unit luminance. Wrapped rather than rewritten: one fit, maintained
    // by somebody else, and this function is the only place the project
    // asks for it.
    const float Clamped = static_cast<float>(FMath::Clamp(TemperatureK, 1000.0, 15000.0));
    FLinearColor Colour = FLinearColor::MakeFromColorTemperature(Clamped);

    // Out of gamut below about 1,900 K the fit dips a channel negative;
    // no display can show less than none of a primary.
    Colour.R = FMath::Max(Colour.R, 0.0f);
    Colour.G = FMath::Max(Colour.G, 0.0f);
    Colour.B = FMath::Max(Colour.B, 0.0f);

    const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
    if (Peak > 0.0f)
    {
        Colour.R /= Peak;
        Colour.G /= Peak;
        Colour.B /= Peak;
    }
    Colour.A = 1.0f;
    return Colour;
}
