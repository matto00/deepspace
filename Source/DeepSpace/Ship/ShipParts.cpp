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
