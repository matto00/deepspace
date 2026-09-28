#include "Ship/ShipParts.h"

FShipRatings FShipRatings::Stock()
{
    FShipRatings Ratings;
    Ratings.ReactorWatts = ShipParts::StockReactorWatts;
    Ratings.WindingWant = ShipParts::StockWindingWant;
    Ratings.ChargeSeconds = ShipParts::StockChargeSeconds;
    Ratings.DriveResponse = ShipParts::StockDriveResponse;
    Ratings.BoostersWant = ShipParts::StockBoostersWant;
    Ratings.LinearAcceleration = ShipParts::StockLinearAcceleration;
    Ratings.LightsWant = ShipParts::StockLightsWant;
    Ratings.RangeLy = ShipParts::StockRangeLy;
    return Ratings;
}

double FShipRatings::Get(EShipRating Rating) const
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return ReactorWatts;
    case EShipRating::WindingWant:        return WindingWant;
    case EShipRating::ChargeSeconds:      return ChargeSeconds;
    case EShipRating::DriveResponse:      return DriveResponse;
    case EShipRating::BoostersWant:       return BoostersWant;
    case EShipRating::LinearAcceleration: return LinearAcceleration;
    case EShipRating::LightsWant:         return LightsWant;
    case EShipRating::RangeLy:            return RangeLy;
    }
    return 0.0;
}

void FShipRatings::Set(EShipRating Rating, double Value)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       ReactorWatts = Value; break;
    case EShipRating::WindingWant:        WindingWant = Value; break;
    case EShipRating::ChargeSeconds:      ChargeSeconds = Value; break;
    case EShipRating::DriveResponse:      DriveResponse = Value; break;
    case EShipRating::BoostersWant:       BoostersWant = Value; break;
    case EShipRating::LinearAcceleration: LinearAcceleration = Value; break;
    case EShipRating::LightsWant:         LightsWant = Value; break;
    case EShipRating::RangeLy:            RangeLy = Value; break;
    }
}

FName ShipBay::Name(EShipBay Bay)
{
    switch (Bay)
    {
    case EShipBay::Reactor:     return FName(TEXT("Reactor"));
    case EShipBay::Drive:       return FName(TEXT("Drive"));
    case EShipBay::Boosters:    return FName(TEXT("Boosters"));
    case EShipBay::Lights:      return FName(TEXT("Lights"));
    case EShipBay::LifeSupport: return FName(TEXT("LifeSupport"));
    case EShipBay::Sensors:     return FName(TEXT("Sensors"));
    case EShipBay::Aux1:        return FName(TEXT("Aux1"));
    case EShipBay::Aux2:        return FName(TEXT("Aux2"));
    case EShipBay::None:        break;
    }
    return NAME_None;
}

TOptional<EShipBay> ShipBay::FromName(FName BayName)
{
    for (const EShipBay Bay : All())
    {
        if (Name(Bay) == BayName)
        {
            return Bay;
        }
    }
    return {};
}

bool ShipBay::IsAux(EShipBay Bay)
{
    return Bay == EShipBay::Aux1 || Bay == EShipBay::Aux2;
}

bool ShipBay::IsCore(EShipBay Bay)
{
    return Bay != EShipBay::None && !IsAux(Bay);
}

const TArray<EShipBay>& ShipBay::All()
{
    static const TArray<EShipBay> Bays = {
        EShipBay::Reactor, EShipBay::Drive, EShipBay::Boosters, EShipBay::Lights,
        EShipBay::LifeSupport, EShipBay::Sensors, EShipBay::Aux1, EShipBay::Aux2,
    };
    return Bays;
}

FName ShipBay::DrawKey(EShipBay Bay)
{
    return FName(*(FString(TEXT("Bay.")) + Name(Bay).ToString()));
}

FName ShipBay::StockPartId(EShipBay Bay)
{
    return IsCore(Bay) ? FName(*(Name(Bay).ToString() + TEXT(".Stock"))) : NAME_None;
}

FString ShipBay::PlateLabel(EShipBay Bay)
{
    switch (Bay)
    {
    case EShipBay::Reactor:     return TEXT("REACTOR");
    case EShipBay::Drive:       return TEXT("DRIVE");
    case EShipBay::Boosters:    return TEXT("BOOSTERS");
    case EShipBay::Lights:      return TEXT("LIGHTS");
    case EShipBay::LifeSupport: return TEXT("LIFE SUPPORT");
    case EShipBay::Sensors:     return TEXT("NAV");
    case EShipBay::Aux1:
    case EShipBay::Aux2:        return TEXT("AUX");
    case EShipBay::None:        break;
    }
    return FString();
}

FName ShipParts::RatingName(EShipRating Rating)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return FName(TEXT("ReactorWatts"));
    case EShipRating::WindingWant:        return FName(TEXT("WindingWant"));
    case EShipRating::ChargeSeconds:      return FName(TEXT("ChargeSeconds"));
    case EShipRating::DriveResponse:      return FName(TEXT("DriveResponse"));
    case EShipRating::BoostersWant:       return FName(TEXT("BoostersWant"));
    case EShipRating::LinearAcceleration: return FName(TEXT("LinearAcceleration"));
    case EShipRating::LightsWant:         return FName(TEXT("LightsWant"));
    case EShipRating::RangeLy:            return FName(TEXT("RangeLy"));
    }
    return NAME_None;
}

