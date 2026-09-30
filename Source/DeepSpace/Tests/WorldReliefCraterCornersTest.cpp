#include <cmath>

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The shared file once more, in this test's own namespaces, so the kernel it
// ships can be held to the one it replaced, written below from the file's own
// parts.
// The kernel's cost is held by counting the corners it visits: the file calls
// WR_CRATER_CORNER_VISITED() at each, and here that counts them.
namespace CraterCornersCount
{
    int64 Visited = 0;
}
#define WR_CRATER_CORNER_VISITED() (++::CraterCornersCount::Visited)
#define WR_CPP 1
namespace CraterCornersF64
{
#define WR_REAL double
#include "../../../Shaders/Private/WorldRelief.ush"

    /** The kernel as it was before the frame ruling's profile: every corner
     *  of the 3 x 3 x 3 round floor(P + 0.5). Kept here, verbatim but for
     *  its name, as the reference. */
    WR_CraterTerm WR_CraterKernelBand27(WR_REAL PX, WR_REAL PY, WR_REAL PZ, int KX, int KY, int KZ)
    {
        WR_CraterTerm Sum = WR_CraterZero();
        const int CX = int(WR_floor(PX + WR_REAL(0.5)));
        const int CY = int(WR_floor(PY + WR_REAL(0.5)));
        const int CZ = int(WR_floor(PZ + WR_REAL(0.5)));
        for (int I = -1; I <= 1; ++I)
        {
            for (int J = -1; J <= 1; ++J)
            {
                for (int K = -1; K <= 1; ++K)
                {
                    const int X = CX + I;
                    const int Y = CY + J;
                    const int Z = CZ + K;
                    const WR_REAL Keep = WR_REAL(WR_Rand3DPCG16(X + KX, Y + KY, Z + KZ).X) / WR_REAL(65535.0);
                    if (Keep > WR_CRATER_KEEP)
                    {
                        continue;
                    }
                    const WR_Vec3 Jitter = WR_VoronoiJitterAt(X + KX, Y + KY, Z + KZ);
                    const WR_REAL AX = PX - (WR_REAL(X) + Jitter.X);
                    const WR_REAL AY = PY - (WR_REAL(Y) + Jitter.Y);
                    const WR_REAL AZ = PZ - (WR_REAL(Z) + Jitter.Z);
                    const WR_REAL Dist = WR_sqrt(AX * AX + AY * AY + AZ * AZ);
                    const WR_REAL Q = Dist * WR_CRATER_INV_RADIUS;
                    if (Q >= WR_CRATER_REACH)
                    {
                        continue;
                    }
                    const WR_REAL Fall = WR_REAL(3.0) - WR_REAL(2.0) * Q;
                    const WR_REAL Profile = Q < WR_REAL(1.0) ? WR_CRATER_DEPTH * (Q * Q - WR_REAL(1.0) + WR_CRATER_RIM)
                                                    : WR_CRATER_DEPTH * WR_CRATER_RIM * Fall * Fall;
                    const WR_REAL Wall = Q < WR_REAL(1.0) ? WR_REAL(2.0) * WR_CRATER_DEPTH * Q
                                                          : WR_REAL(-4.0) * WR_CRATER_DEPTH * WR_CRATER_RIM * Fall;
                    const WR_REAL Apart = WR_max(Dist, WR_REAL(1.0e-4));
                    Sum.H += WR_CRATER_RADIUS * Profile;
                    Sum.GX += Wall * AX / Apart;
                    Sum.GY += Wall * AY / Apart;
                    Sum.GZ += Wall * AZ / Apart;
                    Sum.Albedo += Q < WR_REAL(1.0) ? -(WR_REAL(1.0) - Q * Q) * WR_CRATER_FLOOR_DARK
                                                   : WR_saturate(Fall) * WR_CRATER_RIM_BRIGHT;
                }
            }
        }
        return Sum;
    }
#undef WR_REAL
}
namespace CraterCornersF32
{
#define WR_REAL float
#include "../../../Shaders/Private/WorldRelief.ush"

