#include "Misc/AutomationTest.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/GenStream.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStarSystemGenerationTest,
    "DeepSpace.Universe.SystemGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Named, never anonymous: the unity build pastes every test file into one
// translation unit, where each file's anonymous namespace is the same one
// and a second SameStub is a redefinition.
namespace StarSystemGenerationTestLocal
{
    constexpr int32 SystemCount = 5000;

    /** The first SystemCount stubs of a universe, sector by sector, under
     *  the priors they will be generated with: the stub carries the star, so
     *  a stub made under other class weights would disagree with its system
     *  by construction. */
    TArray<FStarSystemStub> Stubs(uint64 Root, const FGenPriors& Priors)
    {
        const FGalaxyGenerator Galaxy(Root, Priors);
        TArray<FStarSystemStub> Result;
        for (int64 X = 0; Result.Num() < SystemCount; ++X)
        {
            for (int64 Y = 0; Y < 16 && Result.Num() < SystemCount; ++Y)
            {
                for (int64 Z = 0; Z < 16 && Result.Num() < SystemCount; ++Z)
                {
                    Result.Append(Galaxy.GenerateSector(FInt64Vector(X - 8, Y - 8, Z - 8)));
                }
            }
        }
        Result.SetNum(SystemCount);
        return Result;
    }

    /** Priors pushed to where the invariants are hardest to keep: giants
     *  everywhere, heavy rock, the tightest spacing and the most planets. A
     *  tune that could break a guarantee fails here rather than in play. */
    FGenPriors StressPriors()
    {
        FGenPriors P;
        P.ClassWeightB = 0.3;
        P.PlanetCountMean = 10.0;
        P.InnermostMedianFactor = 0.05;
        P.InnermostSigma = 1.0;
        P.HillSpacingMedian = 10.5;
        P.HillSpacingSigma = 1.0;
        P.RockyMassMedianInner = 10.0;
        P.RockyMassMedianOuter = 12.0;
        P.RockyMassSigma = 2.0;
        P.GiantChanceMax = 1.0;
        P.GiantChancePerSolarMass = 100.0;
        P.GiantMassMedian = 2500.0;
        P.GiantMassSigma = 2.0;
        P.InhabitedChance = 1.0;
        return P;
    }

    /** Every prior at the low edge of what GenPriorDomain lets an ini hold:
     *  the smallest suns, the thinnest Beta, the most planets, every median a
     *  hair above zero and every spread none. Decision 14's promise is that
     *  no ini line can break an invariant; this and DomainCeiling are the
     *  corners that promise is hardest to keep at. */
    FGenPriors DomainFloor()
    {
        FGenPriors P;
        P.ClassWeightM = 1.0;
        P.ClassWeightK = P.ClassWeightG = P.ClassWeightF = P.ClassWeightA = P.ClassWeightB = 0.0;
        P.ClassBandBetaA = P.ClassBandBetaB = GenPriorDomain::MinBetaShape;
        P.PlanetCountMean = FGenStream::MaxPoissonMean;
        P.InnermostMedianFactor = 1e-9;
        P.InnermostSigma = 0.0;
        P.HillSpacingMedian = 1e-9;
        P.HillSpacingSigma = 0.0;
        P.RockyMassMedianInner = P.RockyMassMedianOuter = 1e-9;
        P.RockyMassSigma = 0.0;
        P.GiantChanceMax = P.GiantChancePerSolarMass = 0.0;
        P.GiantMassMedian = 1e-9;
        P.GiantMassSigma = 0.0;
        P.OceanFraction = 0.0;
        P.InhabitedChance = 1.0;
        P.PopulationMedian = 1e-9;
        P.PopulationSigma = 0.0;
        P.PopulationMin = P.PopulationMax = 1.0;
        // The floor of this one is an empty galaxy, which is not a set of
        // systems to test; the ceiling packs the sectors instead.
        P.SystemsPerSector = FGenStream::MaxPoissonMean;
        return P;
    }

