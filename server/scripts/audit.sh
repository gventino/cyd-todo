#!/usr/bin/env bash
# Source audits of the server (run by ci.sh). They look for the patterns that cause the bugs the design is
# meant to rule out; a hit is read by a person, it is not always a bug.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
fail=0
say() { echo "audit: $*" >&2; fail=1; }

# no unsafe code, enforced by the compiler too
grep -rnE 'unsafe[[:space:]]+(\{|fn|impl|trait)' src && say "unsafe code in src/"
grep -q 'forbid(unsafe_code)' src/lib.rs && grep -q 'forbid(unsafe_code)' src/main.rs || say "forbid(unsafe_code) is missing"

# SQL is a static string: nothing builds a query at run time
grep -rnE 'format!\([^)]*(SELECT|INSERT|UPDATE|DELETE|WHERE)' src && say "SQL built with format!"
grep -rn 'AssertSqlSafe' src && say "AssertSqlSafe: a query that is not a static string"

# what may be logged: only the access log line, startup lines and database errors; never a body, a header, a token
grep -rnE 'tracing::(trace|debug|info|warn|error)!' src | grep -vE 'src/(app|main|http_error|pages/[a-z]+)\.rs' && say "logging outside app.rs, main.rs, http_error.rs, pages/"
grep -rnE 'println!|eprintln!|dbg!' src | grep -v 'src/main.rs' && say "printing outside main.rs"
grep -rnE 'tracing::.*(token|body|header|authorization)' src && say "a log line that names a token, body or header"

# secrets: no token in the tree
git ls-files | xargs grep -nIE 'CYD_TOKEN=[A-Za-z0-9]{16,}' 2>/dev/null | grep -v 'scripts/audit.sh' && say "a token in a tracked file"

[ "$fail" -eq 0 ] && echo "audit: ok"
exit "$fail"
