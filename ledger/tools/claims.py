#!/usr/bin/env python3
"""Atomic task claims on a git remote (plan 7.3 step 2 and section 8).

THE PROTOCOL

  A claim is the git ref  refs/heads/claim/<task-id>  on the shared remote (the GitHub
  repository, or any bare repository). Creating that ref is the claim. The ref points at a
  small commit whose tree holds one file, claim.json:

      {"schema": 1, "task": "T-blender-0007", "agent": "regblender",
       "branch": "task/T-blender-0007", "claimed_at": "2026-11-18T09:00:00Z",
       "renewed_at": "2026-11-18T09:00:00Z", "attempt": 1,
       "previous_agent": null, "note": ""}

  1. CLAIM    The claimant builds the commit locally (no ref of the local repository is
              touched: objects live in a throw-away bare repository) and pushes it with
                  git push --force-with-lease=refs/heads/claim/<task>: <remote> <sha>:refs/heads/claim/<task>
              The empty value after the colon means "the ref must not exist". The remote
              repeats that check inside its own ref transaction when it applies the update,
              so two agents that race for the same task cannot both succeed, whatever the
              timing: exactly one push is accepted, every other one is rejected and learns
              who won. Even a claim commit that descends from the winner's is rejected.
  2. ALIVE    A claim lives while its owner shows signs of life. The last sign of life is
              the newest of: claimed_at, renewed_at (the `renew` command moves the ref
              forward with a compare-and-swap), and the newest heartbeat of the owning
              agent for that task in ledger/heartbeats/<date>/<agent>.jsonl.
  3. EXPIRY   A claim expires when its last sign of life is more than 45 minutes old
              (--expiry-minutes). Nothing deletes it by itself: the watchdog reports it and
              either `takeover` (give the task to a new agent) or `reap` (free the task)
              acts on it.
  4. TAKEOVER An expired claim is replaced with a compare-and-swap: a new claim commit whose
              parent is the old claim commit, pushed with
                  --force-with-lease=refs/heads/claim/<task>:<old sha>
              If two agents take over at the same moment, one wins; the other is rejected
              because the ref no longer holds <old sha>. The new claim has attempt+1 and
              names previous_agent, so the claim history (git log of the ref) is the retry
              count the plan asks for: at most two relaunches, then the task is split.
  5. RELEASE  The owner deletes the ref when the task is merged or handed back, again with
              a lease on the sha it read, so a claim that was taken over meanwhile is never
              deleted by its previous owner.

  Fencing: the claim commit sha identifies one ownership period. An agent that finds its
  `renew` rejected has lost the task and must stop.

COMMANDS (all print one JSON document on stdout; exit 0 success, 3 refused because of the
state of the claim, 4 refused because of the task file, 2 usage or git problem)

  claims.py [--remote R] [--now T] claim    TASK --agent A [--branch B] [--note N] [--tasks DIR]
  claims.py [--remote R] [--now T] renew    TASK --agent A
  claims.py [--remote R] [--now T] release  TASK --agent A [--force]
  claims.py [--remote R] [--now T] show     TASK [--heartbeats DIR]
  claims.py [--remote R] [--now T] list     [--heartbeats DIR] [--expiry-minutes 45]
  claims.py [--remote R] [--now T] takeover TASK --agent A [--heartbeats DIR] [--expiry-minutes 45]
  claims.py [--remote R] [--now T] reap     [--heartbeats DIR] [--expiry-minutes 45] [--apply]

  --remote is a URL, a path to a (bare) repository, or the name of a remote of the current
  repository (default: origin). --tasks DIR (ledger/tasks) makes `claim` refuse a task that
  has no task file or whose status is already finished.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ledgerlib  # noqa: E402

NAMESPACE = "refs/heads/claim/"
EXPIRY_MINUTES = 45
FINISHED_STATES = ("done", "verified", "merged", "counted", "closed", "cancelled")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$")
ZERO_SHA = "0" * 40
GIT_TIMEOUT = 120


class ClaimError(Exception):
    """A refusal with an exit code (3 state of the claim, 4 task file, 2 environment)."""

    def __init__(self, message, code=3, detail=None):
        super().__init__(message)
        self.code = code
        self.detail = detail or {}


def valid_name(text):
    return (
        bool(NAME_RE.match(text))
        and ".." not in text
        and not text.endswith(".lock")
        and not text.endswith(".")
    )


def ref_for(task, namespace=NAMESPACE):
    return namespace + task


# --------------------------------------------------------------------------------------
# git plumbing
# --------------------------------------------------------------------------------------


def _git(args, git_dir=None, cwd=None, env=None, input_text=None, check=True):
    command = ["git"]
    if git_dir:
        command += ["--git-dir", git_dir]
    command += list(args)
    full_env = dict(os.environ)
    full_env["GIT_TERMINAL_PROMPT"] = "0"
    full_env.pop("GIT_DIR", None)
    full_env.pop("GIT_INDEX_FILE", None)
    if env:
        full_env.update(env)
    try:
        done = subprocess.run(
            command,
            cwd=cwd,
            env=full_env,
            input=input_text,
            capture_output=True,
            text=True,
            timeout=GIT_TIMEOUT,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ClaimError("git %s failed: %s" % (" ".join(args[:2]), exc), code=2)
    if check and done.returncode != 0:
        raise ClaimError(
            "git %s failed: %s"
            % (" ".join(args[:2]), (done.stderr or done.stdout).strip()[-400:]),
            code=2,
        )
    return done


def resolve_remote(remote):
    """A remote name of the current repository becomes its URL; anything else is kept."""
    if os.path.exists(remote) or "/" in remote or ":" in remote:
        return remote
    done = _git(["config", "--get", "remote.%s.url" % remote], check=False)
    url = done.stdout.strip()
    if done.returncode != 0 or not url:
        raise ClaimError(
            "no such remote: %s (give --remote URL or path)" % remote, code=2
        )
    return url


class Workspace:
    """A throw-away bare repository used to build claim commits and to read remote ones."""

    def __init__(self, remote):
        self.remote = resolve_remote(remote)
        self._tmp = tempfile.TemporaryDirectory(prefix="d8-claims-")
        self.git_dir = os.path.join(self._tmp.name, "scratch.git")
        _git(["init", "--bare", "-q", self.git_dir])

    def close(self):
        self._tmp.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def git(self, args, **kw):
        return _git(args, git_dir=self.git_dir, **kw)

    # remote reads -------------------------------------------------------------------
    def remote_claims(self, namespace=NAMESPACE):
        """{task: sha} of every claim ref on the remote."""
        done = _git(["ls-remote", self.remote, namespace + "*"], check=True)
        found = {}
        for line in done.stdout.splitlines():
            sha, _, ref = line.partition("\t")
            if ref.startswith(namespace):
                found[ref[len(namespace) :]] = sha
        return found

    def fetch_claims(self, namespace=NAMESPACE):
        """Fetch every claim commit into the scratch repository; return {task: record}."""
        shas = self.remote_claims(namespace)
        if not shas:
            return {}
        _git(
            [
                "fetch",
                "-q",
                "--no-tags",
                self.remote,
                "+%s*:refs/fetched/*" % namespace,
            ],
            git_dir=self.git_dir,
        )
        records = {}
        for task, sha in shas.items():
            records[task] = self.read_claim(sha, task)
        return records

    def read_claim(self, sha, task=None):
        done = self.git(["cat-file", "-p", "%s:claim.json" % sha], check=False)
        try:
            data = json.loads(done.stdout)
            if not isinstance(data, dict):
                raise ValueError("not an object")
        except ValueError:
            data = {"task": task, "agent": None, "invalid": True}
        data["sha"] = sha
        data.setdefault("task", task)
        return data

    # writes -------------------------------------------------------------------------
    def make_claim_commit(self, body, parent, now):
        blob = self.git(
            ["hash-object", "-w", "--stdin"],
            input_text=json.dumps(body, sort_keys=True, indent=1) + "\n",
        ).stdout.strip()
        tree = self.git(
            ["mktree"], input_text="100644 blob %s\tclaim.json\n" % blob
        ).stdout.strip()
        stamp = ledgerlib.format_time(now)
        agent = body.get("agent") or "claims"
        env = {
            "GIT_AUTHOR_NAME": agent,
            "GIT_AUTHOR_EMAIL": "%s@claims.invalid" % agent,
            "GIT_COMMITTER_NAME": agent,
            "GIT_COMMITTER_EMAIL": "%s@claims.invalid" % agent,
            "GIT_AUTHOR_DATE": stamp,
            "GIT_COMMITTER_DATE": stamp,
        }
        args = [
            "commit-tree",
            tree,
            "-m",
            "claim %s by %s (attempt %s)"
            % (body["task"], agent, body.get("attempt", 1)),
        ]
        if parent:
            args += ["-p", parent]
        return self.git(args, env=env).stdout.strip()

    def push_lease(self, sha, ref, expect):
        """Push sha to ref only if the ref is currently `expect` ('' = must not exist).

        Returns True when the remote accepted the update, False when it was refused because
        the ref is not in the expected state. Other failures raise ClaimError.
        """
        done = self.git(
            [
                "push",
                "-q",
                "--force-with-lease=%s:%s" % (ref, expect),
                self.remote,
                "%s:%s" % (sha, ref),
            ],
            check=False,
        )
        if done.returncode == 0:
            return True
        message = (done.stderr or "") + (done.stdout or "")
        if any(
            word in message
            for word in (
                "stale info",
                "rejected",
                "already exists",
                "cannot lock ref",
                "failed to update ref",
                "reference already",
            )
        ):
            return False
        raise ClaimError(
            "push of %s failed: %s" % (ref, message.strip()[-400:]), code=2
        )

    def delete_lease(self, ref, expect):
        done = self.git(
            [
                "push",
                "-q",
                "--force-with-lease=%s:%s" % (ref, expect),
                self.remote,
                ":%s" % ref,
            ],
            check=False,
        )
        if done.returncode == 0:
            return True
        message = (done.stderr or "") + (done.stdout or "")
        if any(
            word in message
            for word in (
                "stale info",
                "rejected",
                "failed to delete",
                "cannot lock ref",
                "failed to update ref",
            )
        ):
            return False
        raise ClaimError(
            "delete of %s failed: %s" % (ref, message.strip()[-400:]), code=2
        )


# --------------------------------------------------------------------------------------
# Liveness
# --------------------------------------------------------------------------------------


def last_alive(claim, heartbeats):
    """Newest sign of life of a claim (aware datetime or None).

    heartbeats: {agent: [heartbeat dicts with a parsed "time" and optional "task"]}.
    """
    times = []
    for key in ("claimed_at", "renewed_at"):
        when = ledgerlib.try_parse_time(claim.get(key))
        if when:
            times.append(when)
    for beat in (heartbeats or {}).get(claim.get("agent"), []):
        if beat.get("task") in (None, "", claim.get("task")):
            times.append(beat["time"])
    return max(times) if times else None


def describe(claim, heartbeats, now, expiry_minutes=EXPIRY_MINUTES):
    """The claim plus its liveness verdict, ready for JSON."""
    alive = last_alive(claim, heartbeats)
    idle = ledgerlib.minutes_between(alive, now) if alive else None
    return {
        "task": claim.get("task"),
        "agent": claim.get("agent"),
        "sha": claim.get("sha"),
        "branch": claim.get("branch"),
        "attempt": claim.get("attempt", 1),
        "previous_agent": claim.get("previous_agent"),
        "claimed_at": claim.get("claimed_at"),
        "renewed_at": claim.get("renewed_at"),
        "last_alive": ledgerlib.format_time(alive) if alive else None,
        "idle_minutes": round(idle, 1) if idle is not None else None,
        "expired": bool(idle is None or idle > expiry_minutes),
        "invalid": bool(claim.get("invalid")),
    }


def read_heartbeats(directory, lookback_days=2, now=None):
    """Heartbeats of every agent: {agent: [beat dicts sorted by time]} (see ledgerlib)."""
    return ledgerlib.read_heartbeats(directory, lookback_days, now)[0]


# --------------------------------------------------------------------------------------
# Operations (each returns a JSON-able dict or raises ClaimError)
# --------------------------------------------------------------------------------------


def _check_task_file(tasks_dir, task):
    path = os.path.join(tasks_dir, task + ".json")
    if not os.path.isfile(path):
        raise ClaimError("no task file %s" % path, code=4)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            status = json.load(handle).get("status")
    except (OSError, ValueError) as exc:
        raise ClaimError("task file %s is unreadable: %s" % (path, exc), code=4)
    if status in FINISHED_STATES:
        raise ClaimError("task %s is already %s" % (task, status), code=4)


def claim_task(
    remote, task, agent, now, branch=None, note="", tasks_dir=None, namespace=NAMESPACE
):
    if not valid_name(task) or not valid_name(agent):
        raise ClaimError(
            "task and agent names use letters, digits, '.', '_' and '-' only", code=2
        )
    if tasks_dir:
        _check_task_file(tasks_dir, task)
    ref = ref_for(task, namespace)
    with Workspace(remote) as ws:
        existing = ws.fetch_claims(namespace).get(task)
        if existing is not None:
            if existing.get("agent") == agent:
                result = _renew(ws, existing, agent, now, namespace)
                result["already_yours"] = True
                return result
            raise ClaimError(
                "task %s is claimed by %s since %s"
                % (task, existing.get("agent"), existing.get("claimed_at")),
                code=3,
                detail={
                    "held_by": existing.get("agent"),
                    "claimed_at": existing.get("claimed_at"),
                    "sha": existing.get("sha"),
                },
            )
        body = {
            "schema": 1,
            "task": task,
            "agent": agent,
            "branch": branch or "task/%s" % task,
            "claimed_at": ledgerlib.format_time(now),
            "renewed_at": ledgerlib.format_time(now),
            "attempt": 1,
            "previous_agent": None,
            "note": note,
        }
        sha = ws.make_claim_commit(body, None, now)
        if not ws.push_lease(sha, ref, ""):
            winner = ws.fetch_claims(namespace).get(task, {})
            raise ClaimError(
                "task %s was claimed by %s a moment earlier"
                % (task, winner.get("agent")),
                code=3,
                detail={
                    "held_by": winner.get("agent"),
                    "claimed_at": winner.get("claimed_at"),
                    "sha": winner.get("sha"),
                },
            )
        return {
            "claimed": True,
            "task": task,
            "agent": agent,
            "sha": sha,
            "ref": ref,
            "attempt": 1,
        }


def _renew(ws, claim, agent, now, namespace):
    body = {k: v for k, v in claim.items() if k != "sha"}
    body["renewed_at"] = ledgerlib.format_time(now)
    sha = ws.make_claim_commit(body, claim["sha"], now)
    ref = ref_for(claim["task"], namespace)
    if not ws.push_lease(sha, ref, claim["sha"]):
        raise ClaimError(
            "claim on %s changed hands: the task is no longer yours" % claim["task"],
            code=3,
        )
    return {
        "renewed": True,
        "task": claim["task"],
        "agent": agent,
        "sha": sha,
        "ref": ref,
        "attempt": body.get("attempt", 1),
    }


def renew_task(remote, task, agent, now, namespace=NAMESPACE):
    with Workspace(remote) as ws:
        claim = ws.fetch_claims(namespace).get(task)
        if claim is None:
            raise ClaimError("task %s has no claim" % task, code=3)
        if claim.get("agent") != agent:
            raise ClaimError(
                "task %s is claimed by %s, not %s" % (task, claim.get("agent"), agent),
                code=3,
            )
        return _renew(ws, claim, agent, now, namespace)


def release_task(remote, task, agent, force=False, namespace=NAMESPACE):
    with Workspace(remote) as ws:
        claim = ws.fetch_claims(namespace).get(task)
        if claim is None:
            raise ClaimError("task %s has no claim" % task, code=3)
        if claim.get("agent") != agent and not force:
            raise ClaimError(
                "task %s is claimed by %s, not %s" % (task, claim.get("agent"), agent),
                code=3,
            )
        if not ws.delete_lease(ref_for(task, namespace), claim["sha"]):
            raise ClaimError("claim on %s changed hands while releasing" % task, code=3)
        return {
            "released": True,
            "task": task,
            "agent": claim.get("agent"),
            "sha": claim["sha"],
        }


def show_task(
    remote, task, heartbeats, now, expiry_minutes=EXPIRY_MINUTES, namespace=NAMESPACE
):
    with Workspace(remote) as ws:
        claim = ws.fetch_claims(namespace).get(task)
    if claim is None:
        return {"task": task, "claimed": False}
    info = describe(claim, heartbeats, now, expiry_minutes)
    info["claimed"] = True
    return info


def list_claims(
    remote, heartbeats, now, expiry_minutes=EXPIRY_MINUTES, namespace=NAMESPACE
):
    with Workspace(remote) as ws:
        records = ws.fetch_claims(namespace)
    return [
        describe(records[task], heartbeats, now, expiry_minutes)
        for task in sorted(records)
    ]


def takeover_task(
    remote,
    task,
    agent,
    heartbeats,
    now,
    expiry_minutes=EXPIRY_MINUTES,
    namespace=NAMESPACE,
):
    if not valid_name(agent):
        raise ClaimError(
            "agent names use letters, digits, '.', '_' and '-' only", code=2
        )
    with Workspace(remote) as ws:
        claim = ws.fetch_claims(namespace).get(task)
        if claim is None:
            raise ClaimError("task %s has no claim: use claim" % task, code=3)
        info = describe(claim, heartbeats, now, expiry_minutes)
        if not info["expired"]:
            raise ClaimError(
                "claim on %s is alive: %s was last seen %s minutes ago (expiry %d)"
                % (task, claim.get("agent"), info["idle_minutes"], expiry_minutes),
                code=3,
                detail=info,
            )
        body = {
            "schema": 1,
            "task": task,
            "agent": agent,
            "branch": claim.get("branch") or "task/%s" % task,
            "claimed_at": ledgerlib.format_time(now),
            "renewed_at": ledgerlib.format_time(now),
            "attempt": int(claim.get("attempt", 1)) + 1,
            "previous_agent": claim.get("agent"),
            "note": "takeover of an expired claim",
        }
        sha = ws.make_claim_commit(body, claim["sha"], now)
        if not ws.push_lease(sha, ref_for(task, namespace), claim["sha"]):
            raise ClaimError("another agent took over %s first" % task, code=3)
        return {
            "taken_over": True,
            "task": task,
            "agent": agent,
            "previous_agent": claim.get("agent"),
            "attempt": body["attempt"],
            "sha": sha,
        }


def reap_claims(
    remote,
    heartbeats,
    now,
    expiry_minutes=EXPIRY_MINUTES,
    apply=False,
    namespace=NAMESPACE,
):
    with Workspace(remote) as ws:
        records = ws.fetch_claims(namespace)
        expired = []
        for task in sorted(records):
            info = describe(records[task], heartbeats, now, expiry_minutes)
            if not info["expired"]:
                continue
            info["deleted"] = False
            if apply:
                info["deleted"] = ws.delete_lease(
                    ref_for(task, namespace), records[task]["sha"]
                )
            expired.append(info)
    return {"applied": bool(apply), "expired": expired}


# --------------------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------------------


def build_parser():
    parser = argparse.ArgumentParser(description="Atomic task claims on a git remote.")
    parser.add_argument(
        "--remote", default="origin", help="URL, path or remote name (default origin)"
    )
    parser.add_argument("--now", help="time to use instead of the clock (ISO 8601 UTC)")
    parser.add_argument(
        "--namespace",
        default=NAMESPACE,
        help="ref prefix of claims (default %s)" % NAMESPACE,
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name, help_text, task=True, agent=False):
        p = sub.add_parser(name, help=help_text)
        if task:
            p.add_argument("task")
        if agent:
            p.add_argument("--agent", required=True)
        return p

    p = add("claim", "create the claim ref; fails if it exists", agent=True)
    p.add_argument("--branch")
    p.add_argument("--note", default="")
    p.add_argument(
        "--tasks",
        help="directory of task files (ledger/tasks); refuse unknown or finished tasks",
    )
    add(
        "renew",
        "move the claim forward (a sign of life); fails if the claim changed hands",
        agent=True,
    )
    p = add("release", "delete the claim (owner only)", agent=True)
    p.add_argument(
        "--force", action="store_true", help="release a claim held by another agent"
    )
    for name, text, with_task, with_agent in (
        ("show", "show one claim and whether it expired", True, False),
        ("list", "list every claim", False, False),
        ("takeover", "replace an expired claim with a new owner", True, True),
        ("reap", "find (and with --apply delete) expired claims", False, False),
    ):
        p = add(name, text, task=with_task, agent=with_agent)
        p.add_argument(
            "--heartbeats",
            help="heartbeat directory (ledger/heartbeats) that keeps claims alive",
        )
        p.add_argument("--expiry-minutes", type=float, default=EXPIRY_MINUTES)
        if name == "reap":
            p.add_argument("--apply", action="store_true")
    return parser


def run(argv=None, out=None):
    out = out or sys.stdout
    args = build_parser().parse_args(argv)
    try:
        now = ledgerlib.parse_time(args.now) if args.now else ledgerlib.utcnow()
    except ValueError as exc:
        print("claims: %s" % exc, file=sys.stderr)
        return 2
    try:
        heartbeats = {}
        if getattr(args, "heartbeats", None):
            heartbeats = read_heartbeats(args.heartbeats, now=now)
        ns = args.namespace if args.namespace.endswith("/") else args.namespace + "/"
        if args.command == "claim":
            result = claim_task(
                args.remote,
                args.task,
                args.agent,
                now,
                args.branch,
                args.note,
                args.tasks,
                ns,
            )
        elif args.command == "renew":
            result = renew_task(args.remote, args.task, args.agent, now, ns)
        elif args.command == "release":
            result = release_task(args.remote, args.task, args.agent, args.force, ns)
        elif args.command == "show":
            result = show_task(
                args.remote, args.task, heartbeats, now, args.expiry_minutes, ns
            )
        elif args.command == "list":
            result = list_claims(args.remote, heartbeats, now, args.expiry_minutes, ns)
        elif args.command == "takeover":
            result = takeover_task(
                args.remote,
                args.task,
                args.agent,
                heartbeats,
                now,
                args.expiry_minutes,
                ns,
            )
        else:
            result = reap_claims(
                args.remote, heartbeats, now, args.expiry_minutes, args.apply, ns
            )
    except ClaimError as exc:
        print("claims: %s" % exc, file=sys.stderr)
        json.dump({"error": str(exc), **exc.detail}, out, indent=2, sort_keys=True)
        out.write("\n")
        return exc.code
    json.dump(result, out, indent=2, sort_keys=True)
    out.write("\n")
    return 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