    /** Every prior at the high edge, or far out along it where the domain
     *  has none: the biggest suns, the widest spreads, the outermost first
     *  orbit, giants round every star, and everyone inhabited. */
    FGenPriors DomainCeiling()
    {
        FGenPriors P;
        P.ClassWeightB = 1.0;
        P.ClassWeightM = P.ClassWeightK = P.ClassWeightG = P.ClassWeightF = P.ClassWeightA = 0.0;
        P.ClassBandBetaA = P.ClassBandBetaB = 1e6;
        P.PlanetCountMean = FGenStream::MaxPoissonMean;
        P.InnermostMedianFactor = GenPriorDomain::MaxInnermostMedianFactor;
        P.InnermostSigma = 50.0;
        P.HillSpacingMedian = 1e9;
        P.HillSpacingSigma = 50.0;
        P.RockyMassMedianInner = P.RockyMassMedianOuter = 1e9;
        P.RockyMassSigma = 50.0;
        P.GiantChanceMax = 1.0;
        P.GiantChancePerSolarMass = 1e9;
        P.GiantMassMedian = 1e9;
        P.GiantMassSigma = 50.0;
        P.OceanFraction = 1.0;
        P.InhabitedChance = 1.0;
        P.PopulationMedian = 1e12;
        P.PopulationSigma = 50.0;
        P.PopulationMin = 1.0;
        P.PopulationMax = 1e15;
        P.SystemsPerSector = FGenStream::MaxPoissonMean;
        return P;
    }

    bool SameStub(const FStarSystemStub& A, const FStarSystemStub& B)
    {
        return A.Id == B.Id && A.Seed == B.Seed && A.Position == B.Position && A.Name == B.Name
            && A.Class == B.Class && A.LuminositySolar == B.LuminositySolar && A.TemperatureK == B.TemperatureK;
    }

    bool SameStar(const FStar& A, const FStar& B)
    {
        return A.Class == B.Class && A.MassSolar == B.MassSolar && A.RadiusSolar == B.RadiusSolar
            && A.LuminositySolar == B.LuminositySolar && A.TemperatureK == B.TemperatureK
            && A.HabitableInnerAU == B.HabitableInnerAU && A.HabitableOuterAU == B.HabitableOuterAU
            && A.FrostLineAU == B.FrostLineAU;
    }

    bool SamePlanet(const FPlanet& A, const FPlanet& B)
    {
        return A.Id == B.Id && A.Designation == B.Designation && A.GivenName == B.GivenName && A.Kind == B.Kind
            && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth && A.EquilibriumK == B.EquilibriumK
            && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.PhaseRad == B.PhaseRad && A.Population == B.Population
            && A.DayHours == B.DayHours;
    }

    bool SameSystem(const FStarSystem& A, const FStarSystem& B)
    {
        if (!SameStub(A.Stub, B.Stub) || !SameStar(A.Star, B.Star) || A.Planets.Num() != B.Planets.Num())
        {
            return false;
        }
        for (int32 I = 0; I < A.Planets.Num(); ++I)
        {
            if (!SamePlanet(A.Planets[I], B.Planets[I]))
            {
                return false;
            }
        }
        return true;
    }

    bool Finite(double X)
    {
        return std::isfinite(X);
    }

