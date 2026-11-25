#!/usr/bin/env bash
# A walk through every call of docs/api.md against a running server, checking the status of each and
# one field of the answer. Exits non-zero at the first surprise. It cleans up after itself.
#
#   CYD_TOKEN=... BASE=http://127.0.0.1:8080 ./scripts/smoke.sh
set -euo pipefail

: "${CYD_TOKEN:?set CYD_TOKEN to the token of the server}"
BASE="${BASE:-http://127.0.0.1:8080}"
body=$(mktemp); trap 'rm -f "$body"' EXIT
fails=0

# call METHOD PATH [JSON]  ->  sets $status, leaves the body in $body
call() {
    local method=$1 path=$2 data=${3-}
    if [ -n "$data" ]; then
        status=$(curl -s -o "$body" -w '%{http_code}' -X "$method" "$BASE$path" \
            -H "Authorization: Bearer $CYD_TOKEN" -H 'Content-Type: application/json' --data "$data")
    else
        status=$(curl -s -o "$body" -w '%{http_code}' -X "$method" "$BASE$path" -H "Authorization: Bearer $CYD_TOKEN")
    fi
}
field() { python3 -c 'import json,sys; v=json.load(open(sys.argv[1])); print(eval(sys.argv[2], {}, {"v": v}))' "$body" "$1"; }
expect() { # expect "what" want got
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"; else printf '  FAIL  %s: wanted %s, got %s\n' "$1" "$2" "$3"; fails=$((fails + 1)); fi
}

echo "smoke test against $BASE"

echo "health and auth"
status=$(curl -s -o "$body" -w '%{http_code}' "$BASE/health"); expect "GET /health without a token" 200 "$status"
status=$(curl -s -o "$body" -w '%{http_code}' "$BASE/quests"); expect "GET /quests without a token" 401 "$status"
status=$(curl -s -o "$body" -w '%{http_code}' -H 'Authorization: Bearer wrong' "$BASE/quests"); expect "GET /quests with a wrong token" 401 "$status"

echo "quests"
call GET /quests; expect "GET /quests" 200 "$status"; rev0=$(field 'v["rev"]')
call POST /quests '{"title":"Smoke quest","notes":"made by smoke.sh","difficulty":3,"due":"2030-01-31"}'
expect "POST /quests" 201 "$status"; qid=$(field 'v["id"]')
expect "  the new quest is open" open "$(field 'v["state"]')"
call GET "/quests/$qid"; expect "GET /quests/{id}" 200 "$status"; expect "  difficulty" 3 "$(field 'v["difficulty"]')"
call PATCH "/quests/$qid" '{"due":null,"state":"done"}'; expect "PATCH /quests/{id} done" 200 "$status"
expect "  due cleared" None "$(field 'v["due"]')"; expect "  state" done "$(field 'v["state"]')"
call GET /quests; expect "GET /quests again" 200 "$status"; expect "  xp counts the done quest" 'True' "$(field 'v["xp"] >= 30')"
call POST /quests '{"title":""}'; expect "POST an empty title" 422 "$status"
call POST /quests 'not json'; expect "POST garbage" 400 "$status"
call PATCH "/quests/$qid" '{}'; expect "PATCH with nothing" 400 "$status"
call GET /quests/0; expect "GET /quests/0" 404 "$status"

echo "events"
start=1893456000 # 2030-01-01 00:00 UTC
call POST /events "{\"title\":\"Smoke event\",\"description\":\"d\",\"start\":$start,\"end\":$((start + 3600))}"
expect "POST /events" 201 "$status"; eid=$(field 'v["id"]')
call GET "/events?from=$start&to=$((start + 86400))"; expect "GET /events window" 200 "$status"
expect "  one event overlaps" 1 "$(field 'len([e for e in v["events"] if e["id"] == '"$eid"'])')"
call GET "/events?from=$((start + 7200))&to=$((start + 86400))"; expect "GET /events after it ends" 200 "$status"
expect "  it is not listed" 0 "$(field 'len([e for e in v["events"] if e["id"] == '"$eid"'])')"
call PATCH "/events/$eid" "{\"end\":$((start - 1))}"; expect "PATCH end before start" 422 "$status"
call PATCH "/events/$eid" '{"title":"Renamed"}'; expect "PATCH /events/{id}" 200 "$status"
call GET "/events?from=$start&to=$((start + 63 * 86400))"; expect "GET /events window over 62 days" 422 "$status"

