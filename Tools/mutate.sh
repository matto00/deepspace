#!/usr/bin/env bash
# Prove a test can fail: break the code it covers and watch it go red.
#
#   Tools/mutate.sh FILE 'exact old text' 'new text' TESTFILTER
#   MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh FILE OLD NEW Eyes.<Name>   (a rendered check)
#   MUTATE_EXPECT='text of one assertion' Tools/mutate.sh ...              (that assertion must catch it)
#
# Exits 0 if the mutant was killed (a test in TESTFILTER completed with a
# failure), 1 if it SURVIVED -- the test is vacuous -- and 2 if the mutation
# proves nothing: the text was not found, the mutant did not compile, the
# library was not rebuilt, the run went red without any test failing (the
# editor died, or the filter matched nothing), or the filter was already red
# before the mutant.
#
# The baseline is run first, on the unmutated code. A test that is red
# already goes red under any mutant, so its KILLED would say nothing: the
# landing's Eyes.HandoverParity reported a mutant killed while it was failing
# on the frames themselves. So the baseline must be green -- or, with
# MUTATE_EXPECT, the named assertion must pass on the baseline and fail under
# the mutant (an "Error:" line in the log containing that text), and only
# that counts as a kill.
#
# Each of those has happened here. A mutant that fails to compile
# leaves the old library in place, so the tests pass and look like a
# survivor; a mutation whose search text did not match changes nothing, so
# the tests pass and look like a survivor. So the diff is checked, the build
# must succeed, and the library must be newer than the edit, before any
# verdict is read. The file is always restored, and the working tree must be
# clean of it afterwards.
#
# The build is FORCED, both ways. UBT's incremental check has been seen to
# relink without recompiling a file that had just been restored with git
# checkout, leaving the mutant's object in the library: a "clean" rebuild that
# still ran mutated code, and a later mutant that was never compiled at all,
# so its test "survived". The edited file's object is deleted before each
# build -- every object, for a header -- and the build log must name the file
# as compiled. The library is rebuilt clean the same way before exit, so
# nothing is left to remember.
set -u
cd "$(dirname "$0")/.."
F=$1; OLD=$2; NEW=$3; FILTER=$4
# The file is restored with git checkout, which discards every uncommitted
# edit to it along with the mutant. That has already cost one agent its
# work, so refuse rather than restore over anything but HEAD.
git diff --quiet HEAD -- "$F" || { echo "UNCOMMITTED CHANGES IN $F -- commit first; restoring would discard them"; exit 2; }
EXPECT=${MUTATE_EXPECT:-}
# The expected assertion's failure lines in this run's log, if any.
expected_failed() { grep -F "Error:" Saved/Logs/DeepSpace.log | grep -qF -- "$EXPECT"; }
./build.sh > Saved/mutant-baseline-build.log 2>&1 || { echo "BASELINE DOES NOT BUILD -- fix the tree first"; exit 2; }
if "${MUTATE_RUNNER:-./test.sh}" "$FILTER" > Saved/mutant-baseline.log 2>&1; then
    echo "baseline: $FILTER green"
elif [[ -n $EXPECT ]] && grep -qE "Test Completed\. Result=\{(Fail|Error)" Saved/Logs/DeepSpace.log && ! expected_failed; then
    echo "baseline: $FILTER red, but not on '$EXPECT' -- only that assertion counts"
else
    echo "BASELINE RED: $FILTER fails before any mutant${EXPECT:+ (on '$EXPECT')} -- a kill would prove nothing"
    grep -E "Error: Expected" Saved/Logs/DeepSpace.log | head -3
    exit 2
fi
python3 - "$F" "$OLD" "$NEW" <<'PY' || { echo "MUTANT NOT APPLIED (pattern missing)"; exit 2; }
import sys
f,old,new=sys.argv[1:]
s=open(f).read()
if s.count(old)!=1: sys.exit("pattern count %d" % s.count(old))
open(f,"w").write(s.replace(old,new))
PY
git diff --quiet -- "$F" && { echo "MUTANT NOT IN FILE"; exit 2; }
echo "diff: $(git diff -- "$F" | grep -E '^[+-][^+-]' | tr '\n' ' ' | cut -c1-200)"
OBJ=Intermediate/Build/Linux/x64/UnrealEditor/Development/DeepSpace
# Build with F's object gone, and prove F was compiled. Returns non-zero if
# the build failed or did not compile F.
forced_build() {
    case "$F" in
        *.cpp) rm -f "$OBJ/$(basename "$F").o" ;;
        *)     rm -f "$OBJ"/*.cpp.o ;;
    esac
    ./build.sh > "$1" 2>&1 || return 1
    case "$F" in
        *.cpp) grep -qF "Compile $(basename "$F")" "$1" ;;
        *)     grep -q "\] Compile " "$1" ;;
    esac
}
restore() {
    git checkout -q -- "$F"
    if forced_build Saved/mutant-restore.log; then
        git diff --quiet -- "$F" && echo "restored clean, library rebuilt from the restored source"
    else
        echo "!!! RESTORE REBUILD FAILED -- the library may still hold the mutant; run ./build.sh"
    fi
}
if ! forced_build Saved/mutant-build.log; then
    if grep -q "Result: Succeeded" Saved/mutant-build.log; then
        echo "MUTANT NOT COMPILED -- the build did not compile $(basename "$F"); verdict meaningless"
    else
        echo "MUTANT DID NOT COMPILE -- verdict meaningless"
    fi
    restore; exit 2
fi
# MUTATE_RUNNER=Tools/eyes.sh proves a rendered check (Eyes.*) the same way.
if "${MUTATE_RUNNER:-./test.sh}" "$FILTER" > Saved/mutant-test.log 2>&1; then
    echo "SURVIVED: $FILTER stayed GREEN under the mutant  <-- vacuous"
    verdict=1
elif [[ -n $EXPECT ]] && grep -qE "Test Completed\. Result=\{(Fail|Error)" Saved/Logs/DeepSpace.log && ! expected_failed; then
    # Red, but the assertion named held: it did not catch the mutant.
    echo "SURVIVED: '$EXPECT' held under the mutant ($FILTER red elsewhere)  <-- vacuous"
    grep -E "Error: Expected" Saved/Logs/DeepSpace.log | head -3
    verdict=1
elif grep -qE "Test Completed\. Result=\{(Fail|Error)" Saved/Logs/DeepSpace.log; then
    echo "KILLED: $FILTER went red${EXPECT:+ on '$EXPECT'}"; grep -E "passed:|Result=\{Fail" Saved/mutant-test.log | head -3
    verdict=0
else
    # Red with no failing test in the log: the editor died, or nothing ran.
    # A crash is not the test catching the mutant, and counting it as one has
    # already passed off a survivor as killed.
    echo "NO VERDICT: $FILTER ran no test to a failure -- the run died or matched nothing"
    grep -E "passed:|no tests ran" Saved/mutant-test.log | head -3
    verdict=2
fi
restore
exit $verdict
