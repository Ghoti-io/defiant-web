#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the views pages turns its test red, by name
# (web content admin story 5).
#
# The gate is ViewsAdmin.* (a type defined by the test and named nowhere in the
# library, through the real server). Each plant is an exact text edit to a
# scratch copy of this library; nothing is planted in the library. The control
# comes first: the unpatched copy must pass, or a red result below would be a
# broken build, not a caught defect.
#
# Usage: check-views-admin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-views-admin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-views-admin: control passes (ViewsAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-views-admin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-views-admin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testViews' '*'

V=src/views.c
T_WHO='ViewsAdmin.WhoMayEditViewsDecidesWhatIsThereAndAnAreaWithoutItSeesNothing'
T_HOSTILE='ViewsAdmin.AHostileValueIsInertInThePageThePreviewTheListAndTheFormats'
T_PAGE='ViewsAdmin.APagePagesAndADeleteNeedsItsNameTypedAndAFreshForm'
T_REFUSE='ViewsAdmin.ARefusedSaveShowsTheCoresReasonAndGivesWhatWasTypedBack'

# The route's own check, which is the permission, is not asked.
PLANT_NEED_TEXT='NotThere(Get("/admin/views", reader))'
plant route-check-skipped $V testViews "$T_WHO" \
  '  if (!gdefweb_nav_follows(routes, request, VIEWS_BASE, "core.admin-views") ||
      !gdefweb_views_path(path)) {' \
  '  if (routes == NULL || !gdefweb_views_path(path)) {'

# A value in the public table is written as it is.
PLANT_NEED_TEXT='<script>alert'
plant public-cell-not-escaped $V testViews "$T_HOSTILE" \
  '        if (page_str(&page, "<td>") != 0 || page_esc(&page, v, n) != 0 ||' \
  '        if (page_str(&page, "<td>") != 0 || page_add(&page, v, n) != 0 ||'

# A value in a public list is written as it is.
PLANT_NEED_TEXT='<li>&lt;script&gt;'
plant public-list-not-escaped $V testViews "$T_HOSTILE" \
  '        if ((col > 0 && page_str(&page, " &middot; ") != 0) || page_esc(&page, v, n) != 0) {' \
  '        if ((col > 0 && page_str(&page, " &middot; ") != 0) || page_add(&page, v, n) != 0) {'

# A value in the preview is written as it is.
PLANT_NEED_TEXT='prev.body, "<script>alert'
plant preview-cell-not-escaped $V testViews "$T_HOSTILE" \
  '      if (page_str(page, "<td>") != 0 || page_esc(page, v, n) != 0 ||' \
  '      if (page_str(page, "<td>") != 0 || page_add(page, v, n) != 0 ||'

# A heading is written as it is.
PLANT_NEED_TEXT='<b>Head'
plant public-heading-not-escaped $V testViews "$T_HOSTILE" \
  '      if (page_str(&page, "<th>") != 0 || page_esc(&page, l, n) != 0 ||' \
  '      if (page_str(&page, "<th>") != 0 || page_add(&page, l, n) != 0 ||'

# A CSV or a JSON may be sniffed as a page.
PLANT_NEED_TEXT='X-Content-Type-Options'
plant formats-sniffable src/server.c testViews "$T_HOSTILE" \
  '      pub.csp = policy;
      pub.nosniff = 1;
      reply_full(server, sock, page.status, page.reason, page.body, page.len, NULL, page.type,' \
  '      pub.csp = policy;
      pub.nosniff = 0;
      reply_full(server, sock, page.status, page.reason, page.body, page.len, NULL, page.type,'

# The core's reason for a refusal is not shown.
PLANT_NEED_TEXT='Not saved: '
plant reason-dropped $V testViews "$T_REFUSE" \
  '  if (reason != NULL &&
      (page_str(&page, "<p class=\"notice\" role=\"alert\">Not saved: ") != 0 ||' \
  '  if (0 &&
      (page_str(&page, "<p class=\"notice\" role=\"alert\">Not saved: ") != 0 ||'

# A new form may replace a view that is there.
PLANT_NEED_TEXT='a view of that name exists already'
plant new-replaces-existing $V testViews "$T_REFUSE" \
  '  if (entity[0] == '"'"'\0'"'"' && view_entity(c, d.name, own)) {' \
  '  if (0 && view_entity(c, d.name, own)) {'

# A delete does not need the name typed.
PLANT_NEED_TEXT='Nothing was changed'
plant delete-without-the-name $V testViews "$T_PAGE" \
  '  if (strcmp(typed, name) != 0) {' \
  '  if (0) {'

# A delete form drawn before a save is still good.
PLANT_NEED_TEXT='out of date'
plant delete-stale-accepted $V testViews "$T_PAGE" \
  '      strncmp(target, entity, GDEFCORE_PUBLIC_ID_LEN) != 0 ||
      !base_matches(c->catalog, entity, base)) {
    return form_page(c, &d, entity, NULL,
        "Not deleted: the form was out of date. Nothing was changed.", 0, NULL, 0, out);' \
  '      strncmp(target, entity, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    return form_page(c, &d, entity, NULL,
        "Not deleted: the form was out of date. Nothing was changed.", 0, NULL, 0, out);'

# The page asked for is not the page given.
PLANT_NEED_TEXT='the one row left'
plant public-offset-ignored $V testViews "$T_PAGE" \
  '      (uint64_t)(page_no - 1) * limit, &result);' \
  '      (uint64_t)(page_no - 1) * 0, &result);'

# The content form drops the core's words for a refused save.
PLANT_NEED_TEXT='lines.sku'
plant content-refusal-dropped src/content.c testViews 'ViewsAdmin.TheContentFormShowsTheCoresRefusalOfANestedFieldByItsPath' \
  '        if (why != NULL && why_n > 0) {
          snprintf(c->reason, sizeof c->reason, "%.*s", (int)why_n, why);' \
  '        if (0) {
          snprintf(c->reason, sizeof c->reason, "%.*s", (int)why_n, why);'

plant_finish
