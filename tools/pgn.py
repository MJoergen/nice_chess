"""pgn.py - games as PGN, from moves in coordinate notation.

The referee (match.py's Referee, the text front end) gives the FEN of
each position and its legal moves; san() turns a move into standard
algebraic notation from them, and Pgn appends each game to a file.
So no chess library is needed.
"""

from __future__ import annotations

import datetime

KNIGHT = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]
KING = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
ROOK = [(1, 0), (0, 1), (-1, 0), (0, -1)]
BISHOP = [(1, 1), (-1, 1), (-1, -1), (1, -1)]


def parse_fen(fen: str) -> tuple[dict[tuple[int, int], str], str]:
    """The pieces of a FEN, by (file, rank) from 0, and the side to move."""
    fields = fen.split()
    board = {}
    for i, row in enumerate(fields[0].split("/")):
        f = 0
        for c in row:
            if c.isdigit():
                f += int(c)
            else:
                board[(f, 7 - i)] = c
                f += 1
    return board, fields[1]


def attacked(board: dict[tuple[int, int], str], sq: tuple[int, int], white: bool) -> bool:
    """Whether a piece of white (or black) attacks sq."""
    def at(f: int, r: int) -> str | None:
        return board.get((f, r)) if 0 <= f < 8 and 0 <= r < 8 else None

    def mine(p: str | None, kind: str) -> bool:
        return p is not None and p == (kind.upper() if white else kind)

    f, r = sq
    back = -1 if white else 1  # A pawn attacks from one rank behind
    if mine(at(f - 1, r + back), "p") or mine(at(f + 1, r + back), "p"):
        return True
    if any(mine(at(f + df, r + dr), "n") for df, dr in KNIGHT):
        return True
    if any(mine(at(f + df, r + dr), "k") for df, dr in KING):
        return True
    for dirs, kinds in ((ROOK, "rq"), (BISHOP, "bq")):
        for df, dr in dirs:
            x, y = f + df, r + dr
            while 0 <= x < 8 and 0 <= y < 8:
                p = board.get((x, y))
                if p:
                    if any(mine(p, k) for k in kinds):
                        return True
                    break
                x, y = x + df, y + dr
    return False


def square(s: str) -> tuple[int, int]:
    return ord(s[0]) - ord("a"), ord(s[1]) - ord("1")


def san(fen: str, move: str, legal: list[str], fen_after: str, mate: bool = False) -> str:
    """move, legal in the position of fen, in standard algebraic notation.
    fen_after is the position after it, for the check; mate adds #."""
    board, _ = parse_fen(fen)
    frm, to = square(move[:2]), square(move[2:4])
    piece = board[frm].upper()
    if piece == "K" and abs(to[0] - frm[0]) == 2:
        s = "O-O" if to[0] > frm[0] else "O-O-O"
    elif piece == "P":
        s = ""
        if to[0] != frm[0]:  # A capture, en passant too
            s = move[0] + "x"
        s += move[2:4]
        if len(move) == 5:
            s += "=" + move[4].upper()
    else:
        others = [m for m in legal if m[2:4] == move[2:4] and m[:2] != move[:2]
                  and board[square(m[:2])].upper() == piece]
        s = piece
        if others:
            if all(m[0] != move[0] for m in others):
                s += move[0]
            elif all(m[1] != move[1] for m in others):
                s += move[1]
            else:
                s += move[:2]
        if to in board:
            s += "x"
        s += move[2:4]
    if mate:
        return s + "#"
    after, side = parse_fen(fen_after)
    king = "K" if side == "w" else "k"
    ksq = next(sq for sq, p in after.items() if p == king)
    return s + ("+" if attacked(after, ksq, side == "b") else "")


class Pgn:
    """A file of games, to which each game is appended as it ends."""

    def __init__(self, path: str | None, event: str, site: str):
        self.path = path
        self.event = event
        self.site = site
        self.round = 0

    def write(self, white: str, black: str, result: str, sans: list[str],
              why: str = "", comments: list[str] | None = None,
              tags: dict[str, str] | None = None) -> None:
        """Appends a game: its moves in SAN, a comment after each move,
        such as a score, if comments has one, and why it ended, as a
        comment before the result."""
        if not self.path:
            return
        self.round += 1
        head = {
            "Event": self.event,
            "Site": self.site,
            "Date": datetime.date.today().strftime("%Y.%m.%d"),
            "Round": str(self.round),
            "White": white,
            "Black": black,
            "Result": result,
        }
        head.update(tags or {})
        words = []
        for i, s in enumerate(sans):
            if i % 2 == 0:
                words.append(f"{i // 2 + 1}.")
            words.append(s)
            if comments and i < len(comments) and comments[i]:
                words.append("{" + comments[i] + "}")
        if why:
            words.append("{" + why.replace("}", ")") + "}")
        words.append(result)
        lines, line = [], ""
        for w in words:  # PGN's lines are at most 80 characters
            if line and len(line) + 1 + len(w) > 79:
                lines.append(line)
                line = w
            else:
                line = f"{line} {w}" if line else w
        lines.append(line)
        with open(self.path, "a", encoding="utf-8") as f:
            for k, v in head.items():
                v = v.replace("\\", "\\\\").replace('"', '\\"')
                f.write(f'[{k} "{v}"]\n')
            f.write("\n" + "\n".join(lines) + "\n\n")
