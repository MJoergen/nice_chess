"""The tests of release.py: its rules, without git, gh, or the board."""

import unittest

import release


class Tags(unittest.TestCase):
    def test_key(self) -> None:
        self.assertEqual(release.tag_key("V1.0"), (1, 0))
        self.assertEqual(release.tag_key("V2.13"), (2, 13))
        for name in ("V1", "V1.0.1", "v1.0", "RC01", "V1.0-rc"):
            self.assertIsNone(release.tag_key(name), name)

    def test_sorted(self) -> None:
        self.assertEqual(release.sorted_tags(["V1.10", "x", "V1.2", "V2.0", "V1.0.1"]),
                         ["V1.2", "V1.10", "V2.0"])

    def test_next(self) -> None:
        self.assertEqual(release.next_tag_after([]), "V1.0")
        self.assertEqual(release.next_tag_after(["V1.0"]), "V1.1")
        self.assertEqual(release.next_tag_after(["V1.9", "V1.10"]), "V1.11")


class Notes(unittest.TestCase):
    TEXT = ("# Release notes\n\nIntro.\n\n"
            "## V1.1 (2026-12-01)\n\nNew.\n\n- a\n\n"
            "## V1.0 (2026-11-06)\n\nFirst.\n")

    def test_section(self) -> None:
        self.assertEqual(release.notes_section(self.TEXT, "V1.1"), "V1.1 (2026-12-01)\n\nNew.\n\n- a\n")
        self.assertEqual(release.notes_section(self.TEXT, "V1.0"), "V1.0 (2026-11-06)\n\nFirst.\n")
        self.assertIsNone(release.notes_section(self.TEXT, "V1.2"))

    def test_no_prefix_match(self) -> None:
        text = "## V1.10 (2027-01-01)\n\nx\n"
        self.assertIsNone(release.notes_section(text, "V1.1"))


class Makefile(unittest.TestCase):
    def test_var(self) -> None:
        text = "NICE_CPU_TAG    := V1.6\nNICEEMU  ?= deps/x\n"
        self.assertEqual(release.makefile_var(text, "NICE_CPU_TAG"), "V1.6")
        self.assertIsNone(release.makefile_var(text, "MISSING"))

    def test_pin_is_found(self) -> None:
        tag = release.makefile_var(release.MAKEFILE.read_text(), "NICE_CPU_TAG")
        self.assertIsNotNone(tag)


class Stamp(unittest.TestCase):
    def test_free(self) -> None:
        for path in ("RELEASE_NOTES.md", "README.md", "doc/HISTORY.md", "doc/screenshot.png"):
            self.assertTrue(release.stamp_free(path), path)
        for path in ("Makefile", "src/ui/text.c", "tests/text.expected", "tools/match.py",
                     ".github/workflows/release.yml"):
            self.assertFalse(release.stamp_free(path), path)


class CI(unittest.TestCase):
    def job(self, name: str, status: str = "completed", conclusion: str = "success") -> dict[str, str]:
        return {"name": name, "status": status, "conclusion": conclusion}

    def test_jobs(self) -> None:
        text = "name: test\n\non:\n  push:\n\njobs:\n  test:\n    runs-on: x\n    steps:\n      - run: y\n  other:\n    runs-on: x\n"
        self.assertEqual(release.workflow_jobs(text), ["test", "other"])

    def test_workflow_is_parsed(self) -> None:
        self.assertEqual(release.workflow_jobs(release.WORKFLOW.read_text()), ["test"])

    def test_state(self) -> None:
        self.assertEqual(release.run_state([self.job("test")], ["test"])[0], "passed")
        self.assertEqual(release.run_state([self.job("test", "in_progress", "")], ["test"])[0], "pending")
        self.assertEqual(release.run_state([self.job("test", conclusion="failure")], ["test"])[0], "failed")
        self.assertEqual(release.run_state([], ["test"])[0], "failed")
        self.assertEqual(release.run_state([self.job("a", conclusion="failure"), self.job("test", "queued", "")],
                                           ["a", "test"])[0], "failed")

    def test_wait(self) -> None:
        self.assertTrue(release.keep_waiting("pending", 10000))
        self.assertTrue(release.keep_waiting("missing", 0))
        self.assertFalse(release.keep_waiting("missing", release.MISSING_GRACE))
        self.assertFalse(release.keep_waiting("passed", 0))
        self.assertFalse(release.keep_waiting("failed", 0))


if __name__ == "__main__":
    unittest.main()
