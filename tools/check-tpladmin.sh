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

MAKE_CMD="${1:?usage: check-tpladmin.sh <make> <build-dir> [make args]}"
BUILD_DIR="${2:?usage: check-tpladmin.sh <make> <build-dir> [make args]}"
shift 2
MARGS="-j${GHOTI_CHECK_JOBS:-$(n=$(nproc 2>/dev/null || echo 4); [ "$n" -gt 4 ] && n=4; echo "$n")} $*"

unset MAKEFLAGS MFLAGS

ROOT=$(pwd)
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) EXE_EXT=.exe ;; *) EXE_EXT= ;; esac
SCRATCH="$ROOT/build/plants"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

fail=0

copy_tree() {
  mkdir -p "$1"
  for d in $(ls -A "$ROOT"); do
    case "$d" in build | .git | docs) continue ;; esac
    cp -a "$ROOT/$d" "$1/"
  done
}

run_tests() { # run_tests <dir> <log> <exe>...
  dir=$1 log=$2
  shift 2
  exes=
  for e in "$@"; do exes="$exes $BUILD_DIR/apps/$e$EXE_EXT"; done
  ( cd "$dir" && "$MAKE_CMD" --no-print-directory test TEST_GATES= \
      TEST_EXECUTABLES="$exes" $MARGS ) > "$log" 2>&1
}

copy_tree "$SCRATCH/control"
if ! run_tests "$SCRATCH/control" "$SCRATCH/control.log" testTpladmin testEdge; then
  printf 'check-tpladmin: the CONTROL (unpatched) copy fails, so no result below\n' >&2
  printf 'would mean anything. Its log:\n' >&2
  grep -v '^g++\|^cc \|^gcc ' "$SCRATCH/control.log" | tail -40 >&2
  exit 1
fi
printf 'check-tpladmin: control passes (TplAdmin on the unpatched copy)\n'

# begin <name>: a fresh copy to plant in. Sets DIR.
begin() {
  NAME=$1
  DIR="$SCRATCH/$NAME"
  cp -a "$SCRATCH/control" "$DIR"
  EDITED=0
  BAD=0
}

# edit <file> <old> <new>: replace the one occurrence.
edit() {
  if [ "$BAD" -ne 0 ]; then return; fi
  python3 - "$DIR/$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1:4]
text = open(path).read()
if text.count(old) != 1:
    sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
    sys.exit(3)
open(path, "w").write(text.replace(old, new, 1))
PY
  if [ $? -ne 0 ]; then
    printf 'check-tpladmin: %s: THE EDIT DID NOT APPLY (stale anchor), nothing was tested\n' "$NAME" >&2
    BAD=1
    fail=1
    return
  fi
  if cmp -s "$ROOT/$1" "$DIR/$1"; then
    printf 'check-tpladmin: %s: the edited %s equals the original\n' "$NAME" "$1" >&2
    BAD=1
    fail=1
    return
  fi
  EDITED=$((EDITED + 1))
}

