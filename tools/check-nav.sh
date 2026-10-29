#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the admin area's navigation turns its test
# red, by name (site-admin story 6).
#
# The gate is NavAdmin.* (the menu, the breadcrumbs, the tabs and the menus page
# through the real server). Each plant is an exact text edit to a scratch copy
# of this library; nothing is planted in the library. The copy is built, the
# test program is run, and the named test must be among the failures, with the
# named text in its message. A run that fails some other way (a crash, a
# different test, a build error) does not count, and an edit that does not apply
# exactly once, or leaves the file as it was, fails the script before anything
# is built.
#
# The control comes first: the unpatched copy must pass, or a red result below
# would be a broken build, not a caught defect.
#
# Usage: check-nav.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-nav
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-nav: control passes (NavAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-nav: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-nav: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testNavadmin' '*'

# The menus page is served to an administrator whose route check says no: the
# page does not ask the route it is the page of.
PLANT_NEED_TEXT='refused.status'
plant menus-page-ungated src/admin.c testNavadmin NavAdmin.TheMenusPageIsThereOnlyForWhoMayFollowItsRoute \
  '    if (!gdefweb_nav_follows(routes, request, path, "core.admin-menus")) {' \
  '    if (0 && !gdefweb_nav_follows(routes, request, path, "core.admin-menus")) {'

# The menus route names no permission, so its entry is for everyone who reaches
# the area, and the page's own check lets everyone through.
PLANT_NEED_TEXT='/admin/menus'
plant menus-route-without-permission src/nav.c testNavadmin NavAdmin.TheMenuIsTheCoresAndShowsWhatTheRoutesAllow \
  '#define PERM_MENUS "core.menu.edit"' \
  '#define PERM_MENUS NULL'

# The templates entry names no permission: shown to an administrator who may
# edit no template.
PLANT_NEED_TEXT='/admin/templates'
plant templates-entry-without-permission src/nav.c testNavadmin NavAdmin.TheMenuIsTheCoresAndShowsWhatTheRoutesAllow \
  '#define PERM_TEMPLATES GDEFCORE_TEMPLATE_EDIT' \
  '#define PERM_TEMPLATES NULL'

# The pages carry no navigation: the marker is never replaced.
PLANT_NEED_TEXT='menu.empty'
plant navigation-not-spliced src/admin.c testNavadmin NavAdmin.TheMenuIsTheCoresAndShowsWhatTheRoutesAllow \
  '  at = strstr(out->body, kMark);
  if (at == NULL) {' \
  '  at = strstr(out->body, kMark);
  if (at == NULL || 1) {'

# A form is read whether or not it was signed for this session.
PLANT_NEED_TEXT='a damaged token'
plant token-not-checked src/admin.c testNavadmin NavAdmin.AFormNotSignedForThisWritesNothing \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was changed.");
    return redirect(out, "/admin/menus");' \
  '      ((void)keyring, (void)root, (void)root_len, (void)session, (void)session_len, 0) ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was changed.");
    return redirect(out, "/admin/menus");'

# The change is applied without the request, so the core is not asked who may
# edit menus: what the permission does not cover is written.
PLANT_NEED_TEXT='That change was not made.'
plant request-not-carried src/admin.c testNavadmin NavAdmin.WhatThePermissionDoesNotCoverIsRefusedAndSaid \
  '  gdefcore_changeset_set_request(cs, request);
  rc = stage(cs, ctx) == 0' \
  '  rc = stage(cs, ctx) == 0'

# A second change drops what the first set: the stored override is not read.
PLANT_NEED_TEXT='label is stored'
plant override-merge-dropped src/admin.c testNavadmin NavAdmin.RenamingAndReweightingKeepsWhatWasSetBefore \
  '    if (menu_override_stored(catalog, w.name, strlen(w.name), label, weight, hidden,
            parent, menu, sizeof label) != 0) {
      return -1;
    }' \
  '    label[0] = weight[0] = hidden[0] = parent[0] = menu[0] = 0;
    if (0) {
      return -1;
    }'

# An entry'"'"'s label is printed as it is on the menus page.
PLANT_NEED_TEXT='<script>alert'
plant label-unescaped src/admin.c testNavadmin NavAdmin.ASiteEntryIsAddedChangedAndRemoved \
  '    if (page_esc(&page, it.label, it.label_len) != 0 ||' \
  '    if (page_add(&page, it.label, it.label_len) != 0 ||'

plant_finish