TOptional<EShipRating> ShipParts::RatingFromName(const FString& Text)
{
    for (const EShipRating Rating : AllRatings())
    {
        if (Text.Equals(RatingName(Rating).ToString(), ESearchCase::CaseSensitive))
        {
            return Rating;
        }
    }
    return {};
}

const TArray<EShipRating>& ShipParts::AllRatings()
{
    static const TArray<EShipRating> Ratings = {
        EShipRating::ReactorWatts, EShipRating::WindingWant, EShipRating::ChargeSeconds, EShipRating::DriveResponse,
        EShipRating::BoostersWant, EShipRating::LinearAcceleration, EShipRating::LightsWant, EShipRating::RangeLy,
    };
    return Ratings;
}

EShipBay ShipParts::OwnerOf(EShipRating Rating)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return EShipBay::Reactor;
    case EShipRating::WindingWant:
    case EShipRating::ChargeSeconds:
    case EShipRating::DriveResponse:      return EShipBay::Drive;
    case EShipRating::BoostersWant:
    case EShipRating::LinearAcceleration: return EShipBay::Boosters;
    case EShipRating::LightsWant:         return EShipBay::Lights;
    case EShipRating::RangeLy:            return EShipBay::Sensors;
    }
    return EShipBay::None;
}

void ShipParts::Apply(FShipRatings& Into, const TMap<EShipRating, double>& Rated)
{
    for (const TPair<EShipRating, double>& Rating : Rated)
    {
        Into.Set(Rating.Key, Rating.Value);
    }
}

FShipRatings ShipParts::RatingsOf(const TArray<FShipPartSpec>& Fitted)
{
    FShipRatings Out = FShipRatings::Stock();
    for (const FShipPartSpec& Part : Fitted)
    {
        Apply(Out, Part.Ratings);
    }
    return Out;
}

double ShipParts::Effective(double Rated, float CVar)
{
    return CVar >= 0.0f ? static_cast<double>(CVar) : Rated;
}

FShipLoadoutState ShipParts::EmptyLoadout()
{
    FShipLoadoutState State;
    for (const EShipBay Bay : ShipBay::All())
    {
        FShipBayState Entry;
        Entry.Bay = ShipBay::Name(Bay);
        State.Bays.Add(Entry);
    }
    return State;
}

FShipBayState* ShipParts::FindBay(FShipLoadoutState& State, EShipBay Bay)
{
    const FName Wanted = ShipBay::Name(Bay);
    return Wanted.IsNone() ? nullptr
        : State.Bays.FindByPredicate([Wanted](const FShipBayState& Entry) { return Entry.Bay == Wanted; });
}

const FShipBayState* ShipParts::FindBay(const FShipLoadoutState& State, EShipBay Bay)
{
    const FName Wanted = ShipBay::Name(Bay);
    return Wanted.IsNone() ? nullptr
        : State.Bays.FindByPredicate([Wanted](const FShipBayState& Entry) { return Entry.Bay == Wanted; });
}

TArray<ShipParts::FAxis> ShipParts::AxesOf(EShipBay Bay)
{
    const auto Rated = [](EShipRating Rating, bool bHigherIsOpen)
    {
        FAxis Axis;
        Axis.Rating = Rating;
        Axis.bHigherIsOpen = bHigherIsOpen;
        return Axis;
    };
    FAxis Draw;
    Draw.bDraw = true;
    Draw.bHigherIsOpen = false;
    switch (Bay)
    {
    case EShipBay::Reactor:     return { Rated(EShipRating::ReactorWatts, true), Draw };
    case EShipBay::Drive:       return { Rated(EShipRating::DriveResponse, true), Rated(EShipRating::ChargeSeconds, false),
                                         Rated(EShipRating::WindingWant, false), Draw };
    case EShipBay::Boosters:    return { Rated(EShipRating::LinearAcceleration, true), Rated(EShipRating::BoostersWant, false), Draw };
    case EShipBay::Lights:      return { Rated(EShipRating::LightsWant, false), Draw };
    case EShipBay::LifeSupport: return { Draw };
    case EShipBay::Sensors:     return { Rated(EShipRating::RangeLy, true), Draw };
    default:                    return {};
    }
}

double ShipParts::Openness(const FShipPartSpec& Part, const FAxis& Axis)
{
    const double* Rated = Axis.bDraw ? nullptr : Part.Ratings.Find(Axis.Rating);
    const double Value = Axis.bDraw ? Part.Draw : (Rated ? *Rated : FShipRatings::Stock().Get(Axis.Rating));
    return Axis.bHigherIsOpen ? Value : -Value;
}

double ShipParts::AtRestWatts(const FShipPartSpec& Part)
{
    FShipRatings Rated = FShipRatings::Stock();
    Apply(Rated, Part.Ratings);
    double Watts = Part.Draw;
    if (Part.Bay == EShipBay::Lights)
    {
        Watts += Rated.LightsWant;
    }
    if (Part.Bay == EShipBay::Boosters)
    {
        Watts += Rated.BoostersWant;
    }
    return Watts;
}

