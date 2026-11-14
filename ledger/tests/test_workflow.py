"""The CI workflow is valid YAML, triggers on the right paths and only calls what exists."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys
import unittest

from . import support

try:
    import yaml
except ImportError:  # CI installs PyYAML for this test
    yaml = None

WORKFLOW = os.path.join(support.REPO_ROOT, ".github", "workflows", "ledger-check.yml")


def load():
    with open(WORKFLOW, "r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def triggers(data):
    # PyYAML (YAML 1.1) reads the key "on" as the boolean True
    return data["on"] if "on" in data else data[True]


def commands(data):
    """Logical shell command lines of every run step (continuation lines joined)."""
    found = []
    for step in data["jobs"]["check"]["steps"]:
        text = step.get("run")
        if not text:
            continue
        text = text.replace("\\\n", " ")
        for line in text.splitlines():
            line = line.strip()
            if line:
                found.append(line)
    return found


@unittest.skipUnless(yaml, "PyYAML is not installed")
class Workflow(unittest.TestCase):
    def test_it_is_valid_and_has_one_job_with_steps(self):
        data = load()
        self.assertEqual(data["name"], "Ledger check")
        self.assertEqual(list(data["jobs"]), ["check"])
        self.assertGreater(len(data["jobs"]["check"]["steps"]), 5)
        self.assertEqual(data["permissions"], {"contents": "read"})

    def test_it_runs_on_pushes_touching_parity_or_ledger(self):
        on = triggers(load())
        for event in ("push", "pull_request"):
            self.assertIn("parity/**", on[event]["paths"])
            self.assertIn("ledger/**", on[event]["paths"])
            self.assertIn(".github/workflows/ledger-check.yml", on[event]["paths"])
        self.assertNotIn(
            "branches", on["push"]
        )  # task branches and throw-away refs must be checked too

    def test_steps_are_named_and_actions_are_pinned_to_a_version(self):
        for step in load()["jobs"]["check"]["steps"]:
            self.assertTrue(step.get("name"), step)
            if "uses" in step:
                self.assertRegex(step["uses"], r"@v\d+$")

    def test_every_script_and_directory_it_names_exists(self):
        for line in commands(load()):
            for token in shlex.split(line, posix=True) if "<<" not in line else []:
                if token.endswith(".py"):
                    self.assertTrue(
                        os.path.isfile(os.path.join(support.REPO_ROOT, token)), token
                    )
            match = re.search(r"unittest discover -s (\S+)", line)
            if match:
                self.assertTrue(
                    os.path.isdir(os.path.join(support.REPO_ROOT, match.group(1))), line
                )

    def test_every_option_it_passes_is_defined_by_the_tool(self):
        checked = 0
        for line in commands(load()):
            if "<<" in line or ".py" not in line:
                continue
            tokens = shlex.split(line, posix=True)
            script = next((t for t in tokens if t.endswith(".py")), None)
            if script is None or script.startswith("-"):
                continue
            rest = tokens[tokens.index(script) + 1 :]
            sub = [rest[0]] if rest and not rest[0].startswith("-") else []
            help_text = subprocess.run(
                [sys.executable, os.path.join(support.REPO_ROOT, script)]
                + sub
                + ["--help"],
                capture_output=True,
                text=True,
            ).stdout
            for token in rest:
                if token.startswith("--"):
                    self.assertIn(
                        token, help_text, "%s does not define %s" % (script, token)
                    )
                    checked += 1
        self.assertGreater(checked, 8)  # the check really looked at options

    def test_guard_step_reads_the_actor_and_repository_variable(self):
        with open(WORKFLOW, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("github.actor", text)
        self.assertIn("vars.LEDGER_CI_ACTORS", text)


if __name__ == "__main__":
    unittest.main()
