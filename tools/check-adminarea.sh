#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the permission gate of the admin area turns
# its test red, by name (site-admin story 13).
#
# The gate is AdminArea.* (every row of the story's matrix, through the real
# server): a session says who someone is and each page's own route check says
# what they may do, and the role named admin is asked nothing. Each plant is an
# exact text edit to a scratch copy of this library; nothing is planted in the
# library. The copy is built, the test program is run, and the named test must
# be among the failures, with the named text in its message. A run that fails
# some other way (a crash, a different test, a build error) does not count, and
# an edit that does not apply exactly once, or leaves the file as it was, fails
# the script before anything is built. A plant that leaves a variable unused is
# written to keep it used, because the copy is built with -Werror.
#
# The control comes first: the unpatched copy must pass.
#
# The first administrator's grants are planted in the core's gate (the seed) and
# in check-defiant (the program that never seeds), not here.
#
# Usage: check-adminarea.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-adminarea
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-adminarea: control passes (AdminArea on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-adminarea: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-adminarea: every planted defect turned its named test red\n'
}

# Gate 1: nothing names a second, separate admin cookie any more. The words are
# put together here so that this script does not carry them itself.
needle_a='__Host-'
needle_b='admin'
leftover() { # dir
  (cd "$1" && grep -rIlE -e "${needle_a}${needle_b}" -e '[Aa]dmin cookie' src include tests app README.md 2>/dev/null)
}
root=$(cd "$(dirname "$0")/.." && pwd)
found=$(leftover "$root")
if [ -n "$found" ]; then
  printf 'check-adminarea: a second admin cookie is still named in: %s\n' "$found" >&2
  exit 1
fi
seen_dir=$(mktemp -d)
mkdir -p "$seen_dir/src"
printf '/* %s%s */\n' "$needle_a" "$needle_b" > "$seen_dir/src/x.c"
if [ -z "$(leftover "$seen_dir")" ]; then
  rm -rf "$seen_dir"
  printf 'check-adminarea: the search for the second admin cookie did not find a planted name\n' >&2
  exit 1
fi
rm -rf "$seen_dir"
printf 'check-adminarea: no second admin cookie is named, and the search was seen to find one\n'

plant_setup "$@"
plant_control 'testAdminarea' '*'

# The role-name check is back: the session is "allowed" only for who holds the
# role named admin, and a person granted one page's permission is shut out.
PLANT_NEED_TEXT='ONE-GRANT'
plant role-name-check-reintroduced src/admin.c testAdminarea AdminArea.OneGrantReachesThatPageAndNothingElse \
  '    ok = 1;
  }
  wipe(raw, sizeof raw);
  wipe(encoded, sizeof encoded);
  return ok;' \
  '    size_t plant_n = 0;
    const char * plant_id = gdefcore_request_principal(request, &plant_n);
    char ** plant_roles = NULL;
    size_t plant_count = 0;
    size_t plant_i;
    if (plant_id != NULL &&
        gdefcore_access_principal_roles(catalog, plant_id, plant_n, &plant_roles,
            &plant_count) == GDEFCORE_OK) {
      for (plant_i = 0; plant_i < plant_count; plant_i++) {
        if (strcmp(plant_roles[plant_i], "admin") == 0) {
          ok = 1;
        }
      }
      gdefcore_access_free(catalog, plant_roles, plant_count);
    }
  }
  wipe(raw, sizeof raw);
  wipe(encoded, sizeof encoded);
  return ok;'

# The undo page names no permission: every admin session reaches it.
PLANT_NEED_TEXT='OPENS:'
plant page-left-without-a-permission src/nav.c testAdminarea AdminArea.EveryGrantOpensTheOnePageItGovernsAndNoOther \
  '#define PERM_UNDO GDEFCORE_ADMIN_UNDO' \
  '#define PERM_UNDO NULL'

# The matrix page asks the area's permission: whoever may use the area reads
# what is granted.
PLANT_NEED_TEXT='OPENS:'
plant grant-opens-a-page-it-does-not-govern src/nav.c testAdminarea AdminArea.EveryGrantOpensTheOnePageItGovernsAndNoOther \
  '#define PERM_ACCESS GDEFCORE_ACCESS_READ' \
  '#define PERM_ACCESS GDEFCORE_ADMIN_USE'

# The More screen is served to every admin session, whatever the route says.
PLANT_NEED_TEXT='OPENS:'
plant more-screen-ungated src/admin.c testAdminarea AdminArea.EveryGrantOpensTheOnePageItGovernsAndNoOther \
  '    rc = is_get && gdefweb_nav_follows(routes, request, path, "core.admin-more")
             ? more_page(alloc, catalog, request, routes, out)' \
  '    rc = is_get
             ? more_page(alloc, catalog, request, routes, out)'

