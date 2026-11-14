"""Tests for ledger/tools/watchdog.py: one group of fixtures per rule of plan 7.6."""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys
import unittest

from . import support
from .support import RemoteCase, TempDirCase, T0, beat, iso, write_beats

import ledgerlib  # noqa: E402
import watchdog  # noqa: E402


class WatchdogCase(TempDirCase):
    def setUp(self):
        super().setUp()
        self.ledger = self.path("ledger")
        self.heartbeats = os.path.join(self.ledger, "heartbeats")
        os.makedirs(self.heartbeats)

    def beats(self, agent, items, day=None):
        write_beats(self.heartbeats, agent, items, day=day)

    def run_dog(self, minutes, claims=None, *extra):
        argv = ["--ledger", self.ledger, "--now", iso(minutes)]
        if claims is not None:
            path = self.path("claims.json")
            with open(path, "w") as handle:
                json.dump(claims, handle)
            argv += ["--claims-file", path]
        out = io.StringIO()
        code = watchdog.run(argv + list(extra), out=out)
        self.code = code
        return json.loads(out.getvalue())

    def types(self, report, kind=None):
        found = [i["type"] for i in report["incidents"]]
        return sorted(t for t in found if kind is None or t == kind)

    def claim(self, task="T-1", agent="alpha", at=0, attempt=1, renewed=None):
        return {
            "task": task, "agent": agent, "sha": "a" * 40, "branch": "task/" + task,
            "claimed_at": iso(at), "renewed_at": iso(renewed if renewed is not None else at), "attempt": attempt,
        }


class HealthyAgent(WatchdogCase):
    def test_an_agent_that_heartbeats_and_commits_has_no_incident(self):
        self.beats("alpha", [beat(m, commit="c%d" % m, evidence="e%d" % m) for m in (0, 8, 16, 24)])
        report = self.run_dog(30)
        self.assertEqual(report["incidents"], [])
        self.assertEqual(report["agents"][0]["status"], "ok")
        self.assertEqual(report["agents"][0]["beats"], 4)

    def test_no_data_at_all_is_quiet(self):
        report = self.run_dog(0)
        self.assertEqual((report["incidents"], report["agents"], report["claims"]), ([], [], []))


class NoHeartbeat(WatchdogCase):
    def test_fifteen_minutes_is_the_limit(self):
        self.beats("alpha", [beat(0, commit="c1")])
        self.assertEqual(self.run_dog(15)["incidents"], [])
        report = self.run_dog(16)
        self.assertEqual(self.types(report), ["no_heartbeat"])
        incident = report["incidents"][0]
        self.assertEqual((incident["agent"], incident["task"], incident["since"]), ("alpha", "T-1", iso(0)))
        self.assertEqual((incident["severity"], incident["action"]), ("high", "relaunch"))
        self.assertEqual(report["agents"][0]["status"], "stalled")

    def test_thresholds_are_options(self):
        self.beats("alpha", [beat(0)])
        self.assertEqual(self.types(self.run_dog(7, None, "--heartbeat-timeout", "5")), ["no_heartbeat"])

    def test_a_finished_agent_is_not_stalled(self):
        self.beats("alpha", [beat(0), beat(3, state="done")])
        report = self.run_dog(300)
        self.assertEqual(report["incidents"], [])
        self.assertEqual(report["agents"][0]["status"], "finished")

    def test_idle_and_handoff_states_are_finished_too(self):
        for state in ("idle", "handoff", "released"):
            self.setUp()
            self.beats("alpha", [beat(0, state=state)])
            self.assertEqual(self.run_dog(100)["incidents"], [], state)

    def test_one_stalled_agent_does_not_hide_a_healthy_one(self):
        self.beats("alpha", [beat(0)])
        self.beats("beta", [beat(25, task="T-2", commit="x"), beat(31, task="T-2", commit="y")])
        report = self.run_dog(35)
        flagged = {(i["type"], i["agent"]) for i in report["incidents"]}
        self.assertIn(("no_heartbeat", "alpha"), flagged)
        self.assertEqual({agent for _, agent in flagged}, {"alpha"})  # nothing is reported about beta
        self.assertEqual({a["agent"]: a["status"] for a in report["agents"]}, {"alpha": "stalled", "beta": "ok"})