namespace ShipPartsDetail
{
    /** A at least as open as B on every axis of Bay and more open on one. */
    bool Dominates(const FShipPartSpec& A, const FShipPartSpec& B, EShipBay Bay)
    {
        bool bMore = false;
        for (const ShipParts::FAxis& Axis : ShipParts::AxesOf(Bay))
        {
            const double OpenA = ShipParts::Openness(A, Axis);
            const double OpenB = ShipParts::Openness(B, Axis);
            if (OpenA < OpenB)
            {
                return false;
            }
            bMore = bMore || OpenA > OpenB;
        }
        return bMore;
    }
}

TArray<FString> ShipParts::Validate(const TArray<FShipPartSpec>& Catalogue)
{
    TArray<FString> Problems;
    TSet<FName> Seen;
    for (const FShipPartSpec& Part : Catalogue)
    {
        const FString Id = Part.Id.ToString();
        if (Part.Id.IsNone())
        {
            Problems.Add(TEXT("a part has no id"));
        }
        else if (Seen.Contains(Part.Id))
        {
            Problems.Add(FString::Printf(TEXT("%s: two parts share the id"), *Id));
        }
        Seen.Add(Part.Id);
        if (Part.Bay == EShipBay::None)
        {
            Problems.Add(FString::Printf(TEXT("%s: fits no bay"), *Id));
            continue;
        }
        if (Part.Draw < 0.0)
        {
            Problems.Add(FString::Printf(TEXT("%s: a negative draw"), *Id));
        }
        for (const TPair<EShipRating, double>& Rated : Part.Ratings)
        {
            const FString Rating = RatingName(Rated.Key).ToString();
            if (ShipBay::IsAux(Part.Bay))
            {
                Problems.Add(FString::Printf(TEXT("%s: rates %s; aux parts add verbs, never numbers"), *Id, *Rating));
            }
            else if (OwnerOf(Rated.Key) != Part.Bay)
            {
                Problems.Add(FString::Printf(TEXT("%s: rates %s, which the %s bay owns"), *Id, *Rating,
                                             *ShipBay::Name(OwnerOf(Rated.Key)).ToString()));
            }
        }
        if (ShipBay::IsAux(Part.Bay) && Part.Draw > 0.0)
        {
            Problems.Add(FString::Printf(TEXT("%s: draws %.0f W at rest; an aux part wants nothing until its verb is used"), *Id, Part.Draw));
        }
    }

    for (const EShipBay Bay : ShipBay::All())
    {
        if (!ShipBay::IsCore(Bay))
        {
            continue;
        }
        const FShipPartSpec* Stock = Catalogue.FindByPredicate([Bay](const FShipPartSpec& Part)
        {
            return Part.Bay == Bay && Part.Id == ShipBay::StockPartId(Bay);
        });
        if (!Stock)
        {
            Problems.Add(FString::Printf(TEXT("the %s bay has no stock part (%s)"),
                                         *ShipBay::Name(Bay).ToString(), *ShipBay::StockPartId(Bay).ToString()));
            continue;
        }
        TArray<const FShipPartSpec*> Upgrades;
        for (const FShipPartSpec& Part : Catalogue)
        {
            if (Part.Bay == Bay && &Part != Stock)
            {
                Upgrades.Add(&Part);
            }
        }
        for (const FShipPartSpec* Part : Upgrades)
        {
            for (const FAxis& Axis : AxesOf(Bay))
            {
                if (Openness(*Part, Axis) < Openness(*Stock, Axis))
                {
                    Problems.Add(FString::Printf(TEXT("%s: less open than stock on %s"), *Part->Id.ToString(),
                                                 Axis.bDraw ? TEXT("its draw") : *RatingName(Axis.Rating).ToString()));
                }
            }
            for (const FShipPartSpec* Other : Upgrades)
            {
                if (Other != Part && ShipPartsDetail::Dominates(*Part, *Other, Bay))
                {
                    Problems.Add(FString::Printf(TEXT("%s dominates %s: a ladder, not a choice"),
                                                 *Part->Id.ToString(), *Other->Id.ToString()));
                }
            }
        }
    }

    // Every combination, one part per bay, whole at rest under the stock
    // reactor: the worst part of each bay together is the worst loadout.
    double Worst = 0.0;
    for (const EShipBay Bay : ShipBay::All())
    {
        double BayWorst = 0.0;
        for (const FShipPartSpec& Part : Catalogue)
        {
            const bool bFits = ShipBay::IsAux(Bay) ? ShipBay::IsAux(Part.Bay) : Part.Bay == Bay;
            BayWorst = bFits ? FMath::Max(BayWorst, AtRestWatts(Part)) : BayWorst;
        }
        Worst += BayWorst;
    }
    if (Worst > StockReactorWatts)
    {
        Problems.Add(FString::Printf(TEXT("the heaviest loadout asks %.0f W at rest, over the stock reactor's %.0f W"),
                                     Worst, StockReactorWatts));
    }
    return Problems;
}
