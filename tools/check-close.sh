#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
# Copyright (C) 2026 Corey Pennycuff
# This file is part of Ghoti.io Defiant Web.
#
# The close of the web content administration epic: one test, through the real
# server and only its forms, runs the success signal on a site where a developer
# declared two types and nothing else. This gate plants one defect for each of its
# five steps in a copy of the source and requires the test to fail in that step:
#
#   index-lists-types-without-permission  the content index                 (step 1)
#   delete-writes-nothing                 create, edit, delete              (step 2)
#   view-page-draws-no-rows               the view as a page                (step 3)
#   editor-does-not-offer-the-view-widget the view as a widget              (step 4)
#   sweep-does-nothing                    the sweep                         (step 5)
#
# The test's trace for the step (a name in the failure text) is what each plant
# must turn up, so a plant that failed some other step is not counted.
#
# Usage: check-close.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-close
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-close: control passes (Close on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-close: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-close: every planted defect turned the end-to-end test red in its own step\n'
}

plant_setup "$@"
plant_control 'testClose' '*'

T='Close.ADeveloperDeclaresTwoTypesAndAnEditorRunsThemFromTheBrowser'

# Step 1: a person with no right to a type is offered it in the index.
PLANT_NEED_TEXT='the content index'
plant index-lists-types-without-permission src/content.c testClose "$T" \
  '    if (!may_touch(c, type)) {
      continue;
    }
    if (!opened &&' \
  '    if (0 && !may_touch(c, type)) {
      continue;
    }
    if (!opened &&'

# Step 2: the delete form says it deleted and writes nothing.
PLANT_NEED_TEXT='create, edit, delete'
plant delete-writes-nothing src/content.c testClose "$T" \
  '  r = gdefcore_entity_clear(cs, id, GDEFCORE_PUBLIC_ID_LEN);' \
  '  r = GDEFCORE_OK;'

# Step 3: the public page of a view draws its headings and no row.
PLANT_NEED_TEXT='the view as a page'
plant view-page-draws-no-rows src/views.c testClose "$T" \
  '    for (row = 0; row < gdefcore_view_result_rows(result); row++) {
      if (page_str(&page, "<tr>") != 0) {' \
  '    for (row = gdefcore_view_result_rows(result); row < gdefcore_view_result_rows(result); row++) {
      if (page_str(&page, "<tr>") != 0) {'

# Step 4: the layout editor does not offer the view widget.
PLANT_NEED_TEXT='the view as a widget'
plant editor-does-not-offer-the-view-widget src/admin.c testClose "$T" \
  '    if (gdefcore_layout_widget_at(catalog, i, &w) != GDEFCORE_OK) {
      goto fail;
    }
    if (page_str(&page, "<option value=\"") != 0 ||' \
  '    if (gdefcore_layout_widget_at(catalog, i, &w) != GDEFCORE_OK) {
      goto fail;
    }
    if (w.builtin == 9) {
      continue;
    }
    if (page_str(&page, "<option value=\"") != 0 ||'

# Step 5: the host's sweep returns success and sweeps nothing.
PLANT_NEED_TEXT='the sweep'
plant sweep-does-nothing src/files.c testClose "$T" \
  '  rc = gdefcore_upload_sweep(catalog, g_blobs, g_policy, now_ms, out_promoted, out_demoted,
      out_removed);' \
  '  (void)now_ms;
  (void)out_promoted;
  (void)out_demoted;
  (void)out_removed;
  rc = GDEFCORE_OK;'

plant_finish