class NoChange(WatchdogCase):
    def test_three_identical_heartbeats_mark_the_agent_stalled(self):
        self.beats("alpha", [beat(0, commit="c0"), beat(5, commit="c1"), beat(10, commit="c1"), beat(15, commit="c1"), beat(20, commit="c1")])
        # c1 repeated four times from minute 5: the tail has at least three
        report = self.run_dog(22)
        kinds = self.types(report)
        self.assertIn("no_change", kinds)
        incident = [i for i in report["incidents"] if i["type"] == "no_change"][0]
        self.assertEqual(incident["since"], iso(5))
        self.assertEqual(incident["heartbeats_unchanged"], 4)
        self.assertEqual(incident["action"], "relaunch")

    def test_two_identical_heartbeats_are_not_enough(self):
        self.beats("alpha", [beat(0, commit="c0"), beat(5, commit="c1"), beat(10, commit="c1")])
        self.assertEqual(self.types(self.run_dog(12), "no_change"), [])

    def test_a_changing_metric_is_a_change(self):
        self.beats("alpha", [beat(m, metric=m) for m in (0, 5, 10, 15)])
        self.assertEqual(self.types(self.run_dog(16), "no_change"), [])

    def test_new_evidence_is_a_change(self):
        self.beats("alpha", [beat(0, evidence="e1"), beat(5, evidence="e2"), beat(10, evidence="e3")])
        self.assertEqual(self.types(self.run_dog(11), "no_change"), [])

    def test_rewording_the_message_is_not_a_change(self):
        self.beats("alpha", [beat(m, what="still working %d" % m) for m in (0, 5, 10)])
        self.assertEqual(self.types(self.run_dog(11), "no_change"), ["no_change"])

    def test_waiting_for_ci_is_excused_for_forty_minutes(self):
        self.beats("alpha", [beat(m, waiting_on="ci-4711") for m in (0, 10, 20)])
        report = self.run_dog(35)
        self.assertEqual(self.types(report, "no_change"), [])
        self.assertEqual(report["agents"][0]["status"], "waiting")
        self.assertEqual(report["agents"][0]["waiting_on"], "ci-4711")
        late = self.run_dog(41)
        self.assertEqual(self.types(late, "no_change"), ["no_change"])
        self.assertIn("ci-4711", [i for i in late["incidents"] if i["type"] == "no_change"][0]["detail"])

    def test_the_count_is_an_option(self):
        self.beats("alpha", [beat(0), beat(5)])
        self.assertEqual(self.types(self.run_dog(6, None, "--no-change-beats", "2"), "no_change"), ["no_change"])


class CommitOverdue(WatchdogCase):
    def test_no_new_commit_for_thirty_minutes_is_a_warning(self):
        self.beats("alpha", [beat(m, commit="c0", evidence="e%d" % m) for m in (0, 8, 16, 24, 32)])
        report = self.run_dog(34)
        incident = [i for i in report["incidents"] if i["type"] == "commit_overdue"][0]
        self.assertEqual((incident["severity"], incident["action"], incident["since"]), ("warn", "none", iso(0)))

    def test_a_commit_within_thirty_minutes_is_fine(self):
        self.beats("alpha", [beat(0, commit="c0", evidence="e0"), beat(10, commit="c1", evidence="e1"), beat(20, commit="c1", evidence="e2"), beat(30, commit="c1", evidence="e3")])
        self.assertEqual(self.types(self.run_dog(32), "commit_overdue"), [])


