#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the settings pages turns its test red, by
# name (site-admin story 8).
#
# The gate is SettingsAdmin.* (the kernel's page and a module's, the form the
# core generates, and a post through the real server). Each plant is an exact
# text edit to a scratch copy of this library; nothing is planted in the
# library. The control comes first: the unpatched copy must pass, or a red
# result below would be a broken build, not a caught defect.
#
# Usage: check-settingsadmin.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-settingsadmin
PLANT_RUN_FILTER='*'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-settingsadmin: control passes (SettingsAdmin on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-settingsadmin: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-settingsadmin: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testSettingsadmin' '*'

# The settings route names no permission: its page is for every administrator
# who reaches the area.
PLANT_NEED_TEXT='refused.body'
plant settings-route-without-permission src/nav.c testSettingsadmin SettingsAdmin.TheSettingsPagesAreThereOnlyForWhoMayFollowTheirRoute \
  '#define PERM_SETTINGS GDEFCORE_SETTINGS_EDIT' \
  '#define PERM_SETTINGS NULL'

# The pages of a module are not in the table: it cannot place a page.
PLANT_NEED_TEXT='shop'
plant module-routes-not-added src/nav.c testSettingsadmin SettingsAdmin.TheSettingsPagesAreThereOnlyForWhoMayFollowTheirRoute \
  '    if (host != NULL && tenant != NULL) {' \
  '    if (0 && host != NULL && tenant != NULL) {'

# A group nothing placed in the admin menu is served all the same.
PLANT_NEED_TEXT='extras.status'
plant unrouted-group-served src/admin.c testSettingsadmin SettingsAdmin.TheSettingsPagesAreThereOnlyForWhoMayFollowTheirRoute \
  '      !settings_routed(routes, request, path, group[0] == '"'"'\0'"'"' ? group : NULL,
          sizeof group) ||' \
  '      (settings_routed(routes, request, path, group[0] == '"'"'\0'"'"' ? group : NULL,
           sizeof group), 0) ||'

# A form is read whether or not the server signed it: a token with its tag
# damaged is taken.
PLANT_NEED_TEXT='forged.body'
plant token-signature-not-checked src/admin.c testSettingsadmin SettingsAdmin.AWriteNeedsTheSignedFormAndTheRightOne \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 13' \
  '      ((void)keyring, (void)root, (void)root_len, (void)session, (void)session_len, 0) ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 13'

# A token made for another form of the same session is taken for this one.
PLANT_NEED_TEXT='wrong.body'
plant token-purpose-not-checked src/admin.c testSettingsadmin SettingsAdmin.AWriteNeedsTheSignedFormAndTheRightOne \
  '      form_len != 13 || memcmp(form_id, "settings-save", 13) != 0) {' \
  '      0) {'

# The version the form was drawn at is not passed on, so an old form saves.
PLANT_NEED_TEXT='changed since'
plant version-not-sent src/admin.c testSettingsadmin SettingsAdmin.AFormDrawnBeforeAnotherSaveIsStale \
  '      version[0] != '"'"'\0'"'"' ? version : NULL, changes, count, &written, &message,' \
  '      NULL, changes, count, &written, &message,'

# A secret left blank is read as an empty value, and clears the stored one.
PLANT_NEED_TEXT='Nothing changed'
plant blank-secret-clears src/admin.c testSettingsadmin SettingsAdmin.AModulePlacesItsPageAndTabsAndALocalSecretIsNeverShownAgain \
  '      if (s.secret && value_n == 0) {' \
  '      if (0) {'

plant_finish
