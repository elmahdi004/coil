#!/bin/bash -eu
#
# ClusterFuzzLite build for Coil.
#
# Compiles every src/*.c into objects and links each fuzz target against the
# whole set.  As milestones land, drop the new fuzz/<name>_fuzzer.c in and add
# its bare name to TARGETS below.

cd "$SRC"

OBJS=""
for f in src/*.c; do
    [ -e "$f" ] || continue
    o="$(basename "$f" .c).o"
    $CC $CFLAGS -DCOIL_FUZZ -I"$SRC/src" -c "$f" -o "$o"
    OBJS="$OBJS $o"
done

TARGETS="lexer source bytecode json"

for t in $TARGETS; do
    [ -e "fuzz/${t}_fuzzer.c" ] || continue
    $CC $CFLAGS -DCOIL_FUZZ $LIB_FUZZING_ENGINE \
        "fuzz/${t}_fuzzer.c" $OBJS \
        -I"$SRC/src" -lm \
        -o "$OUT/${t}_fuzzer"
done
