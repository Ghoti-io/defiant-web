#!/bin/sh
#
# Show that each planted defect in the layout editor turns its test red, by name
# (layout editor story 24, web half).
#
# The gate is LayoutAdmin.* (through the real server: the list and who sees it, a
# layout drawn as regions in the template's order, every action as a signed form,
# a refused edit that says why, a stale form, a token bound to its layout, undo,
# and the spelling of a name). Each plant is an exact text edit to a scratch copy
# of this library; nothing is planted in the library. The control comes first:
# the unpatched copy must pass.
#
# Usage: check-layoutadmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-layoutadmin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-layoutadmin: control passes (LayoutAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-layoutadmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-layoutadmin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testLayoutadmin' '*'

T_LIST=LayoutAdmin.TheListIsTheLayoutsThePrincipalMayEditAndNobodyElseIsToldOfThem
T_DRAWN=LayoutAdmin.ALayoutIsDrawnAsItsRegionsInTheTemplatesOrderWithLabelsAndNoScript
T_ACTIONS=LayoutAdmin.EveryActionIsAFormThatWorksWithScriptsOffAndIsOneChange
T_REFUSED=LayoutAdmin.ARefusedEditSaysWhyAndShowsTheLayoutAsItIsNow
T_STALE=LayoutAdmin.AFormDrawnBeforeAnotherChangeIsStaleAndTheCurrentLayoutIsShown
T_TOKEN=LayoutAdmin.ATokenIsBoundToItsSignInItsLayoutAndItsPurpose
T_UNDO=LayoutAdmin.TheLastChangeIsUndoneWholeAndOnlyWhenThereIsOne
T_NARROW=LayoutAdmin.ANarrowGrantEditsOnlyTheLayoutsItNamesAndTheRestAreNotThere
T_NAMES=LayoutAdmin.ALayoutNameThatIsNotOneIsNotThere

# The page is shown to whoever is signed in, whatever the route's check says.
PLANT_NEED_TEXT='NotThere'
plant route-check-dropped src/admin.c testLayoutadmin $T_LIST \
  '    if (!gdefweb_nav_follows(routes, request, "/admin/layouts", "core.admin-layouts")) {' \
  '    if (0) {'

# A layout is opened for anyone who can sign in, not only for who may edit it.
PLANT_NEED_TEXT='NotThere'
plant layout-open-ignores-the-permission src/admin.c testLayoutadmin $T_NARROW \
  '  return ok ? 0 : 1;
}

/* A token for a form about layout @p id' \
  '  return 0;
}

/* A token for a form about layout @p id'

# The list shows every layout, not the ones the principal may edit.
PLANT_NEED_TEXT='>post</a>'
plant list-shows-every-layout src/admin.c testLayoutadmin $T_NARROW \
  '            sizeof GDEFCORE_LAYOUT_EDIT - 1, kLayoutType, sizeof kLayoutType - 1, ids[i], &ok) !=
            GDEFCORE_OK ||
        !ok) {' \
  '            sizeof GDEFCORE_LAYOUT_EDIT - 1, kLayoutType, sizeof kLayoutType - 1, ids[i], &ok) !=
            GDEFCORE_OK ||
        0) {'

# A region is shown by its name, not by its descriptor.
PLANT_NEED_TEXT='main_at'
plant region-label-not-the-descriptor src/admin.c testLayoutadmin $T_DRAWN \
  '    if (page_str(&page, "<h2>") != 0 || page_esc(&page, info.label, info.label_len) != 0 ||' \
  '    if (page_str(&page, "<h2>") != 0 || page_esc(&page, v.regions[r], strlen(v.regions[r])) != 0 ||'

# A form is signed but the version it carries is not the layout's.
PLANT_NEED_TEXT='This layout changed'
plant version-not-checked-by-the-form src/admin.c testLayoutadmin $T_STALE \
  '    edit.version = version;
    edit.version_len = strlen(version);' \
  '    edit.version = version;
    edit.version_len = strlen(version);
    { GDEFCORE_Layout_View now; memset(&now, 0, sizeof now); now.size = sizeof now;
      if (gdefcore_layout_view(catalog, name, name_len, &now) == GDEFCORE_OK) {
        memcpy(version, now.version, sizeof now.version);
        gdefcore_layout_view_free(&now);
      } }'

# A token for one layout is taken for another's.
PLANT_NEED_TEXT='out of date'
plant token-not-bound-to-its-layout src/admin.c testLayoutadmin $T_TOKEN \
  '      memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) == 0 &&
      got_len == strlen(form_id)' \
  '      (memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) == 0 || 1) &&
      got_len == strlen(form_id)'

# A form signed before another change writes over it.
PLANT_NEED_TEXT='out of date'
plant stale-token-accepted src/admin.c testLayoutadmin $T_STALE \
  '      !base_matches(catalog, id, base)) {
    return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
        id, name, LN_BAD_FORM, NULL, 0, out);' \
  '      (0 && !base_matches(catalog, id, base))) {
    return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
        id, name, LN_BAD_FORM, NULL, 0, out);'

# The core's refusal is not shown: the page says only that something went wrong.
PLANT_NEED_TEXT='That region is not in this template.'
plant refusal-sentence-hidden src/admin.c testLayoutadmin $T_REFUSED \
  '  if (notice == LN_REFUSED || notice == LN_STALE) {
    if (said != NULL && said_n > 0) {' \
  '  if (notice == LN_REFUSED || notice == LN_STALE) {
    if (0 && said != NULL && said_n > 0) {'

# A placement's title is written into the page as it is.
PLANT_NEED_TEXT='Latest'
plant title-not-escaped src/admin.c testLayoutadmin $T_ACTIONS \
  '      (page_str(page, ", titled “") != 0 || page_esc(page, p->title, strlen(p->title)) != 0 ||' \
  '      (page_str(page, ", titled “") != 0 || page_add(page, p->title, strlen(p->title)) != 0 ||'

# A name is looked up whatever it is spelled like.
PLANT_NEED_TEXT='NotThere'
plant encoded-name-looked-up src/admin.c testLayoutadmin $T_NAMES \
  '      !gdefcore_siteasset_name_ok(name, name_len) || name_len != strlen(rest + 1)) {' \
  '      0) {'

# The undo is the same form as an edit, so an edit's token undoes.
PLANT_NEED_TEXT='an edit'"'"'s token undid'
plant undo-takes-an-edit-token src/admin.c testLayoutadmin $T_UNDO \
  '          post_n, is_undo ? "layout-undo" : "layout-edit", id, &base) ||' \
  '          post_n, "layout-edit", id, &base) ||'

plant_finish