class FirstOutput(WatchdogCase):
    def test_no_output_ten_minutes_after_the_claim(self):
        claim = self.claim(at=0)
        self.beats("alpha", [{"time": iso(4), "task": "T-1", "what": "reading the plan"}])
        self.assertEqual(self.types(self.run_dog(9, [claim]), "late_first_output"), [])
        report = self.run_dog(11, [claim])
        incident = [i for i in report["incidents"] if i["type"] == "late_first_output"][0]
        self.assertEqual((incident["agent"], incident["since"], incident["action"]), ("alpha", iso(0), "relaunch"))

    def test_a_commit_or_evidence_in_a_heartbeat_is_the_first_output(self):
        claim = self.claim(at=0)
        self.beats("alpha", [{"time": iso(5), "task": "T-1", "what": "x", "commit": "abc"}])
        self.assertEqual(self.types(self.run_dog(12, [claim]), "late_first_output"), [])
        self.setUp()
        self.beats("alpha", [{"time": iso(5), "task": "T-1", "what": "x", "evidence": "4 tests ok"}])
        self.assertEqual(self.types(self.run_dog(12, [claim]), "late_first_output"), [])

    def test_output_from_before_the_claim_does_not_count(self):
        claim = self.claim(at=20)
        self.beats("alpha", [beat(5, commit="old")])
        self.assertEqual(self.types(self.run_dog(33, [claim]), "late_first_output"), ["late_first_output"])

    def test_without_claims_the_first_heartbeat_is_the_launch(self):
        self.beats("alpha", [{"time": iso(0), "what": "started"}, {"time": iso(6), "what": "thinking"}])
        self.assertEqual(self.types(self.run_dog(12), "late_first_output"), ["late_first_output"])

    def test_a_claim_with_no_heartbeat_at_all(self):
        claim = self.claim(at=0)
        self.assertEqual(self.types(self.run_dog(5, [claim])), [])
        self.assertEqual(self.types(self.run_dog(11, [claim])), ["late_first_output"])
        late = self.run_dog(16, [claim])
        self.assertEqual(self.types(late), ["late_first_output", "no_heartbeat"])
        self.assertEqual([i for i in late["incidents"] if i["type"] == "no_heartbeat"][0]["since"], iso(0))


class ClaimExpiry(WatchdogCase):
    def test_claim_without_a_sign_of_life_expires_after_45_minutes(self):
        claim = self.claim(at=0)
        self.beats("alpha", [beat(1, commit="c1")])
        report = self.run_dog(47, [claim])
        incident = [i for i in report["incidents"] if i["type"] == "claim_expired"][0]
        self.assertEqual((incident["task"], incident["agent"], incident["action"]), ("T-1", "alpha", "takeover"))
        self.assertEqual(incident["since"], iso(1))  # the last sign of life: the heartbeat
        self.assertTrue(report["claims"][0]["expired"])

    def test_alive_claim(self):
        claim = self.claim(at=0)
        self.beats("alpha", [beat(40, commit="c1"), beat(46, commit="c2")])
        report = self.run_dog(60, [claim])
        self.assertEqual(self.types(report, "claim_expired"), [])
        self.assertFalse(report["claims"][0]["expired"])

    def test_renew_counts_as_a_sign_of_life(self):
        claim = self.claim(at=0, renewed=40)
        self.assertEqual(self.types(self.run_dog(80, [claim]), "claim_expired"), [])
        self.assertEqual(self.types(self.run_dog(90, [claim]), "claim_expired"), ["claim_expired"])

    def test_the_owner_who_finished_but_never_released(self):
        claim = self.claim(at=0)
        self.beats("alpha", [beat(5, commit="c1", state="done")])
        report = self.run_dog(60, [claim])
        incident = [i for i in report["incidents"] if i["type"] == "claim_expired"][0]
        self.assertEqual(incident["action"], "reap_claim")
        self.assertIn("never released", incident["detail"])

    def test_relaunch_accounting_splits_the_task_after_two_relaunches(self):
        tasks = os.path.join(self.ledger, "tasks")
        os.makedirs(tasks)
        report = self.run_dog(60, [self.claim(at=0, attempt=1)])
        self.assertEqual([i["action"] for i in report["incidents"] if i["type"] == "claim_expired"], ["takeover"])
        report = self.run_dog(60, [self.claim(at=0, attempt=2)])
        self.assertEqual([i["action"] for i in report["incidents"] if i["type"] == "claim_expired"], ["takeover"])
        report = self.run_dog(60, [self.claim(at=0, attempt=3)])  # two relaunches already
        incident = [i for i in report["incidents"] if i["type"] == "claim_expired"][0]
        self.assertEqual((incident["action"], incident["relaunches"]), ("split_task", 2))
        # the retries counter of the task file counts as well
        with open(os.path.join(tasks, "T-1.json"), "w") as handle:
            json.dump({"id": "T-1", "retries": 2, "status": "in_progress"}, handle)
        report = self.run_dog(60, [self.claim(at=0, attempt=1)])
        self.assertEqual([i["action"] for i in report["incidents"] if i["type"] == "claim_expired"], ["split_task"])

    def test_stall_incidents_use_the_same_relaunch_accounting(self):
        self.beats("alpha", [beat(0)])
        report = self.run_dog(20, [self.claim(at=0, attempt=3)])
        stall = [i for i in report["incidents"] if i["type"] == "no_heartbeat"][0]
        self.assertEqual((stall["action"], stall["relaunches"]), ("split_task", 2))

    def test_claim_expiry_threshold_is_an_option(self):
        self.assertEqual(self.types(self.run_dog(20, [self.claim(at=0)], "--claim-expiry", "10"), "claim_expired"), ["claim_expired"])


