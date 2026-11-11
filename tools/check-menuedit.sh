#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the menu editor turns its test red, by name
# (site-admin story 10).
#
# The gate is MenuEditor.* (every action as a plain form, a drag as one signed
# post with one undo, the keys, stale and cycle and permission refusals, the
# escaping of a label in the page and in the script, the one hashed script),
# through the real server. Each plant is an exact text edit to a scratch copy of
# this library; nothing is planted in the library. The control comes first: the
# unpatched copy must pass, or a red result below would be a broken build, not a
# caught defect. A plant that leaves a variable unused is written to keep it
# used, because the copy is built with -Werror.
#
# Usage: check-menuedit.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-menuedit
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-menuedit: control passes (MenuEditor on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-menuedit: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-menuedit: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testMenuedit' '*'

plant_stale stale-object src/admin.c testMenuedit \
  'MenuEditor.EveryActionIsAFormThatWorksWithScriptsOff' \
  '  return page_str(page, "<button type=\"submit\" name=\"action\" value=\"") != 0 ||' \
  '  return page_str(page, "<button type=\"button\" name=\"action\" value=\"") != 0 ||'

plant action-requiring-javascript src/admin.c testMenuedit \
  'MenuEditor.EveryActionIsAFormThatWorksWithScriptsOff' \
  '  return page_str(page, "<button type=\"submit\" name=\"action\" value=\"") != 0 ||' \
  '  return page_str(page, "<button type=\"button\" name=\"action\" value=\"") != 0 ||'

PLANT_NEED_TEXT='a stale form was accepted'
plant stale-form-accepted src/admin.c testMenuedit \
  'MenuEditor.AFormBasedOnAnOlderTreeIsRefusedWithAMessageAndTheCurrentTree' \
  '  e.version = version;
  e.version_len = strlen(version);' \
  '  {
    GDEFCORE_Menu * now = NULL;
    GDEFCORE_Route_Match * m0 = NULL;
    if (gdefcore_routes_match(routes, request, "/admin/menus", 12, &m0) == GDEFCORE_OK &&
        gdefcore_menu_entries(catalog, routes, request, m0, &now) == GDEFCORE_OK) {
      (void)gdefcore_menu_version(now, version, sizeof version);
    }
    gdefcore_menu_free(now);
    gdefcore_route_match_free(m0);
  }
  e.version = version;
  e.version_len = strlen(version);'

plant version-not-sent src/admin.c testMenuedit \
  'MenuEditor.AFormBasedOnAnOlderTreeIsRefusedWithAMessageAndTheCurrentTree' \
  '      menu_hidden(&page, "action", "move", 4) != 0 ||
      menu_hidden(&page, "version", version, strlen(version)) != 0 ||' \
  '      menu_hidden(&page, "action", "move", 4) != 0 ||
      menu_hidden(&page, "version", "aaaaaaaaaaaaaaaa", 16) != 0 ||'

PLANT_NEED_TEXT='a token for another purpose'
plant drag-accepted-with-another-forms-token src/admin.c testMenuedit \
  'MenuEditor.WhatThePermissionDoesNotCoverAndAFormNotSignedForThisWriteNothing' \
  '    if (form_len == purpose_n && memcmp(form_id, kMenuPurpose[act->purpose], purpose_n) == 0 &&
        strcmp(word, act->word) == 0) {' \
  '    if ((form_len == purpose_n || 1) && strcmp(word, act->word) == 0) {'

PLANT_NEED_TEXT='another login'"'"'s cookie'
plant drag-written-without-the-signature-check src/admin.c testMenuedit \
  'MenuEditor.WhatThePermissionDoesNotCoverAndAFormNotSignedForThisWriteNothing' \
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

PLANT_NEED_TEXT='That edit is not allowed.'
plant drag-written-without-the-permission src/admin.c testMenuedit \
  'MenuEditor.WhatThePermissionDoesNotCoverAndAFormNotSignedForThisWriteNothing' \
  '  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_menu_stage_edit(cs, routes, request, match, &e);' \
  '  rc = gdefcore_menu_stage_edit(cs, routes, request, match, &e);'

PLANT_NEED_TEXT='data-label'
plant label-unescaped-in-the-attribute src/admin.c testMenuedit \
  'MenuEditor.ALabelWithMarkupIsShownAsTextInThePageAndTheScriptNeverMakesMarkup' \
  '               page_str(&page, "\" data-label=\"") != 0 ||
               page_esc(&page, it.label, it.label_len) != 0 ||' \
  '               page_str(&page, "\" data-label=\"") != 0 ||
               page_add(&page, it.label, it.label_len) != 0 ||'

PLANT_NEED_TEXT='innerHTML'
plant script-sets-markup src/admin.c testMenuedit \
  'MenuEditor.ALabelWithMarkupIsShownAsTextInThePageAndTheScriptNeverMakesMarkup' \
  'return form.querySelector(\"[name=\" + name + \"]\");\n"
    "  }\n"
    "  function say(text) {\n"
    "    live.textContent = text;' \
  'return form.querySelector(\"[name=\" + name + \"]\");\n"
    "  }\n"
    "  function say(text) {\n"
    "    live.innerHTML = text;'

plant key-map-differs-from-the-buttons src/admin.c testMenuedit \
  'MenuEditor.TheEditorIsOperableWithTheKeyboardAlone' \
  '\"ArrowLeft\": \"outdent\"' \
  '\"ArrowLeft\": \"indent\"'

plant entries-not-focusable src/admin.c testMenuedit \
  'MenuEditor.TheEditorIsOperableWithTheKeyboardAlone' \
  '               page_str(&page, "\" tabindex=\"&#48;\" draggable=\"true\">") != 0) {' \
  '               page_str(&page, "\" draggable=\"true\">") != 0) {'

plant no-live-region src/admin.c testMenuedit \
  'MenuEditor.TheEditorIsOperableWithTheKeyboardAlone' \
  '<p id=\"menu-live\" class=\"live\" role=\"status\" aria-live=\"polite\">' \
  '<p id=\"menu-live\" class=\"live\">'

plant keys-script-fields-not-in-the-form src/admin.c testMenuedit \
  'MenuEditor.TheEditorIsOperableWithTheKeyboardAlone' \
  '      menu_field(&page, "Position (counting from the first, which is zero)", "position", "0", 1) != 0 ||' \
  '      menu_field(&page, "Position (counting from the first, which is zero)", "place", "0", 1) != 0 ||'

plant undo-offered-to-everyone src/admin.c testMenuedit \
  'MenuEditor.TheUndoPageOffersTheMenuUndoOnlyToWhoMayEditMenus' \
  '  if (!gdefcore_menu_may_edit(catalog, request)) {
    return 0;
  }
  principal = gdefcore_request_principal(request, &who_n);' \
  '  if (0 && !gdefcore_menu_may_edit(catalog, request)) {
    return 0;
  }
  principal = gdefcore_request_principal(request, &who_n);'

PLANT_NEED_TEXT='The last menu change was undone.'
plant undo-stages-nothing src/admin.c testMenuedit \
  'MenuEditor.ADragIsOneSignedPostOneChangesetAndTheExistingUndoTakesItBack' \
  '  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_menu_stage_undo(cs, request);' \
  '  gdefcore_changeset_set_request(cs, request);
  rc = GDEFCORE_OK;'

plant hide-leaves-the-menu src/admin.c testMenuedit \
  'MenuEditor.HidingRemovesTheEntryFromEveryMenuAndNotThePage' \
  '      menu_button(page, it->hidden ? "show" : "hide", it->hidden ? "Show" : "Hide") != 0 ||' \
  '      menu_button(page, "hide", it->hidden ? "Show" : "Hide") != 0 ||'

plant_finish
