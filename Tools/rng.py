"""
A literal mirror of Source/DeepSpace/Universe/GenSeed.h and GenStream.h.

Not a random library of its own: the same names (snake_case), the same
algorithms, the same order of draws, and the same arithmetic written in the
same order, so that CPython's float operations and glibc's libm give the same
bits the C++ gets on this machine (procgen decision 11: single-platform
determinism, and no more). If a line here and a line in GenStream.h disagree,
the header is the contract and this file is wrong.

There is no generator in Python and there must not be one (ADR 0006). This
file exists so that Tools/rng_vectors.json can be written and read by
something other than the code it pins: `python3 Tools/test_rng.py` holds this
mirror to procgen's known-value table, and DeepSpace.Universe.Stream holds
FGenStream to the same file, so neither side can drift without the other's
test going red.

The exact forms, as pinned by the header:

- ``next_u64``: state += 0x9E3779B97F4A7C15, return finalise(state). So the
  first output of Stream(s) is mix(s).
- ``unit``: the top 53 bits * 2^-53, in [0, 1).
- ``unit_open``: the top 52 bits plus a half, * 2^-52, in (0, 1) exactly.
  (53 bits plus a half does not fit in a double, and its largest value rounds
  up to exactly 1.0.)
- ``uniform_int``: Lemire's multiply-shift with rejection; the full int64
  range is a bare next_u64.
- ``chance``: unit() < p, and it always draws, whatever p is.
- ``poisson``: Knuth, stopping at max_count (plan conflict 11).
- ``normal``: Box-Muller, unit_open then unit, the cosine branch, no cached
  second value.
- ``log_normal_bounded``: up to 16 draws, then the 16th is clamped.
- ``gamma``: Marsaglia-Tsang, u from unit_open; shape < 1 by the
  gamma(shape + 1) * U^(1/shape) boost, U drawn after.
- ``beta``: gamma(a) then gamma(b).
- ``categorical``: one unit(), cumulative, left to right.
"""

import math

MASK = (1 << 64) - 1
GOLDEN = 0x9E3779B97F4A7C15
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3

MAX_BOUNDED_ATTEMPTS = 16
MAX_POISSON_MEAN = 30.0


# -- GenSeed ----------------------------------------------------------------

def finalise(z):
    z &= MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return z ^ (z >> 31)


def mix(value):
    return finalise((value + GOLDEN) & MASK)


def label(text):
    h = FNV_OFFSET
    for byte in text.encode("ascii"):
        h = ((h ^ byte) * FNV_PRIME) & MASK
    return h


def derive(parent, purpose, index=0):
    return mix(parent ^ mix(purpose ^ mix(index & MASK)))


def hash_coord(x, y, z):
    # int64 -> uint64 is two's complement, which masking a Python int is.
    h = mix(x & MASK)
    h = mix(h ^ (y & MASK))
    return mix(h ^ (z & MASK))


def to_int64(u):
    u &= MASK
    return u - (1 << 64) if u >> 63 else u


# -- FGenStream -------------------------------------------------------------

class Stream:
    def __init__(self, seed):
        self.state = seed & MASK

    def next_u64(self):
        self.state = (self.state + GOLDEN) & MASK
        return finalise(self.state)

    def unit(self):
        return (self.next_u64() >> 11) * 2.0 ** -53

    def unit_open(self):
        return ((self.next_u64() >> 12) + 0.5) * 2.0 ** -52

    def uniform_int(self, lo, hi):
        if lo > hi:
            raise ValueError("uniform_int: lo > hi")
        span = (hi - lo + 1) & MASK
        if span == 0:
            return to_int64(self.next_u64())
        m = self.next_u64() * span
        low = m & MASK
        if low < span:
            threshold = ((1 << 64) - span) % span
            while low < threshold:
                m = self.next_u64() * span
                low = m & MASK
        return to_int64((lo & MASK) + (m >> 64))

    def chance(self, p):
        return self.unit() < p

    def poisson(self, mean, max_count):
        if not 0.0 <= mean <= MAX_POISSON_MEAN:
            raise ValueError("poisson: mean outside [0, 30]")
        limit = math.exp(-mean)
        k = 0
        p = self.unit()
        while p > limit and k < max_count:
            k += 1
            p *= self.unit()
        return k

    def normal(self, mean, std_dev):
        u1 = self.unit_open()
        u2 = self.unit()
        return mean + std_dev * (math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2))

    def log_normal(self, median, sigma):
        return median * math.exp(sigma * self.normal(0.0, 1.0))

    def log_normal_bounded(self, median, sigma, lo, hi):
        if not 0.0 < lo <= hi:
            raise ValueError("log_normal_bounded: need 0 < lo <= hi")
        x = 0.0
        for _ in range(MAX_BOUNDED_ATTEMPTS):
            x = self.log_normal(median, sigma)
            if lo <= x <= hi:
                return x
        return min(max(x, lo), hi)

    def exponential(self, mean):
        return -mean * math.log(self.unit_open())

    def pareto_bounded(self, alpha, lo, hi):
        if not (alpha > 0.0 and 0.0 < lo < hi):
            raise ValueError("pareto_bounded: need alpha > 0 and 0 < lo < hi")
        u = self.unit_open()
        tail = math.pow(lo / hi, alpha)
        x = lo * math.pow(1.0 - u * (1.0 - tail), -1.0 / alpha)
        return min(max(x, lo), hi)

    def gamma(self, shape):
        if not shape > 0.0:
            raise ValueError("gamma: shape must be positive")
        if shape < 1.0:
            g = self.gamma(shape + 1.0)
            return g * math.pow(self.unit_open(), 1.0 / shape)
        d = shape - 1.0 / 3.0
        c = 1.0 / math.sqrt(9.0 * d)
        while True:
            while True:
                x = self.normal(0.0, 1.0)
                v = 1.0 + c * x
                if v > 0.0:
                    break
            v = v * v * v
            u = self.unit_open()
            if u < 1.0 - 0.0331 * (x * x) * (x * x):
                return d * v
            if math.log(u) < 0.5 * x * x + d * (1.0 - v + math.log(v)):
                return d * v

    def beta(self, a, b):
        x = self.gamma(a)
        y = self.gamma(b)
        return x / (x + y)

    def categorical(self, weights):
        total = 0.0
        for w in weights:
            if not (w >= 0.0 and math.isfinite(w)):
                raise ValueError("categorical: weights must be finite and non-negative")
            total += w
        if not total > 0.0:
            raise ValueError("categorical: weights sum to zero")
        target = self.unit() * total
        acc = 0.0
        for i, w in enumerate(weights):
            acc += w
            if target < acc:
                return i
        # Only reachable through rounding in the running sum.
        return max(i for i, w in enumerate(weights) if w > 0.0)