    WR_CraterTerm WR_CraterKernelBand27(WR_REAL PX, WR_REAL PY, WR_REAL PZ, int KX, int KY, int KZ)
    {
        WR_CraterTerm Sum = WR_CraterZero();
        const int CX = int(WR_floor(PX + WR_REAL(0.5)));
        const int CY = int(WR_floor(PY + WR_REAL(0.5)));
        const int CZ = int(WR_floor(PZ + WR_REAL(0.5)));
        for (int I = -1; I <= 1; ++I)
        {
            for (int J = -1; J <= 1; ++J)
            {
                for (int K = -1; K <= 1; ++K)
                {
                    const int X = CX + I;
                    const int Y = CY + J;
                    const int Z = CZ + K;
                    const WR_REAL Keep = WR_REAL(WR_Rand3DPCG16(X + KX, Y + KY, Z + KZ).X) / WR_REAL(65535.0);
                    if (Keep > WR_CRATER_KEEP)
                    {
                        continue;
                    }
                    const WR_Vec3 Jitter = WR_VoronoiJitterAt(X + KX, Y + KY, Z + KZ);
                    const WR_REAL AX = PX - (WR_REAL(X) + Jitter.X);
                    const WR_REAL AY = PY - (WR_REAL(Y) + Jitter.Y);
                    const WR_REAL AZ = PZ - (WR_REAL(Z) + Jitter.Z);
                    const WR_REAL Dist = WR_sqrt(AX * AX + AY * AY + AZ * AZ);
                    const WR_REAL Q = Dist * WR_CRATER_INV_RADIUS;
                    if (Q >= WR_CRATER_REACH)
                    {
                        continue;
                    }
                    const WR_REAL Fall = WR_REAL(3.0) - WR_REAL(2.0) * Q;
                    const WR_REAL Profile = Q < WR_REAL(1.0) ? WR_CRATER_DEPTH * (Q * Q - WR_REAL(1.0) + WR_CRATER_RIM)
                                                    : WR_CRATER_DEPTH * WR_CRATER_RIM * Fall * Fall;
                    const WR_REAL Wall = Q < WR_REAL(1.0) ? WR_REAL(2.0) * WR_CRATER_DEPTH * Q
                                                          : WR_REAL(-4.0) * WR_CRATER_DEPTH * WR_CRATER_RIM * Fall;
                    const WR_REAL Apart = WR_max(Dist, WR_REAL(1.0e-4));
                    Sum.H += WR_CRATER_RADIUS * Profile;
                    Sum.GX += Wall * AX / Apart;
                    Sum.GY += Wall * AY / Apart;
                    Sum.GZ += Wall * AZ / Apart;
                    Sum.Albedo += Q < WR_REAL(1.0) ? -(WR_REAL(1.0) - Q * Q) * WR_CRATER_FLOOR_DARK
                                                   : WR_saturate(Fall) * WR_CRATER_RIM_BRIGHT;
                }
            }
        }
        return Sum;
    }
#undef WR_REAL
}
#undef WR_CPP
#undef WR_CRATER_CORNER_VISITED

