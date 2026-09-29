#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "HAL/IConsoleManager.h"
#include "Sky/ShipSky.h"

/**
 * What the cast shadow's rendered checks (Eyes.ReliefLook, Eyes.LandingFrame)
 * measure a frame by, in one place.
 *
 * Two captures of one still scene are not one frame: the far tiles' skirts
 * and seams flicker between them (the review of the cast-shadow plan's Task
 * 1 measured up to 65 LSB on 15% of a ground frame). So nothing here compares
 * a frame with another pixel by pixel over the whole picture. A pair of
 * captures taken the same way, back to back, marks the pixels that held
 * still (within 1 LSB in every channel), and a comparison is read only
 * there; what the flicker does elsewhere is printed beside it as the noise.
 *
 * And a frame at the game's exposure under a 3-degree sun is black: 900 to
 * 6,000 of two million pixels reach a luma of 8. Measures are therefore read
 * at a READ exposure, some whole stops brighter than the game's, held
 * the same for every pass of a view, so the term's darkening is a ratio the
 * exposure does not change. The game-exposure frames stay the ruled ones.
 */
namespace EyesFrames
{
    /** PIL's convert("L"): (R 19595 + G 38470 + B 7471 + 0x8000) >> 16. */
    inline int32 Luma(const FColor& C)
    {
        return (C.R * 19595 + C.G * 38470 + C.B * 7471 + 0x8000) >> 16;
    }

    /** Lit means a luma of 8 or more, the ruling's measure. */
    inline constexpr int32 LitLuma = 8;

    /** Every channel of the two within 1 LSB. */
    inline bool Held(const FColor& A, const FColor& B)
    {
        return FMath::Abs(A.R - B.R) <= 1 && FMath::Abs(A.G - B.G) <= 1 && FMath::Abs(A.B - B.B) <= 1;
    }

    /** What a frame with the term does to one without it, read where two
     *  captures without it held still. */
    struct FShade
    {
        double LitShare = 0.0;   // of the frame: lit without the term, and held
        double HeldShare = 0.0;  // of the frame: held between the two captures without it
        double Coverage = 0.0;   // of the lit and held: taken to under half by the term
        double Flicker = 0.0;    // the same measure between the two captures without it, over all their lit pixels: the noise
        double TakenShare = 0.0; // of the frame: lit and held without the term, taken to under half by it
    };

    inline FShade Shade(const TArray<FColor>& Off, const TArray<FColor>& OffAgain, const TArray<FColor>& On)
    {
        FShade Out;
        const int32 N = FMath::Min3(Off.Num(), OffAgain.Num(), On.Num());
        int32 Held = 0;
        int32 Lit = 0;
        int32 Taken = 0;
        int32 LitAny = 0;
        int32 Flickered = 0;
        for (int32 Index = 0; Index < N; ++Index)
        {
            const int32 Was = Luma(Off[Index]);
            if (Was >= LitLuma)
            {
                ++LitAny;
                Flickered += 2 * Luma(OffAgain[Index]) < Was ? 1 : 0;
            }
            if (!EyesFrames::Held(Off[Index], OffAgain[Index]))
            {
                continue;
            }
            ++Held;
            if (Was >= LitLuma)
            {
                ++Lit;
                Taken += 2 * Luma(On[Index]) < Was ? 1 : 0;
            }
        }
        Out.HeldShare = N > 0 ? static_cast<double>(Held) / N : 0.0;
        Out.LitShare = N > 0 ? static_cast<double>(Lit) / N : 0.0;
        Out.Coverage = Lit > 0 ? static_cast<double>(Taken) / Lit : 0.0;
        Out.TakenShare = N > 0 ? static_cast<double>(Taken) / N : 0.0;
        Out.Flicker = LitAny > 0 ? static_cast<double>(Flickered) / LitAny : 0.0;
        return Out;
    }

    /** The luma below which the given share of the frame lies. */
    inline int32 LumaPercentile(const TArray<FColor>& Pixels, double Share)
    {
        int32 Counts[256] = {};
        for (const FColor& Pixel : Pixels)
        {
            ++Counts[FMath::Clamp(Luma(Pixel), 0, 255)];
        }
        const int64 Want = static_cast<int64>(Share * Pixels.Num());
        int64 Seen = 0;
        for (int32 Value = 0; Value < 256; ++Value)
        {
            Seen += Counts[Value];
            if (Seen > Want)
            {
                return Value;
            }
        }
        return 255;
    }

    /**
     * Draws a capture at the game's exposure, ds.Sky.Exposure, some whole
     * stops brighter; NAN hands it back to the capture's default.
     *
     * A scene capture keeps no view state unless told to, and with none it
     * has no exposure at all: the sky's manual exposure, and any bias on the
     * capture's own settings, change not one pixel (the review's meter run,
     * 14 stops either way). Every Eyes frame before this -- the ruling's
     * 42.9 -> 17.3 among them -- was drawn at that default, not at the
     * game's 0.7 EV100. So a read frame persists a view state, which is
     * what makes the manual exposure take, and 0 stops is the game's own.
     */
    inline void Expose(USceneCaptureComponent2D* Capture, double Stops)
    {
        FPostProcessSettings& Settings = Capture->PostProcessSettings;
        const bool bRead = !FMath::IsNaN(Stops);
        Capture->bAlwaysPersistRenderingState = bRead;
        Settings.bOverride_AutoExposureMethod = bRead;
        Settings.bOverride_AutoExposureBias = bRead;
        Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = bRead;
        Settings.AutoExposureMethod = AEM_Manual;
        Settings.AutoExposureApplyPhysicalCameraExposure = false;
        const IConsoleVariable* Exposure = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Exposure"));
        Settings.AutoExposureBias = bRead ? static_cast<float>(ShipSky::ManualExposureBias(Exposure ? Exposure->GetFloat() : 0.7f) + Stops) : 0.0f;
        Capture->PostProcessBlendWeight = 1.0f;
    }

    /** The capture's own default: no view state, no exposure. */
    inline constexpr double CaptureDefault = NAN;
}
