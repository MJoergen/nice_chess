#!/usr/bin/env python3
"""release.py - the scripted parts of the release procedure of doc/RELEASE.md.

    release.py preflight            step 1: the tree, the branch, the tools, the open issues
    release.py check --eth IP       step 4: make board-test and make board-match on the
    release.py check --port DEVICE    MEGA65, and the stamp that tag needs
    release.py ci [--wait]          step 5: CI passed on HEAD
    release.py notes                a draft of the section of RELEASE_NOTES.md
    release.py tag [NAME]           step 6: tag HEAD, if the stamp and CI allow it
    release.py section NAME         the section of RELEASE_NOTES.md of a tag, for the
                                    GitHub release (.github/workflows/release.yml)

A release is named Vmajor.minor, such as V1.0. The stamp is
build/release/stamp.json, so make clean deletes it.

Python 3 standard library only. Each command stops at the first failure
and exits with status 1.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "release"
STAMP = BUILD / "stamp.json"
WORKFLOW = ROOT / ".github" / "workflows" / "test.yml"
NOTES_FILE = ROOT / "RELEASE_NOTES.md"
MAKEFILE = ROOT / "Makefile"

TAG_PATTERN = re.compile(r"^V(\d+)\.(\d+)$")
DATE = r"\d{4}-\d{2}-\d{2}"

# The programs that must be on PATH, and what needs each.
TOOLS = {
    "git": "everything",
    "make": "everything",
    "gh": "release.py ci and tag",
    "cc": "the host build",
    "clang": "the NICE builds",
    "wasm-ld": "the NICE builds",
    "llvm-ar": "the NICE builds",
    "curl": "make deps",
}


class Failure(Exception):
    pass


def run(args: list[str], cwd: Path = ROOT, check: bool = True) -> str:
    """The output of a command, which must succeed unless check is False."""
    p = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise Failure(f"{' '.join(args)} failed:\n{p.stdout}{p.stderr}".rstrip())
    return p.stdout.strip()


def git(*args: str, check: bool = True) -> str:
    return run(["git", *args], check=check)


def head() -> str:
    return git("rev-parse", "HEAD")


def tree_is_clean() -> bool:
    return git("status", "--porcelain") == ""


def say(ok: bool, text: str) -> bool:
    print(f"  {'ok  ' if ok else 'FAIL'} {text}")
    return ok


def confirm(question: str) -> bool:
    return input(f"{question} [y/N] ").strip().lower() == "y"


def nicemon_dir() -> Path:
    """The checkout of NiceMonMEGA65 that the board targets use."""
    return (ROOT / os.environ.get("NICEMON", "../NiceMonMEGA65")).resolve()


def makefile_var(text: str, name: str) -> Optional[str]:
    """The value of a variable NAME := VALUE of the Makefile, or None."""
    m = re.search(rf"^{re.escape(name)}\s*:?=\s*(\S+)\s*$", text, re.M)
    return m.group(1) if m else None


# ---------------------------------------------------------------- the tags


def tag_key(name: str) -> Optional[tuple[int, int]]:
    """The order of a release tag Vmajor.minor, or None for another name."""
    m = TAG_PATTERN.match(name)
    return (int(m.group(1)), int(m.group(2))) if m else None


def sorted_tags(names: list[str]) -> list[str]:
    """The release tags among names, oldest first."""
    return sorted((t for t in names if tag_key(t)), key=lambda t: tag_key(t) or (0, 0))


def next_tag_after(tags: list[str]) -> str:
    """The next minor version after the release tags, oldest first; V1.0
    if there are none. A new major version is named by hand."""
    if not tags:
        return "V1.0"
    major, minor = tag_key(tags[-1]) or (1, -1)
    return f"V{major}.{minor + 1}"


def release_tags() -> list[str]:
    return sorted_tags(git("tag", "--list").split())


# --------------------------------------------------------------- preflight


def cmd_preflight(_: argparse.Namespace) -> int:
    print("Preflight (doc/RELEASE.md step 1)")
    ok = True
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    ok &= say(branch == "main", f"the branch is main (is {branch})")
    clean = tree_is_clean()
    ok &= say(clean, "the working tree is clean")
    if not clean:
        print(git("status", "--short"))
    p = subprocess.run(["git", "fetch", "origin"], cwd=ROOT, capture_output=True, text=True)
    if say(p.returncode == 0, "git fetch origin"):
        local, remote = head(), git("rev-parse", "origin/main")
        ok &= say(local == remote, f"HEAD is origin/main ({local[:7]} against {remote[:7]})")
    else:
        ok = False
    for tool, why in TOOLS.items():
        ok &= say(shutil.which(tool) is not None, f"{tool} is on PATH, for {why}")
    tag = makefile_var(MAKEFILE.read_text(), "NICE_CPU_TAG")
    ok &= say((ROOT / "deps" / f"nice_cpu-{tag}").is_dir(), f"deps/nice_cpu-{tag} is there (make deps)")
    nicemon = nicemon_dir()
    ok &= say((nicemon / "tools" / "load.py").is_file(),
              f"NiceMonMEGA65 is at {nicemon} (NICEMON), for the board")
    if shutil.which("gh"):
        p = subprocess.run(["gh", "auth", "status"], cwd=ROOT, capture_output=True, text=True)
        ok &= say(p.returncode == 0, "gh is logged in")
        p = subprocess.run(["gh", "issue", "list", "--state", "open", "--json", "number,title"],
                           cwd=ROOT, capture_output=True, text=True)
        if p.returncode == 0:
            issues = json.loads(p.stdout)
            print(f"  open issues (step 1.2, by hand): {len(issues)}")
            for i in issues:
                print(f"       #{i['number']} {i['title']}")
    print(f"  the next tag: {next_tag_after(release_tags())}")
    return 0 if ok else 1


# ------------------------------------------------------------------- check


def read_stamp() -> Optional[dict[str, object]]:
    try:
        data = json.loads(STAMP.read_text())
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def make_step(target: str, link: list[str]) -> dict[str, object]:
    """Runs make TARGET on the board, with the output in a log of its own.
    A failure stops the check."""
    log = BUILD / f"{target}.log"
    print(f"  make {target} ... ", end="", flush=True)
    t0 = time.time()
    with open(log, "w") as f:
        p = subprocess.run(["make", target, *link], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
    secs = time.time() - t0
    print(f"{'ok' if p.returncode == 0 else 'FAILED'} ({secs:.0f} s, {log.relative_to(ROOT)})")
    text = log.read_text()
    if p.returncode != 0:
        print("".join(text.splitlines(keepends=True)[-30:]))
        raise Failure(f"make {target} failed")
    step: dict[str, object] = {"target": target, "seconds": round(secs)}
    score = re.findall(r"^\S+: [\d.]+ of \d+$", text, re.M)
    if score:
        step["score"] = score[-1]
    return step


def cmd_check(a: argparse.Namespace) -> int:
    print("Check on the board (doc/RELEASE.md step 4)")
    if not tree_is_clean():
        print(git("status", "--short"))
        raise Failure("the working tree is not clean")
    sha = head()
    BUILD.mkdir(parents=True, exist_ok=True)
    STAMP.unlink(missing_ok=True)
    link = [f"ETH={a.eth}"] if a.eth else [f"PORT={a.port}"]
    steps = [make_step("board-test", link), make_step("board-match", link)]
    if head() != sha:
        raise Failure("HEAD moved during the check")
    if not tree_is_clean():
        print(git("status", "--short"))
        raise Failure("a step changed the tree")
    nicemon = run(["git", "-C", str(nicemon_dir()), "describe", "--tags", "--always", "--dirty"], check=False)
    stamp = {
        "sha": sha,
        "date": datetime.datetime.now().isoformat(timespec="seconds"),
        "link": link[0],
        "nicemon": nicemon or "unknown",
        "steps": steps,
    }
    STAMP.write_text(json.dumps(stamp, indent=2) + "\n")
    for s in steps:
        if "score" in s:
            print(f"  {s['target']}: {s['score']} (the host build's score)")
    print(f"Passed on {sha[:7]}, with NiceMonMEGA65 {stamp['nicemon']}; the stamp is {STAMP.relative_to(ROOT)}")
    return 0


# ---------------------------------------------------------------------- ci


def workflow_jobs(text: str) -> list[str]:
    """The jobs of a workflow: the keys under jobs:."""
    body = text.split("\njobs:\n", 1)[1]
    return re.findall(r"^  ([\w-]+):\s*$", body, re.M)


def run_state(jobs: list[dict[str, str]], expected: list[str]) -> tuple[str, str]:
    """(state, text) of the jobs of one CI run. Every job of the workflow
    must be there, and must have succeeded. A failure counts even while
    other jobs run."""
    names = [j["name"] for j in jobs]
    absent = [e for e in expected if not any(n == e or n.startswith(e + " (") for n in names)]
    if absent:
        return "failed", f"jobs not in the run: {', '.join(absent)}"
    bad = [j["name"] for j in jobs if j["status"] == "completed" and j["conclusion"] != "success"]
    if bad:
        return "failed", f"jobs failed: {', '.join(bad)}"
    waiting = [j["name"] for j in jobs if j["status"] != "completed"]
    if waiting:
        return "pending", f"waiting for: {', '.join(waiting)}"
    return "passed", f"all {len(jobs)} jobs passed"


def ci_status(sha: str, repo: Optional[str] = None) -> tuple[str, str]:
    """(state, text) of the push runs of test.yml on sha: one of missing,
    pending, failed, and passed. One passed run is enough, as a run that
    failed on a runner is followed by a rerun. Pending beats failed, as a
    run may still pass. repo is OWNER/NAME, for an origin gh cannot find."""
    gh = ["gh", "-R", repo] if repo else ["gh"]
    out = run([*gh, "run", "list", "--workflow", WORKFLOW.name, "--commit", sha, "--limit", "50",
               "--json", "databaseId,event"])
    runs = [r for r in json.loads(out) if r["event"] == "push"]
    if not runs:
        return "missing", "no CI run for this commit: push it"
    expected = workflow_jobs(WORKFLOW.read_text())
    results = []
    for r in runs:
        jobs = json.loads(run([*gh, "run", "view", str(r["databaseId"]), "--json", "jobs"]))["jobs"]
        results.append(run_state(jobs, expected))
    for state in ("passed", "pending", "failed"):
        for st, text in results:
            if st == state:
                return st, text
    raise Failure("unreachable")


# How long ci --wait waits for the run of a commit to appear: GitHub
# lists it some seconds after the push.
MISSING_GRACE = 180


def keep_waiting(state: str, waited: float) -> bool:
    """Whether ci --wait polls again: while the run is pending, and while
    it is missing, for MISSING_GRACE seconds after the start."""
    return state == "pending" or (state == "missing" and waited < MISSING_GRACE)


def cmd_ci(a: argparse.Namespace) -> int:
    print("CI (doc/RELEASE.md step 5)")
    sha = head()
    if git("rev-parse", "origin/main") != sha:
        raise Failure("HEAD is not origin/main: push the commit first")
    t0 = time.time()
    while True:
        state, text = ci_status(sha, a.repo)
        if a.wait and keep_waiting(state, time.time() - t0):
            print(f"  {text}")
            time.sleep(60 if state == "pending" else 15)
            continue
        say(state == "passed", f"{sha[:7]}: {text}")
        return 0 if state == "passed" else 1


# ------------------------------------------------------------------- notes


def draft_notes() -> str:
    """A draft of the section of RELEASE_NOTES.md: the pins, the board of
    the stamp, and the commits since the last tag. The text is written by
    hand: the subjects are not the notes."""
    tags = release_tags()
    last = tags[-1] if tags else None
    makefile = MAKEFILE.read_text()
    out = [f"## {next_tag_after(tags)} ({datetime.date.today().isoformat()})", ""]
    out.append(f"Built with nice_cpu {makefile_var(makefile, 'NICE_CPU_TAG')}.")
    stamp = read_stamp()
    if stamp:
        out.append(f"Checked on the MEGA65 under NiceMonMEGA65 {stamp.get('nicemon')}"
                   f" (stamp of {str(stamp.get('sha'))[:7]}).")
        steps = stamp.get("steps")
        for s in steps if isinstance(steps, list) else []:
            if isinstance(s, dict) and "score" in s:
                out.append(f"make {s['target']}: {s['score']}.")
    if last:
        out.append("")
        out.append(f"Changed .expected files since {last}:")
        out.append(git("diff", "--stat", last, "--", "tests/*.expected", check=False) or "  none")
    out.append("")
    out.append(f"Commits since {last}:" if last else "Commits:")
    out.append(git("log", "--reverse", "--format=- %s", f"{last}..HEAD" if last else "HEAD"))
    return "\n".join(out) + "\n"


def cmd_notes(_: argparse.Namespace) -> int:
    print(draft_notes(), end="")
    return 0


def notes_section(text: str, name: str) -> Optional[str]:
    """The section of RELEASE_NOTES.md of the tag name, from its heading
    to the next, without the leading ##, or None."""
    m = re.search(rf"^## ({re.escape(name)}\b.*?)(?=^## |\Z)", text, re.M | re.S)
    return m.group(1).strip() + "\n" if m else None


