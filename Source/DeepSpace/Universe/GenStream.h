#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Universe/GenSeed.h"

/**
 * A deterministic random stream: SplitMix64, one uint64 of state.
 *
 *   NextU64: State += GenSeed::Golden; return GenSeed::Finalise(State);
 *
 * so NextU64() from state S is exactly GenSeed::Mix(S).
 *
 * Every sampler lives here, written out by hand, because the standard
 * library's distributions are implementation-defined -- libstdc++ and MSVC
 * give different normal draws from the same engine -- and a universe that
 * depends on which compiler built it is not deterministic.
 *
 * Construct one per *quantity* (GenSeed::Derive with a Label), draw it, and
 * discard the stream (procgen decision 2). Draws that together make one
 * quantity -- a position's components, a name's syllables -- share it.
 *
 * THE CONTRACT. Each sampler's comment below is its exact form, arithmetic
 * in the order written, and Tools/rng.py mirrors it line for line. The
 * vectors in Tools/rng_vectors.json hold both sides to it bit for bit, so an
 * implementation that is merely equivalent in exact arithmetic is wrong. In
 * particular:
 *
 * - Transcendentals are <cmath>'s std::exp, std::log, std::sqrt, std::cos
 *   and std::pow, never FMath's: those are what CPython's math calls
 *   (glibc's libm), and FMath is free to become a vectorised approximation
 *   in any engine release. Pi is UE_DOUBLE_PI, which is math.pi.
 * - The module must keep UBT's default precise floating point, which on
 *   clang is -ffp-contract=off. A fused multiply-add changes last bits --
 *   built with -mfma -ffp-contract=fast, 69 of the vectors' 1,056 sampler
 *   draws came out different -- and -ffast-math anywhere in
 *   DeepSpace.Build.cs would make the universe depend on the optimiser.
 * - Bit-identical across platforms is *not* promised (procgen decision 11):
 *   libm's last bit differs between math libraries. Discrete outcomes
 *   (counts, classes, kinds) are what must be robust to that, later.
 *
 * Nothing here is a sampler because it might be useful. Each one has a
 * caller: procgen's generators, and the sky's starfield (Exponential for the
 * Laplace disc, ParetoBounded for star brightness).
 */
struct DEEPSPACE_API FGenStream
{
public:
    explicit FGenStream(uint64 Seed);

    /** State += GenSeed::Golden; return GenSeed::Finalise(State). */
    uint64 NextU64();

    /** [0, 1): double(NextU64() >> 11) * 0x1.0p-53. The top 53 bits, exactly
     *  representable, so every value is a multiple of 2^-53. */
    double Unit();

    /** (0, 1), strictly: (double(NextU64() >> 12) + 0.5) * 0x1.0p-52.
     *  Safe for std::log at either end. 52 bits because 52 bits plus a half
     *  is exact in a double; 53 bits plus a half is not, and its largest
     *  value rounds up to exactly 1.0 -- so the spec's first form was open at
     *  one end only. */
    double UnitOpen();

    /**
     * Uniform on [Min, MaxInclusive], unbiased: Lemire's multiply-shift with
     * rejection. Asserts Min <= MaxInclusive.
     *
     *   Span = uint64(MaxInclusive) - uint64(Min) + 1;   // 0 for the whole int64 range
     *   if (Span == 0) return int64(NextU64());
     *   M = uint128(NextU64()) * Span; Low = uint64(M);
     *   if (Low < Span) {
     *       Threshold = (0 - Span) % Span;               // 2^64 mod Span
     *       while (Low < Threshold) { M = uint128(NextU64()) * Span; Low = uint64(M); }
     *   }
     *   return int64(uint64(Min) + uint64(M >> 64));
     *
     * A modulo would favour low values whenever Span does not divide 2^64,
     * which for a sector's 2^18 chunks it does -- but nothing should have to
     * know that to be correct. uint128 is clang's unsigned __int128.
     */
    int64 UniformInt(int64 Min, int64 MaxInclusive);

    /** Unit() < P. Always draws exactly once, whatever P is, so a quantity
     *  whose probability is tuned to 0 or 1 consumes the same stream. */
    bool Chance(double P);

