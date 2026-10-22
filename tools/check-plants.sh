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

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-plants
# A plant runs every test of its program(s), as the control does; the named
# test has to be among the failures, with the named text in the log.
PLANT_RUN_FILTER='*'
# The defiant program is built from app/, which the copy needs besides the
# directories the helpers take by default.
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-plants: control passes (Scan and Tenants on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-plants: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-plants: every planted defect turned its named test red\n'
}

# The make arguments are what Makefile passes (PREFIX, and the paths it bakes
# into objects, so the copies compile as this tree did).
plant_setup "$@"
plant_control 'testScanner testTenants' '*'

# An internal id in a page: the undo page prints a revision's sequence number
# again, as it did before the scanner found it.
PLANT_NEED_TEXT='/admin/undo (200) body'
plant undo-seq src/admin.c testScanner Scan.EveryRoute \
  '    (void)seq;
    if (page_str(page, "<li>") != 0) {' \
  '    char seq_text[32];
    int n = snprintf(seq_text, sizeof seq_text, "%llu", (unsigned long long)seq);
    if (n <= 0 || page_str(page, "<li>") != 0 ||
        page_esc(page, seq_text, (size_t)n) != 0 || page_str(page, " ") != 0) {'

# An internal id in a header: a row number beside the redirect.
PLANT_NEED_TEXT='header X-Entity-Row'
plant row-header src/server.c testScanner Scan.EveryRoute \
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

# An internal id in an error page: the fixed 404 names a tenant row.
PLANT_NEED_TEXT='unknown host (404) body'
plant error-page src/server.c testScanner Scan.EveryRoute \
  'static const char kFixed[] = "Not found\n";' \
  'static const char kFixed[] = "Not found (tenant 3)\n";'

# A tenant-A token accepted at tenant B: the session is checked against the
# tenant that last signed anyone in, not the one the host names.
PLANT_NEED_TEXT='session at [AB]'
plant session-tenant src/server.c testTenants Tenants.SessionAtOtherTenant \
  'static const char kActive[] = "active";' \
  'static const char kActive[] = "active";
static char plant_last_login[GDEFCORE_PUBLIC_ID_LEN + 1];
static const void * plant_owner;' \
  '  if (gdefcore_inst_storage_key(server->inst, id, strlen(id), storage) !=' \
  '  if (plant_owner != server) {
    plant_owner = server;
    plant_last_login[0] = 0;
  }
  if (in.cookie[0] != 0 && plant_last_login[0] != 0) {
    memcpy(id, plant_last_login, sizeof id);
  }
  if (gdefcore_inst_storage_key(server->inst, id, strlen(id), storage) !=' \
  '    note_success(server, user_key);' \
  '    note_success(server, user_key);
    memcpy(plant_last_login, id, sizeof plant_last_login);'

# The host chosen by a header a stranger can send.
PLANT_NEED_TEXT='Beta Title'
plant forwarded-host src/server.c testTenants Tenants.HostSelects \
  "  if (trusted && in.xhost[0] != '\\0') {" \
  "  if (in.xhost[0] != '\\0') {"

plant_finish
