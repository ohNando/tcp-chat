#!/usr/bin/env bash
# Runs the standard benchmark scenarios against a fresh server each time.
#   tools/bench.sh            (from the Backend directory, after `make`)
set -euo pipefail
cd "$(dirname "$0")/.."

PORT=${PORT:-18090}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

run() {
    local title=$1; shift
    echo "== $title  (bench $*)"
    (cd "$WORK" && exec "$OLDPWD/bin/server" -p "$PORT" -w 0 -m 256 -r 0 >/dev/null 2>&1) &
    local pid=$!
    sleep 0.3
    ./bin/bench "$@" -P "$pid" 127.0.0.1 "$PORT" | sed 's/^/   /' || true
    kill "$pid"
    wait "$pid" 2>/dev/null || true
    echo
}

run "light: 50 clients x 10 msg/s"            -c 50  -r 10 -d 10
run "heavy: 200 clients x 5 msg/s"            -c 200 -r 5  -d 10
run "slow readers: 50 x 20 msg/s, 2 KB, +20 slow" -c 50 -r 20 -s 2000 -S 20 -d 10
