#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in how the server answers for the files a
# module ships, and in the policy it sends a public page, turns its test red,
# by name (site-admin layout editor, story 19).
#
# The gate is Assets.* (through the real server: type, bytes, caching, 304,
# separate owners, a disabled module, spelling, and the two policies). Each
# plant is an exact text edit to a scratch copy of this library; nothing is
# planted in the library. The control comes first: the unpatched copy must pass.
#
# Usage: check-assets.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-assets
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-assets: control passes (Assets on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-assets: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-assets: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testAssets' '*'

CACHE=Assets.TheHashAddressCachesForAYearAndTheBareOneRevalidates
NOTMOD=Assets.AMatchingEtagIsAnsweredNotModifiedWithNoBody
SHIPPED=Assets.AShippedFileIsServedWithItsDeclaredTypeAndExactBytes
POLICY=Assets.APublicPageMayLoadTheSitesFilesAndAnAdminPageKeepsItsOwnPolicy

# Every address is cached for a year, so an old link serves stale text.
PLANT_NEED_TEXT='no-cache'
plant always-immutable src/server.c testAssets $CACHE \
  '  extra.cache = hashed ? "public, max-age=31536000, immutable" : "no-cache";' \
  '  extra.cache = "public, max-age=31536000, immutable"; (void)hashed;'

# Any query at all is taken for the hash.
PLANT_NEED_TEXT='no-cache'
plant any-query-is-the-hash src/server.c testAssets $CACHE \
  '  hashed = query_has_hash(in->query, found.hash);' \
  '  hashed = in->query[0] != '"'"'\0'"'"';'

# No ETag is sent, so nothing can be answered 304.
PLANT_NEED_TEXT='ETag'
plant no-etag src/server.c testAssets $CACHE \
  '  extra.etag = etag;' \
  '  extra.etag = NULL;'

# If-None-Match is never read.
PLANT_NEED_TEXT='304'
plant never-not-modified src/server.c testAssets $NOTMOD \
  '  if (in->inm[0] != '"'"'\0'"'"' && etag_matches(in->inm, etag)) {' \
  '  if (0) {'

# Any If-None-Match is a match, so a stale copy is told it is current.
PLANT_NEED_TEXT='200'
plant any-tag-matches src/server.c testAssets $NOTMOD \
  '  if (in->inm[0] != '"'"'\0'"'"' && etag_matches(in->inm, etag)) {' \
  '  if (in->inm[0] != '"'"'\0'"'"') {'

# The type is not declared by the file's kind but guessed from its name.
PLANT_NEED_TEXT='Content-Type'
plant type-from-name src/server.c testAssets $SHIPPED \
  '  reply_full(server, sock, 200, "OK", found.bytes, found.bytes_len, NULL,
      found.media_type, in->method, in->path, client, NULL, &extra);' \
  '  reply_full(server, sock, 200, "OK", found.bytes, found.bytes_len, NULL,
      strstr(in->path, ".png") != NULL ? "image/png" : found.media_type,
      in->method, in->path, client, NULL, &extra);'

# The bytes are taken for text and cut at the first NUL.
PLANT_NEED_TEXT='pixel.png'
plant bytes-as-text src/server.c testAssets $SHIPPED \
  '  reply_full(server, sock, 200, "OK", found.bytes, found.bytes_len, NULL,' \
  '  reply_full(server, sock, 200, "OK", found.bytes,
      strlen((const char *)found.bytes), NULL,'

# The browser is left to guess a type.
PLANT_NEED_TEXT='nosniff'
plant no-nosniff src/server.c testAssets $SHIPPED \
  '  extra.nosniff = 1;' \
  '  extra.nosniff = 0;'

# A picture that can carry script is served with no sandbox.
PLANT_NEED_TEXT='sandbox'
plant svg-unsandboxed src/server.c testAssets $SHIPPED \
  '      ? "default-src '"'"'none'"'"'; sandbox"' \
  '      ? "default-src '"'"'none'"'"'"'

# A public page is sent the admin's policy, which no site file can load under.
PLANT_NEED_TEXT="script-src 'self'"
plant public-page-admin-policy src/server.c testAssets $POLICY \
  '      pub.csp = gdefweb_public_csp();' \
  '      pub.csp = gdefweb_admin_csp();'

# The public policy allows an inline source.
PLANT_NEED_TEXT='unsafe'
plant public-unsafe-inline src/admin.c testAssets $POLICY \
  '    "img-src '"'"'self'"'"'; font-src '"'"'self'"'"'; frame-ancestors '"'"'none'"'"'";' \
  '    "img-src '"'"'self'"'"' '"'"'unsafe-inline'"'"'; font-src '"'"'self'"'"'; frame-ancestors '"'"'none'"'"'";'

# The admin's policy is widened to the site's own scripts.
PLANT_NEED_TEXT="script-src 'self'"
plant admin-policy-widened src/admin.c testAssets $POLICY \
  '      "default-src '"'"'none'"'"'; script-src '"'"'%s'"'"'; style-src '"'"'self'"'"'; "' \
  '      "default-src '"'"'none'"'"'; script-src '"'"'self'"'"' '"'"'%s'"'"'; style-src '"'"'self'"'"'; "'

plant_finish
