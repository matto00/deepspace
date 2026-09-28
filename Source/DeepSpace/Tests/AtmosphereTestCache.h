#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Templates/UniquePtr.h"

/**
 * The optics tests' fixture airs, built once per run and shared read-only
 * (the default suite's budget, atmosphere optics plan, Global Constraints).
 * A full multiple-scattering table costs over a second a world and a
 * reference's column tables a noticeable fraction of one, and several
 * tests build the same air under the same star: each asks here instead,
 * and the first to ask pays. Nothing is precomputed or stored between
 * runs: every entry is FAtmosphere::Build's or FReferenceAir's own result,
 * made lazily in this process, so a test run alone builds what it needs
 * and a broken builder breaks every test that reads through the cache.
 *
 * An entry is found by the whole of its inputs -- every field of the spec,
 * the star's temperature and the table's coverage, compared exactly -- so
 * two airs that differ anywhere are never one entry. The static_assert
 * below holds the comparison to FAirSpec's fields: add one there and this
 * stops compiling until SameSpec compares it too.
 *
 * Game thread only, as automation tests run. Nothing outside Tests/ may
 * include this.
 */
namespace AtmosphereTestCache
{
    static_assert(sizeof(FAirSpec) == 10 * sizeof(double), "FAirSpec changed: compare every field in AtmosphereTestCache::SameSpec");

    inline bool SameSpec(const FAirSpec& A, const FAirSpec& B)
    {
        return A.RadiusCm == B.RadiusCm && A.GasScaleHeightCm == B.GasScaleHeightCm && A.GasTau550 == B.GasTau550
            && A.OzoneTau600 == B.OzoneTau600 && A.AerosolScaleHeightCm == B.AerosolScaleHeightCm && A.AerosolTau550 == B.AerosolTau550
            && A.AerosolAngstrom == B.AerosolAngstrom && A.AerosolAsymmetry == B.AerosolAsymmetry
            && A.AerosolAlbedo450 == B.AerosolAlbedo450 && A.AerosolAlbedo650 == B.AerosolAlbedo650;
    }

    /** FAtmosphere::Build(Spec, StarTemperatureK, Coverage), built on first
     *  asking. The reference lives until the process exits. */
    inline const FAtmosphere& Law(const FAirSpec& Spec, double StarTemperatureK, EAtmosphereTable Coverage = EAtmosphereTable::Full)
    {
        struct FEntry
        {
            FAirSpec Spec;
            double StarTemperatureK = 0.0;
            EAtmosphereTable Coverage = EAtmosphereTable::Full;
            FAtmosphere Built;
        };
        // Function-local in an inline function: one instance for the whole
        // module, whichever test file asks. Held by pointer so a reference
        // handed out survives the array growing.
        static TArray<TUniquePtr<FEntry>> Entries;
        for (const TUniquePtr<FEntry>& Entry : Entries)
        {
            if (Entry->StarTemperatureK == StarTemperatureK && Entry->Coverage == Coverage && SameSpec(Entry->Spec, Spec))
            {
                return Entry->Built;
            }
        }
        TUniquePtr<FEntry>& Added = Entries.Add_GetRef(MakeUnique<FEntry>());
        Added->Spec = Spec;
        Added->StarTemperatureK = StarTemperatureK;
        Added->Coverage = Coverage;
        Added->Built = FAtmosphere::Build(Spec, StarTemperatureK, Coverage);
        return Added->Built;
    }

    /** FReferenceAir(Spec, StarTemperatureK), built on first asking. */
    inline const FReferenceAir& Reference(const FAirSpec& Spec, double StarTemperatureK)
    {
        struct FEntry
        {
            FAirSpec Spec;
            double StarTemperatureK = 0.0;
            TUniquePtr<FReferenceAir> Built;
        };
        static TArray<TUniquePtr<FEntry>> Entries;
        for (const TUniquePtr<FEntry>& Entry : Entries)
        {
            if (Entry->StarTemperatureK == StarTemperatureK && SameSpec(Entry->Spec, Spec))
            {
                return *Entry->Built;
            }
        }
        TUniquePtr<FEntry>& Added = Entries.Add_GetRef(MakeUnique<FEntry>());
        Added->Spec = Spec;
        Added->StarTemperatureK = StarTemperatureK;
        Added->Built = MakeUnique<FReferenceAir>(Spec, StarTemperatureK);
        return *Added->Built;
    }
}