    /** Every property a system must have whatever the priors, measured from
     *  the output. Returns the first violation, or empty. */
    FString Violation(const FStarSystem& System, const FGenPriors& Priors)
    {
        const FStar& Star = System.Star;
        if (!Finite(Star.MassSolar) || !Finite(Star.LuminositySolar) || !Finite(Star.RadiusSolar) || !Finite(Star.TemperatureK)
            || Star.MassSolar <= 0.0 || Star.LuminositySolar <= 0.0)
        {
            return TEXT("the star is not a star");
        }
        if (Star.Class != System.Stub.Class || Star.LuminositySolar != System.Stub.LuminositySolar
            || Star.TemperatureK != System.Stub.TemperatureK)
        {
            return TEXT("the star disagrees with its stub");
        }
        if (!(Star.HabitableInnerAU < Star.HabitableOuterAU && Star.HabitableOuterAU < Star.FrostLineAU))
        {
            return TEXT("the zones are out of order");
        }
        if (System.Planets.Num() > GenGuarantees::MaxPlanets)
        {
            return TEXT("more planets than the cap");
        }

        const double StarRadiusAU = Star.RadiusSolar * UniverseUnits::CmPerSolarRadius / UniverseUnits::CmPerAU;
        const double CapEarth = GenGuarantees::MaxPlanetToStarMass * Star.MassSolar / UniverseUnits::EarthMassSolar;

        for (int32 I = 0; I < System.Planets.Num(); ++I)
        {
            const FPlanet& P = System.Planets[I];
            if (!Finite(P.MassEarth) || !Finite(P.RadiusEarth) || !Finite(P.EquilibriumK) || !Finite(P.SemiMajorAxisAU)
                || !Finite(P.PhaseRad) || !Finite(P.Population))
            {
                return FString::Printf(TEXT("planet %d has a NaN"), I);
            }
            if (P.Id != FBodyId{System.Stub.Id, I, -1})
            {
                return FString::Printf(TEXT("planet %d has the wrong id"), I);
            }
            if (P.Designation != SystemNames::Designation(System.Stub.Name, I))
            {
                return FString::Printf(TEXT("planet %d is designated '%s'"), I, *P.Designation);
            }
            if (P.MassEarth > CapEarth * (1.0 + 1e-12) || P.MassEarth < GenGuarantees::RockyMassMin)
            {
                return FString::Printf(TEXT("planet %d's mass %.4g is outside [min, %.4g]"), I, P.MassEarth, CapEarth);
            }
            if (P.PhaseRad < 0.0 || P.PhaseRad >= 2.0 * UE_DOUBLE_PI)
            {
                return FString::Printf(TEXT("planet %d's phase is outside [0, 2 pi)"), I);
            }

            // Kind is consistent with mass and temperature -- derived, never drawn.
            const bool bGiant = P.MassEarth >= GenGuarantees::GasGiantMinEarthMasses;
            const bool bBarren = !bGiant && (P.MassEarth < GenGuarantees::BarrenBelowEarthMasses || P.EquilibriumK > GenGuarantees::BarrenAboveK);
            const bool bIce = !bGiant && !bBarren && P.EquilibriumK < GenGuarantees::IceBelowK;
            const bool bTemperate = !bGiant && !bBarren && !bIce;
            const bool bKindOk =
                (P.Kind == EPlanetKind::GasGiant && bGiant) || (P.Kind == EPlanetKind::Barren && bBarren)
                || (P.Kind == EPlanetKind::Ice && bIce)
                || ((P.Kind == EPlanetKind::Ocean || P.Kind == EPlanetKind::Terrestrial) && bTemperate);
            if (!bKindOk)
            {
                return FString::Printf(TEXT("planet %d is kind %d at %.3g Mearth and %.0f K"), I, int32(P.Kind), P.MassEarth, P.EquilibriumK);
            }

            // A giant has a day, bounded; rock has none drawn.
            if (bGiant ? !(P.DayHours >= 5.0 && P.DayHours <= 30.0) : P.DayHours != 0.0)
            {
                return FString::Printf(TEXT("planet %d, kind %d, has a %.3g h day"), I, int32(P.Kind), P.DayHours);
            }

            // Somebody lives only where it is temperate, and only they are named.
            if ((P.Population > 0.0) != !P.GivenName.IsEmpty())
            {
                return FString::Printf(TEXT("planet %d has a given name without a population, or the reverse"), I);
            }
            if (P.Population > 0.0 && (!bTemperate || P.Population < Priors.PopulationMin || P.Population > Priors.PopulationMax))
            {
                return FString::Printf(TEXT("planet %d has %.0f people at kind %d"), I, P.Population, int32(P.Kind));
            }

            const FVector FromStar = System.PlanetPosition(I) - System.Stub.Position;
            const double RadiusCm = P.SemiMajorAxisAU * UniverseUnits::CmPerAU;
            if (FMath::Abs(FromStar.Size() - RadiusCm) > 1e-9 * RadiusCm + 1.0 || FromStar.Z != 0.0)
            {
                return FString::Printf(TEXT("planet %d is not on its circle"), I);
            }
        }

        if (!System.Planets.IsEmpty() && System.Planets[0].SemiMajorAxisAU < GenGuarantees::InnermostMinStellarRadii * StarRadiusAU * (1.0 - 1e-12))
        {
            return TEXT("the innermost planet is inside five stellar radii");
        }

        // The property itself, not the parameter meant to produce it: every
        // neighbouring pair at least ten mutual Hill radii apart.
        for (int32 I = 0; I + 1 < System.Planets.Num(); ++I)
        {
            const FPlanet& Inner = System.Planets[I];
            const FPlanet& Outer = System.Planets[I + 1];
            const double H = std::cbrt((Inner.MassEarth + Outer.MassEarth) * UniverseUnits::EarthMassSolar / (3.0 * Star.MassSolar));
            const double MutualHill = H * 0.5 * (Inner.SemiMajorAxisAU + Outer.SemiMajorAxisAU);
            const double Separation = (Outer.SemiMajorAxisAU - Inner.SemiMajorAxisAU) / MutualHill;
            if (!(Separation >= GenGuarantees::MinHillSpacing * (1.0 - 1e-9)))
            {
                return FString::Printf(TEXT("planets %d and %d are %.4f mutual Hill radii apart"), I, I + 1, Separation);
            }
        }
        return FString();
    }
}

