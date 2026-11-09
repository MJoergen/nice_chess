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
# the time of go in the windowed front end, 1/9, 1/3, 1, 3, or 9
# (src/ui/screen.c). The engine plays the same moves as the NICE
# builds at the same nodes, and 93000 nodes is about 5 seconds a move
# on the MEGA65, the time of go and of level 3 (LOG.md). Stockfish
# gets 60+0.6, the time control its UCI_Elo was fitted at, to CCRL 40/4.
#
# The settings come from the environment, as the Makefile exports them:
# CUTECHESS, ORDO, STOCKFISH (commands), BOOK, NODES, LEVELS, ELOS,
# GAMES, CONCURRENCY. The games go to build/rate/games.pgn, and the
# rating to build/rate/ratings.txt. A new run starts a new games.pgn.
# It plays a copy of the engine in build/rate/, so that a build during
# the hours of a run does not change the player.
set -eu
: "${BOOK:=deps/books/8moves_v3.pgn}"
: "${CUTECHESS:=cutechess-cli}" "${ORDO:=ordo}" "${STOCKFISH:=stockfish}"
: "${NODES:=93000}" "${LEVELS:=1 2 3 4 5}" "${ELOS:=1600 1800 2000 2200}"
: "${GAMES:=100}" "${CONCURRENCY:=4}"

case "$BOOK" in
    *.epd) format=epd ;;
    *.pgn) format=pgn ;;
    *) echo "rate.sh: BOOK must be a .pgn or .epd file" >&2; exit 1 ;;
esac

dir=build/rate
mkdir -p $dir
rm -f $dir/games.pgn $dir/anchors.csv
cp build/host/chess_fast $dir/chess_fast

# The levels are the seeds of the gauntlet: each plays each Stockfish.
set --
seeds=0
for level in $LEVELS; do
    case $level in
        1) nodes=$((NODES / 9)) ;;
        2) nodes=$((NODES / 3)) ;;
        3) nodes=$NODES ;;
        4) nodes=$((NODES * 3)) ;;
        5) nodes=$((NODES * 9)) ;;
        *) echo "rate.sh: LEVELS must be of 1 to 5" >&2; exit 1 ;;
    esac
    set -- "$@" -engine name=level$level cmd=$dir/chess_fast proto=uci tc=inf nodes=$nodes
    seeds=$((seeds + 1))
done
for elo in $ELOS; do
    set -- "$@" -engine name=sf$elo cmd="$STOCKFISH" proto=uci tc=60+0.6 \
        option.UCI_LimitStrength=true option.UCI_Elo=$elo option.Threads=1 option.Hash=16
    echo "\"sf$elo\",$elo" >> $dir/anchors.csv
done

# Each pairing plays GAMES games: GAMES/2 openings, each twice.
"$CUTECHESS" -tournament gauntlet -seeds $seeds "$@" \
    -openings file="$BOOK" format=$format order=random -repeat -games 2 \
    -rounds $((GAMES / 2)) -concurrency "$CONCURRENCY" -recover \
    -resign movecount=3 score=1000 -draw movenumber=40 movecount=8 score=10 \
    -maxmoves 200 -pgnout $dir/games.pgn

# Ordo 1.2.6 does not fix the rating of an anchor of -m when it is the
# only one, so one anchor is given by -A and -a.
set -- $ELOS
if [ $# -eq 1 ]; then
    anchors="-A sf$1 -a $1"
else
    anchors="-m $dir/anchors.csv"
fi
"$ORDO" -p $dir/games.pgn $anchors -W -D -s 1000 -o $dir/ratings.txt
cat $dir/ratings.txt