    /**
     * A count of independent events with this mean. Knuth, exactly:
     *
     *   L = std::exp(-Mean); K = 0; P = Unit();
     *   while (P > L && K < Max) { ++K; P *= Unit(); }
     *   return K;
     *
     * Stops at Max rather than redrawing (plan conflict 11): at the means
     * used the cap bites at most once in five thousand draws. Asserts
     * 0 <= Mean <= 30 -- Knuth costs O(Mean) draws, and a mean over 30 is a
     * design error that should fail loudly rather than go slow.
     */
    int32 Poisson(double Mean, int32 Max);

    /**
     * Box-Muller, the cosine branch, with no cached second value:
     *
     *   U1 = UnitOpen(); U2 = Unit();
     *   return Mean + StdDev * (std::sqrt(-2.0 * std::log(U1)) * std::cos(2.0 * UE_DOUBLE_PI * U2));
     *
     * Caching the sine branch would make the stream's state two values, and a
     * copied stream that has or has not consumed the cache is exactly the
     * bug that surfaces as "this planet is different on the second visit".
     */
    double Normal(double Mean, double StdDev);

    /** Median * std::exp(Sigma * Normal(0.0, 1.0)). A magnitude built from
     *  many multiplied factors. */
    double LogNormal(double Median, double Sigma);

    /**
     * LogNormal truncated to [Min, Max], not clamped: up to 16 draws of
     * LogNormal(Median, Sigma), returning the first inside the bounds
     * (inclusive); if all 16 miss, the 16th is clamped. Asserts
     * 0 < Min <= Max.
     *
     * Clamping piles every out-of-range draw onto the bound -- a suspicious
     * crowd of planets at exactly the maximum mass. There is no closed-form
     * inverse to truncate exactly, so this resamples, and with the bounds
     * the generator uses the clamp fires about once in a billion draws. That
     * is ADR 0008's "rejection is a floor, not a method" at the scale of one
     * number, and because a stream feeds one quantity, however many draws it
     * takes touches nothing else.
     */
    double LogNormalBounded(double Median, double Sigma, double Min, double Max);

    /** Waiting time between events at this mean: -Mean * std::log(UnitOpen()). */
    double Exponential(double Mean);

    /**
     * A Pareto of index Alpha truncated to [Min, Max] by exact inversion, so
     * it needs no rejection:
     *
     *   U = UnitOpen(); Tail = std::pow(Min / Max, Alpha);
     *   X = Min * std::pow(1.0 - U * (1.0 - Tail), -1.0 / Alpha);
     *   return FMath::Clamp(X, Min, Max);          // last-bit guard only
     *
     * Asserts Alpha > 0 and 0 < Min < Max.
     */
    double ParetoBounded(double Alpha, double Min, double Max);

    /**
     * Gamma(Shape, 1), Marsaglia-Tsang. Asserts Shape > 0.
     *
     *   if (Shape < 1.0) { G = Gamma(Shape + 1.0); return G * std::pow(UnitOpen(), 1.0 / Shape); }
     *   D = Shape - 1.0 / 3.0; C = 1.0 / std::sqrt(9.0 * D);
     *   for (;;) {
     *       do { X = Normal(0.0, 1.0); V = 1.0 + C * X; } while (V <= 0.0);
     *       V = V * V * V;
     *       U = UnitOpen();
     *       if (U < 1.0 - 0.0331 * (X * X) * (X * X)) return D * V;
     *       if (std::log(U) < 0.5 * X * X + D * (1.0 - V + std::log(V))) return D * V;
     *   }
     *
     * The cube is V * V * V, never std::pow. U is UnitOpen because the second
     * test takes its log. Below shape 1 the boost's uniform is drawn *after*
     * the inner Gamma.
     */
    double Gamma(double Shape);

    /** A bounded proportion: X = Gamma(A); Y = Gamma(B); return X / (X + Y).
     *  A's gamma first. */
    double Beta(double A, double B);

    /**
     * An index chosen in proportion to Weights, with one Unit():
     *
     *   Total = sum of Weights, left to right;
     *   Target = Unit() * Total; Acc = 0;
     *   for each i: { Acc += Weights[i]; if (Target < Acc) return i; }
     *   return the last i whose weight is > 0;      // rounding in Acc only
     *
     * A zero weight is never chosen. Asserts every weight is finite and
     * non-negative and that Total > 0.
     */
    int32 Categorical(TConstArrayView<double> Weights);

    /** How many draws LogNormalBounded makes before it clamps. */
    static constexpr int32 MaxBoundedAttempts = 16;

    /** The largest mean Poisson accepts. */
    static constexpr double MaxPoissonMean = 30.0;

private:
    uint64 State;
};
