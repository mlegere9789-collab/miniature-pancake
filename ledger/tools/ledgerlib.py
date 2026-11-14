"""Helpers shared by the ledger tools: UTC times, JSONL files, natural ordering, pace line.

Only the Python standard library is used. All times are UTC and written as
YYYY-MM-DDTHH:MM:SSZ.
"""

from __future__ import annotations

import datetime
import json
import re

UTC = datetime.timezone.utc

_TIME_RE = re.compile(
    r"^(\d{4})-(\d{2})-(\d{2})"
    r"(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:[.,](\d+))?)?)?"
    r"\s*(Z|[+-]\d{2}(?::?\d{2})?)?$"
)


def parse_time(value):
    """Parse an ISO 8601 time into an aware UTC datetime. Raises ValueError."""
    if isinstance(value, datetime.datetime):
        return value if value.tzinfo else value.replace(tzinfo=UTC)
    match = _TIME_RE.match(str(value).strip())
    if not match:
        raise ValueError("not an ISO 8601 time: %r" % (value,))
    year, month, day, hour, minute, second, fraction, zone = match.groups()
    micro = int((fraction or "0").ljust(6, "0")[:6])
    when = datetime.datetime(
        int(year),
        int(month),
        int(day),
        int(hour or 0),
        int(minute or 0),
        int(second or 0),
        micro,
        tzinfo=UTC,
    )
    if zone and zone != "Z":
        sign = 1 if zone[0] == "+" else -1
        digits = zone[1:].replace(":", "")
        offset = datetime.timedelta(hours=int(digits[:2]), minutes=int(digits[2:] or 0))
        when = when - sign * offset
    return when


def format_time(when):
    """Format an aware datetime as YYYY-MM-DDTHH:MM:SSZ (whole seconds, UTC)."""
    when = when.astimezone(UTC)
    return when.strftime("%Y-%m-%dT%H:%M:%SZ")


def utcnow():
    return datetime.datetime.now(UTC).replace(microsecond=0)


def try_parse_time(value):
    """parse_time, but None for anything unusable."""
    try:
        return parse_time(value)
    except (ValueError, TypeError):
        return None


def minutes_between(earlier, later):
    return (later - earlier).total_seconds() / 60.0


def natural_key(text):
    """Sort key that orders numbers numerically: run 9 before run 10."""
    parts = re.split(r"(\d+)", str(text))
    key = []
    for part in parts:
        if part == "":
            continue
        key.append((0, int(part), "") if part.isdigit() else (1, 0, part))
    return tuple(key)


def read_jsonl(path):
    """Read a JSONL file. Returns (items, problems).

    items is a list of (line number, object); problems is a list of (line number, message)
    for lines that are not JSON objects. Blank lines are skipped.
    """
    items = []
    problems = []
    with open(str(path), "r", encoding="utf-8-sig") as handle:
        for number, text in enumerate(handle, 1):
            if text.strip() == "":
                continue
            try:
                obj = json.loads(text)
            except ValueError as exc:
                problems.append((number, "not valid JSON: %s" % exc))
                continue
            if not isinstance(obj, dict):
                problems.append((number, "not a JSON object"))
                continue
            items.append((number, obj))
    return items, problems


