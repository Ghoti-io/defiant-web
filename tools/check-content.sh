#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the content administration turns its test
# red, by name (web content admin stories 1 and 2).
#
# The gate is ContentAdmin.* and ContentFields.* (a type defined by the test and named nowhere in
# the library, through the real server). Each plant is an exact text edit to a
# scratch copy of this library; nothing is planted in the library. The control
# comes first: the unpatched copy must pass, or a red result below would be a
# broken build, not a caught defect.
#
# Usage: check-content.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-content
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-content: control passes (ContentAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-content: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-content: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testContent' '*'

# A type is found only if it is one the code was written for: the pages are for
# the types the site defines, whatever their names.
PLANT_NEED_TEXT='the other type as well'
plant type-named-in-the-code src/content.c testContent ContentAdmin.ATypeTheLibraryNeverHeardOfHasItsListCreateEditAndDeletePages \
  '      found = lens[i] == name_len && memcmp(names[i], name, name_len) == 0;' \
  '      found = lens[i] == name_len && memcmp(names[i], name, name_len) == 0 &&
              memcmp(name, "site.recipe", 11) == 0;'

# Every type is offered to everyone who reaches the admin area.
PLANT_NEED_TEXT='There is no type you may manage'
plant every-type-touchable src/content.c testContent ContentAdmin.WhoMayDoWhatDecidesWhatIsThereAndAnythingElseIsNotThere \
  '  return may_create(c, type) || allowed(c, GDEFCORE_ACTION_UPDATE, type, NULL) ||
         allowed(c, GDEFCORE_ACTION_DELETE, type, NULL);' \
  '  (void)c;
  (void)type;
  return 1;'

# The route's own check, which is the admin area permission, is not asked.
PLANT_NEED_TEXT='NotThere(Get("/admin/content/site.recipe", outsider))'
plant area-check-skipped src/content.c testContent ContentAdmin.TheAdminAreaPermissionIsStillWhatOpensThePages \
  '  if (!gdefweb_nav_follows(routes, request, path, seg[seg_n] == '"'"'\0'"'"'
                                                      ? "core.admin-content-type"
                                                      : "core.admin-content-entity") &&
      !gdefweb_nav_follows(routes, request, path, "core.admin-content-page")) {' \
  '  if (0) {'

# The button's page does not ask whether this person may create this type.
PLANT_NEED_TEXT='a create grant on another type'
plant new-without-create-check src/content.c testContent ContentAdmin.WhoMayDoWhatDecidesWhatIsThereAndAnythingElseIsNotThere \
  '  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  if (!may_create(c, type)) {' \
  '  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  if (0) {'

# A value in a list is written as it is.
PLANT_NEED_TEXT='listed.body, "<script>alert(1)'
plant list-cell-not-escaped src/content.c testContent ContentAdmin.AValueIsInertInTheListTheFormAndTheMessages \
  '    rc = page_esc(page, texts[0], n) != 0 ||' \
  '    rc = page_add(page, texts[0], n) != 0 ||'

# A value in a form is written as it is.
PLANT_NEED_TEXT='edit.body, "<script>alert(1)'
plant form-value-not-escaped src/content.c testContent ContentAdmin.AValueIsInertInTheListTheFormAndTheMessages \
  '  return page_str(p, " value=\"") != 0 || page_esc(p, text, n) != 0 || page_str(p, "\">");' \
  '  return page_str(p, " value=\"") != 0 || page_add(p, text, n) != 0 || page_str(p, "\">");'

# The version the form was drawn at is not compared, so an old form saves.
PLANT_NEED_TEXT='changed while you were editing'
plant version-not-compared src/content.c testContent ContentAdmin.AFormOpenedBeforeAnotherSaveIsStaleAndWritesNothing \
  '  if (!token_ok(c, body, body_len, "content-save", id, &base) ||
      (!more && !base_matches(c->catalog, id, base))) {' \
  '  if (!token_ok(c, body, body_len, "content-save", id, &base)) {'

# A token made for another purpose of the same session is taken for this one.
PLANT_NEED_TEXT='ada_, drop'
plant token-purpose-not-checked src/content.c testContent ContentAdmin.EveryFormIsSignedForItsSessionItsPurposeAndItsEntity \
  '      got_len == strlen(form_id) && memcmp(got_form, form_id, got_len) == 0) {' \
  '      ((void)form_id, got_len > 0)) {'

# A content entity is deleted without the box being ticked.
PLANT_NEED_TEXT='unticked.body, "Nothing was changed"'
plant delete-without-confirmation src/content.c testContent ContentAdmin.ATypeTheLibraryNeverHeardOfHasItsListCreateEditAndDeletePages \
  '              : !(typed_n == 3 && memcmp(typed, "yes", 3) == 0))) {' \
  '              : 0)) {'

# Configuration is saved without the type name typed.
PLANT_NEED_TEXT='wrong.status'
plant config-saved-without-confirmation src/content.c testContent ContentAdmin.ConfigurationIsChangedAndDeletedOnlyWithTheTypeNameTyped \
  '          typed_n != strlen(type) || memcmp(typed, type, typed_n) != 0 || confirm_n == 0) {' \
  '          confirm_n == 0) {'

# The core is not asked whether the values fit the fields.
PLANT_NEED_TEXT='beside the field'
plant constraints-not-asked src/content.c testContent ContentAdmin.ARequiredFieldAndALimitRefuseTheFormAndGiveWhatWasTypedBack \
  '  if (values_fit(c, body, body_len, given, ngiven)) {
    return;
  }' \
  '  if ((void)given, 1) {
    return;
  }'

# ---- story 2: every field type, rows and groups ---------------------------

# A value is not checked against its field's type, so no message is beside it
# (the core would still refuse it, with no word about which).
PLANT_NEED_TEXT='must be a whole number'
plant typed-value-not-checked src/content.c testContent ContentFields.AValueOfTheWrongKindIsToldBesideItsFieldAndWhatWasTypedComesBack \
  '  size_t i = 0;
  switch (f->type) {' \
  '  size_t i = 0;
  switch (0) {'

# A reference is accepted when the person could not have chosen it.
PLANT_NEED_TEXT='typed.status'
plant reference-readability-not-checked src/content.c testContent ContentFields.ACustomerOnlyTheEditorMaySeeIsNotOfferedAndNotAccepted \
  '        ok = allowed(c, GDEFCORE_ACTION_READ, type, canon);' \
  '        ok = 1;'

# The entities a reference offers are listed without the access filter.
PLANT_NEED_TEXT='not offered'
plant references-offered-unfiltered src/content.c testContent ContentFields.ACustomerOnlyTheEditorMaySeeIsNotOfferedAndNotAccepted \
  '    if (gdefcore_access_list_page(c->catalog, c->request, &query, &ids, &nids, &total) !=
        GDEFCORE_OK) {' \
  '    if (((void)query, (void)total, gdefcore_entity_ids(c->catalog, type, lens[t], &ids, &nids)) !=
        GDEFCORE_OK) {'

# Adding rows writes what was typed.
PLANT_NEED_TEXT='more.status'
plant add-rows-writes src/content.c testContent ContentFields.AManyFieldIsARowToEachValueAndRowsCanBeAddedWithoutSaving \
  '  if (more) {
    /* Rows were asked for, and nothing is written. */' \
  '  if (0) {
    /* Rows were asked for, and nothing is written. */'

# An item sent back with its id is saved as a new one, so it loses its identity.
PLANT_NEED_TEXT='ValueOf(last.body, "lines-0--i")'
plant group-item-identity-dropped src/content.c testContent ContentFields.AGroupIsItemsThatKeepTheirIdentityAndCanBeAddedAndTakenAway \
  '    if (gdefcore_tree_add(nt, nitem, fname(g), fname_len(g), gid, &nc) != GDEFCORE_OK ||' \
  '    if (gdefcore_tree_add(nt, nitem, fname(g), fname_len(g), 0, &nc) != GDEFCORE_OK ||'

# An item ticked to be removed is kept.
PLANT_NEED_TEXT='ValueOf(last.body, "lines-1-item")'
plant removed-item-kept src/content.c testContent ContentFields.AGroupIsItemsThatKeepTheirIdentityAndCanBeAddedAndTakenAway \
  '    if (form_field_alloc(v->body, v->body_len, rm, 0, &got, &got_n) == 0) {
      free(got);
      continue;
    }
    snprintf(nprefix, sizeof nprefix, "%s%.*s-%zu-", prefix, (int)fname_len(g), fname(g), k);
    if (gid == 0' \
  '    if ((void)got_n, 0) {
      free(got);
      continue;
    }
    snprintf(nprefix, sizeof nprefix, "%s%.*s-%zu-", prefix, (int)fname_len(g), fname(g), k);
    if (gid == 0'

# A blank row of a group is made an item.
PLANT_NEED_TEXT='saved.status'
plant blank-row-made-an-item src/content.c testContent ContentFields.AGroupIsItemsThatKeepTheirIdentityAndCanBeAddedAndTakenAway \
  '    if (gid == 0 && item_blank(v->s, gi, nprefix, v->body, v->body_len)) {' \
  '    if (0) {'

# A required group is not asked to have an item (the core still refuses, silently).
PLANT_NEED_TEXT='at least one is required'
plant required-group-not-wanted src/content.c testContent ContentFields.AGroupIsItemsThatKeepTheirIdentityAndCanBeAddedAndTakenAway \
  '  if (g->required && kept == 0) {' \
  '  if (g->required && kept == 0 && 0) {'

# A field the person may not update is dropped from the save instead of kept.
PLANT_NEED_TEXT='ValueOf(after.body, "memo")'
plant unshown-field-not-kept src/content.c testContent ContentFields.AFieldThePersonMayNotUpdateIsNotOnTheirFormAndIsKeptAsItWas \
  '      if (ot != NULL && copy_field(v->s, j, ot, 0, nt, nitem) != 0) {' \
  '      if ((void)ot, 0) {'

# A type with a personal field is saved as a tree, which would take that value away.
PLANT_NEED_TEXT='entries is kept as it is'
plant personal-type-saved-as-tree src/content.c testContent ContentFields.ATypeWithAPersonalFieldKeepsItsGroupsAsTheyAreAndSaysSo \
  '  return !s->has_personal;' \
  '  return (void)s, 1;'

plant_finish