class Pace(WatchdogCase):
    DAY = ledgerlib.parse_time("2026-11-19T12:00:00Z")

    def credits(self, times):
        with open(os.path.join(self.ledger, "credits.jsonl"), "w") as handle:
            for index, when in enumerate(times):
                handle.write(json.dumps({"id": "D8-X-Y-%04d" % index, "at": ledgerlib.format_time(when)}) + "\n")

    def at(self, hours_before):
        import datetime

        return self.DAY - datetime.timedelta(hours=hours_before)

    def pace_report(self, total=8200, now=None, extra=()):
        out = io.StringIO()
        argv = ["--ledger", self.ledger, "--now", ledgerlib.format_time(now or self.DAY), "--total", str(total)] + list(extra)
        watchdog.run(argv, out=out)
        return json.loads(out.getvalue())

    def test_window_minimum_is_half_a_quarter_of_the_days_requirement(self):
        pace = ledgerlib.load_pace(None)
        # 18-24 Nov: 820 rows over 7 days = 117.14 per day; a quarter is 29.3; half of that 14.6
        self.assertAlmostEqual(ledgerlib.required_per_day(pace, 8200, self.DAY), 117.14, places=1)
        self.assertAlmostEqual(ledgerlib.window_minimum(pace, 8200, self.DAY), 14.64, places=1)

    def test_a_window_with_enough_rows_is_met(self):
        self.credits([self.at(1)] * 15)
        report = self.pace_report()
        self.assertEqual(report["pace"]["status"], "met")
        self.assertEqual(report["incidents"], [])

    def test_a_missed_window_opens_an_incident_with_a_two_hour_root_cause_deadline(self):
        self.credits([self.at(1)] * 14 + [self.at(8)] * 20)  # 14 < 14.64 now; the window before was met
        report = self.pace_report()
        self.assertEqual(report["pace"]["status"], "missed")
        incident = report["incidents"][0]
        self.assertEqual((incident["type"], incident["action"], incident["severity"]), ("pace_miss", "reallocate", "high"))
        self.assertEqual(incident["root_cause_due"], "2026-11-19T14:00:00Z")
        self.assertEqual((incident["rows_verified"], incident["rows_required"]), (14, 14.64))

    def test_rows_outside_the_window_do_not_count(self):
        self.credits([self.at(7)] * 100)
        self.assertEqual(self.pace_report()["pace"]["rows_verified"], 0)
        self.credits([self.at(6)] * 3)  # exactly six hours ago is outside (start exclusive)
        self.assertEqual(self.pace_report()["pace"]["rows_verified"], 0)

    def test_two_consecutive_misses_trigger_the_replan(self):
        self.credits([self.at(1)] * 2 + [self.at(8)] * 2)
        report = self.pace_report()
        self.assertEqual(self.types(report), ["pace_miss", "replan_trigger"])
        replan = [i for i in report["incidents"] if i["type"] == "replan_trigger"][0]
        self.assertEqual((replan["severity"], replan["action"]), ("critical", "replan"))
        self.assertEqual(report["incidents"][0]["type"], "replan_trigger")  # critical sorts first

    def test_a_met_previous_window_means_no_replan(self):
        self.credits([self.at(1)] * 2 + [self.at(8)] * 20)
        self.assertEqual(self.types(self.pace_report()), ["pace_miss"])

    def test_no_requirement_during_wave_zero_or_warmup_or_hardening(self):
        self.credits([])
        wave0 = self.pace_report(now=ledgerlib.parse_time("2026-11-16T12:00:00Z"))
        self.assertEqual((wave0["pace"]["status"], wave0["incidents"]), ("wave0_or_warmup", []))
        warmup = self.pace_report(now=ledgerlib.parse_time("2026-11-18T03:00:00Z"))  # the window starts before 18 Nov
        self.assertEqual((warmup["pace"]["status"], warmup["incidents"]), ("wave0_or_warmup", []))
        ready = self.pace_report(now=ledgerlib.parse_time("2026-11-18T06:00:00Z"))
        self.assertEqual(ready["pace"]["status"], "missed")
        hardening = self.pace_report(now=ledgerlib.parse_time("2026-12-20T12:00:00Z"))
        self.assertEqual((hardening["pace"]["status"], hardening["incidents"]), ("no_requirement", []))

    def test_the_requirement_rises_through_the_periods(self):
        pace = ledgerlib.load_pace(None)
        for stamp, per_day in (("2026-11-18T12:00:00Z", 117.1), ("2026-11-27T12:00:00Z", 205.0), ("2026-12-03T12:00:00Z", 292.9),
                               ("2026-12-10T12:00:00Z", 351.4), ("2026-12-16T12:00:00Z", 410.0)):
            self.assertAlmostEqual(ledgerlib.required_per_day(pace, 8200, ledgerlib.parse_time(stamp)), per_day, places=1)

    def test_the_register_size_comes_from_percent_json(self):
        self.credits([self.at(1)] * 14)
        with open(os.path.join(self.ledger, "percent.json"), "w") as handle:
            json.dump({"total": 4100}, handle)  # half the size: half the requirement (7.3)
        out = io.StringIO()
        watchdog.run(["--ledger", self.ledger, "--now", ledgerlib.format_time(self.DAY)], out=out)
        self.assertEqual(json.loads(out.getvalue())["pace"]["status"], "met")

    def test_without_credits_the_rule_is_skipped_not_failed(self):
        report = self.pace_report()
        self.assertEqual((report["pace"]["evaluated"], report["incidents"]), (False, []))

    def test_a_custom_pace_file_replaces_the_defaults(self):
        self.credits([self.at(1)] * 3)
        with open(os.path.join(self.ledger, "pace.json"), "w") as handle:
            json.dump({"periods": [{"from": "2026-11-18", "to": "2026-11-25", "cumulative_pct": 100}], "window_fraction": 0.1}, handle)
        report = self.pace_report(total=800)  # 800 rows over 8 days = 100 a day; 10% of a quarter = 2.5 rows
        self.assertEqual(report["pace"]["rows_required"], 2.5)
        self.assertEqual(report["pace"]["status"], "met")
        self.credits([self.at(1)] * 2)
        self.assertEqual(self.pace_report(total=800)["pace"]["status"], "missed")


