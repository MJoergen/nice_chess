"""The copies of the tables of the levels (src/ui/screen.c) and of the
strengths (src/engine/search.c): tools/rate.sh, tools/screen_uci.py,
and README.md cannot include the C source, so this test compares them
with it. If it fails, a table was changed in one place only."""

import os
import re
import unittest

import screen_uci

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def read(path: str) -> str:
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def level_table(name: str) -> list[int]:
    """An array of the levels in src/ui/screen.c, by level."""
    m = re.search(r"\b%s\[5\] = \{([^}]*)\};" % name, read("src/ui/screen.c"))
    assert m, f"src/ui/screen.c has no {name}[5]"
    return [int(x) for x in m.group(1).split(",")]


class Levels(unittest.TestCase):
    def setUp(self) -> None:
        self.num = level_table("level_num")
        self.den = level_table("level_den")
        self.noise = level_table("level_noise")

    def test_screen_uci(self) -> None:
        self.assertEqual(screen_uci.LEVEL_NOISE, self.noise)

    def test_rate_sh(self) -> None:
        # Such as: 1) nodes=$((NODES / 9)) noise=800 ;;
        rows = re.findall(r"^ *([1-5])\) nodes=(.+) noise=(\d+) ;;$", read("tools/rate.sh"), re.M)
        want = []
        for i in range(5):
            # A share is a whole multiple, or one part of a whole number.
            self.assertIn(1, (self.num[i], self.den[i]))
            share = ("$NODES" if self.num[i] == self.den[i] else
                     f"$((NODES / {self.den[i]}))" if self.num[i] == 1 else
                     f"$((NODES * {self.num[i]}))")
            want.append((str(i + 1), share, str(self.noise[i])))
        self.assertEqual(rows, want)

    def test_readme(self) -> None:
        # The time of a move on the board, in seconds.
        m = re.search(r"platform_move_time\(void\)\s*\{\s*return platform_on_board\(\) \? (\d+) :",
                      read("src/platform/nice.c"))
        assert m, "src/platform/nice.c has no time of a move"
        # Such as: | 1     | 0.6 s       | ±800 cp  | 790             |
        rows = re.findall(r"^\| ([1-5]) +\| +([0-9.]+) s +\| +(?:±(\d+) cp)? +\| +\d+ +\|$",
                          read("README.md"), re.M)
        want = []
        for i in range(5):
            seconds = int(m.group(1)) / 1e6 * self.num[i] / self.den[i]
            want.append((str(i + 1), f"{seconds:.1f}".removesuffix(".0"),
                         str(self.noise[i]) if self.noise[i] else ""))
        self.assertEqual(rows, want)


class Strengths(unittest.TestCase):
    def test_readme(self) -> None:
        # Such as {500, 38}: the noise, and the divisor of the time in tenths.
        m = re.search(r"\bstrengths\[ENGINE_STRENGTH_MAX - 1\] =\s*\{(.*?)\};", read("src/engine/search.c"), re.S)
        assert m, "src/engine/search.c has no strengths[]"
        table = [(int(a), int(b)) for a, b in re.findall(r"\{ *(\d+), *(\d+)\}", m.group(1))]
        self.assertEqual(len(table), 4)
        # Such as: | 1        | ±500 cp  | 1/3.8             | 1280            |
        rows = re.findall(r"^\| ([1-5]) +\| +(?:±(\d+) cp)? +\| +(all|1/[0-9.]+) +\| +\d+ +\|$",
                          read("README.md"), re.M)
        want = [(str(i + 1), str(noise), f"1/{divisor / 10:g}") for i, (noise, divisor) in enumerate(table)]
        self.assertEqual(sorted(rows), want + [("5", "", "all")])


if __name__ == "__main__":
    unittest.main()
