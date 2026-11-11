#!/bin/sh
#
# Show that each planted defect in the page structure editor turns its test red, by
# name (layout editor story 27, web half).
#
# The gate is StructureAdmin.* (through the real server: the list and who sees it,
# a structure started from a point and drawn as a preview and rows, every action a
# signed form, a rename that carries a layout's placements, a removal that sets
# them aside and says so, a stale form, a template written by hand that needs its
# name typed, and a new template). Each plant is an exact text edit to a scratch
# copy of this library; nothing is planted in the library. The control comes
# first: the unpatched copy must pass.
#
# Usage: check-structureadmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-structureadmin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-structureadmin: control passes (StructureAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-structureadmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-structureadmin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testStructureadmin' '*'

T_LIST=StructureAdmin.TheListIsTheTemplatesThePrincipalMayEditAndNobodyElseIsToldOfThem
T_ACTIONS=StructureAdmin.EveryActionIsASignedFormThatWorksWithScriptsOff
T_CARRY=StructureAdmin.ARenameCarriesALayoutsPlacementsAndARemovalSetsThemAsideAndSaysSo
T_STALE=StructureAdmin.AFormDrawnBeforeAnotherChangeAndATokenOfAnotherFormWriteNothing
T_CUSTOM=StructureAdmin.ATemplateWrittenByHandNeedsItsNameAndIsOnlyReplacedByANewStart
T_NEW=StructureAdmin.ANewTemplateIsMadeFromANameAndARegionAndABadNameIsRefused

# The page is shown to whoever is signed in, whatever the route's check says, and a
# template is opened for anyone who can sign in, not only for who may edit it.
PLANT_NEED_TEXT='NotThere'
plant route-check-dropped src/admin.c testStructureadmin $T_LIST \
  '    if (!gdefweb_nav_follows(routes, request, "/admin/structures", "core.admin-structures")) {' \
  '    if (0) {'

PLANT_NEED_TEXT='NotThere'
plant template-open-ignores-the-permission src/admin.c testStructureadmin $T_LIST \
  '    if (!gdefweb_nav_follows(routes, request, "/admin/structures", "core.admin-structures")) {' \
  '    if (0) {' \
  '  ok = gdefcore_template_may_edit(catalog, request, "core.page", 9);
  return ok ? 0 : 1;' \
  '  ok = gdefcore_template_may_edit(catalog, request, "core.page", 9);
  return (ok || 1) ? 0 : 1;'

# A form drawn before another change is not refused by the page.
PLANT_NEED_TEXT='out of date'
plant stale-form-not-refused-by-the-page src/admin.c testStructureadmin $T_STALE \
  '      !base_matches(catalog, id, base) || layout_field_text(post, post_n, "act", act, sizeof act) != 0 ||' \
  '      (0 && !base_matches(catalog, id, base)) || layout_field_text(post, post_n, "act", act, sizeof act) != 0 ||'

# A template is handed to the hand without its name typed.
PLANT_NEED_TEXT='The name typed does not match'
plant custom-without-its-name src/admin.c testStructureadmin $T_CUSTOM \
  '      (strlen(confirm) != name_len || memcmp(confirm, name, name_len) != 0)) {' \
  '      (0 && (strlen(confirm) != name_len || memcmp(confirm, name, name_len) != 0))) {'

# What a removal set aside is not said.
PLANT_NEED_TEXT='Placements set aside'
plant set-aside-not-said src/admin.c testStructureadmin $T_CARRY \
  '    if (moved != NULL && moved_len > 0) {' \
  '    if (0 && moved != NULL && moved_len > 0) {'

# The core's refusal is not shown.
PLANT_NEED_TEXT='A region is used once in a structure.'
plant refusal-sentence-hidden src/admin.c testStructureadmin $T_ACTIONS \
  '  } else if (notice == SN_REFUSED || notice == SN_STALE) {
    if (said != NULL && said_n > 0) {' \
  '  } else if (notice == SN_REFUSED || notice == SN_STALE) {
    if (0 && said != NULL && said_n > 0) {'

# A new template is made by a form with no token.
PLANT_NEED_TEXT='out of date'
plant new-template-without-a-token src/admin.c testStructureadmin $T_NEW \
  '  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post, post_n,
          "structure-new", self, NULL) ||' \
  '  if ((0 && !asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post, post_n,
          "structure-new", self, NULL)) ||'

# A name that is not one is not refused by the page.
PLANT_NEED_TEXT='is not one that exists'
plant bad-template-name-not-refused-by-the-page src/admin.c testStructureadmin $T_NEW \
  '  if (!gdefcore_structure_name_ok(name, strlen(name)) || find_by_name(catalog, kTemplateType, name, strlen(name), id) == 0) {' \
  '  if ((0 && !gdefcore_structure_name_ok(name, strlen(name))) || find_by_name(catalog, kTemplateType, name, strlen(name), id) == 0) {'

plant_finish
