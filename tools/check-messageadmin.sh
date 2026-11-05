#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the admin directory and the messages turns
# its test red, by name (site-admin story 9).
#
# The gate is MessageAdmin.* (the More screen built from the admin menu and
# grouped by module, and a save that answers with a redirect and leaves one
# message, through the real server). The queue's own defects (a message shown
# twice, to a different session, forged, unescaped, past its lifetime) are the
# core's gate. Each plant is an exact text edit to a scratch copy of this
# library; nothing is planted in the library. The control comes first: the
# unpatched copy must pass, or a red result below would be a broken build, not a
# caught defect.
#
# Usage: check-messageadmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-messageadmin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-messageadmin: control passes (MessageAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-messageadmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-messageadmin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testMessageadmin' '*'

BIND_OLD='  (void)gdefcore_message_bind(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);
  routes = gdefweb_nav_open(catalog);'
BIND_NEW='  routes = gdefweb_nav_open(catalog);'

# The session's queue is not bound to an admin request: nothing is ever shown.
PLANT_NEED_TEXT='admin-menu'
plant_stale stale-object src/admin.c testMessageadmin \
  MessageAdmin.ASaveAnswersWithARedirectAndLeavesOneMessageShownOnceToTheSessionThatSaved \
  "$BIND_OLD" "$BIND_NEW"

PLANT_NEED_TEXT='admin-menu'
plant messages-not-bound src/admin.c testMessageadmin \
  MessageAdmin.ASaveAnswersWithARedirectAndLeavesOneMessageShownOnceToTheSessionThatSaved \
  "$BIND_OLD" "$BIND_NEW"

# A public page that places the widget is drawn for a request with no queue.
PLANT_NEED_TEXT='class="main"'
plant public-page-not-bound src/server.c testMessageadmin \
  MessageAdmin.APublicPageThatPlacesTheWidgetShowsTheSessionItsMessageOnce \
  '    gdefweb_admin_bind_messages(catalog, request, keyring, server->root,
        sizeof server->root, session_value);' \
  '    (void)0;'

# A save redirects and says nothing.
PLANT_NEED_TEXT='admin-menu'
plant save-leaves-no-message src/admin.c testMessageadmin \
  MessageAdmin.ASaveAnswersWithARedirectAndLeavesOneMessageShownOnceToTheSessionThatSaved \
  '    say(catalog, request, GDEFCORE_MESSAGE_STATUS,
        written > 0 ? "Saved." : "Nothing changed.");' \
  '    (void)written;'

# The routes ask nobody: the directory lists a page the viewer may not follow.
PLANT_NEED_TEXT='/admin/settings'
plant entry-shown-without-route-permission src/nav.c testMessageadmin \
  MessageAdmin.TheMoreScreenIsTheAdminMenuGroupedByModuleAndEntriesTheViewerMayNotFollowAreAbsent \
  '  if (gdefcore_routes_set_check(routes, gdefcore_access_route_check, catalog) !=
      GDEFCORE_OK) {' \
  '  if (gdefcore_routes_set_check(routes, NULL, NULL) !=
      GDEFCORE_OK) {'

# The directory knows its pages: a module'"'"'s entries are not in it until the
# kernel is changed to list them.
PLANT_NEED_TEXT='<h2>core</h2>'
plant module-route-needing-a-kernel-change src/admin.c testMessageadmin \
  MessageAdmin.TheMoreScreenIsTheAdminMenuGroupedByModuleAndEntriesTheViewerMayNotFollowAreAbsent \
  '    items[nitems].module_len = len;
' \
  '    items[nitems].module_len = len;
    if (strcmp(items[nitems].module, "core") != 0) {
      continue;
    }
'

# A module'"'"'s label is printed as the markup it is not.
PLANT_NEED_TEXT='<b>tools</b>'
plant more-label-unescaped src/admin.c testMessageadmin \
  MessageAdmin.TheMoreScreenIsTheAdminMenuGroupedByModuleAndEntriesTheViewerMayNotFollowAreAbsent \
  '          page_esc(&page, items[i].label, items[i].label_len) != 0 ||' \
  '          page_add(&page, items[i].label, items[i].label_len) != 0 ||'

plant_finish