def dated_section(name: str) -> str:
    """The section of the tag name, whose heading must have the date of
    the release: "V1.0 (2026-11-06)"."""
    section = notes_section(NOTES_FILE.read_text(), name)
    if section is None:
        raise Failure(f"RELEASE_NOTES.md has no section for {name}: "
                      "write it (release.py notes drafts it) and commit it")
    title = section.splitlines()[0]
    if not re.fullmatch(rf"{re.escape(name)} \({DATE}\)", title):
        raise Failure(f"the heading of the section is '## {title}', not '## {name} (YYYY-MM-DD)'")
    return section


def cmd_section(a: argparse.Namespace) -> int:
    print(dated_section(a.name), end="")
    return 0


# --------------------------------------------------------------------- tag


def stamp_free(path: str) -> bool:
    """Whether a file may change between check and tag: a document, which
    changes no build and no test."""
    return path.endswith(".md") or path.startswith("doc/")


def stamp_covers(stamp_sha: str, sha: str) -> bool:
    """Whether the stamp of stamp_sha holds for sha: the same commit, or a
    descendant that changed only documents."""
    if stamp_sha == sha:
        return True
    if git("merge-base", stamp_sha, sha, check=False) != stamp_sha:
        return False
    return all(stamp_free(f) for f in git("diff", "--name-only", stamp_sha, sha).split())


