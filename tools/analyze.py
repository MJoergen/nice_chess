#!/usr/bin/env python3
"""analyze.py - patterns in an engine's games, by Stockfish's scores.

Usage: analyze.py [options] PGN ENGINE

Reads the games of PGN, as cutechess-cli writes them, in which ENGINE
(a name in the White and Black tags) played. The referee (--referee,
build/host/chess_text) replays each game; Stockfish (--stockfish, at
full strength, one thread) scores each position for --movetime ms,
in --workers processes at once. The scores go to --cache
(build/analyze/NAME.json, for PGN NAME.pgn), and a later run with
the same cache only prints the report.

A move's loss is the drop in the mover's expected score, from
Stockfish's centipawns by lichess's curve: an inaccuracy from 0.10, a
mistake from 0.20, a blunder from 0.30. The opening is moves 1 to 12;
the endgame, after it, the positions whose phase, as the engine counts
it (knight and bishop 1, rook 2, queen 4), is 8 or less; the
middlegame is the rest. A move of ENGINE without a comment is a move
of its book.

The report gives, for ENGINE and its opponent: the losses by phase;
in ENGINE's lost games, the expected score it gave away in each phase,
and the phase in which the game fell below 0.2 for good; how the games
that reached an endgame ended, by the expected score with which it
started; Stockfish's score where ENGINE left its book; and ENGINE's
blunders: whether Stockfish's reply is a capture, and whether ENGINE's
own score after the move (its comment) was 1.5 pawns or more above
Stockfish's, so that it did not see it.
"""

from __future__ import annotations

import argparse
import collections
import json
import math
import multiprocessing as mp
import os
import re
import subprocess
import sys

from match import Referee
from pgn import san

PHASE = {"n": 1, "b": 1, "r": 2, "q": 4}
PHASES = ("opening", "middlegame", "endgame")
MATE = 100000


def parse_pgn(path: str) -> list[dict]:
    """The games of a PGN: their tags, moves in SAN, and the comment
    after each move ("" if none)."""
    games, head, text = [], {}, []

    def flush():
        if head:
            games.append({"head": dict(head), "text": " ".join(text)})

    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("[Event ") and (head or text):
            flush()
            head.clear()
            text.clear()
        m = re.match(r'\[(\w+) "(.*)"\]', line)
        if m:
            head[m.group(1)] = m.group(2)
        elif line.strip():
            text.append(line)
    flush()
    for g in games:
        sans, comments = [], []
        for tok in re.finditer(r"\{([^}]*)\}|(\S+)", g.pop("text")):
            if tok.group(1) is not None:
                if sans:
                    comments[-1] = tok.group(1)
                continue
            w = re.sub(r"^\d+\.+", "", tok.group(2))
            if w and w not in ("1-0", "0-1", "1/2-1/2", "*"):
                sans.append(w)
                comments.append("")
        g["sans"], g["comments"] = sans, comments
    return games


def replay(ref: Referee, sans: list[str]) -> tuple[list[str], list[str]]:
    """The FEN before each move and after the last, and the moves in
    coordinate notation."""
    ref.ask("new")
    fens, moves = [], []
    for s in sans:
        fen, legal = ref.fen(), ref.moves()
        want = s.rstrip("+#")
        # san() with mate=True adds # instead of looking for a check.
        hit = [m for m in legal if san(fen, m, legal, fen, True)[:-1] == want]
        if len(hit) != 1:
            raise RuntimeError(f"analyze.py: cannot read {s} in {fen}")
        fens.append(fen)
        moves.append(hit[0])
        ref.move(hit[0])
    fens.append(ref.fen())
    return fens, moves


def score_positions(job: tuple[str, list[str], int]) -> list[tuple]:
    """Stockfish's score of each FEN, from white's side, its best move,
    and its principal variation."""
    cmd, fens, movetime = job
    sf = subprocess.Popen([cmd], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True, bufsize=1)

    def send(s: str) -> None:
        sf.stdin.write(s + "\n")
        sf.stdin.flush()

    send("uci")
    send("setoption name Threads value 1")
    send("setoption name Hash value 64")
    send("isready")
    while sf.stdout.readline().strip() != "readyok":
        pass
    out = []
    for fen in fens:
        send("ucinewgame")
        send(f"position fen {fen}")
        send(f"go movetime {movetime}")
        score, pv, best = 0, [], None
        while True:
            w = sf.stdout.readline().split()
            if w[:1] == ["bestmove"]:
                best = w[1] if len(w) > 1 and w[1] != "(none)" else None
                break
            if "score" in w:
                i = w.index("score")
                n = int(w[i + 2])
                score = n if w[i + 1] == "cp" else (MATE - abs(n)) * (1 if n > 0 else -1)
                if "pv" in w:
                    pv = w[w.index("pv") + 1:][:5]
        if fen.split()[1] == "b":
            score = -score
        out.append((fen, score, best, pv))
    send("quit")
    sf.wait()
    return out


def W(cp: float) -> float:
    """The expected score at cp centipawns, by lichess's curve."""
    return 1 / (1 + math.exp(-0.00368208 * max(-2000, min(2000, cp))))


