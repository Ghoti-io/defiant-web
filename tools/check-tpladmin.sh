#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the template editor turns its test red, by
# name (site-admin story 3).
#
# The gate is TplAdmin.* (the editor through the real server). Each plant is an
# exact text edit to a scratch copy of this library; nothing is planted in the
# library. The copy is built, the test program is run, and the named test must
# be among the failures, with the named text in its message. A run that fails some other way (a crash, a different test, a
# build error) does not count, and an edit that does not apply exactly once, or
# leaves the file as it was, fails the script before anything is built.
#
# The control comes first: the unpatched copy must pass, or a red result below
# would be a broken build, not a caught defect.
#
# Usage: check-tpladmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-tpladmin
# A plant runs every test of its program(s), as the control does; the named
# test has to be among the failures, with the named text in the log.
PLANT_RUN_FILTER='*'
# The defiant program is built from app/, which the copy needs besides the
# directories the helpers take by default.
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-tpladmin: control passes (TplAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-tpladmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-tpladmin: every planted defect turned its named test red\n'
}

# The make arguments are what Makefile passes (PREFIX, and the paths it bakes
# into objects, so the copies compile as this tree did).
plant_setup "$@"
plant_control 'testTpladmin testEdge' '*'

# The preview is rendered under the live overlay, so it is offered to the
# shared cache that every visitor is served from.
PLANT_NEED_TEXT='offered to the shared cache'
plant preview-shared src/admin.c testTpladmin TplAdmin.PreviewRendersSampleDataAndNothingIsWrittenOrShared \
  '    static const char kOverlay[] = "preview";' \
  '    static const char kOverlay[] = "live";'

# A template's body is shown to a principal who may not edit its kind: the
# permission is not asked when a template is opened.
PLANT_NEED_TEXT='missing.body'
plant body-without-permission src/admin.c testTpladmin TplAdmin.AnotherKindsTemplateIsLikeAMissingOne \
  '  if (field_copy(catalog, id, "kind", kind, kind_cap) != 0 || kind[0] == '"'"'\0'"'"' ||
      !gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    return 1;
  }' \
  '  if (field_copy(catalog, id, "kind", kind, kind_cap) != 0 || kind[0] == '"'"'\0'"'"' ||
      (0 && !gdefcore_template_may_edit(catalog, request, kind, strlen(kind)))) {
    return 1;
  }'

# The save no longer asks for the template's name to be typed.
PLANT_NEED_TEXT='name typed does not match'
plant confirmation-not-required src/admin.c testTpladmin TplAdmin.SaveNeedsTheTypedName \
  '    if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
        confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
          N_WRONG_CONFIRM, kind, body, body_n, NULL, 0, NULL, 0, 0, out);' \
  '    if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 && 0) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
          N_WRONG_CONFIRM, kind, body, body_n, NULL, 0, NULL, 0, 0, out);'

# The revert no longer asks for it either.
PLANT_NEED_TEXT='name typed does not match'
plant revert-confirmation-not-required src/admin.c testTpladmin TplAdmin.HistoryListsRevisionsAndRevertRestoresTheOldBody \
  '  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
    notice = N_WRONG_CONFIRM;' \
  '  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 && 0) {
    notice = N_WRONG_CONFIRM;'

# The list names a kind the principal may not edit, and its templates.
PLANT_NEED_TEXT='Page layout'
plant list-names-unpermitted-kind src/admin.c testTpladmin TplAdmin.ListShowsOnlyKindsThePrincipalMayEdit \
  '        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        !gdefcore_template_may_edit(catalog, request, name, strlen(name))) {
      continue;
    }' \
  '        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        (0 && !gdefcore_template_may_edit(catalog, request, name, strlen(name)))) {
      continue;
    }'

# A stale form is not noticed: the template is overwritten.
PLANT_NEED_TEXT='this template changed while you were editing it'
plant stale-form-ignored src/admin.c testTpladmin TplAdmin.AStaleFormIsAConflictAndNothingIsOverwritten \
  '  if (!base_matches(catalog, id, base)) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_CONFLICT,' \
  '  if (0 && !base_matches(catalog, id, base)) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_CONFLICT,'

# The stored body is written into the textarea as it is.
PLANT_NEED_TEXT='closes'
plant textarea-unescaped src/admin.c testTpladmin TplAdmin.AHostileStoredBodyCannotBreakTheEditPage \
  '      page_esc(&page, body, body_len) != 0 ||
      page_str(&page, "</textarea></label></p><p><label>To save' \
  '      page_add(&page, body, body_len) != 0 ||
      page_str(&page, "</textarea></label></p><p><label>To save'

# What a redirect carries is shown to any session.
PLANT_NEED_TEXT='DRAFT-SECRET'
plant flash-not-session-bound src/admin.c testTpladmin TplAdmin.WhatARedirectCarriesBelongsToOneSession \
  '        memcmp(f->session, sdig, sizeof sdig) != 0 ||
' \
  ''

# A kind the principal may not edit is accepted from a posted form.
PLANT_NEED_TEXT='choose one of the kinds listed'
plant kind-not-checked src/admin.c testTpladmin TplAdmin.MovingToAnotherKindNeedsBothKindsAndTheNewContract \
  '  if (!gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_KIND,' \
  '  if (0 && !gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_KIND,'

# The check token is taken for the save token.
PLANT_NEED_TEXT='out of date'
plant token-purpose-ignored src/admin.c testTpladmin TplAdmin.ASignedFormIsRequired \
  '      strlen(act == A_SAVE ? "tpl-save" : "tpl-check") != form_len ||
      memcmp(form_id, act == A_SAVE ? "tpl-save" : "tpl-check", form_len) != 0) {' \
  '      0) {'

# The editor is shown the body in force and not the latest draft (what the
# editor opens is the draft, else the current revision; story S6b).
PLANT_NEED_TEXT='LATEST-DRAFT-BODY'
plant editor-shows-the-body-in-force src/admin.c testTpladmin TplAdmin.TheEditorOpensTheLatestDraftAndNotOnlyTheBodyInForce \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_DRAFT,' \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,'

# The pages name entities by the published view, so an unpublished template
# cannot be opened by the person who would republish it.
PLANT_NEED_TEXT='<textarea name'
plant admin-names-by-the-published-view src/admin.c testTpladmin TplAdmin.AnUnpublishedTemplateIsStillFoundAndOpenedByItsName \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);' \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);'

# The same plant, seen from the post list: a post that is only a draft has no
# title in the editor's undo page.
PLANT_NEED_TEXT='Post Held Back'
plant admin-names-posts-by-the-published-view src/admin.c testEdge Edge.AdminNamesAnUnpublishedPostAndOffersNoUndoOfADraft \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);' \
  '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);'

plant_finish
