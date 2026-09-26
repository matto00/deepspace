#!/usr/bin/env bash
# Prove a test can fail: break the code it covers and watch it go red.
#
#   Tools/mutate.sh FILE 'exact old text' 'new text' TESTFILTER
#
# Exits 0 if the mutant was killed (a test in TESTFILTER completed with a
# failure), 1 if it SURVIVED -- the test is vacuous -- and 2 if the mutation
# proves nothing: the text was not found, the mutant did not compile, the
# library was not rebuilt, or the run went red without any test failing (the
# editor died, or the filter matched nothing).
#
# Each of those has happened here. A mutant that fails to compile
# leaves the old library in place, so the tests pass and look like a
# survivor; a mutation whose search text did not match changes nothing, so
# the tests pass and look like a survivor. So the diff is checked, the build
# must succeed, and the library must be newer than the edit, before any
# verdict is read. The file is always restored, and the working tree must be
# clean of it afterwards. Rebuild before trusting the library again: the
# last build contained the mutant.
set -u
cd "$(dirname "$0")/.."
F=$1; OLD=$2; NEW=$3; FILTER=$4
# The file is restored with git checkout, which discards every uncommitted
# edit to it along with the mutant. That has already cost one agent its
# work, so refuse rather than restore over anything but HEAD.
git diff --quiet HEAD -- "$F" || { echo "UNCOMMITTED CHANGES IN $F -- commit first; restoring would discard them"; exit 2; }
python3 - "$F" "$OLD" "$NEW" <<'PY' || { echo "MUTANT NOT APPLIED (pattern missing)"; exit 2; }
import sys
f,old,new=sys.argv[1:]
s=open(f).read()
if s.count(old)!=1: sys.exit("pattern count %d" % s.count(old))
open(f,"w").write(s.replace(old,new))
PY
git diff --quiet -- "$F" && { echo "MUTANT NOT IN FILE"; exit 2; }
echo "diff: $(git diff -- "$F" | grep -E '^[+-][^+-]' | tr '\n' ' ' | cut -c1-200)"
if ! ./build.sh > Saved/mutant-build.log 2>&1; then
    echo "MUTANT DID NOT COMPILE -- verdict meaningless"; git checkout -q -- "$F"; exit 2
fi
lib=Binaries/Linux/libUnrealEditor-DeepSpace.so
[[ $lib -nt $F ]] || { echo "LIBRARY OLDER THAN MUTANT -- not rebuilt"; git checkout -q -- "$F"; exit 2; }
if ./test.sh "$FILTER" > Saved/mutant-test.log 2>&1; then
    echo "SURVIVED: $FILTER stayed GREEN under the mutant  <-- vacuous"
    verdict=1
elif grep -qE "Test Completed\. Result=\{(Fail|Error)" Saved/Logs/DeepSpace.log; then
    echo "KILLED: $FILTER went red"; grep -E "passed:|Result=\{Fail" Saved/mutant-test.log | head -3
    verdict=0
else
    # Red with no failing test in the log: the editor died, or nothing ran.
    # A crash is not the test catching the mutant, and counting it as one has
    # already passed off a survivor as killed.
    echo "NO VERDICT: $FILTER ran no test to a failure -- the run died or matched nothing"
    grep -E "passed:|no tests ran" Saved/mutant-test.log | head -3
    verdict=2
fi
git checkout -q -- "$F"; git diff --quiet -- "$F" && echo "restored clean"
exit $verdict
