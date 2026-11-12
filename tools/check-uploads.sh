#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the upload page turns its test red, by name
# (web content admin story 6).
#
# The gate is Uploads.* (a file posted as multipart/form-data through the real
# server, a directory store on disk). Each plant is an exact text edit to a
# scratch copy of this library; nothing is planted in the library. The control
# comes first: the unpatched copy must pass, or a red result below would be a
# broken build, not a caught defect. A plant of two edits is a defect two
# guards each stop: both are removed, so that the test sees the defect and not
# the second guard.
#
# Usage: check-uploads.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-uploads
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-uploads: control passes (Uploads on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-uploads: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-uploads: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testUploads' '*'

M=src/multipart.c
F=src/files.c
B=src/blobdir.c
S=src/server.c
T_BAD='Uploads.ABodyThatIsNotAWellFormedMultipartIsReadAsNothing'
T_POST='Uploads.AFileIsPostedByAFormListedAndGivenBackToItsUploaderAlone'
T_ACTIVE='Uploads.AnActiveTypeIsNeverServedAsItselfFromTheSitesOwnAddress'
T_TOKEN='Uploads.AFormWithNoTokenOrAnotherPersonsTokenStoresNothing'
T_PERM='Uploads.APersonWhoMayNotUploadIsToldWhatAPageThatIsNotThereIsTold'
T_OFF='Uploads.UploadsAreOffUntilTheyAreBoundAndStaySoWhenUnbound'
T_DIR='Uploads.TheDirectoryStoreKeepsATenantsBytesApartAndNeverLeavesItsRoot'
T_CAP='Uploads.OnlyAFilePostedToItsOwnPageMayCarryMoreThanAFormDoes'

PLANT_NEED_TEXT='a part with no name'
plant part-without-a-name-allowed $M testUploads "$T_BAD" \
  '  if (!have_disp || part->name == NULL || part->name_len == 0 ||' \
  '  if (!have_disp ||'

BS_OLD="        if (c < 0x20 || c == 0x7f || c == '\\\\') {"
BS_NEW='        if (c < 0x20 || c == 0x7f) {'
PLANT_NEED_TEXT='a backslash in a name'
plant backslash-in-a-name-allowed $M testUploads "$T_BAD" "$BS_OLD" "$BS_NEW"

PLANT_NEED_TEXT='a file name over its cap'
plant file-name-cap-dropped $M testUploads "$T_BAD" \
  '      (part->filename != NULL && part->filename_len > GDEFWEB_MP_MAX_FILENAME)) {' \
  '      0) {'

PLANT_NEED_TEXT='two files'
plant a-name-two-parts-carry-is-read $M testUploads "$T_BAD" \
  '      if (found != NULL) {
        return NULL;
      }
      found = &mp->parts[i];' \
  '      if (found == NULL) {
        found = &mp->parts[i];
      }'

PLANT_NEED_TEXT='data after the closing boundary'
plant data-after-the-closing-boundary-allowed $M testUploads "$T_BAD" \
  '      if (p == end || (p + 2 == end && p[0] == '"'"'\r'"'"' && p[1] == '"'"'\n'"'"')) {' \
  '      if (1) {'

PLANT_NEED_TEXT='no closing boundary'
plant unterminated-last-part-allowed $M testUploads "$T_BAD" \
  '    next = find(p, end, sep, sn);
    if (next == NULL) {
      break;
    }' \
  '    next = find(p, end, sep, sn);
    if (next == NULL) {
      part.data = (const uint8_t *)p;
      part.len = (size_t)(end - p);
      out->parts[out->count++] = part;
      return 0;
    }'

PLANT_NEED_TEXT='text before the first boundary'
plant text-before-the-first-boundary-allowed $M testUploads "$T_BAD" \
  '  if (len < dn || memcmp(body, delim, dn) != 0) {
    return -1;
  }
  p = body + dn;' \
  '  {
    const char * first = len < dn ? NULL : find(body, end, delim, dn);
    if (first == NULL) {
      return -1;
    }
    p = first + dn;
  }'

PLANT_NEED_TEXT='a head over its cap'
plant part-head-cap-dropped $M testUploads "$T_BAD" \
  '    if (hend == NULL || (size_t)(hend - p) > GDEFWEB_MP_MAX_HEAD) {' \
  '    if (hend == NULL) {'

