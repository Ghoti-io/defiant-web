#!/bin/bash
# The defiant program, started and stopped for real. `make test` runs it.
#
#   tools/check-defiant.sh <path to the defiant program>
#
# Rows of the story-18 matrix: first start, second start, memory run, a stop
# that exits 0, and the two refusals. A missing curl fails the gate; it does
# not skip it.
set -u
PROG=${1:?usage: check-defiant.sh <defiant program>}
command -v curl >/dev/null || { echo "check-defiant: curl is needed" >&2; exit 2; }
W=$(mktemp -d) || exit 2
PID=
trap '[ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$W"' EXIT
SECRET=$(printf 'cd%.0s' $(seq 32))
fails=0
fail() { echo "check-defiant: FAIL: $*" >&2; fails=$((fails + 1)); }

# start <log> <env assignments...>; sets PID and PORT, or fails the caller.
start() {
  local log=$1; shift
  env DEFIANT_LISTEN=127.0.0.1:0 "$@" "$PROG" >/dev/null 2>"$log" &
  PID=$!
  PORT=
  for _ in $(seq 100); do
    PORT=$(sed -n 's/.*listening on port \([0-9]*\).*/\1/p' "$log")
    [ -n "$PORT" ] && return 0
    kill -0 "$PID" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}
stop() { kill -INT "$PID"; wait "$PID"; local rc=$?; PID=; return $rc; }
login() { # login <user> <password>: prints the status and the Location
  curl -s -o /dev/null -w '%{http_code} %{redirect_url}' -X POST \
    -H "Origin: http://127.0.0.1:$PORT" --data-urlencode "username=$1" \
    --data-urlencode "password=$2" "http://127.0.0.1:$PORT/login"
}

# First start: the tenant, the kit and the admin are made.
start "$W/a.log" DEFIANT_DATA="$W/data" DEFIANT_SECRET="$SECRET" \
  DEFIANT_ADMIN_USER=ada DEFIANT_ADMIN_PASSWORD=first-pass-1 || fail "first start"
[ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/ready")" = 200 ] || fail "ready"
grep -q "created the first admin" "$W/a.log" || fail "first admin not reported"
[ "$(login ada first-pass-1)" = "303 http://127.0.0.1:$PORT/admin" ] || fail "first login: $(login ada first-pass-1)"
curl -s "http://127.0.0.1:$PORT/" | grep -q 'class="main"' || fail "front page not served"
stop || fail "stop after the first start did not exit 0"

# Second start: the same data, no admin variables, no second admin.
start "$W/b.log" DEFIANT_DATA="$W/data" DEFIANT_SECRET="$SECRET" \
  DEFIANT_ADMIN_USER=eve DEFIANT_ADMIN_PASSWORD=other-pass-2 || fail "second start"
grep -q "created the first admin" "$W/b.log" && fail "a second admin was created"
[ "$(login ada first-pass-1)" = "303 http://127.0.0.1:$PORT/admin" ] || fail "ada after restart"
[ "$(login eve other-pass-2 | cut -c1-3)" = 401 ] || fail "eve exists after restart"
stop || fail "stop after the second start did not exit 0"

# Memory run: says so, and keeps nothing.
start "$W/c.log" DEFIANT_ADMIN_USER=mem DEFIANT_ADMIN_PASSWORD=mem-pass-3 || fail "memory start"
grep -q "memory backend" "$W/c.log" || fail "memory run not announced"
[ "$(login mem mem-pass-3 | cut -c1-3)" = 303 ] || fail "memory login"
stop || fail "stop after the memory run did not exit 0"

# The cookie policy: secure unless told otherwise, and relaxed only by name.
cookie_of() { # cookie_of <user> <password>: the Set-Cookie line of a login
  curl -s -D - -o /dev/null -X POST -H "Origin: http://127.0.0.1:$PORT" \
    --data-urlencode "username=$1" --data-urlencode "password=$2" \
    "http://127.0.0.1:$PORT/login" | tr -d '\r' | sed -n 's/^[Ss]et-[Cc]ookie: //p'
}
policy() { # policy <label> <expected cookie pattern> <env assignments...>
  local label=$1 want=$2; shift 2
  start "$W/p.log" DEFIANT_ADMIN_USER=pol DEFIANT_ADMIN_PASSWORD=pol-pass-4 "$@" \
    || { fail "$label: start"; return; }
  local got
  got=$(cookie_of pol pol-pass-4)
  printf '%s\n' "$got" | grep -Eq "$want" || fail "$label: the session cookie is: $got"
  [ "$(printf '%s\n' "$got" | wc -l)" = 1 ] || fail "$label: more than one cookie"
  stop || fail "$label: stop did not exit 0"
}
policy "default cookie" '^__Host-session=[^;]+; Secure; HttpOnly; SameSite=Strict; Path=/$'
policy "lax cookie" '^__Host-session=[^;]+; Secure; HttpOnly; SameSite=Lax; Path=/$' \
  DEFIANT_COOKIE_SAMESITE=lax
policy "plain loopback cookie" '^session=[^;]+; HttpOnly; SameSite=Strict; Path=/$' \
  DEFIANT_COOKIE_TRANSPORT=plain-loopback

# Refusals.
env DEFIANT_LISTEN=127.0.0.1:0 DEFIANT_DATA="$W/other" "$PROG" >/dev/null 2>"$W/d.log"
[ $? = 2 ] && grep -q DEFIANT_SECRET "$W/d.log" || fail "data without a secret was not refused by name"
env DEFIANT_LISTEN=127.0.0.1:0 DEFIANT_DATA="$W/third" DEFIANT_SECRET="$SECRET" "$PROG" >/dev/null 2>"$W/e.log"
[ $? = 2 ] && grep -q DEFIANT_ADMIN_USER "$W/e.log" || fail "a first start with no admin was not refused by name"
# The plain-HTTP cookie is refused off loopback, before anything is made; and a
# setting that is neither of its two words is refused by name.
env DEFIANT_LISTEN=0.0.0.0:0 DEFIANT_COOKIE_TRANSPORT=plain-loopback DEFIANT_DATA="$W/refused" \
  DEFIANT_SECRET="$SECRET" DEFIANT_ADMIN_USER=x DEFIANT_ADMIN_PASSWORD=x-pass-5 \
  "$PROG" >/dev/null 2>"$W/f.log"
[ $? = 2 ] && grep -q DEFIANT_COOKIE_TRANSPORT "$W/f.log" || fail "plain-loopback on 0.0.0.0 was not refused by name"
[ ! -e "$W/refused" ] || fail "a refused start made its data directory"
env DEFIANT_LISTEN=127.0.0.1:0 DEFIANT_COOKIE_SAMESITE=none "$PROG" >/dev/null 2>"$W/g.log"
[ $? = 2 ] && grep -q DEFIANT_COOKIE_SAMESITE "$W/g.log" || fail "an unknown SameSite word was not refused by name"
env DEFIANT_LISTEN=127.0.0.1:0 DEFIANT_COOKIE_TRANSPORT=plain "$PROG" >/dev/null 2>"$W/h.log"
[ $? = 2 ] && grep -q DEFIANT_COOKIE_TRANSPORT "$W/h.log" || fail "an unknown transport word was not refused by name"
grep -rq "first-pass-1\|other-pass-2\|mem-pass-3\|pol-pass-4\|x-pass-5" "$W"/*.log && fail "a password reached a log"

[ "$fails" = 0 ] && echo "check-defiant: the program starts, restarts, refuses and stops" && exit 0
exit 1
