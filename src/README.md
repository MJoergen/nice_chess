# src

- `engine/`: the engine core. `engine.h` is its interface; `internal.h`
  is what its files share. It does no I/O, reads no clock, and calls no
  malloc, so it builds for NICE and for the host alike.
  - `board.c`: the position, the Zobrist keys, and attacks.
  - `movegen.c`: move generation, and the global move stack.
  - `makemove.c`: make and undo, which keep the material, the king
    squares, and the Zobrist key up to date.
  - `fen.c`: FEN input and output, and moves in coordinate notation.
  - `eval.c`, `search.c`: the evaluation, the search, and perft.
  - `game.c`: the game as the front ends see it, and its result.
- `ui/text.c`: the text front end, on the console. `help` lists its
  commands.

The engine started as `runtime/libc/test/mchess.c` of
[nice_cpu](https://github.com/MJoergen/nice_cpu) at tag `V1.2`, by the
same author, under the MIT license. Castling, en passant, FEN, and the
Zobrist keys follow [mchess2](https://github.com/MJoergen/mchess2) at
commit `65e8855`, which is not copied here.