class Malformed(WatchdogCase):
    def test_bad_lines_are_reported_and_good_ones_still_count(self):
        folder = os.path.join(self.heartbeats, iso(0)[:10])
        os.makedirs(folder)
        with open(os.path.join(folder, "alpha.jsonl"), "w") as handle:
            handle.write("{broken\n")
            handle.write(json.dumps({"what": "no time"}) + "\n")
            handle.write(json.dumps({"time": iso(0), "agent": "someone-else"}) + "\n")
            handle.write(json.dumps(beat(5, commit="c")) + "\n")
        report = self.run_dog(8)
        incident = [i for i in report["incidents"] if i["type"] == "malformed_heartbeat"][0]
        self.assertEqual((incident["severity"], incident["action"]), ("info", "none"))
        self.assertIn("3 unusable", incident["detail"])
        self.assertEqual(report["agents"][0]["beats"], 1)
        self.assertEqual(self.types(report, "no_heartbeat"), [])

    def test_a_file_that_is_not_an_agent_name_is_reported(self):
        folder = os.path.join(self.heartbeats, iso(0)[:10])
        os.makedirs(folder)
        with open(os.path.join(folder, "-bad name.jsonl"), "w") as handle:
            handle.write(json.dumps(beat(0)) + "\n")
        self.assertEqual(self.types(self.run_dog(1)), ["malformed_heartbeat"])


