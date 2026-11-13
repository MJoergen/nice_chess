#!/bin/sh
# lichess_engine.sh: the engine on the MEGA65, as lichess-bot runs it.
#
# lichess-bot runs an engine as one command, with no arguments of its
# own, from its own directory. This script runs tools/screen_uci.py on
# the windowed front end built for the monitor
# (build/mega65/chess_screen.bin), from the root of nice_chess, so the
# paths hold wherever lichess-bot runs it. So the MEGA65's screen shows
# the game. The text front end (build/mega65/chess_text.bin) plays on
# if the windowed one cannot. tools/lichess-bot.yml is the template of
# lichess-bot's config.
#
# The link to the board is $BOARD_ETH (HOST[:PORT]), or else
# $BOARD_PORT (a device, /dev/ttyUSB3 by default). $NICEMON is the
# checkout of NiceMonMEGA65 (default ../NiceMonMEGA65, from the root).
# lichess-bot starts the engine for each game, so the program is
# loaded into the board for each game. With $BOARD_DEBUG set, the
# script prints lichess-bot's lines, the keys it presses, and the front
# end's moves to stderr.

set -e
cd "$(dirname "$0")/.."
NICEMON=${NICEMON:-../NiceMonMEGA65}
export NICEMON
if [ -n "$BOARD_ETH" ]; then
    set -- --eth "$BOARD_ETH"
else
    set -- --port "${BOARD_PORT:-/dev/ttyUSB3}"
fi
exec "${PYTHON:-python3}" tools/screen_uci.py "$@" ${BOARD_DEBUG:+--debug} \
    --text build/mega65/chess_text.bin build/mega65/chess_screen.bin
