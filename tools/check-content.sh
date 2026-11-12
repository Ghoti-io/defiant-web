#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the content administration turns its test
# red, by name (web content admin story 1).
#
# The gate is ContentAdmin.* (a type defined by the test and named nowhere in
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
  '            page_esc(page, texts[j], text_lens[j]) != 0) {' \
  '            page_add(page, texts[j], text_lens[j]) != 0) {'

# The version the form was drawn at is not compared, so an old form saves.
PLANT_NEED_TEXT='changed while you were editing'
plant version-not-compared src/content.c testContent ContentAdmin.AFormOpenedBeforeAnotherSaveIsStaleAndWritesNothing \
  '  if (!token_ok(c, body, body_len, "content-save", id, &base) ||
      !base_matches(c->catalog, id, base)) {' \
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
PLANT_NEED_TEXT='long_title.status'
plant constraints-not-asked src/content.c testContent ContentAdmin.ARequiredFieldAndALimitRefuseTheFormAndGiveWhatWasTypedBack \
  '  if (!values_fit(c, body, body_len, values, nvalues)) {' \
  '  if (0) {'

plant_finish
