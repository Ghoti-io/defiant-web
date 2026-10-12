#!/bin/bash
# The success signal, shown against the real program.
#
#   APP_DIR=<build dir> tools/demo.sh        (make demo sets it)
#
# An editor writes a post and drags a widget; a day passes and the editor
# undoes the drag; a person is erased and the earlier snapshot is restored
# without bringing them back; a second tenant on the same process sees
# nothing of it. Each step prints one line. The first failure stops the run
# and exits non-zero.
#
# GHOTI_DEMO_PLANT=ledger restores the snapshot through the storage import and
# lets the snapshot's older ledger replace the live one, and the run must fail
# at step 5.
#
# Linux only: the day passes through an LD_PRELOAD clock.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
APP=${APP_DIR:?APP_DIR names the build directory with defiant and defiant-demo}
DEFIANT=$APP/defiant
DEMO=$APP/defiant-demo
[ -x "$DEFIANT" ] && [ -x "$DEMO" ] || { echo "demo: build defiant and defiant-demo first (make demo)" >&2; exit 2; }
command -v cc >/dev/null || { echo "demo: a C compiler is needed for the clock" >&2; exit 2; }
W=$(mktemp -d) || exit 2
PID=
trap '[ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$W"' EXIT
cc -shared -fPIC -o "$W/democlock.so" "$HERE/democlock.c" -ldl || exit 2
export DEFIANT_SECRET=$(printf 'ef%.0s' $(seq 32))
export DEFIANT_DATA="$W/data"
export DEFIANT_LISTEN=127.0.0.1:0
export DEFIANT_ADMIN_USER=ada DEFIANT_ADMIN_PASSWORD=demo-pass-1

start() { # start <skew seconds>
  DEMO_CLOCK_SKEW=$1 LD_PRELOAD="$W/democlock.so" "$DEFIANT" >/dev/null 2>"$W/log" &
  PID=$!
  PORT=
  for _ in $(seq 100); do
    PORT=$(sed -n 's/.*listening on port \([0-9]*\).*/\1/p' "$W/log")
    [ -n "$PORT" ] && return 0
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
  done
  echo "demo: the program did not start: $(tail -1 "$W/log")" >&2
  return 1
}
stop() { kill -INT "$PID"; wait "$PID"; local rc=$?; PID=; return $rc; }
step() { "$@" || { echo "demo: stopped at the first failing step" >&2; exit 1; }; }

mkdir -p "$W/scratch"
# Day one.
step start 0
step "$DEMO" web1 "$PORT" "$W/scratch"
step stop
step "$DEMO" operator1 "$W/data" "$W/scratch"
# A day and an hour later: yesterday's sessions are over.
step start 90000
step "$DEMO" web2 "$PORT" "$W/scratch"
step stop
step "$DEMO" operator2 "$W/data" "$W/scratch"
step start 90000
step "$DEMO" web3 "$PORT" "$W/scratch"
step stop
echo "demo: six steps passed"
