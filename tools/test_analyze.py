"""The tests of analyze.py: the moves of a book, and a Stockfish that
ends, without Stockfish or the referee."""

import contextlib
import io
import os
import stat
import tempfile
import unittest

import analyze

# 1. e4 e5 2. Nf3 Nc6, as cutechess-cli writes it after an opening of
# two plies.
PGN = """[Event "test"]
[White "me"]
[Black "other"]
[Result "1-0"]

1. e4 {book} e5 {book} 2. Nf3 {+0.30/5 0.010s} Nc6 {-0.20/9 1.2s, White wins by adjudication}
1-0
"""
FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
]


class Book(unittest.TestCase):
    def test_is_book(self) -> None:
        self.assertTrue(analyze.is_book("book", True))
        self.assertTrue(analyze.is_book("book", False))
        self.assertTrue(analyze.is_book("", True))
        self.assertFalse(analyze.is_book("", False))
        self.assertFalse(analyze.is_book("+0.30/5 0.010s", True))

    def test_report(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "g.pgn")
            with open(path, "w") as f:
                f.write(PGN)
            games = analyze.parse_pgn(path)
        self.assertEqual(games[0]["sans"], ["e4", "e5", "Nf3", "Nc6"])
        self.assertEqual(games[0]["comments"][:3], ["book", "book", "+0.30/5 0.010s"])
        games[0]["fens"], games[0]["moves"] = FENS, ["e2e4", "e7e5", "g1f3", "b8c6"]
        evals = {f: {"score": 30, "best": None, "pv": []} for f in FENS}
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            analyze.report(games, evals, "me")
        rows = {tuple(line.split()[:2]): int(line.split()[2]) for line in out.getvalue().splitlines()
                if line.split()[:1] == ["opening"]}
        # One move each from a search; the two of the opening are in no count.
        self.assertEqual(rows, {("opening", "engine"): 1, ("opening", "opponent"): 1})
        self.assertIn("The book: 1 games with a book move.", out.getvalue())
        self.assertIn("the last book move: median move 1", out.getvalue())


class Stockfish(unittest.TestCase):
    def fake(self, d: str, body: str) -> str:
        path = os.path.join(d, "fake")
        with open(path, "w") as f:
            f.write("#!/bin/sh\n" + body)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path

    def test_ends_at_start(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(RuntimeError, "has ended, with exit status 3"):
                analyze.score_positions((self.fake(d, "exit 3\n"), [FENS[0]], 10))

    def test_ends_in_a_search(self) -> None:
        # It answers isready, and ends at the first go.
        body = ('while read w rest; do\n'
                '  [ "$w" = isready ] && echo readyok\n'
                '  [ "$w" = go ] && exit 0\n'
                'done\n')
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(RuntimeError, "has ended"):
                analyze.score_positions((self.fake(d, body), [FENS[0]], 10))

    def test_scores(self) -> None:
        body = ('while read w rest; do\n'
                '  [ "$w" = isready ] && echo readyok\n'
                '  [ "$w" = go ] && echo "info depth 1 score cp 25 pv e2e4 e7e5" && echo "bestmove e2e4"\n'
                '  [ "$w" = quit ] && exit 0\n'
                'done\n')
        with tempfile.TemporaryDirectory() as d:
            res = analyze.score_positions((self.fake(d, body), [FENS[0], FENS[1]], 10))
        self.assertEqual(res, [(FENS[0], 25, "e2e4", ["e2e4", "e7e5"]),
                               (FENS[1], -25, "e2e4", ["e2e4", "e7e5"])])


if __name__ == "__main__":
    unittest.main()
