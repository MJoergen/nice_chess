# The board as a lichess bot

This is how to play the engine on the MEGA65 on lichess.org, as a BOT
account, with [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot).
lichess-bot runs on a PC. It talks to lichess, and starts
`tools/lichess_engine.sh` for each game. That script loads the text
front end into the board over the link, and passes the UCI lines both
ways (`tools/board_uci.py`).

## What it needs

- The MEGA65 under NiceMonMEGA65's monitor, reached over its Ethernet
  port or its debug UART, and a checkout of NiceMonMEGA65 (`NICEMON`,
  `../NiceMonMEGA65` by default). See [README.md](README.md).
- nice_chess built: `make deps`, then `make all`. The bot plays
  `build/mega65/chess_text.bin`, not a file on the SD card.
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

The board must be at the monitor's prompt, with no program running:
quit a front end that runs (Q in the windowed one, `quit` in the text
one). Then, from nice_chess:

    printf 'uci\nisready\nposition startpos\ngo movetime 3000\nquit\n' |
        BOARD_ETH=192.168.1.65 tools/lichess_engine.sh

It prints `id name ...`, `uciok`, `readyok`, lines of `info`, and a
`bestmove`. `BOARD_ETH` is the board's Ethernet port, as HOST[:PORT].
Without it, the script uses the debug UART in `BOARD_PORT`, such as
`/dev/ttyUSB1`.

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

The engine plays from its book first (`OwnBook`), at full strength.
`Strength` (1 to 5) and `Noise` (0 to 1000 centipawns) under
`engine.uci_options` make it weaker; README.md has their ratings.
lichess-bot takes `move_overhead`, 2 seconds, off the time of each
move, for the link. The engine does not ponder: the board does not
read its input while it searches.

A new bot's rating is 1500, and moves to its strength over its first
games. Leave `rated` out of `challenge.modes` to play casual games
only.

## While it runs

- The board is the bot's. lichess-bot loads the program into it at
  the start of each game, so run nothing else on it. Stop the bot
  first.
- The PC and the link stay up. A game whose link drops is lost on
  time.
- Losses on time again and again, or errors of the engine in the
  log, point at the board. Stop the bot, and run
  `make board-test ETH=192.168.1.65` (or `PORT=DEVICE`).
