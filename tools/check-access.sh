#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the access matrix page turns its test red,
# by name (access-conditions story A4).
#
# The gate is AccessAdmin.* (the page through the real server). Each plant is an
# exact text edit to a scratch copy of this library; nothing is planted in the
# library. The copy is built, the test program is run, and the named test must
# be among the failures, with the named text in its message. A run that fails some other way (a crash, a different test, a
# build error) does not count, and an edit that does not apply exactly once, or
# leaves the file as it was, fails the script before anything is built.
#
# The control comes first: the unpatched copy must pass, or a red result below
# would be a broken build, not a caught defect.
#
# Usage: check-access.sh <make> <build-dir> [make args]

set -u

MAKE_CMD="${1:?usage: check-access.sh <make> <build-dir> [make args]}"
BUILD_DIR="${2:?usage: check-access.sh <make> <build-dir> [make args]}"
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
if ! run_tests "$SCRATCH/control" "$SCRATCH/control.log" testAccessadmin; then
  printf 'check-access: the CONTROL (unpatched) copy fails, so no result below\n' >&2
  printf 'would mean anything. Its log:\n' >&2
  grep -v '^g++\|^cc \|^gcc ' "$SCRATCH/control.log" | tail -40 >&2
  exit 1
fi
printf 'check-access: control passes (AccessAdmin on the unpatched copy)\n'

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
    printf 'check-access: %s: THE EDIT DID NOT APPLY (stale anchor), nothing was tested\n' "$NAME" >&2
    BAD=1
    fail=1
    return
  fi
  if cmp -s "$ROOT/$1" "$DIR/$1"; then
    printf 'check-access: %s: the edited %s equals the original\n' "$NAME" "$1" >&2
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
    printf 'check-access: %s: %s PASSED with the defect planted\n' "$NAME" "$exe" >&2
    fail=1
  elif grep -q "^\[  FAILED  \] $test\b" "$DIR.log" && grep -q -- "$text" "$DIR.log"; then
    printf 'check-access: %s: planted defect turned %s red (%s)\n' "$NAME" "$test" "$text"
  else
    printf 'check-access: %s: red, but not %s showing "%s" - read %s\n' \
      "$NAME" "$test" "$text" "$DIR.log" >&2
    grep -v '^g++\|^cc \|^gcc ' "$DIR.log" | tail -25 >&2
    fail=1
  fi
  rm -rf "$DIR"
}

# A role's name is printed as it was written.
begin source-unescaped
edit src/admin.c \
'        access_cell(&page, "<td>", r->source, r->source_len) != 0 ||' \
'        page_str(&page, "<td>") != 0 || page_add(&page, r->source, r->source_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'
caught testAccessadmin AccessAdmin.HostileTextIsEscaped 'Actual: true'

# A permission's label (a module author's) is printed as it was written.
begin permission-unescaped
edit src/admin.c \
'        access_cell(&page, "<td>", r->permission, r->permission_len) != 0 ||' \
'        page_str(&page, "<td>") != 0 || page_add(&page, r->permission, r->permission_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'
caught testAccessadmin AccessAdmin.HostileTextIsEscaped 'Actual: true'

# A condition's words (which carry the constants an author wrote) are printed
# as they were written.
begin explain-unescaped
edit src/admin.c \
'        access_cell(&page, "<td>", r->explain, r->explain_len) != 0 ||' \
'        page_str(&page, "<td>") != 0 || page_add(&page, r->explain, r->explain_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'
caught testAccessadmin AccessAdmin.HostileTextIsEscaped 'Actual: true'

# The page answers a POST as it answers a GET.
begin post-accepted
edit src/admin.c \
'    rc = is_get ? access_page(alloc, catalog, request, out)
                : unknown_admin(method, out);' \
'    rc = access_page(alloc, catalog, request, out);'
caught testAccessadmin AccessAdmin.ReadOnly 'Granted by'

# A module's rows are left off the page.
begin module-rows-dropped
edit src/admin.c \
'  for (i = 0; i < n; i++) {
    const GDEFCORE_Access_Matrix_Row * r = &rows[i];' \
'  for (i = 0; i < n; i++) {
    const GDEFCORE_Access_Matrix_Row * r = &rows[i];
    if (r->from_module) {
      continue;
    }'
caught testAccessadmin AccessAdmin.TheAdministratorSeesRoleAndModuleRows 'module acc'

if [ "$fail" -ne 0 ]; then
  exit 1
fi
printf 'check-access: every planted defect turned its named test red\n'