def cmd_tag(a: argparse.Namespace) -> int:
    print("Tag (doc/RELEASE.md step 6)")
    tags = release_tags()
    name = a.name or next_tag_after(tags)
    if not TAG_PATTERN.match(name):
        raise Failure(f"{name} is not a version Vmajor.minor, such as V1.0")
    if name in git("tag", "--list").split():
        raise Failure(f"the tag {name} exists")
    if tags and (tag_key(name) or (0, 0)) <= (tag_key(tags[-1]) or (0, 0)):
        raise Failure(f"{name} does not come after the last tag, {tags[-1]}")
    if not tree_is_clean():
        raise Failure("the working tree is not clean")
    sha = head()
    if git("rev-parse", "origin/main") != sha:
        raise Failure("HEAD is not origin/main: push the commit first")
    stamp = read_stamp()
    if stamp is None or not stamp_covers(str(stamp.get("sha")), sha):
        raise Failure("no stamp of release.py check for HEAD: run make release-check "
                      "(only documents may change after it)")
    message = dated_section(name)
    state, text = ci_status(sha, a.repo)
    if state != "passed":
        raise Failure(f"CI: {text}")
    print(f"  the stamp ({str(stamp.get('sha'))[:7]}, NiceMonMEGA65 {stamp.get('nicemon')}) "
          f"and CI are good for {sha[:7]}")
    print(f"\nTag {name} on {sha[:7]}, with the section of RELEASE_NOTES.md as its message:\n\n{message}")
    if not confirm(f"Create the tag {name}?"):
        return 1
    git("tag", "-a", "--cleanup=whitespace", name, "-m", message)
    print(f"  created {name}")
    if confirm(f"Push {name} to origin? This starts the release workflow, which publishes it."):
        git("push", "origin", name)
        print(f"  pushed {name}; the release workflow builds and publishes it (doc/RELEASE.md step 7)")
    else:
        print(f"  not pushed: git push origin {name}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("preflight").set_defaults(fn=cmd_preflight)
    c = sub.add_parser("check")
    link = c.add_mutually_exclusive_group(required=True)
    link.add_argument("--eth", metavar="IP", help="the board's Ethernet port")
    link.add_argument("--port", metavar="DEVICE", help="the board's debug UART")
    c.set_defaults(fn=cmd_check)
    c = sub.add_parser("ci")
    c.add_argument("--wait", action="store_true", help="poll until the jobs finish")
    c.add_argument("--repo", metavar="OWNER/NAME", help="the GitHub repository, if origin is not it")
    c.set_defaults(fn=cmd_ci)
    sub.add_parser("notes").set_defaults(fn=cmd_notes)
    c = sub.add_parser("tag")
    c.add_argument("name", nargs="?", help="default: the next minor version, such as V1.1")
    c.add_argument("--repo", metavar="OWNER/NAME", help="the GitHub repository, if origin is not it")
    c.set_defaults(fn=cmd_tag)
    c = sub.add_parser("section")
    c.add_argument("name", help="the tag, such as V1.0")
    c.set_defaults(fn=cmd_section)
    a = ap.parse_args()
    try:
        return int(a.fn(a))
    except Failure as e:
        print(f"release.py: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