# caught <exe> <test> <text>: build, run, and require <test> to fail and the
# log to carry <text>.
caught() {
  exe=$1 test=$2 text=$3
  if [ "$BAD" -ne 0 ] || [ "$EDITED" -eq 0 ]; then
    return
  fi
  # Objects are kept, so only what the edit touched recompiles; the programs
  # and libraries are removed so that every one is linked again.
  rm -f "$DIR/$BUILD_DIR"/apps/test* "$DIR/$BUILD_DIR"/apps/*.a "$DIR/$BUILD_DIR"/apps/*.so*
  if run_tests "$DIR" "$DIR.log" "$exe"; then
    printf 'check-tpladmin: %s: %s PASSED with the defect planted\n' "$NAME" "$exe" >&2
    fail=1
  elif grep -q "^\[  FAILED  \] $test\b" "$DIR.log" && grep -q -- "$text" "$DIR.log"; then
    printf 'check-tpladmin: %s: planted defect turned %s red (%s)\n' "$NAME" "$test" "$text"
  else
    printf 'check-tpladmin: %s: red, but not %s showing "%s" - read %s\n' \
      "$NAME" "$test" "$text" "$DIR.log" >&2
    grep -v '^g++\|^cc \|^gcc ' "$DIR.log" | tail -25 >&2
    fail=1
  fi
  rm -rf "$DIR"
}

# The preview is rendered under the live overlay, so it is offered to the
# shared cache that every visitor is served from.
begin preview-shared
edit src/admin.c '    static const char kOverlay[] = "preview";' \
  '    static const char kOverlay[] = "live";'
caught testTpladmin TplAdmin.PreviewRendersSampleDataAndNothingIsWrittenOrShared \
  'offered to the shared cache'

# A template's body is shown to a principal who may not edit its kind: the
# permission is not asked when a template is opened.
begin body-without-permission
edit src/admin.c \
'  if (field_copy(catalog, id, "kind", kind, kind_cap) != 0 || kind[0] == '"'"'\0'"'"' ||
      !gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    return 1;
  }' \
'  if (field_copy(catalog, id, "kind", kind, kind_cap) != 0 || kind[0] == '"'"'\0'"'"' ||
      (0 && !gdefcore_template_may_edit(catalog, request, kind, strlen(kind)))) {
    return 1;
  }'
caught testTpladmin TplAdmin.AnotherKindsTemplateIsLikeAMissingOne 'missing.body'

# The save no longer asks for the template's name to be typed.
begin confirmation-not-required
edit src/admin.c \
'    if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
        confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
          N_WRONG_CONFIRM, kind, body, body_n, NULL, 0, NULL, 0, 0, out);' \
'    if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 && 0) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
          N_WRONG_CONFIRM, kind, body, body_n, NULL, 0, NULL, 0, 0, out);'
caught testTpladmin TplAdmin.SaveNeedsTheTypedName 'name typed does not match'

# The revert no longer asks for it either.
begin revert-confirmation-not-required
edit src/admin.c \
'  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
    notice = N_WRONG_CONFIRM;' \
'  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 && 0) {
    notice = N_WRONG_CONFIRM;'
caught testTpladmin TplAdmin.HistoryListsRevisionsAndRevertRestoresTheOldBody \
  'name typed does not match'

# The list names a kind the principal may not edit, and its templates.
begin list-names-unpermitted-kind
edit src/admin.c \
'        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        !gdefcore_template_may_edit(catalog, request, name, strlen(name))) {
      continue;
    }' \
'        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        (0 && !gdefcore_template_may_edit(catalog, request, name, strlen(name)))) {
      continue;
    }'
caught testTpladmin TplAdmin.ListShowsOnlyKindsThePrincipalMayEdit 'Page layout'

# A stale form is not noticed: the template is overwritten.
begin stale-form-ignored
edit src/admin.c '  if (!base_matches(catalog, id, base)) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_CONFLICT,' \
'  if (0 && !base_matches(catalog, id, base)) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_CONFLICT,'
caught testTpladmin TplAdmin.AStaleFormIsAConflictAndNothingIsOverwritten \
  'this template changed while you were editing it'

# The stored body is written into the textarea as it is.
begin textarea-unescaped
edit src/admin.c '      page_esc(&page, body, body_len) != 0 ||
      page_str(&page, "</textarea></label></p><p><label>To save' \
'      page_add(&page, body, body_len) != 0 ||
      page_str(&page, "</textarea></label></p><p><label>To save'
caught testTpladmin TplAdmin.AHostileStoredBodyCannotBreakTheEditPage 'closes'

# What a redirect carries is shown to any session.
begin flash-not-session-bound
edit src/admin.c '        memcmp(f->session, sdig, sizeof sdig) != 0 ||
' ''
caught testTpladmin TplAdmin.WhatARedirectCarriesBelongsToOneSession 'DRAFT-SECRET'

# A kind the principal may not edit is accepted from a posted form.
begin kind-not-checked
edit src/admin.c '  if (!gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_KIND,' \
'  if (0 && !gdefcore_template_may_edit(catalog, request, kind, strlen(kind))) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_KIND,'
caught testTpladmin TplAdmin.MovingToAnotherKindNeedsBothKindsAndTheNewContract \
  'choose one of the kinds listed'

# The check token is taken for the save token.
begin token-purpose-ignored
edit src/admin.c \
'      strlen(act == A_SAVE ? "tpl-save" : "tpl-check") != form_len ||
      memcmp(form_id, act == A_SAVE ? "tpl-save" : "tpl-check", form_len) != 0) {' \
'      0) {'
caught testTpladmin TplAdmin.ASignedFormIsRequired 'out of date'

# The editor is shown the body in force and not the latest draft (what the
# editor opens is the draft, else the current revision; story S6b).
begin editor-shows-the-body-in-force
edit src/admin.c '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_DRAFT,' \
'  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,'
caught testTpladmin TplAdmin.TheEditorOpensTheLatestDraftAndNotOnlyTheBodyInForce \
  'LATEST-DRAFT-BODY'

# The pages name entities by the published view, so an unpublished template
# cannot be opened by the person who would republish it.
begin admin-names-by-the-published-view
edit src/admin.c '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);' \
'  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);'
caught testTpladmin TplAdmin.AnUnpublishedTemplateIsStillFoundAndOpenedByItsName \
  '<textarea name'

# The same plant, seen from the post list: a post that is only a draft has no
# title in the editor's undo page.
begin admin-names-posts-by-the-published-view
edit src/admin.c '  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);' \
'  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);'
caught testEdge Edge.AdminNamesAnUnpublishedPostAndOffersNoUndoOfADraft 'Post Held Back'

if [ "$fail" -ne 0 ]; then
  exit 1
fi
printf 'check-tpladmin: every planted defect turned its named test red\n'