/*
 * The frame ruling's profile found the ground's pixel shader the frame's
 * largest cost, and the crater kernel most of that shader: 27 lattice
 * corners a band, six bands. Only the 2 x 2 x 2 corners round P can hold a
 * site within a crater's reach, so the kernel now visits those, in the same
 * order. This holds the kernel the file ships to the 27-corner one, to the
 * last bit, in double (the C++ ground) and in float (the GPU's precision),
 * over a million points at the lattice shifts and magnitudes the bands
 * reach, and counts the points where two or more craters overlap, so a
 * sample that never met one proves nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCraterKernelCornersTest, "DeepSpace.Surface.CraterKernelCorners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CraterCornersLocal
{
    template <typename TTerm>
    bool Same(const TTerm& A, const TTerm& B)
    {
        // Bitwise: -0 against +0 or a NaN would be a difference too.
        return FMemory::Memcmp(&A.H, &B.H, sizeof(A.H)) == 0 && FMemory::Memcmp(&A.GX, &B.GX, sizeof(A.GX)) == 0
            && FMemory::Memcmp(&A.GY, &B.GY, sizeof(A.GY)) == 0 && FMemory::Memcmp(&A.GZ, &B.GZ, sizeof(A.GZ)) == 0
            && FMemory::Memcmp(&A.Albedo, &B.Albedo, sizeof(A.Albedo)) == 0;
    }
}

bool FCraterKernelCornersTest::RunTest(const FString& Parameters)
{
    using namespace CraterCornersLocal;
    FRandomStream Random(20260928);
    int32 Differ64 = 0;
    int32 Differ32 = 0;
    int32 Hit = 0;
    int32 Overlap = 0;
    int32 Busy64 = 0;
    int32 Busy32 = 0;
    constexpr int32 Samples = 1000000;
    for (int32 Sample = 0; Sample < Samples; ++Sample)
    {
        // The finest band is 12288 cycles a radius, the offset under 256, so
        // |P| reaches about 12,544; the lattice shift is the offset's integer
        // part, up to about 106 x 83. A quarter of the points sit within
        // 1e-3 of a half cell, where floor(P) and floor(P + 0.5) part.
        const double Scale = FMath::Pow(10.0, Random.FRandRange(-1.0, 4.1));
        double P[3];
        for (double& Axis : P)
        {
            Axis = Random.FRandRange(-Scale, Scale);
            if (Random.FRand() < 0.25)
            {
                Axis = FMath::Floor(Axis) + 0.5 + Random.FRandRange(-1.0e-3, 1.0e-3);
            }
        }
        const int32 K[3] = { Random.RandRange(-9000, 9000), Random.RandRange(-9000, 9000), Random.RandRange(-9000, 9000) };

        CraterCornersCount::Visited = 0;
        const CraterCornersF64::WR_CraterTerm New64 = CraterCornersF64::WR_CraterKernelBand(P[0], P[1], P[2], K[0], K[1], K[2]);
        Busy64 += CraterCornersCount::Visited == 8 ? 0 : 1;
        const CraterCornersF64::WR_CraterTerm Old64 = CraterCornersF64::WR_CraterKernelBand27(P[0], P[1], P[2], K[0], K[1], K[2]);
        Differ64 += Same(New64, Old64) ? 0 : 1;
        const float F[3] = { static_cast<float>(P[0]), static_cast<float>(P[1]), static_cast<float>(P[2]) };
        CraterCornersCount::Visited = 0;
        const CraterCornersF32::WR_CraterTerm New32 = CraterCornersF32::WR_CraterKernelBand(F[0], F[1], F[2], K[0], K[1], K[2]);
        Busy32 += CraterCornersCount::Visited == 8 ? 0 : 1;
        const CraterCornersF32::WR_CraterTerm Old32 = CraterCornersF32::WR_CraterKernelBand27(F[0], F[1], F[2], K[0], K[1], K[2]);
        Differ32 += Same(New32, Old32) ? 0 : 1;

        // How many sites reach this point: a point under two craters is
        // where a missed corner would show.
        int32 Reaching = 0;
        const int CX = int(FMath::Floor(P[0] + 0.5));
        const int CY = int(FMath::Floor(P[1] + 0.5));
        const int CZ = int(FMath::Floor(P[2] + 0.5));
        for (int I = -1; I <= 1; ++I)
        {
            for (int J = -1; J <= 1; ++J)
            {
                for (int L = -1; L <= 1; ++L)
                {
                    using namespace CraterCornersF64;
                    const int X = CX + I, Y = CY + J, Z = CZ + L;
                    if (double(WR_Rand3DPCG16(X + K[0], Y + K[1], Z + K[2]).X) / 65535.0 > WR_CRATER_KEEP)
                    {
                        continue;
                    }
                    const WR_Vec3 Jitter = WR_VoronoiJitterAt(X + K[0], Y + K[1], Z + K[2]);
                    const double Dist = FMath::Sqrt(FMath::Square(P[0] - X - Jitter.X) + FMath::Square(P[1] - Y - Jitter.Y) + FMath::Square(P[2] - Z - Jitter.Z));
                    Reaching += Dist * WR_CRATER_INV_RADIUS < WR_CRATER_REACH ? 1 : 0;
                }
            }
        }
        Hit += Reaching > 0 ? 1 : 0;
        Overlap += Reaching > 1 ? 1 : 0;
    }
    AddInfo(FString::Printf(TEXT("%d points: %d under a crater, %d under two or more; %d differ in double, %d in float; %d visit other than 8 corners in double, %d in float"),
        Samples, Hit, Overlap, Differ64, Differ32, Busy64, Busy32));
    TestTrue(FString::Printf(TEXT("the sample meets overlapping craters (%d of %d points)"), Overlap, Samples), Overlap > Samples / 100);
    TestEqual(TEXT("the 2 x 2 x 2 kernel is the 27-corner one to the last bit, in double"), Differ64, 0);
    TestEqual(TEXT("the 2 x 2 x 2 kernel is the 27-corner one to the last bit, in float"), Differ32, 0);
    // The speed-up itself: the sums above cannot see a corner visited in
    // vain, so a restored 3 x 3 x 3 would pass them. The frame it cost (the
    // landing plan's Task 39: 18.09 -> 13.85 ms at 1.5 m) is not asserted
    // anywhere; this holds what bought it.
    TestEqual(TEXT("and it visits 8 corners a band, never 27, in double"), Busy64, 0);
    TestEqual(TEXT("and it visits 8 corners a band, never 27, in float"), Busy32, 0);
    return true;
}

#endif
