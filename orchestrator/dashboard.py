"""Minimal local dashboard.

A single-file, dependency-free web dashboard (Python stdlib ``http.server``)
that reads the shared SQLite database and shows:

* a summary strip (total earnings, pending reviews),
* one card per module (status, last activity, earnings, pending count),
* the review queue, with working **Approve** / **Reject** buttons,
* a recent-activity feed.

Run it::

    python -m orchestrator.dashboard

Then open http://127.0.0.1:8787 . It binds to loopback by default, so it is
not exposed to your network. Change host/port via DASHBOARD_HOST /
DASHBOARD_PORT in `.env`, or pass --host / --port.
"""

from __future__ import annotations

import argparse
import html
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import database as db
from .config import config

_STATE_COLORS = {
    "ok": "#1a7f37",
    "running": "#0969da",
    "idle": "#57606a",
    "warning": "#9a6700",
    "error": "#cf222e",
}


def _fmt_money(v: float) -> str:
    return f"${v:,.2f}"


def _esc(v: object) -> str:
    return html.escape(str(v if v is not None else ""))


def render_page() -> str:
    overview = db.module_overview()
    reviews = db.pending_reviews()
    activity = db.recent_activity(limit=30)
    tot = db.totals()

    cards = []
    for m in overview:
        color = _STATE_COLORS.get(m.get("state") or "idle", "#57606a")
        pending = m["pending_reviews"]
        badge = (
            f'<span class="pill" style="background:#fff8c5;color:#9a6700">'
            f"{pending} awaiting review</span>"
            if pending
            else ""
        )
        cards.append(
            f"""
        <div class="card">
          <div class="card-head">
            <h3>{_esc(m['display_name'])}</h3>
            <span class="state" style="background:{color}">{_esc(m.get('state') or 'idle')}</span>
          </div>
          <p class="detail">{_esc(m.get('detail') or '')}</p>
          <div class="metric">{_fmt_money(m['total_earnings'])}<span>earned</span></div>
          <p class="last">{_esc(m.get('last_activity') or 'No activity yet')}</p>
          <p class="ts">{_esc(m.get('last_activity_at') or '')}</p>
          {badge}
        </div>"""
        )

    if reviews:
        rows = []
        for r in reviews:
            payload = ""
            if r.get("payload"):
                try:
                    payload = (
                        "<pre>"
                        + _esc(json.dumps(json.loads(r["payload"]), indent=2))
                        + "</pre>"
                    )
                except (json.JSONDecodeError, TypeError):
                    payload = "<pre>" + _esc(r["payload"]) + "</pre>"
            rows.append(
                f"""
            <tr>
              <td>{_esc(r['module'])}</td>
              <td><strong>{_esc(r['title'])}</strong><br><span class="muted">{_esc(r['description'])}</span>{payload}</td>
              <td class="ts">{_esc(r['created_at'])}</td>
              <td class="actions">
                <form method="POST" action="/review">
                  <input type="hidden" name="id" value="{r['id']}">
                  <button name="decision" value="approved" class="approve">Approve</button>
                  <button name="decision" value="rejected" class="reject">Reject</button>
                </form>
              </td>
            </tr>"""
            )
        review_html = f"""
        <table class="review">
          <thead><tr><th>Module</th><th>Item</th><th>Flagged</th><th></th></tr></thead>
          <tbody>{''.join(rows)}</tbody>
        </table>"""
    else:
        review_html = '<p class="empty">Nothing awaiting review. 🎉</p>'

    feed = []
    for a in activity:
        lvl = a.get("level") or "info"
        feed.append(
            f"""
        <li class="lvl-{_esc(lvl)}">
          <span class="ts">{_esc(a['created_at'])}</span>
          <span class="mod">{_esc(a['module'])}</span>
          {_esc(a['message'])}
        </li>"""
        )
    feed_html = (
        f'<ul class="feed">{"".join(feed)}</ul>'
        if feed
        else '<p class="empty">No activity logged yet.</p>'
    )

    return _TEMPLATE.format(
        total_earnings=_fmt_money(tot["total_earnings"]),
        pending=tot["pending_reviews"],
        cards="".join(cards),
        reviews=review_html,
        feed=feed_html,
    )


