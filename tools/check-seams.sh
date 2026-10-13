#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Fail if one subsystem of defiant-web includes another's private header.
#
# admin (src/admin.c, src/admin.h) is the server-rendered admin pages. Its
# header is private: only admin.c and server.c, which routes to it, may include
# it. core.c (results and the default allocator), the public headers, the
# defiant program and demo under app/ and the tests use the public API only.
#
# Two rules, both read from the include lines:
#
#  1. A private header (every header under src/) may be included only by the
#     source files listed for it in OWNERS below. Public headers (include/),
#     the app, the tests and every other source file must not.
#  2. Nothing may reach into another library's private code: no angle-bracket
#     include of an *internal.h, no path through src/, and no "../" escape.
#
# The real tree must pass. A clean temporary tree must pass too (the control:
# a scanner that refuses everything proves nothing), and each planted
# violation in a temporary tree must be refused.
#
# Usage: check-seams.sh <root>

set -eu

ROOT="${1:?usage: check-seams.sh <root>}"

owners_of() {
  case "$1" in
    admin.h) printf '%s\n' 'admin.c server.c' ;;
    *) printf '\n' ;;
  esac
}

# include_lines <dir...>: "file:line:text" for every include directive.
include_lines() {
  for d in "$@"; do
    [ -d "$d" ] || continue
    grep -R -n -E '^[[:space:]]*#[[:space:]]*include' "$d" \
      --include='*.c' --include='*.h' --include='*.cpp' --include='*.hpp' 2>/dev/null || true
  done
}

# scan <tree>: print each violation, return 1 if there were any.
scan() {
  tree=$1
  status=0
  lines=$(include_lines "$tree/src" "$tree/include" "$tree/app" "$tree/tests" "$tree/examples")
  # Rule 1.
  for hdr in $(find "$tree/src" -name '*.h' 2>/dev/null | sed 's|.*/||' | sort -u); do
    allowed=$(owners_of "$hdr")
    pattern="[\"</]${hdr}[\">]"
    hits=$(printf '%s\n' "$lines" | grep -E "$pattern" || true)
    [ -n "$hits" ] || continue
    printf '%s\n' "$hits" | while IFS= read -r hit; do
      [ -n "$hit" ] || continue
      file=${hit%%:*}
      base=${file##*/}
      case "$file" in
        "$tree"/src/*)
          ok=0
          for a in $allowed; do [ "$a" = "$base" ] && ok=1; done
          # A private header may include its own siblings.
          case "$base" in *.h) ok=1 ;; esac
          [ "$ok" -eq 1 ] && continue
          ;;
      esac
      printf 'check-seams: %s includes private %s\n' "$hit" "$hdr" >&2
      echo x >> "$tree/.seam-violation"
    done
  done
  # Rule 2.
  cross=$(printf '%s\n' "$lines" | grep -E '#[[:space:]]*include[[:space:]]*<[^>]*internal\.h>|#[[:space:]]*include[[:space:]]*["<][^">]*/src/|#[[:space:]]*include[[:space:]]*"\.\./' || true)
  if [ -n "$cross" ]; then
    printf '%s\n' "$cross" | sed 's/^/check-seams: reaches into private code: /' >&2
    echo x >> "$tree/.seam-violation"
  fi
  if [ -e "$tree/.seam-violation" ]; then
    status=1
  fi
  return "$status"
}

# Guard: the pieces this check reads must exist, or it measures nothing.
if [ ! -f "$ROOT/src/admin.h" ] || [ ! -f "$ROOT/src/admin.c" ] ||
   [ ! -f "$ROOT/src/server.c" ] || [ ! -f "$ROOT/src/core.c" ] ||
   [ ! -d "$ROOT/include/ghoti.io/defiant-web" ]; then
  printf 'check-seams: private headers are missing; this gate is measuring nothing\n' >&2
  exit 1
fi

run_scan() {
  rm -f "$1/.seam-violation"
  if scan "$1"; then return 0; fi
  rm -f "$1/.seam-violation"
  return 1
}

if ! run_scan "$ROOT"; then
  exit 1
fi
printf 'check-seams: defiant-web includes only the headers it is allowed to\n'

plant=$(mktemp -d)
trap 'rm -rf "$plant"' EXIT
mkdir -p "$plant/src" "$plant/include/ghoti.io/defiant-web" "$plant/app" "$plant/tests"
printf '%s\n' '/* private */' > "$plant/src/admin.h"
printf '%s\n' '#include "admin.h"' > "$plant/src/admin.c"
printf '%s\n' '#include "admin.h"' > "$plant/src/server.c"
printf '%s\n' '#include <ghoti.io/defiant-web/macros.h>' > "$plant/src/core.c"
printf '%s\n' '#include <ghoti.io/defiant-core/account.h>' > "$plant/app/demo.cpp"
printf '%s\n' '#include <ghoti.io/defiant-web/server.h>' > "$plant/tests/test_edge.cpp"

if ! run_scan "$plant"; then
  printf 'check-seams: the clean control tree was refused; the scanner proves nothing\n' >&2
  exit 1
fi
printf 'check-seams: control: a clean tree passes\n'

refused() {
  what=$1
  if run_scan "$plant" 2>/dev/null; then
    printf 'check-seams: the planted %s passed\n' "$what" >&2
    exit 1
  fi
  printf 'check-seams: planted %s was refused\n' "$what"
}

mkdir -p "$plant/src"
printf '%s\n' '#include "admin.h"' > "$plant/src/core.c"
refused "core.c include of admin.h"
rm -f "$plant/src/core.c"

mkdir -p "$plant/include/ghoti.io/defiant-web"
printf '%s\n' '#include "admin.h"' > "$plant/include/ghoti.io/defiant-web/server.h"
refused "public-header include of admin.h"
rm -f "$plant/include/ghoti.io/defiant-web/server.h"

mkdir -p "$plant/app"
printf '%s\n' '#include "../src/admin.h"' > "$plant/app/defiant.c"
refused "app include of admin.h"
rm -f "$plant/app/defiant.c"

mkdir -p "$plant/tests"
printf '%s\n' '#include "admin.h"' > "$plant/tests/test_other.cpp"
refused "test include of admin.h"
rm -f "$plant/tests/test_other.cpp"

mkdir -p "$plant/src"
printf '%s\n' '#include <ghoti.io/defiant-core/subject_internal.h>' > "$plant/src/server.c"
refused "include of another library's internal header"
rm -f "$plant/src/server.c"

mkdir -p "$plant/src"
printf '%s\n' '#include "../../defiant-core/src/subject/subject_internal.h"' > "$plant/src/server.c"
refused "include through another library's src/"
rm -f "$plant/src/server.c"

