#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in how the server draws a public page by the
# layout the route trie chooses turns its test red, by name (site-admin
# story 7).
#
# The gate is Layouts.* (the kit's pages, a module's selector and widget type,
# and a layout that names an unknown region, through the real server). Each
# plant is an exact text edit to a scratch copy of this library; nothing is
# planted in the library. The control comes first: the unpatched copy must pass.
#
# Usage: check-layouts.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-layouts
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-layouts: control passes (Layouts on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-layouts: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-layouts: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testLayouts' '*'

# The server draws the page with no trie of the modules enabled for the tenant:
# a module's selector is never asked.
PLANT_NEED_TEXT='selector_calls'
plant trie-not-built src/server.c testLayouts Layouts.AModulesSelectorAndWidgetChangeAPublicPageWithNoChangeToTheServer \
  '    if (routes != NULL && server->host != NULL) {' \
  '    if (0) {'

# The trie is built, and the modules'"'"' routes and contributors are not put in it.
PLANT_NEED_TEXT='selector_calls'
plant contributors-not-added src/server.c testLayouts Layouts.AModulesSelectorAndWidgetChangeAPublicPageWithNoChangeToTheServer \
  '      (void)gdefcore_module_add_routes(server->host, tenant, tenant_n, NULL,
          routes, &err, &err_n);' \
  '      (void)tenant;
      (void)tenant_n;
      (void)err_n;'

plant_finish
