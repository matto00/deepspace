#!/usr/bin/env bash
# Prove a test can fail: break the code it covers and watch it go red.
#
#   Tools/mutate.sh FILE 'exact old text' 'new text' TESTFILTER
#
# Exits 0 if the mutant was killed (TESTFILTER went red), 1 if it SURVIVED --
# the test is vacuous -- and 2 if the mutation proves nothing: the text was
# not found, the mutant did not compile, or the library was not rebuilt.
#
# Each of those three has happened here. A mutant that fails to compile
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
else
    echo "KILLED: $FILTER went red"; grep -E "passed:|Result=\{Fail" Saved/mutant-test.log | head -3
    verdict=0
fi
git checkout -q -- "$F"; git diff --quiet -- "$F" && echo "restored clean"
exit $verdict
