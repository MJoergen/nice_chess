#!/bin/sh
# runtest.sh NAME CHECK INPUT host|nice PROGRAM [ARGS...]
#
# Runs PROGRAM with INPUT as its console input, on the host or in
# niceemu ($NICEEMU), and leaves its output in build/test/NAME.raw.
# The lines that match a pattern of tests/CHECK.grep (with the digits
# at the end of CHECK left out: perftsuite3 uses perftsuite.grep), after the
# prompts "> " and the times of info lines (" time N") are taken off, and
# then "exit N" with main's result,
# must be tests/CHECK.expected. Prints PASS or FAIL, and exits with 1
# on a failure.
set -u
name=$1 check=$2 input=$3 kind=$4 prog=$5
shift 5
dir=build/test
mkdir -p $dir
raw=$dir/$name.raw
out=$dir/$name.out

if [ "$kind" = nice ]; then
    "$NICEEMU" -i "$input" "$prog" < /dev/null > $raw
    r1=$(grep -o 'R1  [0-9a-f]*' $raw | cut -c5-)
    if grep -q 'Halted (HALT)' $raw && [ -n "$r1" ]; then
        status=$(( 0x$r1 ))
    else
        status="none (the program did not halt)"
    fi
else
    "$prog" "$@" < "$input" > $raw
    status=$?
fi

{ sed 's/^\(> \)*//; s/ time [0-9]*//' $raw | grep -f tests/${check%%[0-9]*}.grep; echo "exit $status"; } > $out
if cmp -s $out tests/$check.expected; then
    echo "PASS $name"
else
    echo "FAIL $name"
    diff tests/$check.expected $out
    exit 1
fi
