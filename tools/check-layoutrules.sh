#!/bin/sh
#
# Show that each planted defect in the selector rules editor and the checker turns
# its test red, by name (layout editor story 26).
#
# The gate is LayoutRules.* (through the real server: the rules in the engine's order
# with their rank, a rule added, changed and removed by a signed form, a form about a
# rule that changed since, a rule that is not one, and the checker's answer). Each
# plant is an exact text edit to a scratch copy of this library; nothing is planted
# in the library. The control comes first:
# the unpatched copy must pass.
#
# Usage: check-layoutrules.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-layoutrules
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-layoutrules: control passes (LayoutRules on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-layoutrules: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-layoutrules: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testLayoutrules' '*'

T_ORDER=LayoutRules.TheRulesAreInTheEnginesOrderWithTheRankItGivesEachAndOnlyForWhoMayEditThem
T_CRUD=LayoutRules.ARuleIsAddedChangedAndRemovedByASignedFormAndAStaleOneIsRefused
T_TYPE=LayoutRules.TheTypeOfARuleIsAChoiceOfTheTypesTheSiteHasAndAKeptTypeIsNeverDroppedUnseen
T_BAD=LayoutRules.ARuleThatIsNotOneIsRefusedWithAnoticeAndNothingIsWritten
T_TOKEN=LayoutRules.ATokenIsBoundToItsSignInAndItsPurposeAndAForgedOneWritesNothing
T_CHECK=LayoutRules.TheCheckerSaysWhichLayoutAPathGetsAndWhyEachOtherLost

PLANT_NEED_TEXT='NotThere'
plant route-check-dropped src/admin.c testLayoutrules $T_ORDER \
  '    if (!gdefweb_nav_follows(routes, request, path, "core.admin-layout-rules")) {' \
  '    if (0) {'

PLANT_NEED_TEXT='the more specific first'
plant rules-listed-in-the-order-they-were-made src/admin.c testLayoutrules $T_ORDER \
  '  qsort(rows, n, sizeof rows[0], rule_cmp);' \
  '  (void)rule_cmp;'

PLANT_NEED_TEXT='the more specific first'
plant rank-left-out-of-the-order src/admin.c testLayoutrules $T_ORDER \
  '  if (x->rank != y->rank) {
    return x->rank > y->rank ? -1 : 1;
  }' \
  '  if (0 && x->rank != y->rank) {
    return x->rank > y->rank ? -1 : 1;
  }'

PLANT_NEED_TEXT='That rule changed'
plant stale-rule-form-accepted src/admin.c testLayoutrules $T_CRUD \
  '          if (strcmp(rows[i].name, full) == 0 && strcmp(fp, was) == 0) {' \
  '          if (strcmp(rows[i].name, full) == 0 && (strcmp(fp, was) == 0 || 1)) {'

PLANT_NEED_TEXT='out of date'
plant rule-token-not-checked src/admin.c testLayoutrules $T_TOKEN \
  '  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post, post_n,
          "rule-edit", self, NULL)) {' \
  '  if (0 && !asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post, post_n,
          "rule-edit", self, NULL)) {'

PLANT_NEED_TEXT='weight=x'
plant weight-not-validated src/admin.c testLayoutrules $T_BAD \
  '      if (weight[0] != '"'"'\0'"'"' && menu_int(weight, &w) != 0) {' \
  '      if (0 && weight[0] != '"'"'\0'"'"' && menu_int(weight, &w) != 0) {'

PLANT_NEED_TEXT='a less specific pattern'
plant checker-does-not-resolve-an-alias src/admin.c testLayoutrules $T_CHECK \
  '          if (gdefcore_form_resolve(routes, catalog, asked, checked_path + 1,' \
  '          if (0 && gdefcore_form_resolve(routes, catalog, asked, checked_path + 1,'

PLANT_NEED_TEXT='<b>x</b>'
plant checked-path-not-escaped src/admin.c testLayoutrules $T_CHECK \
  '      page_esc(&page, checked_path != NULL ? checked_path : "/", strlen(checked_path != NULL ? checked_path : "/")) != 0 ||' \
  '      page_add(&page, checked_path != NULL ? checked_path : "/", strlen(checked_path != NULL ? checked_path : "/")) != 0 ||'

# ---- the type of a rule is a choice (not typed text) -------------------------------------

# The choice offers every type, config ones too.
PLANT_NEED_TEXT='a config type is not a type of page'
plant type-choice-offers-config-types src/admin.c testLayoutrules $T_TYPE \
  '  if (gdefcore_type_names(catalog, GDEFCORE_TYPE_CONTENT, &names, &lens, &n) != GDEFCORE_OK) {' \
  '  if (gdefcore_type_names(catalog, GDEFCORE_TYPE_CONFIG, &names, &lens, &n) != GDEFCORE_OK) {'

# A rule's type that is not defined is dropped from the form, so saving it would clear it unseen.
PLANT_NEED_TEXT='site.gone'
plant type-choice-drops-a-kept-type src/admin.c testLayoutrules $T_TYPE \
  '  if (current[0] != '"'"'\0'"'"' && !found &&' \
  '  if (0 && current[0] != '"'"'\0'"'"' && !found &&'

# The chosen type is not marked, so an edit form reads "any".
PLANT_NEED_TEXT='selected>site.note'
plant type-choice-selects-nothing src/admin.c testLayoutrules $T_TYPE \
  '        page_str(page, chosen ? "\" selected>" : "\">") != 0 || page_esc(page, names[i], lens[i]) != 0 ||' \
  '        page_str(page, "\">") != 0 || page_esc(page, names[i], lens[i]) != 0 ||'

plant_finish