PLANT_NEED_TEXT='a part that is not form-data'
plant part-that-is-not-form-data-allowed $M testUploads "$T_BAD" \
  '      if (have_disp || vlen < 10 || !ci_eq(val, 10, "form-data;")) {' \
  '      if (have_disp || vlen < 10) {'

# The token is genuine for another sign-in and made for another person: both
# checks are removed, so that her token is the whole of the defect.
plant token-not-bound-to-this-person $F testUploads "$T_TOKEN" \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session, session_len,
          (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||' \
  '      (0 && keyring != NULL && root != NULL && root_len > 0 && session != NULL && session_len > 0) ||' \
  '      memcmp(target, who, GDEFCORE_PUBLIC_ID_LEN) != 0) {' \
  '      0) {'

plant upload-posted-while-off $F testUploads "$T_OFF" \
  '  if (!gdefweb_files_enabled() || ctype == NULL) {' \
  '  if (ctype == NULL) {'

plant route-check-skipped $F testUploads "$T_PERM" \
  '  if (!gdefweb_nav_follows(routes, request, path, "core.admin-files")) {' \
  '  if (routes == NULL && path == NULL && 0) {'

plant file-name-not-escaped-in-the-list $F testUploads "$T_POST" \
  '      if (page_str(&page, "<tr><td>") != 0 || page_esc(&page, name, strlen(name)) != 0 ||' \
  '      if (page_str(&page, "<tr><td>") != 0 || page_str(&page, name) != 0 ||'

plant active-type-served-as-itself $F testUploads "$T_ACTIVE" \
  '  snprintf(out->type_buf, sizeof out->type_buf, "%s", active ? "application/octet-stream" : media);' \
  '  snprintf(out->type_buf, sizeof out->type_buf, "%s", media);'

plant everything-served-inline $F testUploads "$T_ACTIVE" \
  '  disposition_of(gdefcore_upload_file_name(file), !active && inline_type(media),' \
  '  disposition_of(gdefcore_upload_file_name(file), 1,'

plant served-file-has-no-sandbox $F testUploads "$T_POST" \
  '  out->csp = "default-src '"'"'none'"'"'; sandbox";' \
  '  out->csp = NULL;'

plant file-name-header-not-made-safe $F testUploads "$T_POST" \
  "                            : '_');" \
  '                            : c);'

plant served-file-may-be-sniffed $S testUploads "$T_POST" \
  '      pub.nosniff = 1;
      pub.disposition = page.disposition[0]' \
  '      pub.nosniff = 0;
      pub.disposition = page.disposition[0]'

plant file-body-cap-never-raised $S testUploads "$T_CAP" \
  '              gdefweb_files_body_cap() > body_cap) {' \
  '              0) {'

plant every-route-may-carry-a-file $S testUploads "$T_CAP" \
  '              head_path_len == sizeof "/admin/files" - 1 &&
              memcmp(head_path, "/admin/files", head_path_len) == 0 &&' \
  '              1 &&'

PLANT_NEED_TEXT='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
plant digest-not-hex-allowed $B testUploads "$T_DIR" \
  "    if (!((d[i] >= '0' && d[i] <= '9') || (d[i] >= 'a' && d[i] <= 'f'))) {" \
  '    if (0) {'

plant digest-length-not-checked $B testUploads "$T_DIR" \
  '  if (d == NULL || strlen(d) != DIGEST_LEN) {' \
  '  if (d == NULL || strlen(d) < 1) {'

plant tenant-not-in-the-path $B testUploads "$T_DIR" \
  '  n = snprintf(out, cap, "%s/%s/%.2s/%s", d->root, hex, digest, digest);' \
  '  n = snprintf(out, cap, "%s/%.0s/%.2s/%s", d->root, hex, digest, digest);'

plant removing-what-is-not-there-fails $B testUploads "$T_DIR" \
  '  return r == GCU_FILE_OK || r == GCU_FILE_ERR_NOT_FOUND ? 0 : 1;' \
  '  return r == GCU_FILE_OK ? 0 : 1;'

# The host's sweep runs although uploads are not bound, and says it did.
plant sweep-runs-unbound $F testUploads \
  'Uploads.AnEditorAttachesAFileFromTheContentFormAndTheReferenceKeepsItPastItsExpiry' \
  '  if (catalog == NULL || g_blobs == NULL || g_policy == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  rc = gdefcore_upload_sweep(' \
  '  if (catalog == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  if (g_blobs == NULL || g_policy == NULL) {
    return GDEFWEB_OK;
  }
  rc = gdefcore_upload_sweep('

plant_finish
