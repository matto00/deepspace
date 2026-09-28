#pragma once

#include "CoreMinimal.h"
#include "ShipParts.generated.h"

/**
 * The ship's bays (wear and upgrades decision 1): six fixed, one per thing
 * the ship already models, and two auxiliary slots. Each holds one part.
 * Fixed bays with one part each leave nothing to pack, so no loadout is a
 * knapsack with an optimum to converge on.
 *
 * None is first on purpose. A part whose Bay was never set (made with
 * NewObject in a test, or an asset a script missed) reads None, and
 * UShipSubsystem::FitPart refuses it rather than defaulting it into the
 * reactor bay and displacing the reactor. Bays are saved by name
 * (ShipBay::Name), never by position, so adding one later shifts nothing.
 */
UENUM(BlueprintType)
enum class EShipBay : uint8
{
    None,
    Reactor,
    Drive,
    Boosters,
    Lights,
    LifeSupport,
    Sensors,
    Aux1,
    Aux2,
};

/** A rated value a part can set. Each is owned by exactly one bay
 *  (decision 2's table; ShipParts::OwnerOf). No rating is a top speed. */
UENUM(BlueprintType)
enum class EShipRating : uint8
{
    ReactorWatts,
    WindingWant,
    ChargeSeconds,
    DriveResponse,
    BoostersWant,
    LinearAcceleration,
    LightsWant,
    RangeLy,
};

/** One part as the pure rules see it, with no UObject:
 *  UShipModuleDataAsset::GetSpec builds it, and the tests build it from
 *  Tools/ship_parts.json. */
struct DEEPSPACE_API FShipPartSpec
{
    FName Id;
    EShipBay Bay = EShipBay::None;

    /** Watts off the top while fitted. */
    double Draw = 0.0;

    /** Only the ratings this part sets; any other reads stock. */
    TMap<EShipRating, double> Ratings;
};

/** Every rated value the ship runs on, derived and never stored. */
struct DEEPSPACE_API FShipRatings
{
    double ReactorWatts = 0.0;
    double WindingWant = 0.0;
    double ChargeSeconds = 0.0;
    double DriveResponse = 0.0;
    double BoostersWant = 0.0;
    double LinearAcceleration = 0.0;
    double LightsWant = 0.0;
    double RangeLy = 0.0;

    /** Today's ship (ruling 1), and what an empty core bay reads (decision 3). */
    static FShipRatings Stock();

    double Get(EShipRating Rating) const;
    void Set(EShipRating Rating, double Value);
};

/**
 * One particular part, fitted or spare (decision 11): a plain value the
 * struct serialiser can write field by field and a test can compare. Ids,
 * never pointers, so a part that no longer exists can be reported and fall
 * back to stock. The wear fields exist from slice 1 and stay zero until
 * slice 4, so the save (slice 3) needs no migration when wear arrives.
 */
USTRUCT()
struct DEEPSPACE_API FShipPartState
{
    GENERATED_BODY()

    /** Which part. None in a bay is an empty bay: it reads the stock part's
     *  ratings and draws nothing (decision 3). */
    UPROPERTY()
    FName PartId;

    /** Slice 4: jumps this part has aged. */
    UPROPERTY()
    double AgeJumps = 0.0;

    /** Slice 4: the age at which its symptom arrives. */
    UPROPERTY()
    double LifeJumps = 0.0;

    /** Slice 4: false until first fitted (decision 10). */
    UPROPERTY()
    bool bHasLife = false;

    /** Slice 4: None, or one of the bay's symptoms. */
    UPROPERTY()
    FName Symptom;

    /** Slice 4: history for its words and its look. */
    UPROPERTY()
    int32 Repairs = 0;

    /** Slice 4: one of the ship's seeded stock parts (decision 19). */
    UPROPERTY()
    bool bOriginal = false;
};

/** One bay: its name, never its position, and the part in it. */
USTRUCT()
struct DEEPSPACE_API FShipBayState
{
    GENERATED_BODY()

    /** ShipBay::Name of the bay. */
    UPROPERTY()
    FName Bay;

    UPROPERTY()
    FShipPartState Part;

    /** Slice 4: lives this bay has drawn, for the wear seed. */
    UPROPERTY()
    int32 LivesDrawn = 0;
};

/** Everything aboard that is a part: the one truth UShipSubsystem keeps. */
USTRUCT()
struct DEEPSPACE_API FShipLoadoutState
{
    GENERATED_BODY()

    /** One per bay, found by name (ShipParts::FindBay). */
    UPROPERTY()
    TArray<FShipBayState> Bays;

    /** Each keeps its own state (decision 10). No carry limit, no weight:
     *  a spare is not an object to manage. */
    UPROPERTY()
    TArray<FShipPartState> Spares;
};

namespace ShipBay
{
    /** "Reactor", "LifeSupport", "Aux1": what a bay is saved and found by.
     *  NAME_None for None. */
    DEEPSPACE_API FName Name(EShipBay Bay);

    /** The bay a name names; unset for None and for a name no bay has. */
    DEEPSPACE_API TOptional<EShipBay> FromName(FName BayName);

    /** The six fixed bays. */
    DEEPSPACE_API bool IsCore(EShipBay Bay);

    /** Aux1 and Aux2. */
    DEEPSPACE_API bool IsAux(EShipBay Bay);

    /** Every bay, in bay order (the nameplates' order): the six core bays,
     *  then the two auxiliary slots. Never None. */
    DEEPSPACE_API const TArray<EShipBay>& All();

    /** The key a bay's draw is booked under in FShipPowerState, "Bay.Lights"
     *  (decision 4). A swap is RemoveDraw and AddDraw under one key, so two
     *  parts in one bay can never both draw. */
    DEEPSPACE_API FName DrawKey(EShipBay Bay);