class Lookback(WatchdogCase):
    def test_yesterdays_file_is_read_just_after_midnight(self):
        late = ledgerlib.format_time(ledgerlib.parse_time("2026-11-18T23:50:00Z"))
        self.beats("alpha", [{"time": late, "task": "T-1", "what": "x", "commit": "c"}], day="2026-11-18")
        out = io.StringIO()
        watchdog.run(["--ledger", self.ledger, "--now", "2026-11-19T00:10:00Z"], out=out)
        report = json.loads(out.getvalue())
        self.assertEqual(report["agents"][0]["beats"], 1)
        self.assertEqual([i["type"] for i in report["incidents"]], ["no_heartbeat"])  # 20 minutes old

    def test_old_days_are_ignored(self):
        self.beats("alpha", [beat(0)], day="2026-11-10")
        out = io.StringIO()
        watchdog.run(["--ledger", self.ledger, "--now", "2026-11-19T00:10:00Z"], out=out)
        self.assertEqual(json.loads(out.getvalue())["agents"], [])


class Incidents(WatchdogCase):
    def test_incident_ids_are_stable_between_runs(self):
        self.beats("alpha", [beat(0)])
        first = self.run_dog(20)["incidents"][0]["id"]
        second = self.run_dog(25)["incidents"][0]["id"]
        self.assertEqual(first, second)

    def test_incident_files_are_written_once_and_never_overwritten(self):
        self.beats("alpha", [beat(0)])
        self.beats("beta", [{"time": iso(0), "agent": "beta"}], day=iso(0)[:10])
        folder = os.path.join(self.ledger, "incidents")
        report = self.run_dog(20, None, "--write-incidents", folder)
        self.assertEqual(len(report["written"]), len(self.types(report)))
        path = report["written"][0]
        with open(path, "a") as handle:
            handle.write("Root cause: the container was reclaimed.\n")
        again = self.run_dog(25, None, "--write-incidents", folder)
        self.assertEqual(again["written"], [])
        with open(path) as handle:
            self.assertIn("container was reclaimed", handle.read())

    def test_info_incidents_get_no_file(self):
        folder = os.path.join(self.heartbeats, iso(0)[:10])
        os.makedirs(folder)
        with open(os.path.join(folder, "alpha.jsonl"), "w") as handle:
            handle.write("{broken\n")
        report = self.run_dog(1, None, "--write-incidents", os.path.join(self.ledger, "incidents"))
        self.assertEqual(report["written"], [])

    def test_fail_on_incident_exit_code(self):
        self.beats("alpha", [beat(0)])
        self.run_dog(5, None, "--fail-on-incident")
        self.assertEqual(self.code, 0)
        self.run_dog(20, None, "--fail-on-incident")
        self.assertEqual(self.code, 1)
        self.run_dog(20)
        self.assertEqual(self.code, 0)  # without the flag a monitor never fails

    def test_incidents_are_ordered_critical_first(self):
        self.beats("alpha", [beat(0)])
        report = self.run_dog(60, [self.claim(at=0)])
        ranks = [watchdog.SEVERITY_RANK[i["severity"]] for i in report["incidents"]]
        self.assertEqual(ranks, sorted(ranks))


class CommandLineAndRemote(RemoteCase):
    def test_reads_claim_refs_from_a_git_remote(self):
        self.tool("--now", iso(0), "claim", "T-1", "--agent", "alpha")
        self.tool("--now", iso(30), "claim", "T-2", "--agent", "beta")
        ledger = self.path("ledger")
        os.makedirs(os.path.join(ledger, "heartbeats"))
        write_beats(os.path.join(ledger, "heartbeats"), "beta", [beat(40, task="T-2", commit="c1")])
        result = subprocess.run(
            [sys.executable, os.path.join(support.TOOLS_DIR, "watchdog.py"), "--ledger", ledger, "--remote", self.remote, "--now", iso(50)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        expired = [i for i in report["incidents"] if i["type"] == "claim_expired"]
        self.assertEqual([(i["task"], i["agent"]) for i in expired], [("T-1", "alpha")])
        self.assertEqual([c["task"] for c in report["claims"]], ["T-1", "T-2"])

    def test_bad_input_is_exit_two(self):
        for argv in (["--now", "not a time"], ["--claims-file", self.path("missing.json")]):
            result = subprocess.run(
                [sys.executable, os.path.join(support.TOOLS_DIR, "watchdog.py"), "--ledger", self.path("ledger")] + argv,
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 2, argv)


if __name__ == "__main__":
    unittest.main()