def phase_of(fen: str) -> str:
    if int(fen.split()[5]) <= 12:
        return "opening"
    material = sum(PHASE.get(c.lower(), 0) for c in fen.split()[0])
    return "endgame" if material <= 8 else "middlegame"


def is_capture(fen: str, move: str) -> bool:
    """Whether move captures, en passant too, or promotes."""
    rows = fen.split()[0].split("/")
    board = {}
    for i, row in enumerate(rows):
        f = 0
        for c in row:
            if c.isdigit():
                f += int(c)
            else:
                board[(f, 7 - i)] = c
                f += 1
    frm = (ord(move[0]) - 97, int(move[1]) - 1)
    to = (ord(move[2]) - 97, int(move[3]) - 1)
    pawn = board.get(frm, " ").lower() == "p"
    return to in board or (pawn and to[0] != frm[0]) or len(move) == 5


def own_score(comment: str) -> float | None:
    """The engine's score, from its side, in a comment such as
    "+0.32/7 0.026s" or "-M5/12 0.1s"."""
    s = comment.split("/")[0]
    if "M" in s:
        return 2000 if not s.startswith("-") else -2000
    try:
        return float(s) * 100
    except ValueError:
        return None


def report(games: list[dict], evals: dict, me: str) -> None:
    results = collections.Counter()
    stats = {who: {ph: collections.Counter() for ph in PHASES} for who in ("engine", "opponent")}
    lost_by_phase = collections.Counter()
    gone_in = collections.Counter()
    at_move_12, endgames, book_exit, blunders, plies = [], [], [], [], []
    for gi, g in enumerate(games, 1):
        white = g["head"].get("White") == me
        if not white and g["head"].get("Black") != me:
            continue
        r = {"1-0": 1, "0-1": 0, "1/2-1/2": 0.5}.get(g["head"].get("Result"))
        if r is None:
            continue
        mine = r if white else 1 - r
        results[{1: "wins", 0: "losses", 0.5: "draws"}[mine]] += 1
        fens, moves = g["fens"], g["moves"]
        plies.append(len(moves))
        cp = [evals[f]["score"] for f in fens]   # White's side
        mine_w = [W(c if white else -c) for c in cp]
        last_book = -1
        for i, m in enumerate(moves):
            ours = (i % 2 == 0) == white
            if ours and g["comments"][i] == "":
                last_book = i
                continue
            sign = 1 if i % 2 == 0 else -1
            before, after = sign * cp[i], sign * cp[i + 1]
            loss = W(before) - W(after)
            ph = phase_of(fens[i])
            c = stats["engine" if ours else "opponent"][ph]
            c["moves"] += 1
            c["cp"] += max(0, max(-1000, min(1000, before)) - max(-1000, min(1000, after)))
            c["inaccuracies"] += 0.10 <= loss < 0.20
            c["mistakes"] += 0.20 <= loss < 0.30
            c["blunders"] += loss >= 0.30
            if not ours:
                continue
            if mine == 0 and loss > 0:
                lost_by_phase[ph] += loss
            if loss >= 0.30:
                reply = evals[fens[i + 1]]["best"]
                own = own_score(g["comments"][i])
                blunders.append({
                    "game": gi, "move": i // 2 + 1, "san": g["sans"][i], "phase": ph,
                    "loss": loss, "before": before, "after": after, "own": own,
                    "best": evals[fens[i]]["best"], "reply": reply,
                    "capture": bool(reply) and is_capture(fens[i + 1], reply),
                    "unseen": own is not None and own - after >= 150,
                })
        if mine == 0:
            k = len(mine_w) - 1
            while k > 0 and mine_w[k - 1] < 0.2:
                k -= 1
            gone_in[phase_of(fens[k])] += 1
        if len(fens) > 24:
            at_move_12.append(mine_w[24])
        e = next((i for i, f in enumerate(fens) if phase_of(f) == "endgame"), None)
        if e is not None:
            endgames.append((mine_w[e], mine, gi))
        if last_book >= 0:
            book_exit.append((cp[last_book + 1] * (1 if white else -1), last_book // 2 + 1,
                              gi, white, " ".join(g["sans"][:last_book + 1])))

    n = sum(results.values())
    if not n:
        sys.exit(f"analyze.py: no finished game of {me}")
    score = (results["wins"] + results["draws"] / 2) / n
    print(f"{me}: {n} games, {results['wins']} wins, {results['draws']} draws, "
          f"{results['losses']} losses, {score:.1%}; median length "
          f"{sorted(plies)[len(plies) // 2] // 2} moves")

    print("\nMoves out of the book: the mean centipawns lost (capped at 1000), and")
    print("inaccuracies, mistakes, and blunders per 100 moves:")
    print(f"  {'':11} {'':9} {'moves':>6} {'cp':>6} {'inacc':>6} {'mist':>6} {'blund':>6}")
    for ph in PHASES:
        for who in ("engine", "opponent"):
            c = stats[who][ph]
            k = c["moves"] or 1
            print(f"  {ph:11} {who:9} {c['moves']:6} {c['cp'] / k:6.1f} "
                  f"{100 * c['inaccuracies'] / k:6.1f} {100 * c['mistakes'] / k:6.1f} "
                  f"{100 * c['blunders'] / k:6.1f}")

    if results["losses"]:
        tot = sum(lost_by_phase.values()) or 1
        print("\nIn the lost games, the expected score given away, by phase, and the phase")
        print("in which the game fell below 0.2 for good:")
        for ph in PHASES:
            print(f"  {ph:11} {lost_by_phase[ph]:6.2f} ({lost_by_phase[ph] / tot:3.0%})"
                  f"   {gone_in[ph]:3} games")

    if at_move_12:
        v = sorted(at_move_12)
        print(f"\nThe expected score after move 12: median {v[len(v) // 2]:.2f}, "
              f"below 0.35 in {sum(x < 0.35 for x in v)} of {len(v)} games")

    if endgames:
        print(f"\nGames that reached an endgame: {len(endgames)}; by the expected score where it started:")
        for lo, hi, name in ((0, 0.25, "losing"), (0.25, 0.75, "balanced"), (0.75, 1.01, "winning")):
            sel = [m for w, m, _ in endgames if lo <= w < hi]
            if sel:
                print(f"  {name:9} {len(sel):4} games, scored {sum(sel) / len(sel):4.0%}")
        lost = [(gi, m) for w, m, gi in endgames if w >= 0.75 and m < 1]
        if lost:
            print("  winning, but not won: " + ", ".join(f"game {gi} ({m:g})" for gi, m in lost))

    if book_exit:
        sc = sorted(x[0] for x in book_exit)
        print(f"\nThe book: {len(book_exit)} games with a book move. Stockfish's score where it was left:")
        print(f"  median {sc[len(sc) // 2] / 100:+.2f}, mean {sum(sc) / len(sc) / 100:+.2f}, "
              f"-0.50 or worse in {sum(s <= -50 for s in sc)}; the last book move: median move "
              f"{sorted(x[1] for x in book_exit)[len(sc) // 2]}")
        for s, _, gi, w, line in sorted(book_exit)[:5]:
            print(f"  {s / 100:+.2f} game {gi} ({'white' if w else 'black'}): {line}")

    print(f"\nBlunders: {len(blunders)}")
    if blunders:
        k = len(blunders)
        cap = sum(b["capture"] for b in blunders)
        unseen = sum(b["unseen"] for b in blunders)
        print(f"  by phase: " + ", ".join(f"{ph} {sum(b['phase'] == ph for b in blunders)}"
                                         for ph in PHASES))
        print(f"  Stockfish's reply a capture or promotion: {cap} ({cap / k:.0%})")
        print(f"  unseen (the engine's own score 1.5 pawns or more above Stockfish's): "
              f"{unseen} ({unseen / k:.0%})")
        print("  The worst, with Stockfish's best move instead and its reply:")
        for b in sorted(blunders, key=lambda b: -b["loss"])[:10]:
            own = f"{b['own'] / 100:+.2f}" if b["own"] is not None else "?"
            print(f"  game {b['game']} move {b['move']} {b['san']} ({b['phase']}): "
                  f"{b['before'] / 100:+.2f} -> {b['after'] / 100:+.2f}, own {own}, "
                  f"best {b['best']}, reply {b['reply']}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pgn")
    ap.add_argument("engine", help="the engine's name in the PGN's tags")
    ap.add_argument("--referee", default="build/host/chess_text")
    ap.add_argument("--stockfish", default=os.environ.get("STOCKFISH", "stockfish"))
    ap.add_argument("--movetime", type=int, default=1000, help="ms a position")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--cache", help="the file of the scores (build/analyze/NAME.json)")
    a = ap.parse_args()
    cache = a.cache or os.path.join(
        "build/analyze", os.path.splitext(os.path.basename(a.pgn))[0] + ".json")

    games = parse_pgn(a.pgn)
    ref = Referee(a.referee)
    for g in games:
        g["fens"], g["moves"] = replay(ref, g["sans"])
    ref.close()

    evals = {}
    if os.path.exists(cache):
        evals = json.load(open(cache))
    todo = sorted({f for g in games for f in g["fens"]} - evals.keys())
    if todo:
        jobs = [(a.stockfish, todo[i::a.workers * 4], a.movetime) for i in range(a.workers * 4)]
        print(f"Scoring {len(todo)} positions at {a.movetime} ms, {a.workers} at a time: "
              f"about {len(todo) * a.movetime / 1000 / a.workers / 60:.0f} minutes",
              file=sys.stderr, flush=True)
        with mp.Pool(a.workers) as pool:
            for res in pool.imap_unordered(score_positions, [j for j in jobs if j[1]]):
                for fen, score, best, pv in res:
                    evals[fen] = {"score": score, "best": best, "pv": pv}
        os.makedirs(os.path.dirname(cache) or ".", exist_ok=True)
        with open(cache, "w") as f:
            json.dump(evals, f)
    report(games, evals, a.engine)


if __name__ == "__main__":
    main()