def dump_json(obj, path=None):
    """Stable pretty JSON (sorted keys, 2 spaces, trailing newline)."""
    text = json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    if path is not None:
        with open(str(path), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    return text


# --------------------------------------------------------------------------------------
# The pace line (plan 6.4 and 7.6)
# --------------------------------------------------------------------------------------

# Plan 6.4: required cumulative rows passing, as a share of the frozen Register. They are
# targets implied by the date, recomputed on freeze day (the file ledger/pace.json replaces
# these defaults).
DEFAULT_PACE = {
    "version": 1,
    "note": "Plan 6.4 targets as shares of the Register; wave 0 (14-17 Nov) has no row target.",
    "pace_rules_start": "2026-11-18T00:00:00Z",
    "periods": [
        {"from": "2026-11-18", "to": "2026-11-24", "cumulative_pct": 10},
        {"from": "2026-11-25", "to": "2026-11-30", "cumulative_pct": 25},
        {"from": "2026-12-01", "to": "2026-12-07", "cumulative_pct": 50},
        {"from": "2026-12-08", "to": "2026-12-14", "cumulative_pct": 80},
        {"from": "2026-12-15", "to": "2026-12-18", "cumulative_pct": 100},
    ],
    "window_hours": 6,
    "window_fraction": 0.5,
    "ahead_factor": 1.1,
    "behind_factor": 0.9,
}


def load_pace(path=None):
    """Load ledger/pace.json, or the plan 6.4 defaults when no file is given or present."""
    if path is None:
        return json.loads(json.dumps(DEFAULT_PACE))
    try:
        with open(str(path), "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError:
        return json.loads(json.dumps(DEFAULT_PACE))
    pace = json.loads(json.dumps(DEFAULT_PACE))
    pace.update(data)
    validate_pace(pace)
    return pace


def validate_pace(pace):
    periods = pace.get("periods")
    if not isinstance(periods, list) or not periods:
        raise ValueError("pace.json needs a non-empty 'periods' list")
    previous_end = None
    previous_cum = 0
    for period in periods:
        start = parse_time(period["from"])
        end = parse_time(period["to"]) + datetime.timedelta(days=1)
        if end <= start:
            raise ValueError("pace period %s..%s ends before it starts" % (period["from"], period["to"]))
        if previous_end is not None and start < previous_end:
            raise ValueError("pace periods must be in order and must not overlap")
        cum = float(period["cumulative_pct"])
        if not previous_cum <= cum <= 100:
            raise ValueError("cumulative_pct must rise from period to period and stay <= 100")
        previous_end, previous_cum = end, cum


def _periods(pace):
    out = []
    previous = 0.0
    for period in pace["periods"]:
        start = parse_time(period["from"])
        end = parse_time(period["to"]) + datetime.timedelta(days=1)
        cum = float(period["cumulative_pct"])
        out.append(
            {
                "from": period["from"],
                "to": period["to"],
                "start": start,
                "end": end,
                "days": (end - start).total_seconds() / 86400.0,
                "cumulative_pct": cum,
                "share_pct": cum - previous,
            }
        )
        previous = cum
    return out


def period_at(pace, when):
    """The pace period that contains `when`, or None (wave 0, or after the last period)."""
    for period in _periods(pace):
        if period["start"] <= when < period["end"]:
            return period
    return None


def required_per_day(pace, total_rows, when):
    """Rows that must be verified per day in the period containing `when` (0 outside)."""
    period = period_at(pace, when)
    if period is None:
        return 0.0
    return period["share_pct"] / 100.0 * total_rows / period["days"]


def required_cumulative(pace, total_rows, when):
    """Rows that must be passing at `when`: linear inside a period, flat between them."""
    periods = _periods(pace)
    if not periods or when < periods[0]["start"]:
        return 0.0
    previous_cum = 0.0
    for period in periods:
        if when < period["end"]:
            elapsed = (when - period["start"]).total_seconds() / 86400.0
            fraction = max(0.0, min(1.0, elapsed / period["days"]))
            cum = previous_cum + period["share_pct"] * fraction
            return cum / 100.0 * total_rows
        previous_cum = period["cumulative_pct"]
    return previous_cum / 100.0 * total_rows


def window_minimum(pace, total_rows, window_end):
    """Rows a six-hour window must verify (plan 7.6): 50% of one quarter of the day's need.

    The day is the one that contains the end of the window. Returns 0.0 before the pace
    rules start and after the last period.
    """
    start = try_parse_time(pace.get("pace_rules_start"))
    if start is not None and window_end < start:
        return 0.0
    per_day = required_per_day(pace, total_rows, window_end)
    hours = float(pace.get("window_hours", 6))
    return float(pace.get("window_fraction", 0.5)) * per_day * hours / 24.0


def trajectory(pace, total_rows, passing, when):
    """The statement the daily report needs: ahead, on pace or behind the line (plan 1.5)."""
    start = try_parse_time(pace.get("pace_rules_start"))
    periods = _periods(pace)
    block = {
        "as_of": format_time(when),
        "required_cumulative_rows": round(required_cumulative(pace, total_rows, when), 1),
        "required_rows_per_day": round(required_per_day(pace, total_rows, when), 1),
        "passing": passing,
    }
    period = period_at(pace, when)
    if period is not None:
        block["period"] = {"from": period["from"], "to": period["to"], "cumulative_pct": period["cumulative_pct"]}
    if start is not None and when < start:
        block["status"] = "wave0"
    elif periods and when >= periods[-1]["end"]:
        block["status"] = "hardening"
    else:
        required = block["required_cumulative_rows"]
        if required <= 0:
            block["status"] = "on_pace"
        elif passing >= required * float(pace.get("ahead_factor", 1.1)):
            block["status"] = "ahead"
        elif passing < required * float(pace.get("behind_factor", 0.9)):
            block["status"] = "behind"
        else:
            block["status"] = "on_pace"
    block["gap_rows"] = round(block["required_cumulative_rows"] - passing, 1)
    return block
