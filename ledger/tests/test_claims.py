"""Tests for ledger/tools/claims.py using real temporary git repositories."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest

import claims  # noqa: E402
import ledgerlib  # noqa: E402

from . import support
from .support import RemoteCase, beat, git, iso, write_beats


def load(result):
    return json.loads(result.stdout)


class CreateClaim(RemoteCase):
    def test_claim_creates_the_ref_and_records_the_claimant(self):
        result = self.tool(
            "--now", iso(0), "claim", "T-1", "--agent", "alpha", "--note", "first"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        refs = self.claim_refs()
        self.assertEqual(list(refs), ["refs/heads/claim/T-1"])
        body = self.read_claim_json(refs["refs/heads/claim/T-1"])
        self.assertEqual(body["agent"], "alpha")
        self.assertEqual(body["task"], "T-1")
        self.assertEqual(body["branch"], "task/T-1")
        self.assertEqual(body["claimed_at"], iso(0))
        self.assertEqual(body["attempt"], 1)
        self.assertEqual(load(result)["sha"], refs["refs/heads/claim/T-1"])

    def test_a_second_claim_of_the_same_task_fails_and_changes_nothing(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        before = self.claim_refs()
        result = self.tool("--now", iso(1), "claim", "T-1", "--agent", "beta")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(load(result)["held_by"], "alpha")
        self.assertIn("claimed by alpha", result.stderr)
        self.assertEqual(self.claim_refs(), before)

    def test_the_same_agent_claiming_again_renews_instead(self):
        first = load(self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha"))
        again = self.tool("--now", iso(10), "claim", "T-1", "--agent", "alpha")
        self.assertEqual(again.returncode, 0, again.stderr)
        data = load(again)
        self.assertTrue(data["already_yours"] and data["renewed"])
        self.assertNotEqual(data["sha"], first["sha"])
        self.assertEqual(self.read_claim_json(data["sha"])["renewed_at"], iso(10))
        self.assertEqual(self.read_claim_json(data["sha"])["claimed_at"], iso(0))

    def test_different_tasks_do_not_interfere(self):
        for task, agent in (("T-1", "alpha"), ("T-2", "beta"), ("T-3", "alpha")):
            self.assertEqual(
                self.tool("--now", iso(0), "claim", task, "--agent", agent).returncode,
                0,
            )
        self.assertEqual(
            sorted(self.claim_refs()),
            ["refs/heads/claim/T-1", "refs/heads/claim/T-2", "refs/heads/claim/T-3"],
        )

    def test_bad_names_are_refused_before_git_is_touched(self):
        for task in ("a/b", "..", "x.lock", "-x", "a b", "t."):
            result = self.tool("claim", task, "--agent", "alpha")
            self.assertIn(result.returncode, (2,), task)
        self.assertEqual(self.tool("claim", "T-9", "--agent", "bad name").returncode, 2)
        self.assertEqual(self.claim_refs(), {})

    def test_claims_never_touch_the_refs_of_the_local_repository(self):
        clone = self.path("clone")
        git("clone", "-q", self.remote, clone)
        git("commit", "--allow-empty", "-q", "-m", "init", cwd=clone)
        before = git("for-each-ref", cwd=clone).stdout
        result = subprocess.run(
            [
                sys.executable,
                os.path.join(support.TOOLS_DIR, "claims.py"),
                "claim",
                "T-1",
                "--agent",
                "alpha",
            ],
            cwd=clone,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            result.returncode, 0, result.stderr
        )  # --remote defaults to origin
        self.assertEqual(git("for-each-ref", cwd=clone).stdout, before)
        self.assertEqual(list(self.claim_refs()), ["refs/heads/claim/T-1"])

    def test_task_file_gate(self):
        tasks = self.path("tasks")
        os.makedirs(tasks)
        self.assertEqual(
            self.tool("claim", "T-1", "--agent", "alpha", "--tasks", tasks).returncode,
            4,
        )  # no task file
        with open(os.path.join(tasks, "T-1.json"), "w") as handle:
            json.dump({"id": "T-1", "status": "merged"}, handle)
        self.assertEqual(
            self.tool("claim", "T-1", "--agent", "alpha", "--tasks", tasks).returncode,
            4,
        )  # finished
        with open(os.path.join(tasks, "T-1.json"), "w") as handle:
            json.dump({"id": "T-1", "status": "open"}, handle)
        self.assertEqual(
            self.tool("claim", "T-1", "--agent", "alpha", "--tasks", tasks).returncode,
            0,
        )
        self.assertEqual(self.claim_refs().keys(), {"refs/heads/claim/T-1"})


class Race(RemoteCase):
    def race(self, task, agents):
        processes = [
            subprocess.Popen(
                [
                    sys.executable,
                    os.path.join(support.TOOLS_DIR, "claims.py"),
                    "--remote",
                    self.remote,
                    "--now",
                    iso(0),
                    "claim",
                    task,
                    "--agent",
                    agent,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            for agent in agents
        ]
        outcomes = []
        for agent, process in zip(agents, processes):
            out, err = process.communicate(timeout=120)
            outcomes.append((agent, process.returncode, out, err))
        return outcomes

    def test_exactly_one_of_many_simultaneous_claims_wins(self):
        agents = ["agent%d" % i for i in range(8)]
        for round_number in range(3):
            task = "T-race-%d" % round_number
            outcomes = self.race(task, agents)
            winners = [a for a, code, _, _ in outcomes if code == 0]
            losers = [a for a, code, _, _ in outcomes if code == 3]
            self.assertEqual(len(winners), 1, outcomes)
            self.assertEqual(len(losers), len(agents) - 1, outcomes)
            body = self.read_claim_json(self.claim_refs()["refs/heads/claim/" + task])
            self.assertEqual(body["agent"], winners[0])

    def test_exactly_one_simultaneous_takeover_wins(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        agents = ["taker%d" % i for i in range(6)]
        processes = [
            subprocess.Popen(
                [
                    sys.executable,
                    os.path.join(support.TOOLS_DIR, "claims.py"),
                    "--remote",
                    self.remote,
                    "--now",
                    iso(60),
                    "takeover",
                    "T-1",
                    "--agent",
                    agent,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            for agent in agents
        ]
        codes = []
        for process in processes:
            process.communicate(timeout=120)
            codes.append(process.returncode)
        self.assertEqual(codes.count(0), 1, codes)
        self.assertEqual(codes.count(3), len(agents) - 1, codes)
        body = self.read_claim_json(self.claim_refs()["refs/heads/claim/T-1"])
        self.assertEqual((body["attempt"], body["previous_agent"]), (2, "alpha"))


class Renew(RemoteCase):
    def test_owner_renews_and_the_ref_moves_forward(self):
        first = load(self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha"))
        renewed = self.tool("--now", iso(20), "renew", "T-1", "--agent", "alpha")
        self.assertEqual(renewed.returncode, 0, renewed.stderr)
        sha = load(renewed)["sha"]
        self.assertEqual(self.claim_refs()["refs/heads/claim/T-1"], sha)
        parent = git("--git-dir", self.remote, "rev-parse", sha + "^").stdout.strip()
        self.assertEqual(parent, first["sha"])

    def test_non_owner_cannot_renew(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.assertEqual(
            self.tool("--now", iso(5), "renew", "T-1", "--agent", "beta").returncode, 3
        )

    def test_renew_without_a_claim_fails(self):
        self.assertEqual(self.tool("renew", "T-1", "--agent", "alpha").returncode, 3)

    def test_previous_owner_cannot_renew_after_a_takeover(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.assertEqual(
            self.tool(
                "--now", iso(60), "takeover", "T-1", "--agent", "beta"
            ).returncode,
            0,
        )
        result = self.tool("--now", iso(61), "renew", "T-1", "--agent", "alpha")
        self.assertEqual(result.returncode, 3)
        self.assertIn("not alpha", result.stderr)


class Expiry(RemoteCase):
    def listing(self, minutes, heartbeats=None):
        args = ["--now", iso(minutes), "list"]
        if heartbeats:
            args += ["--heartbeats", heartbeats]
        result = self.tool(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        return load(result)

    def test_claim_expires_after_45_minutes_without_a_sign_of_life(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.assertFalse(self.listing(44)[0]["expired"])
        self.assertFalse(
            self.listing(45)[0]["expired"]
        )  # exactly 45 minutes is not past the limit
        late = self.listing(46)[0]
        self.assertTrue(late["expired"])
        self.assertEqual(late["idle_minutes"], 46.0)

    def test_the_owners_heartbeats_keep_the_claim_alive(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        beats = self.path("heartbeats")
        write_beats(beats, "alpha", [beat(40, task="T-1")])
        self.assertFalse(self.listing(80, beats)[0]["expired"])
        self.assertTrue(self.listing(86, beats)[0]["expired"])

    def test_other_agents_and_other_tasks_do_not_keep_it_alive(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        beats = self.path("heartbeats")
        write_beats(beats, "beta", [beat(40, task="T-1")])
        write_beats(beats, "alpha", [beat(40, task="T-OTHER")])
        self.assertTrue(self.listing(60, beats)[0]["expired"])

    def test_a_heartbeat_without_a_task_counts_for_the_agents_claim(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        beats = self.path("heartbeats")
        write_beats(
            beats,
            "alpha",
            [{"time": iso(40), "what": "x", "commit": "c", "evidence": "e"}],
        )
        self.assertFalse(self.listing(60, beats)[0]["expired"])

    def test_renew_is_a_sign_of_life(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.tool("--now", iso(40), "renew", "T-1", "--agent", "alpha")
        self.assertFalse(self.listing(80)[0]["expired"])
        self.assertTrue(self.listing(90)[0]["expired"])

    def test_show(self):
        self.assertEqual(
            load(self.tool("show", "T-1")), {"claimed": False, "task": "T-1"}
        )
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        info = load(self.tool("--now", iso(50), "show", "T-1"))
        self.assertTrue(info["claimed"] and info["expired"])
        self.assertEqual(info["agent"], "alpha")


class Takeover(RemoteCase):
    def test_takeover_is_refused_while_the_claim_is_alive(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        result = self.tool("--now", iso(30), "takeover", "T-1", "--agent", "beta")
        self.assertEqual(result.returncode, 3)
        self.assertIn("is alive", result.stderr)
        self.assertEqual(
            self.read_claim_json(self.claim_refs()["refs/heads/claim/T-1"])["agent"],
            "alpha",
        )

    def test_takeover_of_an_expired_claim_keeps_the_history(self):
        first = load(self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha"))
        result = self.tool("--now", iso(50), "takeover", "T-1", "--agent", "beta")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = load(result)
        self.assertEqual((data["attempt"], data["previous_agent"]), (2, "alpha"))
        sha = self.claim_refs()["refs/heads/claim/T-1"]
        body = self.read_claim_json(sha)
        self.assertEqual(
            (body["agent"], body["claimed_at"], body["attempt"]), ("beta", iso(50), 2)
        )
        self.assertEqual(
            git("--git-dir", self.remote, "rev-parse", sha + "^").stdout.strip(),
            first["sha"],
        )
        # the second relaunch is attempt 3: the retry count the plan limits to two relaunches
        third = load(
            self.tool("--now", iso(120), "takeover", "T-1", "--agent", "gamma")
        )
        self.assertEqual(third["attempt"], 3)

    def test_takeover_without_a_claim_is_refused(self):
        self.assertEqual(self.tool("takeover", "T-1", "--agent", "beta").returncode, 3)

    def test_heartbeats_prevent_a_takeover(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        beats = self.path("heartbeats")
        write_beats(beats, "alpha", [beat(40, task="T-1")])
        result = self.tool(
            "--now",
            iso(60),
            "takeover",
            "T-1",
            "--agent",
            "beta",
            "--heartbeats",
            beats,
        )
        self.assertEqual(result.returncode, 3)


class ReleaseAndReap(RemoteCase):
    def test_owner_releases(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.assertEqual(self.tool("release", "T-1", "--agent", "alpha").returncode, 0)
        self.assertEqual(self.claim_refs(), {})
        self.assertEqual(
            self.tool("--now", iso(1), "claim", "T-1", "--agent", "beta").returncode, 0
        )  # free again

    def test_non_owner_cannot_release_without_force(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.assertEqual(self.tool("release", "T-1", "--agent", "beta").returncode, 3)
        self.assertEqual(len(self.claim_refs()), 1)
        self.assertEqual(
            self.tool("release", "T-1", "--agent", "beta", "--force").returncode, 0
        )
        self.assertEqual(self.claim_refs(), {})

    def test_release_of_a_missing_claim_fails(self):
        self.assertEqual(self.tool("release", "T-1", "--agent", "alpha").returncode, 3)

    def test_reap_dry_run_then_apply_deletes_only_expired_claims(self):
        self.tool("--now", iso(0), "claim", "T-old", "--agent", "alpha")
        self.tool("--now", iso(40), "claim", "T-new", "--agent", "beta")
        dry = load(self.tool("--now", iso(60), "reap"))
        self.assertEqual([c["task"] for c in dry["expired"]], ["T-old"])
        self.assertEqual(len(self.claim_refs()), 2)
        applied = load(self.tool("--now", iso(60), "reap", "--apply"))
        self.assertTrue(applied["expired"][0]["deleted"])
        self.assertEqual(list(self.claim_refs()), ["refs/heads/claim/T-new"])


class Library(unittest.TestCase):
    def test_describe_without_any_sign_of_life_is_expired(self):
        info = claims.describe({"task": "T", "agent": "a", "sha": "x"}, {}, support.T0)
        self.assertTrue(info["expired"])
        self.assertIsNone(info["last_alive"])

    def test_last_alive_picks_the_newest_sign(self):
        claim = {
            "task": "T-1",
            "agent": "a",
            "claimed_at": iso(0),
            "renewed_at": iso(5),
        }
        beats = {
            "a": [
                {"time": ledgerlib.parse_time(iso(12)), "task": "T-1"},
                {"time": ledgerlib.parse_time(iso(30)), "task": "T-2"},
            ]
        }
        self.assertEqual(
            ledgerlib.format_time(claims.last_alive(claim, beats)), iso(12)
        )

    def test_resolve_remote_keeps_paths_and_urls(self):
        self.assertEqual(claims.resolve_remote("/some/path.git"), "/some/path.git")
        self.assertEqual(
            claims.resolve_remote("https://example.invalid/r.git"),
            "https://example.invalid/r.git",
        )
        with self.assertRaises(claims.ClaimError):
            claims.resolve_remote("no-such-remote-name-xyz")


if __name__ == "__main__":
    unittest.main()
