#!/bin/sh
# runtest.sh NAME CHECK INPUT host|nice|screen|board|board-screen PROGRAM [ARGS...]
#
# Runs PROGRAM with INPUT as its console input, on the host, in
# niceemu ($NICEEMU, with --step-time, so that its clock counts steps),
# or on the board under its monitor (board, through tools/board.py,
# with the link in $BOARDLINK, such as "--eth 192.168.1.65"),
# and leaves its output in build/test/NAME.raw.
# screen is nice with --color, so that the screen at the halt, with
# its colors, is in the output too, and board-screen is board with it.
# On the board the clock is real, so a timed search, its figures, and
# the move it finds, differ from the emulator's: board-screen leaves
# out of the comparison the rows that show the figures, and the moves,
# the FEN, the move list, and the board's squares.
# The lines that match a pattern of tests/CHECK.grep (with the digits
# at the end of CHECK left out: perftsuite3 uses perftsuite.grep), after the
# prompts "> " and the times of info lines (" time N") are taken off, and
# then "exit N" with main's result,
# with the figures of the players' clocks on the screen as m:ss, and
# the version that follows "nice_chess " ($VERSION, which the Makefile
# exports) as VERSION, must be tests/CHECK.expected. Prints PASS or FAIL, and exits with 1
# on a failure.
set -u
name=$1 check=$2 input=$3 kind=$4 prog=$5
shift 5
dir=build/test
mkdir -p $dir
raw=$dir/$name.raw
out=$dir/$name.out

if [ "$kind" = board ] || [ "$kind" = board-screen ]; then
    color=
    [ "$kind" = board-screen ] && color=--color
    python3 tools/board.py $BOARDLINK $color -i "$input" "$prog" < /dev/null > $raw
    r=$(grep -o '^Returned [0-9a-f]*' $raw | cut -c10-)
    if [ -n "$r" ]; then
        status=$(( 0x$r ))
    else
        status="none (the program did not return to the monitor)"
    fi
elif [ "$kind" = nice ] || [ "$kind" = screen ]; then
    color=
    [ "$kind" = screen ] && color=--color
    "$NICEEMU" --step-time $color -i "$input" "$prog" < /dev/null > $raw
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

expected=tests/$check.expected
clocks='/m\(White\|Black\)  /s/[0-9][0-9:]*:[0-9][0-9]/m:ss/'
version="s/nice_chess ${VERSION:?the Makefile exports VERSION}/nice_chess VERSION/g"
{ sed 's/^\(> \)*//; s/ time [0-9]*//' $raw | grep -f tests/${check%%[0-9]*}.grep | sed "$clocks; $version"; echo "exit $status"; } > $out
if [ "$kind" = board-screen ]; then
    # The engine's moves come from timed searches too, so they and what
    # they decide are left out as well: the moves, the FEN, the move list,
    # and the board's squares, the segments whose background is not the
    # panel's (17;34;51, PANEL in src/ui/screen.c).
    p=$(printf '\001') e=$(printf '\033')
    squares="/^|/{s/$e\[48;2;17;34;51;/$p/g; s/$e\[48;2;[0-9;]*m[^$e$p]*//g; s/$p/$e[48;2;17;34;51;/g}"
    timed="/mDepth [0-9]*  Score /d; /m PV /d; /^move /d; /^fen /d; $squares; /m[0-9][0-9]*\. /d"
    expected=$dir/$name.expected
    sed "$timed" tests/$check.expected > $expected
    sed -i "$timed" $out
fi
if cmp -s $out $expected; then
    echo "PASS $name"
else
    echo "FAIL $name"
    diff $expected $out
    exit 1
fi
