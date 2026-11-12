#!/bin/sh
# lichess_engine.sh: the engine on the MEGA65, as lichess-bot runs it.
#
# lichess-bot runs an engine as one command, with no arguments of its
# own, from its own directory. This script runs tools/board_uci.py on
# the text front end built for the monitor
# (build/mega65/chess_text.bin), from the root of nice_chess, so the
# paths hold wherever lichess-bot runs it. tools/lichess-bot.yml is the
# template of lichess-bot's config.
#
# The link to the board is $BOARD_ETH (HOST[:PORT]), or else
# $BOARD_PORT (a device, /dev/ttyUSB3 by default). $NICEMON is the
# checkout of NiceMonMEGA65 (default ../NiceMonMEGA65, from the root).
# lichess-bot starts the engine for each game, so the program is
# loaded into the board for each game.

set -e
cd "$(dirname "$0")/.."
NICEMON=${NICEMON:-../NiceMonMEGA65}
export NICEMON
if [ -n "$BOARD_ETH" ]; then
    set -- --eth "$BOARD_ETH"
else
    set -- --port "${BOARD_PORT:-/dev/ttyUSB3}"
fi
exec "${PYTHON:-python3}" tools/board_uci.py "$@" build/mega65/chess_text.bin
