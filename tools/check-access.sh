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

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-access
# A plant runs every test of its program(s), as the control does; the named
# test has to be among the failures, with the named text in the log.
PLANT_RUN_FILTER='*'
# The defiant program is built from app/, which the copy needs besides the
# directories the helpers take by default.
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-access: control passes (AccessAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-access: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-access: every planted defect turned its named test red\n'
}

# The make arguments are what Makefile passes (PREFIX, and the paths it bakes
# into objects, so the copies compile as this tree did).
plant_setup "$@"
plant_control 'testAccessadmin' '*'

# A role's name is printed as it was written.
PLANT_NEED_TEXT='Actual: true'
plant source-unescaped src/admin.c testAccessadmin AccessAdmin.HostileTextIsEscaped \
  '        access_cell(&page, "<td>", r->source, r->source_len) != 0 ||' \
  '        page_str(&page, "<td>") != 0 || page_add(&page, r->source, r->source_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'

# A permission's label (a module author's) is printed as it was written.
PLANT_NEED_TEXT='Actual: true'
plant permission-unescaped src/admin.c testAccessadmin AccessAdmin.HostileTextIsEscaped \
  '        access_cell(&page, "<td>", r->permission, r->permission_len) != 0 ||' \
  '        page_str(&page, "<td>") != 0 || page_add(&page, r->permission, r->permission_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'

# A condition's words (which carry the constants an author wrote) are printed
# as they were written.
PLANT_NEED_TEXT='Actual: true'
plant explain-unescaped src/admin.c testAccessadmin AccessAdmin.HostileTextIsEscaped \
  '        access_cell(&page, "<td>", r->explain, r->explain_len) != 0 ||' \
  '        page_str(&page, "<td>") != 0 || page_add(&page, r->explain, r->explain_len) != 0 ||
        page_str(&page, "</td>") != 0 ||'

# The page answers a POST as it answers a GET.
PLANT_NEED_TEXT='Granted by'
plant post-accepted src/admin.c testAccessadmin AccessAdmin.ReadOnly \
  '    rc = is_get ? access_page(alloc, catalog, request, out)
                : unknown_admin(method, out);' \
  '    rc = access_page(alloc, catalog, request, out);'

# A module's rows are left off the page.
PLANT_NEED_TEXT='module acc'
plant module-rows-dropped src/admin.c testAccessadmin AccessAdmin.TheAdministratorSeesRoleAndModuleRows \
  '  for (i = 0; i < n; i++) {
    const GDEFCORE_Access_Matrix_Row * r = &rows[i];' \
  '  for (i = 0; i < n; i++) {
    const GDEFCORE_Access_Matrix_Row * r = &rows[i];
    if (r->from_module) {
      continue;
    }'

# The stale-object plant. A plant's copy starts from the finished build, so the
# edit has to be compiled into it; this one is made not to be (the objects that
# depend on the edit are left looking up to date), and the gate has to refuse
# it as a STALE OBJECT. If it ever passes instead, a warm copy could pass with
# the defect never built in.
PLANT_NEED_TEXT=
plant_stale stale-object src/admin.c testAccessadmin AccessAdmin.ReadOnly \
  '    rc = is_get ? access_page(alloc, catalog, request, out)
                : unknown_admin(method, out);' \
  '    rc = access_page(alloc, catalog, request, out);'

plant_finish
