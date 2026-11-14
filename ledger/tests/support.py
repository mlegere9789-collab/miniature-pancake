"""Helpers for the ledger tool tests: temporary git remotes, heartbeat files, CLI runners."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LEDGER_DIR = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(LEDGER_DIR)
TOOLS_DIR = os.path.join(LEDGER_DIR, "tools")
PARITY_TOOLS_DIR = os.path.join(REPO_ROOT, "parity", "tools")

for _path in (TOOLS_DIR, PARITY_TOOLS_DIR, REPO_ROOT):
    if _path not in sys.path:
        sys.path.insert(0, _path)

import ledgerlib  # noqa: E402

T0 = ledgerlib.parse_time("2026-11-18T09:00:00Z")


def iso(minutes=0, base=T0):
    """Time `minutes` after base as an ISO string."""
    import datetime

    return ledgerlib.format_time(base + datetime.timedelta(minutes=minutes))


def write_beats(root, agent, beats, day=None):
    """Append heartbeat dicts to <root>/<day>/<agent>.jsonl (day defaults to each beat's date)."""
    for beat in beats:
        folder_day = day or beat["time"][:10]
        folder = os.path.join(root, folder_day)
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, agent + ".jsonl"), "a", encoding="utf-8") as handle:
            handle.write(json.dumps(beat) + "\n")


def beat(minutes, task="T-1", commit="c1", evidence="e1", **extra):
    out = {"time": iso(minutes), "task": task, "what": "work", "commit": commit, "evidence": evidence}
    out.update(extra)
    return out


class TempDirCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="d8-ledger-test-")
        self.addCleanup(self._tmp.cleanup)
        self.tmp = self._tmp.name

    def path(self, *parts):
        return os.path.join(self.tmp, *parts)


def git(*args, cwd=None):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    for key in ("GIT_DIR", "GIT_INDEX_FILE", "GIT_WORK_TREE"):
        env.pop(key, None)
    done = subprocess.run(["git"] + list(args), cwd=cwd, env=env, capture_output=True, text=True)
    return done


class RemoteCase(TempDirCase):
    """A test case with a bare repository in self.remote."""

    def setUp(self):
        super().setUp()
        self.remote = self.path("remote.git")
        done = git("init", "--bare", "-q", self.remote)
        assert done.returncode == 0, done.stderr

    def claim_refs(self):
        done = git("--git-dir", self.remote, "for-each-ref", "--format=%(refname) %(objectname)", "refs/heads/claim/")
        return dict(line.split(" ") for line in done.stdout.splitlines())

    def read_claim_json(self, sha):
        done = git("--git-dir", self.remote, "cat-file", "-p", "%s:claim.json" % sha)
        return json.loads(done.stdout)

    def tool(self, *args, extra_env=None):
        env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
        env.update(extra_env or {})
        return subprocess.run(
            [sys.executable, os.path.join(TOOLS_DIR, "claims.py"), "--remote", self.remote] + list(args),
            capture_output=True,
            text=True,
            env=env,
        )