bool FStarSystemGenerationTest::RunTest(const FString& Parameters)
{
    using namespace StarSystemGenerationTestLocal;

    const TArray<FStarSystemStub> All = Stubs(20260925, FGenPriors{});
    TestEqual(TEXT("enough systems to test"), All.Num(), SystemCount);

    // -- invariants, under the defaults, the ini's, and priors pushed to the edge
    // The ini's are the universe actually played; a tune in progress is
    // exactly when an invariant is most likely to be broken, so they are
    // checked as they stand. While the ini holds the code defaults that pass
    // repeats the first -- it earns its place the day it does not. The domain's
    // corners are what make it more than that: every one of these is a set
    // the ini is allowed to hold, so an invariant any accepted tune could
    // break breaks here first.
    struct FPriorSet
    {
        const TCHAR* Name;
        FGenPriors Priors;
    };
    const FPriorSet Sets[] = {
        {TEXT("default priors"), FGenPriors{}},
        {TEXT("the ini's priors"), GetDefault<UProcGenPriorsConfig>()->ToPriors()},
        {TEXT("stress priors"), StressPriors()},
        {TEXT("the domain's floor"), DomainFloor()},
        {TEXT("the domain's ceiling"), DomainCeiling()}};

    for (const FPriorSet& Set : Sets)
    {
        const TArray<FString> Refused = GenPriorDomain::Refusals(Set.Priors);
        TestEqual(FString::Printf(TEXT("%s: an ini could hold these (%s)"), Set.Name, *FString::Join(Refused, TEXT("; "))),
            Refused.Num(), 0);

        int32 Violations = 0;
        int32 Differs = 0;
        FString First;
        int32 Planets = 0;
        int32 Kinds[5] = {};
        int32 Inhabited = 0;
        int32 WithTemperate = 0;
        int32 ClassCounts[NumStarClasses] = {};

        for (const FStarSystemStub& Stub : Stubs(20260925, Set.Priors))
        {
            const FStarSystem System = FStarSystemGenerator::Generate(Stub, Set.Priors);
            const FStarSystem Again = FStarSystemGenerator::Generate(Stub, Set.Priors);
            if (!SameSystem(System, Again))
            {
                ++Differs;
            }
            const FString Problem = Violation(System, Set.Priors);
            if (!Problem.IsEmpty() && Violations++ == 0)
            {
                First = FString::Printf(TEXT("%s: %s"), *System.Stub.Name, *Problem);
            }

            ++ClassCounts[int32(System.Star.Class)];
            Planets += System.Planets.Num();
            bool bAnyone = false;
            bool bTemperate = false;
            for (const FPlanet& P : System.Planets)
            {
                ++Kinds[int32(P.Kind)];
                bAnyone |= P.Population > 0.0;
                bTemperate |= P.Kind == EPlanetKind::Ocean || P.Kind == EPlanetKind::Terrestrial;
            }
            Inhabited += bAnyone ? 1 : 0;
            WithTemperate += bTemperate ? 1 : 0;
        }

        TestEqual(FString::Printf(TEXT("%s: generating twice is field-for-field identical"), Set.Name), Differs, 0);
        TestEqual(FString::Printf(TEXT("%s: no system breaks an invariant (first: %s)"), Set.Name, *First), Violations, 0);

        const double N = SystemCount;
        AddInfo(FString::Printf(
            TEXT("%s: per system %.2f planets -- %.2f barren, %.2f terrestrial, %.2f ocean, %.2f ice, %.3f gas giants; ")
            TEXT("%.0f%% with a temperate world, %.1f%% inhabited; classes M %.3f K %.3f G %.3f F %.3f A %.4f B %.4f"),
            Set.Name, Planets / N, Kinds[0] / N, Kinds[1] / N, Kinds[2] / N, Kinds[3] / N, Kinds[4] / N,
            100.0 * WithTemperate / N, 100.0 * Inhabited / N,
            ClassCounts[0] / N, ClassCounts[1] / N, ClassCounts[2] / N, ClassCounts[3] / N, ClassCounts[4] / N, ClassCounts[5] / N));

        if (&Set == &Sets[0])
        {
            // Honest weights (the developer's ruling): three suns in four are
            // red dwarfs. Four standard errors of 5,000 draws is about 0.024.
            TestTrue(TEXT("three suns in four are red dwarfs"), FMath::Abs(ClassCounts[0] / N - 0.76) < 0.025);
            // InhabitedChance 0.08 gives roughly one system in sixteen: a
            // playtest meets one, and it is still an event.
            TestTrue(TEXT("somebody lives in a few per cent of systems"), Inhabited / N > 0.02 && Inhabited / N < 0.12);
        }
    }

    // -- a giant's day: log-normal about 12 h, not flat ---------------------------
    // Twenty thousand giants' days, drawn exactly as Generate draws them. A
    // log-normal is symmetric in log space about its median, and holds about
    // two thirds of its draws within a sigma of it; a uniform day across the
    // same [5, 30] would hold a third there and put its median at 17.5 h.
    {
        TArray<double> Logs;
        int32 WithinSigma = 0;
        constexpr int32 Giants = 20000;
        for (int32 I = 0; I < Giants; ++I)
        {
            const double Day = FStarSystemGenerator::GenerateGiantDay(GenSeed::Derive(20260926, GenSeed::Label("giant"), I));
            Logs.Add(FMath::Loge(Day));
            WithinSigma += (Day >= 12.0 * FMath::Exp(-0.35) && Day <= 12.0 * FMath::Exp(0.35)) ? 1 : 0;
        }
        Logs.Sort();
        const double Median = FMath::Exp(Logs[Giants / 2]);
        double Mean = 0.0;
        for (const double L : Logs) { Mean += L / Giants; }
        double Variance = 0.0;
        for (const double L : Logs) { Variance += FMath::Square(L - Mean) / Giants; }
        const double Spread = FMath::Sqrt(Variance);
        TestTrue(FString::Printf(TEXT("a giant's median day is 12 h (%.2f h)"), Median), FMath::Abs(Median / 12.0 - 1.0) < 0.02);
        // The bounds sit 2.5 sigma out, which trims the spread by about 4%.
        TestTrue(FString::Printf(TEXT("its log spread is the 0.35 asked for, less what the bounds trim (%.3f)"), Spread),
            Spread > 0.31 && Spread < 0.355);
        TestTrue(FString::Printf(TEXT("and two thirds of days lie within a sigma of it (%.3f)"), double(WithinSigma) / Giants),
            double(WithinSigma) / Giants > 0.62);
        TestTrue(TEXT("every day inside [5, 30] h"), FMath::Exp(Logs[0]) >= 5.0 && FMath::Exp(Logs.Last()) <= 30.0);
    }

    // -- independence: a tune moves only what depends on it ----------------------
    // Ocean fraction, inhabited chance, the population prior and the planet
    // count touch no orbit and no mass. (The mass prior does move outer orbits,
    // and is meant to: spacing is in Hill radii, which is physics, not stream
    // coupling.)
    {
        const FGenPriors Base;
        TArray<TPair<const TCHAR*, FGenPriors>> Variants;
        { FGenPriors P = Base; P.OceanFraction = 0.95; Variants.Emplace(TEXT("ocean fraction"), P); }
        { FGenPriors P = Base; P.InhabitedChance = 0.9; Variants.Emplace(TEXT("inhabited chance"), P); }
        { FGenPriors P = Base; P.PopulationMedian = 3.0e6; P.PopulationSigma = 0.5; Variants.Emplace(TEXT("population prior"), P); }
        { FGenPriors P = Base; P.PlanetCountMean = 7.5; Variants.Emplace(TEXT("planet-count mean"), P); }

        for (const TPair<const TCHAR*, FGenPriors>& Variant : Variants)
        {
            int32 Moved = 0;
            int32 Compared = 0;
            for (const FStarSystemStub& Stub : All)
            {
                const FStarSystem A = FStarSystemGenerator::Generate(Stub, Base);
                const FStarSystem B = FStarSystemGenerator::Generate(Stub, Variant.Value);
                const int32 Surviving = FMath::Min(A.Planets.Num(), B.Planets.Num());
                for (int32 I = 0; I < Surviving; ++I)
                {
                    ++Compared;
                    Moved += (A.Planets[I].SemiMajorAxisAU != B.Planets[I].SemiMajorAxisAU
                        || A.Planets[I].MassEarth != B.Planets[I].MassEarth) ? 1 : 0;
                }
            }
            TestEqual(FString::Printf(TEXT("changing the %s moves no surviving orbit or mass (%d compared)"), Variant.Key, Compared), Moved, 0);
        }
    }

    // A fifth planet appends; it does not move the first four.
    {
        int32 Moved = 0;
        for (const FStarSystemStub& Stub : All)
        {
            const FStarSystem A = FStarSystemGenerator::Generate(Stub, FGenPriors{});
            const FStarSystem B = FStarSystemGenerator::GenerateWithPlanetCount(Stub, FGenPriors{}, A.Planets.Num() + 1);
            bool bSame = B.Planets.Num() == A.Planets.Num() + 1;
            for (int32 I = 0; bSame && I < A.Planets.Num(); ++I)
            {
                bSame = SamePlanet(A.Planets[I], B.Planets[I]);
            }
            Moved += bSame ? 0 : 1;
        }
        TestEqual(TEXT("one more planet leaves the first n identical"), Moved, 0);
    }

    // A system's name is the same whether or not its planets were generated,
    // and the stub is a strict prefix of the system.
    {
        int32 Differs = 0;
        for (const FStarSystemStub& Stub : All)
        {
            const FStarSystem Bare = FStarSystemGenerator::GenerateWithPlanetCount(Stub, FGenPriors{}, 0);
            const FStarSystem Full = FStarSystemGenerator::Generate(Stub, FGenPriors{});
            Differs += (Bare.Stub.Name != Full.Stub.Name || Full.Stub.Name != FStarSystemGenerator::GenerateName(Stub.Seed)
                || !SameStar(Bare.Star, Full.Star)) ? 1 : 0;
        }
        TestEqual(TEXT("name and star do not depend on the planets"), Differs, 0);
    }

    // Names: pronounceable-looking, capitalised, and plain ASCII letters.
    {
        bool bWellFormed = true;
        FString Bad;
        for (int32 I = 0; I < 200; ++I)
        {
            const FString& Name = All[I].Name;
            bool bOk = Name.Len() >= 2 && FChar::IsUpper(Name[0]);
            for (int32 C = 1; C < Name.Len(); ++C)
            {
                bOk &= FChar::IsLower(Name[C]);
            }
            if (!bOk && bWellFormed)
            {
                Bad = Name;
            }
            bWellFormed &= bOk;
        }
        TestTrue(TEXT("system names are one capitalised word: ") + Bad, bWellFormed);
        TestEqual(TEXT("designation IV"), SystemNames::Designation(TEXT("Kessa"), 3), FString(TEXT("Kessa IV")));
        TestEqual(TEXT("designation XII"), SystemNames::Designation(TEXT("Kessa"), 11), FString(TEXT("Kessa XII")));
        TestEqual(TEXT("numeral IX"), SystemNames::RomanNumeral(9), FString(TEXT("IX")));
        AddInfo(FString::Printf(TEXT("names: %s, %s, %s, %s, %s, %s"), *All[0].Name, *All[1].Name, *All[2].Name,
            *All[3].Name, *All[4].Name, *All[5].Name));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