_TEMPLATE = """<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="30">
<title>Income Orchestrator</title>
<style>
  :root {{ color-scheme: light dark; }}
  * {{ box-sizing: border-box; }}
  body {{ font-family: -apple-system, Segoe UI, Roboto, sans-serif; margin: 0;
         background: #f6f8fa; color: #1f2328; }}
  header {{ background: #24292f; color: #fff; padding: 18px 24px; }}
  header h1 {{ margin: 0; font-size: 20px; }}
  header p {{ margin: 4px 0 0; opacity: .7; font-size: 13px; }}
  .wrap {{ max-width: 1100px; margin: 0 auto; padding: 24px; }}
  .summary {{ display: flex; gap: 16px; margin-bottom: 24px; flex-wrap: wrap; }}
  .summary .box {{ background: #fff; border: 1px solid #d0d7de; border-radius: 10px;
                   padding: 16px 22px; flex: 1; min-width: 180px; }}
  .summary .box b {{ display: block; font-size: 28px; }}
  .summary .box span {{ color: #57606a; font-size: 13px; }}
  h2 {{ font-size: 15px; text-transform: uppercase; letter-spacing: .05em;
        color: #57606a; margin: 28px 0 12px; }}
  .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(300px,1fr));
           gap: 16px; }}
  .card {{ background: #fff; border: 1px solid #d0d7de; border-radius: 10px; padding: 16px; }}
  .card-head {{ display: flex; justify-content: space-between; align-items: center; }}
  .card h3 {{ margin: 0; font-size: 15px; }}
  .state {{ color: #fff; font-size: 11px; padding: 2px 8px; border-radius: 999px;
            text-transform: uppercase; }}
  .detail {{ color: #57606a; font-size: 13px; margin: 8px 0; min-height: 18px; }}
  .metric {{ font-size: 24px; font-weight: 600; }}
  .metric span {{ font-size: 12px; color: #57606a; font-weight: 400; margin-left: 6px; }}
  .last {{ font-size: 13px; margin: 10px 0 2px; }}
  .ts {{ color: #8c959f; font-size: 11px; margin: 0; }}
  .pill {{ display: inline-block; margin-top: 10px; font-size: 12px;
           padding: 2px 8px; border-radius: 999px; }}
  table.review {{ width: 100%; border-collapse: collapse; background: #fff;
                  border: 1px solid #d0d7de; border-radius: 10px; overflow: hidden; }}
  table.review th, table.review td {{ text-align: left; padding: 10px 12px;
                  border-bottom: 1px solid #eaeef2; font-size: 14px; vertical-align: top; }}
  .muted {{ color: #57606a; font-size: 13px; }}
  pre {{ background: #f6f8fa; padding: 8px; border-radius: 6px; font-size: 12px;
         overflow-x: auto; margin: 8px 0 0; }}
  .actions form {{ display: flex; gap: 6px; }}
  button {{ border: 0; border-radius: 6px; padding: 6px 12px; cursor: pointer;
            font-size: 13px; color: #fff; }}
  button.approve {{ background: #1a7f37; }}
  button.reject {{ background: #cf222e; }}
  ul.feed {{ list-style: none; padding: 0; background: #fff; border: 1px solid #d0d7de;
             border-radius: 10px; margin: 0; }}
  ul.feed li {{ padding: 8px 14px; border-bottom: 1px solid #eaeef2; font-size: 13px; }}
  ul.feed li .mod {{ display: inline-block; background: #eaeef2; border-radius: 4px;
             padding: 0 6px; margin: 0 8px; font-size: 12px; }}
  ul.feed li.lvl-error {{ background: #fff0f0; }}
  ul.feed li.lvl-warning {{ background: #fffbea; }}
  .empty {{ color: #57606a; }}
</style></head><body>
<header>
  <h1>Income Orchestrator</h1>
  <p>Local control panel · auto-refreshes every 30s</p>
</header>
<div class="wrap">
  <div class="summary">
    <div class="box"><b>{total_earnings}</b><span>total earnings (all modules)</span></div>
    <div class="box"><b>{pending}</b><span>items awaiting your review</span></div>
  </div>

  <h2>Modules</h2>
  <div class="grid">{cards}</div>

  <h2>Review queue</h2>
  {reviews}

  <h2>Recent activity</h2>
  {feed}
</div>
</body></html>"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, body: str, status: int = 200, ctype: str = "text/html") -> None:
        payload = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", f"{ctype}; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:  # noqa: N802 (stdlib naming)
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/index.html"):
            self._send(render_page())
        elif parsed.path == "/api/overview":
            self._send(
                json.dumps(
                    {
                        "modules": db.module_overview(),
                        "totals": db.totals(),
                        "reviews": db.pending_reviews(),
                    },
                    default=str,
                ),
                ctype="application/json",
            )
        else:
            self._send("<h1>404</h1>", status=404)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path != "/review":
            self._send("<h1>404</h1>", status=404)
            return
        length = int(self.headers.get("Content-Length", 0))
        form = parse_qs(self.rfile.read(length).decode("utf-8"))
        try:
            item_id = int(form.get("id", ["0"])[0])
            decision = form.get("decision", [""])[0]
            db.resolve_review_item(item_id, decision)
        except (ValueError, KeyError):
            pass
        # Redirect back to the dashboard (Post/Redirect/Get).
        self.send_response(303)
        self.send_header("Location", "/")
        self.end_headers()

    def log_message(self, *args: object) -> None:
        return  # keep the console quiet


def serve(host: str | None = None, port: int | None = None) -> None:
    db.init_db()
    host = host or config.get("DASHBOARD_HOST", "127.0.0.1")
    port = int(port or config.get("DASHBOARD_PORT", "8787"))
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"Dashboard running at http://{host}:{port}  (Ctrl-C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard stopped.")
        server.server_close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Income orchestrator dashboard")
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    args = parser.parse_args(argv)
    serve(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
