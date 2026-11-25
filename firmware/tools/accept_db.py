#!/usr/bin/env python3
"""The acceptance of the phone and the board, judged by the server's own data and log, not by a description.

  CYD_TOKEN=... [CYD_URL=http://127.0.0.1:8080] [CYD_LOG=/tmp/cyd-server.log] accept_db.py snapshot FILE
      ... the owner then does the steps (a quest and an event written on the phone behind the board's QR codes, a quest
          completed with a tap on the board) ...
  CYD_TOKEN=... accept_db.py diff FILE        exit 0 only if all three things happened

It reads the quests and events through the device API and, if CYD_LOG is set, the lines the server logged since the snapshot
(`POST /link`, `GET /m/:code`, `POST /app/quests`, `POST /app/events`, `PATCH /quests/N`): they say who did it.
"""
import json
import os
import sys
import time
import urllib.request

URL = os.environ.get("CYD_URL", "http://127.0.0.1:8080")
TOKEN = os.environ.get("CYD_TOKEN", "")
LOG = os.environ.get("CYD_LOG", "")


def get(path):
    req = urllib.request.Request(URL + path, headers={"Authorization": "Bearer " + TOKEN})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def state():
    now = int(time.time())
    q = get("/quests")
    e = get("/events?from=%d&to=%d" % (now - 30 * 86400, now + 31 * 86400))
    return {
        "xp": q["xp"],
        "quests": {str(x["id"]): {"title": x["title"], "state": x["state"]} for x in q["quests"]},
        "events": {str(x["id"]): {"title": x["title"], "start": x["start"]} for x in e["events"]},
        "log_offset": os.path.getsize(LOG) if LOG and os.path.exists(LOG) else 0,
    }


def log_lines(since):
    if not LOG or not os.path.exists(LOG):
        return []
    with open(LOG, "rb") as f:
        f.seek(since)
        text = f.read().decode("utf-8", "replace")
    out = []
    for line in text.splitlines():
        line = line.replace("\x1b[2m", "").replace("\x1b[0m", "")
        import re
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        out.append(line)
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("snapshot", "diff") or not TOKEN:
        print(__doc__)
        return 2
    mode, path = sys.argv[1], sys.argv[2]
    if mode == "snapshot":
        s = state()
        json.dump(s, open(path, "w"))
        print("snapshot: %d quests, %d events, xp %d" % (len(s["quests"]), len(s["events"]), s["xp"]))
        return 0
    before = json.load(open(path))
    now = state()
    new_q = [v["title"] for k, v in now["quests"].items() if k not in before["quests"]]
    new_e = [v["title"] for k, v in now["events"].items() if k not in before["events"]]
    done = [v["title"] for k, v in now["quests"].items() if k in before["quests"] and before["quests"][k]["state"] != v["state"]]
    lines = log_lines(before["log_offset"])
    count = lambda *needles: sum(1 for l in lines if all(n in l for n in needles))
    print("new quests:        %s" % (new_q or "none"))
    print("new events:        %s" % (new_e or "none"))
    print("quests that changed state: %s   (xp %d -> %d)" % (done or "none", before["xp"], now["xp"]))
    if lines:
        print("server log since the snapshot: link requests %d, links used %d, phone-page quest posts %d, phone-page event posts %d, state patches %d" % (
            count("POST /link"), count("GET /m/:code", " 303 "), count("POST /app/quests"), count("POST /app/events"), count("PATCH /quests")))
    ok = bool(new_q) and bool(new_e) and bool(done)
    print("%s a quest was written, %s an event was written, %s a quest changed state on the board" % (
        "PASS" if new_q else "FAIL", "PASS" if new_e else "FAIL", "PASS" if done else "FAIL"))
    return 0 if ok else 1


sys.exit(main())
