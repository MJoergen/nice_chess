#!/bin/sh
# rate.sh: a rating of each level of the board's engine, on the CCRL
# 40/4 scale.
#
# cutechess-cli plays a gauntlet: build/host/chess_fast at each level
# of $LEVELS against Stockfish at each UCI_Elo of $ELOS, $GAMES games
# a pairing, from the openings of $BOOK (a .pgn or .epd file), each
# opening with both colors. Then Ordo rates the levels in one pool,
# with each Stockfish anchored at its UCI_Elo.
#
# A level gets "go nodes N" for each move: $NODES times its share of
# the time of go in the windowed front end. The engine plays the same
# moves as the NICE builds at the same nodes, and 172000 nodes is about
# 5 seconds a move on the MEGA65, the time of go and of level 3
# (LOG.md). It gets the level's noise too, with the UCI option Noise.
# The shares and the noise, below, are a copy of the table of the
# levels in src/ui/screen.c, which tools/test_levels.py compares with
# it. Stockfish gets 60+0.6, the time control its UCI_Elo was fitted
# at, to CCRL 40/4.
#
# Each strength of $STRENGTHS (none by default) plays too, with the UCI
# option Strength, at 3/2 of $NODES a move: about 7.5 seconds on the
# MEGA65, the engine's time for its first move at Blitz 3+2. Below
# full strength the engine takes its share of those nodes.
#
# Each of $OPPONENTS (none by default), NAME=ELO=COMMAND, is another
# UCI engine at its own default settings and Stockfish's time control,
# anchored at ELO: an engine of the CCRL 40/4 list, such as one rated
# below Stockfish's lowest UCI_Elo, 1320. COMMAND has no spaces.
#
# The settings come from the environment, as the Makefile exports them:
# CUTECHESS, ORDO, STOCKFISH (commands), BOOK, NODES, LEVELS, STRENGTHS,
# ELOS, OPPONENTS, GAMES, CONCURRENCY. An empty LEVELS rates no level.
# The games go to $PGN (build/rate/games.pgn by default; make rate
# gives each run a file of its own in games/), and the rating to
# build/rate/ratings.txt. A run starts $PGN anew.
# It plays a copy of the engine in build/rate/, so that a build during
# the hours of a run does not change the player.
set -eu
: "${BOOK:=deps/books/8moves_v3.pgn}"
: "${CUTECHESS:=cutechess-cli}" "${ORDO:=ordo}" "${STOCKFISH:=stockfish}"
: "${NODES:=172000}" "${LEVELS=1 2 3 4 5}" "${STRENGTHS=}" "${ELOS:=1600 1800 2000 2200}" "${OPPONENTS=}"
: "${GAMES:=100}" "${CONCURRENCY:=4}" "${PGN:=build/rate/games.pgn}"

case "$BOOK" in
    *.epd) format=epd ;;
    *.pgn) format=pgn ;;
    *) echo "rate.sh: BOOK must be a .pgn or .epd file" >&2; exit 1 ;;
esac

dir=build/rate
mkdir -p $dir
rm -f "$PGN" $dir/anchors.csv
cp build/host/chess_fast $dir/chess_fast

# The levels are the seeds of the gauntlet: each plays each Stockfish.
set --
seeds=0
for level in $LEVELS; do
    case $level in
        1) nodes=$((NODES / 9)) noise=800 ;;
        2) nodes=$((NODES / 3)) noise=600 ;;
        3) nodes=$NODES noise=320 ;;
        4) nodes=$((NODES * 3)) noise=170 ;;
        5) nodes=$((NODES * 9)) noise=0 ;;
        *) echo "rate.sh: LEVELS must be of 1 to 5" >&2; exit 1 ;;
    esac
    set -- "$@" -engine name=level$level cmd=$dir/chess_fast proto=uci tc=inf nodes=$nodes option.Noise=$noise
    seeds=$((seeds + 1))
done
for strength in $STRENGTHS; do
    case $strength in
        [1-5]) ;;
        *) echo "rate.sh: STRENGTHS must be of 1 to 5" >&2; exit 1 ;;
    esac
    set -- "$@" -engine name=strength$strength cmd=$dir/chess_fast proto=uci tc=inf \
        nodes=$((NODES * 3 / 2)) option.Strength=$strength
    seeds=$((seeds + 1))
done
if [ $seeds -eq 0 ]; then
    echo "rate.sh: nothing to rate: LEVELS and STRENGTHS are empty" >&2
    exit 1
fi
for elo in $ELOS; do
    set -- "$@" -engine name=sf$elo cmd="$STOCKFISH" proto=uci tc=60+0.6 \
        option.UCI_LimitStrength=true option.UCI_Elo=$elo option.Threads=1 option.Hash=16
    echo "\"sf$elo\",$elo" >> $dir/anchors.csv
done
for opponent in $OPPONENTS; do
    name=${opponent%%=*} rest=${opponent#*=}
    elo=${rest%%=*} cmd=${rest#*=}
    case $elo in
        ''|*[!0-9]*) echo "rate.sh: OPPONENTS must be NAME=ELO=COMMAND" >&2; exit 1 ;;
    esac
    set -- "$@" -engine name=$name cmd="$cmd" proto=uci tc=60+0.6
    echo "\"$name\",$elo" >> $dir/anchors.csv
done

# Each pairing plays GAMES games: GAMES/2 openings, each twice.
"$CUTECHESS" -tournament gauntlet -seeds $seeds "$@" \
    -event "rate.sh NODES=$NODES LEVELS=$LEVELS STRENGTHS=$STRENGTHS ELOS=$ELOS" \
    -openings file="$BOOK" format=$format order=random -repeat -games 2 \
    -rounds $((GAMES / 2)) -concurrency "$CONCURRENCY" -recover \
    -resign movecount=3 score=1000 -draw movenumber=40 movecount=8 score=10 \
    -maxmoves 200 -pgnout "$PGN"

# Ordo 1.2.6 does not fix the rating of an anchor of -m when it is the
# only one, so one anchor is given by -A and -a.
if [ $(wc -l < $dir/anchors.csv) -eq 1 ]; then
    IFS=, read -r name elo < $dir/anchors.csv
    name=${name#\"} name=${name%\"}
    anchors="-A $name -a $elo"
else
    anchors="-m $dir/anchors.csv"
fi
"$ORDO" -p "$PGN" $anchors -W -D -s 1000 -o $dir/ratings.txt
cat $dir/ratings.txt