    /** "Reactor.Stock": the part a core bay starts with. None for an aux slot. */
    DEEPSPACE_API FName StockPartId(EShipBay Bay);

    /** The nameplate's first column: "REACTOR", "LIFE SUPPORT", "NAV", "AUX". */
    DEEPSPACE_API FString PlateLabel(EShipBay Bay);
}

namespace ShipParts
{
    /**
     * The stock ship's numbers, today's exactly (ruling 1). Moved here from
     * UShipSubsystem's constants and the four CVars' old defaults; the
     * catalogue's stock rows (Tools/ship_parts.json) carry the same numbers,
     * and DeepSpace.Ship.Parts.Contract holds them equal.
     *
     * The reactor is sized so the stock ship is whole at rest: its draws (620
     * W) plus the lights (300) and the boosters (450) come to 1370 W, so at
     * the default split nothing is dimmed while nothing is being asked of the
     * ship. The split bites when the jump winds -- the first playtest found
     * the lights at 63% on a quiet ship under the old 1000 W, which read as
     * broken rather than strained (developer's ruling, 2026-09-26).
     * DeepSpace.Ship.JumpCanWindAtFullSpeed holds it.
     *
     * The wants sum to more than the reactor makes, deliberately: if
     * everything could be fed at once the split would never be a choice, and
     * a choice with no cost is not one. The engine has no want at rest: it
     * asks WindingWant only while the jump winds, so an idle drive costs the
     * ship nothing (nav decision 4). An idle want of zero reads as full
     * satisfaction, so anything that follows the winding -- the hum -- reads
     * watts delivered over WindingWant, never satisfaction (plan conflict 8).
     */
    inline constexpr double StockReactorWatts = 1400.0;
    inline constexpr double StockLightsWant = 300.0;
    inline constexpr double StockBoostersWant = 450.0;

    /** Cruise's own 2 km/s^2 at full feed (FShipFlightLimits::Cruise). */
    inline constexpr double StockLinearAcceleration = 2.0e5;

    /** 380 W: small enough that an engine-first split winds at full speed on
     *  the stock hauler, which has 780 W left once its draws are off the top.
     *  At the old 800 W against a 1000 W reactor the best any split reached
     *  was 48% fed, and the charge time was a number no player could see.
     *  Settled 2026-09-26. */
    inline constexpr double StockWindingWant = 380.0;

    /** Settled 2026-09-26; FShipFlightState::JumpChargeSeconds is the same. */
    inline constexpr double StockChargeSeconds = 45.0;

    /** ShipDriveLever::DefaultResponse. */
    inline constexpr double StockDriveResponse = 3.0;

    inline constexpr double StockRangeLy = 12.0;

    /** "ReactorWatts": the key Tools/ship_parts.json rates by. */
    DEEPSPACE_API FName RatingName(EShipRating Rating);

    /** Case-sensitive, as the JSON is; unset for a name no rating has. */
    DEEPSPACE_API TOptional<EShipRating> RatingFromName(const FString& Text);

    DEEPSPACE_API const TArray<EShipRating>& AllRatings();

    /** The one bay that may rate Rating (decision 2's table). */
    DEEPSPACE_API EShipBay OwnerOf(EShipRating Rating);

    /** Each of Rated's values over Into's. */
    DEEPSPACE_API void Apply(FShipRatings& Into, const TMap<EShipRating, double>& Rated);

    /** Stock, with every fitted part's ratings applied over it. */
    DEEPSPACE_API FShipRatings RatingsOf(const TArray<FShipPartSpec>& Fitted);

    /**
     * A playtest CVar over a fitted part's rating (decision 6): the CVar when
     * it is 0 or more, else the rating. -1 is the default. Zero overrides --
     * ds.Nav.ChargeSeconds 0 is how tests make a jump instant -- and any
     * negative value, not only -1, reads the part.
     */
    DEEPSPACE_API double Effective(double Rated, float CVar);

    /** Every bay, in ShipBay::All's order, empty. */
    DEEPSPACE_API FShipLoadoutState EmptyLoadout();

    /** The entry for Bay, found by name; null if State has none. */
    DEEPSPACE_API FShipBayState* FindBay(FShipLoadoutState& State, EShipBay Bay);
    DEEPSPACE_API const FShipBayState* FindBay(const FShipLoadoutState& State, EShipBay Bay);

    /** One axis a bay's parts are compared on (decision 7's table): the
     *  draw, or one of its ratings. Every watt figure is more open when
     *  lower; every capability when higher. */
    struct FAxis
    {
        bool bDraw = false;
        EShipRating Rating = EShipRating::ReactorWatts;
        bool bHigherIsOpen = true;
    };

    /** Bay's axes: the draw in every core bay, and its ratings. None for an
     *  aux slot, whose parts rate nothing. */
    DEEPSPACE_API TArray<FAxis> AxesOf(EShipBay Bay);

    /** Part's value on Axis, signed so that more open is always larger; a
     *  rating the part lacks reads stock. */
    DEEPSPACE_API double Openness(const FShipPartSpec& Part, const FAxis& Axis);

    /** What Part asks at rest: its draw, and the want it rates (lights and
     *  boosters). The engine wants nothing at rest. */
    DEEPSPACE_API double AtRestWatts(const FShipPartSpec& Part);

    /**
     * Decision 7 over a catalogue, and decision 8's rules on a part: one
     * sentence per problem, empty when every rule holds. A design
     * constraint, not only a test: parts widen what the ship can do; they
     * never make it need more of anything.
     */
    DEEPSPACE_API TArray<FString> Validate(const TArray<FShipPartSpec>& Catalogue);
}