echo "changes"
call GET /changes; expect "GET /changes" 200 "$status"; rev1=$(field 'v["rev"]')
expect "  the revision moved" True "$([ "$rev1" -gt "$rev0" ] && echo True || echo False)"
call GET "/changes?since=$rev1"; expect "GET /changes?since=rev" 200 "$status"; expect "  nothing changed" False "$(field 'v["changed"]')"

echo "headers"
hdr=$(curl -s -D - -o /dev/null -H "Authorization: Bearer $CYD_TOKEN" "$BASE/quests" | tr -d '\r')
expect "Content-Length present" 1 "$(printf '%s\n' "$hdr" | grep -ci '^content-length:' || true)"
expect "not chunked" 0 "$(printf '%s\n' "$hdr" | grep -ci '^transfer-encoding:' || true)"
expect "JSON content type" 1 "$(printf '%s\n' "$hdr" | grep -ci '^content-type: application/json' || true)"


echo "phone pages"
call POST /link '{"kind":"quest"}'; expect "POST /link" 201 "$status"; url=$(field 'v["url"]')
jar=$(mktemp); trap 'rm -f "$body" "$jar"' EXIT
status=$(curl -s -o /dev/null -w '%{http_code}' -c "$jar" "$url"); expect "GET the one-time link" 303 "$status"
status=$(curl -s -o /dev/null -w '%{http_code}' "$url"); expect "the same link again" 404 "$status"
status=$(curl -s -o "$body" -w '%{http_code}' -b "$jar" "$BASE/app/quests/new"); expect "GET /app/quests/new with the session" 200 "$status"
csrf=$(grep -o 'name="csrf" value="[^"]*"' "$body" | head -1 | sed 's/.*value="//;s/"//')
expect "  a CSRF token is on the form" 26 "${#csrf}"
status=$(curl -s -o /dev/null -w '%{http_code}' -b "$jar" --data "title=x&difficulty=1" "$BASE/app/quests"); expect "POST without the CSRF token (a missing field)" 400 "$status"
status=$(curl -s -o /dev/null -w '%{http_code}' -b "$jar" -H 'Origin: http://evil.example' --data "csrf=$csrf&title=x&difficulty=1" "$BASE/app/quests"); expect "POST from a foreign Origin" 403 "$status"
status=$(curl -s -o /dev/null -w '%{http_code}' -b "$jar" --data-urlencode "csrf=$csrf" --data-urlencode "title=<script>alert(1)</script>" --data "difficulty=2" "$BASE/app/quests"); expect "POST a quest from the phone page" 303 "$status"
status=$(curl -s -o "$body" -w '%{http_code}' -b "$jar" "$BASE/app/quests"); expect "GET /app/quests" 200 "$status"
expect "  the title is escaped" 1 "$(grep -c '&lt;script&gt;alert(1)&lt;/script&gt;' "$body" || true)"
expect "  no raw script tag" 0 "$(grep -c '<script' "$body" || true)"
hdr=$(curl -s -D - -o /dev/null -b "$jar" "$BASE/app" | tr -d '\r')
expect "  Content-Security-Policy present" 1 "$(printf '%s\n' "$hdr" | grep -ci '^content-security-policy: default-src .none.')"
call GET /quests; expect "the phone's quest reached the device API" 1 "$(field 'len([q for q in v["quests"] if q["title"].startswith("<script>")])')"
pid=$(field '[q["id"] for q in v["quests"] if q["title"].startswith("<script>")][0]'); call DELETE "/quests/$pid"; expect "DELETE it" 204 "$status"

echo "cleanup"
call DELETE "/events/$eid"; expect "DELETE /events/{id}" 204 "$status"
call DELETE "/quests/$qid"; expect "DELETE /quests/{id}" 204 "$status"
call DELETE "/quests/$qid"; expect "DELETE again" 404 "$status"

if [ "$fails" -ne 0 ]; then echo "smoke test FAILED ($fails)"; exit 1; fi
echo "smoke test passed"
