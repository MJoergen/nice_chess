# The board as a lichess bot

This is how to play the engine on the MEGA65 on lichess.org, as a BOT
account, with [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot).
lichess-bot runs on a PC. It talks to lichess, and starts
`tools/lichess_engine.sh` for each game. That script loads the
windowed front end into the board over the link, and plays it through
its keys (`tools/screen_uci.py`): it types the opponent's moves, and
reads the engine's from the console. So the MEGA65's screen shows the
game as it is played.

## What it needs

- The MEGA65 under NiceMonMEGA65's monitor, reached over its Ethernet
  port or its debug UART, and a checkout of NiceMonMEGA65 (`NICEMON`,
  `../NiceMonMEGA65` by default). See [README.md](README.md).
- nice_chess built: `make deps`, then `make all`. The bot plays
  `build/mega65/chess_screen.bin`, not a file on the SD card, and
  `build/mega65/chess_text.bin` if the windowed front end cannot go on.
- Python 3.11 or later, for lichess-bot.
- A lichess account for the bot that has never played a game.

## Setting it up

This is done once.

1. Install lichess-bot, next to nice_chess or anywhere else:

       git clone https://github.com/lichess-bot-devs/lichess-bot.git
       cd lichess-bot
       python3 -m venv venv
       . venv/bin/activate
       python3 -m pip install -r requirements.txt

2. Make an account for the bot on lichess.org. If you already have a
   lichess account for yourself, the bot's is a new one: an account
   that has played games cannot become a BOT account. lichess takes
   each e-mail address for one account only, so the bot's account
   needs an e-mail address other than yours. Logged in as it, create
   an API token with the scope `bot:play`, at
   <https://lichess.org/account/oauth/token/create?scopes[]=bot:play&description=lichess-bot>.
   lichess shows the token once; save it.

3. Copy `tools/lichess-bot.yml` of nice_chess to `config.yml` in
   lichess-bot. Set `engine.dir` to the absolute path of nice_chess's
   `tools/` directory. Put the token in the environment variable
   `LICHESS_BOT_TOKEN`, or in `token` in `config.yml`. `config.yml`
   is in lichess-bot's `.gitignore`.

4. Upgrade the account to a BOT account. This cannot be undone:

       LICHESS_BOT_TOKEN=... python3 lichess-bot.py -u

   It then starts the bot, as in [Running it](#running-it); Ctrl-C
   stops it.

## Checking the engine on the board

From nice_chess:

    printf 'uci\nisready\nposition startpos\ngo movetime 3000\nquit\n' |
        BOARD_ETH=192.168.1.65 tools/lichess_engine.sh

It prints `id name ...`, `uciok`, `readyok`, and after 3 seconds a
`bestmove`, and the MEGA65's screen shows the board with white's first
move. `BOARD_ETH` is the board's Ethernet port, as HOST[:PORT].
Without it, the script uses the debug UART in `BOARD_PORT`, such as
`/dev/ttyUSB1`. The load resets the board, so a program that runs
there, such as the front end of the last game, ends. With
`BOARD_DEBUG=1` the script also prints the keys it presses and the
front end's moves.

## Running it

From lichess-bot:

    . venv/bin/activate
    BOARD_ETH=192.168.1.65 LICHESS_BOT_TOKEN=... python3 lichess-bot.py -v

It logs in, and waits for challenges. The bot is online on lichess
while it runs, and offline when it stops. Ctrl-C stops it.

To test it, challenge the bot from another lichess account, to a
casual game at 5+3. The log shows the game start, the load of the
program into the board, and the moves.

## Who plays it

With `tools/lichess-bot.yml` as it is, the bot accepts challenges from
people and from other bots, of standard chess, rated or casual, with
a base time of 5 to 30 minutes and an increment of 0 to 20 seconds. It
declines other challenges, and those that come during a game: it plays
one game at a time.

It does not challenge others, as `matchmaking.allow_matchmaking` is
false. Its games come from other bots, many of which challenge bots
that are online, and from people, who find it on its profile or in
the list of online bots, <https://lichess.org/player/bots>. True there
makes it challenge other bots too; lichess-bot's `config.yml.default`
has the other keys of `matchmaking`.

## How it plays

The engine plays in the windowed front end, at level 5, its full
strength, from its book first (`OwnBook`). `Level` (1 to 5) under
`engine.uci_options` makes it weaker, by the noise of the level and
the time of its move; README.md has the levels and their ratings.

lichess keeps the clock of the game. The front end has no time control
here: the clocks on the screen count up the time each side has taken.
lichess-bot gives the engine its clock with each move, less
`move_overhead`, 2 seconds, for the link. `screen_uci.py` gives the
move a thirtieth of the engine's time and three quarters of its
increment, which is the most the text front end takes, and then
presses Esc, which makes the engine move. A move takes no more than
the time of the level, 45 seconds at level 5.

The engine ponders, as the windowed front end does: it thinks on the
opponent's time, about the move it expects. `engine.ponder` stays
false. That is lichess-bot's pondering, by UCI's `go ponder`, which
`screen_uci.py` does not take.

The opponent's side is at the bottom of the screen, as the player's
is. After a game the front end stays, with the last position on the
screen, until the next game is loaded, or Q on the MEGA65's keyboard
ends it. The clock of the side to move runs on there if the game ended
in a way the front end does not see, such as a resignation.

A game that goes on after lichess-bot starts again is set up through
the keys, in a few seconds. If the windowed front end cannot go on
with a game, the script loads the text front end, which plays on, and
the screen no longer shows the game: that is so after a key pressed on
the MEGA65's keyboard has changed the game there.

A new bot's rating is 1500, and moves to its strength over its first
games. Leave `rated` out of `challenge.modes` to play casual games
only.

## While it runs

- The board is the bot's. lichess-bot loads the program into it at
  the start of each game, so run nothing else on it. Stop the bot
  first.
- The MEGA65's keyboard is the game's: a key pressed there goes to the
  front end, as the script's keys do. Leave it alone during a game.
- The PC and the link stay up. A game whose link drops is lost on
  time.
- Losses on time again and again, or errors of the engine in the
  log, point at the board. Stop the bot, and run
  `make board-test ETH=192.168.1.65` (or `PORT=DEVICE`).
