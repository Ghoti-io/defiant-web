#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the server turns its gate red, by name.
#
# The gates are Scan.EveryRoute (the internal-id scanner over every response,
# AD-18 item 8) and Tenants.* (two tenants on one server, AD-25's cross-tenant
# attack at the web level). Each plant is an exact text edit to a scratch copy
# of this library; nothing is planted in the library. The copy is built, the
# one test program that holds the gate is run, and the named test must be among
# the failures. A run that fails some other way (a crash, a different test, a
# build error) does not count, and an edit that does not apply exactly once, or
# leaves the file as it was, fails the script before anything is built.
#
# The control comes first: the unpatched copy must pass, or a red result below
# would be a broken build, not a caught defect.
#
# Usage: check-plants.sh <make> <build-dir> [make args]

set -u

MAKE_CMD="${1:?usage: check-plants.sh <make> <build-dir> [make args]}"
BUILD_DIR="${2:?usage: check-plants.sh <make> <build-dir> [make args]}"
shift 2
MARGS="-j${GHOTI_CHECK_JOBS:-$(n=$(nproc 2>/dev/null || echo 4); [ "$n" -gt 4 ] && n=4; echo "$n")} $*"

unset MAKEFLAGS MFLAGS

ROOT=$(pwd)
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) EXE_EXT=.exe ;; *) EXE_EXT= ;; esac
SCRATCH="$ROOT/build/plants"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

fail=0

copy_tree() {
  mkdir -p "$1"
  for d in $(ls -A "$ROOT"); do
    case "$d" in build | .git | docs) continue ;; esac
    cp -a "$ROOT/$d" "$1/"
  done
}

run_tests() { # run_tests <dir> <log> <exe>...
  dir=$1 log=$2
  shift 2
  exes=
  for e in "$@"; do exes="$exes $BUILD_DIR/apps/$e$EXE_EXT"; done
  ( cd "$dir" && "$MAKE_CMD" --no-print-directory test TEST_GATES= \
      TEST_EXECUTABLES="$exes" $MARGS ) > "$log" 2>&1
}

copy_tree "$SCRATCH/control"
if ! run_tests "$SCRATCH/control" "$SCRATCH/control.log" testScanner testTenants; then
  printf 'check-plants: the CONTROL (unpatched) copy fails, so no result below\n' >&2
  printf 'would mean anything. Its log:\n' >&2
  grep -v '^g++\|^cc \|^gcc ' "$SCRATCH/control.log" | tail -40 >&2
  exit 1
fi
printf 'check-plants: control passes (Scan and Tenants on the unpatched copy)\n'

# begin <name>: a fresh copy to plant in. Sets DIR.
begin() {
  NAME=$1
  DIR="$SCRATCH/$NAME"
  cp -a "$SCRATCH/control" "$DIR"
  EDITED=0
  BAD=0
}

# edit <file> <old> <new>: replace the one occurrence.
edit() {
  if [ "$BAD" -ne 0 ]; then return; fi
  python3 - "$DIR/$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1:4]
text = open(path).read()
if text.count(old) != 1:
    sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
    sys.exit(3)
open(path, "w").write(text.replace(old, new, 1))
PY
  if [ $? -ne 0 ]; then
    printf 'check-plants: %s: THE EDIT DID NOT APPLY (stale anchor), nothing was tested\n' "$NAME" >&2
    BAD=1
    fail=1
    return
  fi
  if cmp -s "$ROOT/$1" "$DIR/$1"; then
    printf 'check-plants: %s: the edited %s equals the original\n' "$NAME" "$1" >&2
    BAD=1
    fail=1
    return
  fi
  EDITED=$((EDITED + 1))
}