# More lists every entry the menu has, as the editor's list does, and not the
# ones this viewer may follow.
PLANT_NEED_TEXT='MORE:'
plant more-lists-a-page-the-viewer-may-not-reach src/admin.c testAdminarea AdminArea.MoreListsOnlyWhatTheViewerMayReach \
  '    if (gdefcore_menu_tree(catalog, routes, request, match, "admin", 5, &menu) !=
        GDEFCORE_OK) {' \
  '    if (gdefcore_menu_entries(catalog, routes, request, match, &menu) !=
        GDEFCORE_OK) {'

# A sign-in leads to the admin home whoever signs in.
PLANT_NEED_TEXT='ONE-GRANT'
plant sign-in-lands-on-the-home src/server.c testAdminarea AdminArea.OneGrantReachesThatPageAndNothingElse \
  'landing[0] != '"'"'\0'"'"' ? landing : "/");' \
  'landing[0] != '"'"'\0'"'"' ? "/admin" : "/");'

# The refusal says what would have allowed it.
PLANT_NEED_TEXT='NAMES:'
plant refusal-names-the-permission src/admin.c testAdminarea AdminArea.UndoOffersAndUndoesOnlyWhatTheViewerHoldsThePermissionOf \
  '"That cannot be undone here."' \
  '"That cannot be undone here: it needs core.role.revert."'

# The undo page offers the roles to who may not undo them.
PLANT_NEED_TEXT='UNDO:'
plant undo-offers-without-the-permission src/admin.c testAdminarea AdminArea.UndoOffersAndUndoesOnlyWhatTheViewerHoldsThePermissionOf \
  '  for (i = 0; may_roles && i < count; i++) {' \
  '  for (i = 0; (may_roles || 1) && i < count; i++) {'

# The undo of a role is made for who may not undo roles.
PLANT_NEED_TEXT='UNDO:'
plant undo-writes-without-the-permission src/admin.c testAdminarea AdminArea.UndoOffersAndUndoesOnlyWhatTheViewerHoldsThePermissionOf \
  '    allowed = holds(catalog, request, GDEFCORE_ROLE_REVERT);' \
  '    allowed = 1;'

# The front page is saved without the request: the core is not asked who may
# edit the display, and a grant of the rules alone changes it.
PLANT_NEED_TEXT='FRONT:'
plant front-saved-without-the-request src/admin.c testAdminarea AdminArea.TheFrontPageWriteAsksTheLayoutDecisionOfTheDisplay \
  '  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, &value, 1);' \
  '  rc = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, &value, 1);'

# Gate 2: a grant made after sign-in opens its page with no new sign-in. The
# authentication reads a second cookie again (put together so as not to name it).
PLANT_NEED_TEXT='LATE:'
plant second-cookie-required-again src/admin.c testAdminarea AdminArea.AGrantMadeAfterSignInOpensItsPageOnTheNextRequestWithNoNewSignIn \
  '  if (cookie_named(cookie, "__Host-session", encoded, sizeof encoded) != 0) {
    return 0;
  }' \
  '  if (cookie_named(cookie, "__Host-" "admin", encoded, sizeof encoded) != 0) {
    return 0;
  }'

# The session cookie is withheld from a person who has no admin page to go to at
# the moment they sign in, which is what the second cookie did.
PLANT_NEED_TEXT='LATE:'
plant session-withheld-from-a-person-with-no-admin-page src/server.c testAdminarea AdminArea.AGrantMadeAfterSignInOpensItsPageOnTheNextRequestWithNoNewSignIn \
  '      landing[0] = '"'"'\0'"'"';
    }
    gdefcore_text_free(catalog, token);' \
  '      landing[0] = '"'"'\0'"'"';
      cookies.values[0] = NULL;
    }
    gdefcore_text_free(catalog, token);'

# Gate 3: every mutating admin endpoint refuses a request with a valid session
# and no, or a damaged, form token. Each plant removes one signature check.
PLANT_NEED_TEXT='TOKEN:'
plant menu-undo-written-without-the-signature-check src/admin.c testAdminarea \
  AdminArea.UndoingAMenuEditNeedsTheSignedFormOfThisSession \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 9 || memcmp(form_id, "menu-undo", 9) != 0) {' \
  '      ((void)keyring, (void)root, (void)root_len, (void)session, (void)session_len, 0) ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 9 || memcmp(form_id, "menu-undo", 9) != 0) {'

# The post form has no plant here: its own check above the core is a second
# layer, because gdefcore_form_resign() verifies the token's signature again
# before anything is written (the core's gate plants that one), so removing the
# web check alone changes nothing a request can see. The test of the post form
# (WritingAPostNeedsTheSignedFormOfThisSession) still runs in every control.

PLANT_NEED_TEXT='TOKEN:'
plant front-saved-without-the-signature-check src/admin.c testAdminarea \
  AdminArea.SavingTheFrontPageNeedsTheSignedFormOfThisSession \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, stale);' \
  '      ((void)keyring, (void)root, (void)root_len, (void)session, (void)session_len, 0) ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, stale);'

PLANT_NEED_TEXT='TOKEN:'
plant undo-written-without-the-signature-check src/admin.c testAdminarea \
  AdminArea.UndoingAChangeNeedsTheSignedFormOfThisSession \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "undo", 4) != 0 ||' \
  '      ((void)keyring, (void)root, (void)root_len, (void)session, (void)session_len, 0) ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "undo", 4) != 0 ||'

# The stale-object plant. A plant's copy starts from the finished build, so the
# edit has to be compiled into it; this one is made not to be (the objects that
# depend on the edit are left looking up to date), and the gate has to refuse
# it as a STALE OBJECT.
PLANT_NEED_TEXT=
plant_stale stale-object src/nav.c testAdminarea AdminArea.EveryGrantOpensTheOnePageItGovernsAndNoOther \
  '#define PERM_UNDO GDEFCORE_ADMIN_UNDO' \
  '#define PERM_UNDO NULL'

plant_finish
