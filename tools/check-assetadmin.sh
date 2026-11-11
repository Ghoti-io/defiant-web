#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the editor of the site's own stylesheets and
# scripts turns its test red, by name (layout editor story 21).
#
# The gate is AssetAdmin.* (through the real server: the two permissions, the signed
# form, a stale form, the typed name of a script, a delete, the history, the
# spelling of a name, a grant narrowed to some files). Each plant is an exact text
# edit to a scratch copy of this library; nothing is planted in the library. The
# control comes first: the unpatched copy must pass.
#
# Usage: check-assetadmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-assetadmin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-assetadmin: control passes (AssetAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-assetadmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-assetadmin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testAssetadmin' '*'

T_PAGES=AssetAdmin.ThePagesAreThereOnlyForWhoHoldsTheirPermission
T_REFUSED=AssetAdmin.ARefusedFormWritesNothingAndGivesTheTextBack
T_SIGNED=AssetAdmin.ASignedFormIsRequiredAndIsBoundToItsSessionItsPurposeAndItsFile
T_STALE=AssetAdmin.AFormOpenedBeforeAnotherSaveIsStaleAndWritesNothing
T_TYPED=AssetAdmin.AScriptIsSavedOnlyWithItsNameTypedAndAStylesheetIsNot
T_HIST=AssetAdmin.TheHistoryListsTheChangesAndTheLatestIsRevertedWithTheNameTyped
T_NAMES=AssetAdmin.AFileOfTheOtherKindOrASpellingThatIsNotAFileNameIsNotThere
T_NARROW=AssetAdmin.ANarrowGrantEditsOnlyTheFilesItNamesAndTheRestAreNotThere
T_MADE=AssetAdmin.AStylesheetIsMadeByAFormAndServedAtOnce

# The page is shown to whoever is signed in, whatever the route's check says.
PLANT_NEED_TEXT='NotThere'
plant route-check-dropped src/admin.c testAssetadmin $T_PAGES \
  '    if (!gdefweb_nav_follows(routes, request, k->base, k->machine)) {' \
  '    if (0) {'

# A form's signature is not verified.
PLANT_NEED_TEXT='elsewhere'
plant signature-not-checked src/admin.c testAssetadmin $T_SIGNED \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&
      token_meta(raw, raw_n, got_form,' \
  '      (gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) == GDEFCORE_OK || 1) &&
      token_meta(raw, raw_n, got_form,'

# A token for one file is taken for another's.
PLANT_NEED_TEXT='wrong_file'
plant token-not-bound-to-its-file src/admin.c testAssetadmin $T_SIGNED \
  '      memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) == 0 &&
      got_len == strlen(form_id)' \
  '      (memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) == 0 || 1) &&
      got_len == strlen(form_id)'

# A token for one action is taken for another's.
PLANT_NEED_TEXT='wrong_purpose'
plant token-not-bound-to-its-action src/admin.c testAssetadmin $T_SIGNED \
  '      got_len == strlen(form_id) && memcmp(got_form, form_id, got_len) == 0) {' \
  '      (got_len == strlen(form_id) && memcmp(got_form, form_id, got_len) == 0) || 1) {'

# A form signed before another save writes over it.
PLANT_NEED_TEXT='changed while you were editing'
plant stale-form-saved src/admin.c testAssetadmin $T_STALE \
  '  } else if (!base_matches(catalog, id, base)) {
    notice = AN_CONFLICT;' \
  '  } else if (0) {
    notice = AN_CONFLICT;'

# A script is saved without its name typed.
PLANT_NEED_TEXT='name typed does not match'
plant script-saved-without-its-name src/admin.c testAssetadmin $T_TYPED \
  '  } else if ((save && !k->typed) ? 0' \
  '  } else if (save ? 0'

# A delete of a stylesheet is made without its name typed.
PLANT_NEED_TEXT='keep'
plant stylesheet-deleted-without-its-name src/admin.c testAssetadmin $T_TYPED \
  '  } else if ((save && !k->typed) ? 0' \
  '  } else if (!k->typed ? 0'

# Scripts are treated as stylesheets are, so no name is asked.
PLANT_NEED_TEXT='name typed does not match'
plant scripts-not-typed src/admin.c testAssetadmin $T_TYPED \
  '        GDEFCORE_ASSET_JS_TYPE, GDEFCORE_ASSET_JS_EDIT, "Scripts", "script", 1},' \
  '        GDEFCORE_ASSET_JS_TYPE, GDEFCORE_ASSET_JS_EDIT, "Scripts", "script", 0},'

# A refused form loses what was typed.
PLANT_NEED_TEXT='evil()'
plant refused-text-lost src/admin.c testAssetadmin $T_TYPED \
  '      session_len, k, id, name, save ? text : NULL, save ? text_n : 0, notice, said,' \
  '      session_len, k, id, name, NULL, 0, notice, said,'

# A text is written into the page as it is.
PLANT_NEED_TEXT='alert'
plant text-not-escaped src/admin.c testAssetadmin $T_MADE \
  '         page_esc(page, text, n) != 0 || page_str(page, "</textarea></label></p>");' \
  '         page_add(page, text, n) != 0 || page_str(page, "</textarea></label></p>");'

# A name in a path is looked up whatever it is spelled like.
PLANT_NEED_TEXT='NotThere'
plant encoded-name-looked-up src/admin.c testAssetadmin $T_NAMES \
  '      name_len != (size_t)(tail - rest)) {' \
  '      0) {'

# A file the principal's grant does not name is opened.
PLANT_NEED_TEXT='yours.css'
plant file-open-ignores-the-grant src/admin.c testAssetadmin $T_NARROW \
  '  return ok ? 0 : 1;' \
  '  return 0;'

# A revert is made without the name typed.
PLANT_NEED_TEXT='name typed does not match'
plant revert-without-its-name src/admin.c testAssetadmin $T_HIST \
  '  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
    notice = AN_WRONG_CONFIRM;' \
  '  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n == (size_t)-1) {
    notice = AN_WRONG_CONFIRM;'

plant_finish