# caught <exe> <test> <text>: build, run, and require <test> to fail and the
# log to carry <text>.
caught() {
  exe=$1 test=$2 text=$3
  if [ "$BAD" -ne 0 ] || [ "$EDITED" -eq 0 ]; then
    return
  fi
  # Objects are kept, so only what the edit touched recompiles; the programs
  # and libraries are removed so that every one is linked again.
  rm -f "$DIR/$BUILD_DIR"/apps/test* "$DIR/$BUILD_DIR"/apps/*.a "$DIR/$BUILD_DIR"/apps/*.so*
  if run_tests "$DIR" "$DIR.log" "$exe"; then
    printf 'check-plants: %s: %s PASSED with the defect planted\n' "$NAME" "$exe" >&2
    fail=1
  elif grep -q "^\[  FAILED  \] $test\b" "$DIR.log" && grep -q -- "$text" "$DIR.log"; then
    printf 'check-plants: %s: planted defect turned %s red (%s)\n' "$NAME" "$test" "$text"
  else
    printf 'check-plants: %s: red, but not %s showing "%s" - read %s\n' \
      "$NAME" "$test" "$text" "$DIR.log" >&2
    grep -v '^g++\|^cc \|^gcc ' "$DIR.log" | tail -25 >&2
    fail=1
  fi
  rm -rf "$DIR"
}

# An internal id in a page: the undo page prints a revision's sequence number
# again, as it did before the scanner found it.
begin undo-seq
edit src/admin.c \
'    (void)seq;
    if (page_str(page, "<li>") != 0) {' \
'    char seq_text[32];
    int n = snprintf(seq_text, sizeof seq_text, "%llu", (unsigned long long)seq);
    if (n <= 0 || page_str(page, "<li>") != 0 ||
        page_esc(page, seq_text, (size_t)n) != 0 || page_str(page, " ") != 0) {'
caught testScanner Scan.EveryRoute '/admin/undo (200) body'

# An internal id in a header: a row number beside the redirect.
begin row-header
edit src/server.c \
'  if (location != NULL &&
      ghttp_headers_add(headers, "Location", location) != GHTTP_OK) {
    goto done;
  }' \
'  if (location != NULL &&
      ghttp_headers_add(headers, "Location", location) != GHTTP_OK) {
    goto done;
  }
  if (ghttp_headers_add(headers, "X-Entity-Row", "17") != GHTTP_OK) {
    goto done;
  }'
caught testScanner Scan.EveryRoute 'header X-Entity-Row'

# An internal id in an error page: the fixed 404 names a tenant row.
begin error-page
edit src/server.c 'static const char kFixed[] = "Not found\n";' \
  'static const char kFixed[] = "Not found (tenant 3)\n";'
caught testScanner Scan.EveryRoute 'unknown host (404) body'

# A tenant-A token accepted at tenant B: the session is checked against the
# tenant that last signed anyone in, not the one the host names.
begin session-tenant
edit src/server.c 'static const char kActive[] = "active";' \
'static const char kActive[] = "active";
static char plant_last_login[GDEFCORE_PUBLIC_ID_LEN + 1];
static const void * plant_owner;'
edit src/server.c \
'  if (gdefcore_inst_storage_key(server->inst, id, strlen(id), storage) !=' \
'  if (plant_owner != server) {
    plant_owner = server;
    plant_last_login[0] = 0;
  }
  if (in.cookie[0] != 0 && plant_last_login[0] != 0) {
    memcpy(id, plant_last_login, sizeof id);
  }
  if (gdefcore_inst_storage_key(server->inst, id, strlen(id), storage) !='
edit src/server.c '    note_success(server, user_key);' \
'    note_success(server, user_key);
    memcpy(plant_last_login, id, sizeof plant_last_login);'
caught testTenants Tenants.SessionAtOtherTenant 'session at [AB]'

# The host chosen by a header a stranger can send.
begin forwarded-host
edit src/server.c "  if (trusted && in.xhost[0] != '\\0') {" \
  "  if (in.xhost[0] != '\\0') {"
caught testTenants Tenants.HostSelects 'Beta Title'

if [ "$fail" -ne 0 ]; then
  exit 1
fi
printf 'check-plants: every planted defect turned its named test red\n'
