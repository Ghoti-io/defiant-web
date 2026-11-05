#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# Show that each planted defect in the session cookie policy and in sign-out
# turns its test red, by name (site-admin story 15).
#
# The gates are Edge.CookiePolicy*, Edge.PlainHttpCookieIsRefusedOffLoopback and
# Edge.LogoutNeedsTheSignedTokenOfTheSessionsOwnPage and Edge.SigningOut* (site-admin story 16), through the real server.
# Each plant is an exact text edit to a scratch copy of this library; nothing is
# planted in the library. The copy is built, the test program is run, and the
# named test must be among the failures, with the named text in its message. A
# run that fails some other way (a crash, a different test, a build error) does
# not count, and an edit that does not apply exactly once, or leaves the file as
# it was, fails the script before anything is built.
#
# The control comes first: the unpatched copy must pass.
#
# Usage: check-cookies.sh <make> <build-dir> [make args]

set -u

. "$(dirname "$0")/plant-load.sh"
PLANT_TAG=check-cookies
PLANT_RUN_FILTER='Edge.CookiePolicy*:Edge.PlainHttp*:Edge.Logout*:Edge.SigningOut*:Edge.Login:Edge.Cookie:Edge.AdminCookies'
PLANT_TREE_DIRS='src include tests tools pkgconfig documentation examples app'

plant_hook_control() { # exe filter
  plant_say "check-cookies: control passes (the cookie policy and sign-out on the unpatched copy)"
}

plant_hook_caught() { # name expect log
  printf 'check-cookies: %s: planted defect turned %s red (%s)\n' "$1" "$2" "$PLANT_NEED_TEXT"
}

plant_hook_done() {
  printf 'check-cookies: every planted defect turned its named test red\n'
}

plant_setup "$@"
plant_control 'testEdge' "$PLANT_RUN_FILTER"

COOKIES=Edge.CookiePolicyShapesTheCookieAndItsClearing
LOOPBACK=Edge.PlainHttpCookieIsRefusedOffLoopback
LOGOUT=Edge.LogoutNeedsTheSignedTokenOfTheSessionsOwnPage
REVOKES=Edge.SigningOutEndsThisSessionAndNoOther
DEAD=Edge.SigningOutWithoutALiveSessionStillClearsTheCookie
FAILS=Edge.SigningOutThatCannotEndTheSessionIsNotASignOut

# A server created with no options, or with the default options, is Lax.
PLANT_NEED_TEXT='COOKIE:'
plant default-weakened-to-lax src/server.c testEdge $COOKIES \
  '  out->same_site = GDEFWEB_COOKIE_SAME_SITE_STRICT;' \
  '  out->same_site = GDEFWEB_COOKIE_SAME_SITE_LAX;'

# A server created with no options is not Secure: it drops the prefix too.
PLANT_NEED_TEXT='COOKIE:'
plant default-weakened-to-plain-http src/server.c testEdge $COOKIES \
  '  out->transport = GDEFWEB_COOKIE_TRANSPORT_SECURE;' \
  '  out->transport = GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK;'

# The plain-HTTP cookie is accepted whatever the listener is.
PLANT_NEED_TEXT='LOOPBACK:'
plant plain-http-accepted-off-loopback src/server.c testEdge $LOOPBACK \
  '      !listen_is_loopback(listen, listen_len)) {' \
  '      !listen_is_loopback("127.0.0.1:1", 11)) {'

# A neighbour of 127.0.0.0/8 is taken for it.
PLANT_NEED_TEXT='LOOPBACK:'
plant loopback-range-too-wide src/server.c testEdge $LOOPBACK \
  '    return addr->bytes[0] == 127u;' \
  '    return addr->bytes[0] >= 127u;'

# Any IPv6 address ending in 1 or 2 is taken for ::1.
PLANT_NEED_TEXT='LOOPBACK:'
plant ipv6-loopback-only-the-last-byte src/server.c testEdge $LOOPBACK \
  '    for (i = 0; i < 15; i++) {
      if (addr->bytes[i] != 0u) {
        return 0;
      }
    }
    return addr->bytes[15] == 1u;' \
  '    (void)i;
    return addr->bytes[15] == 1u || addr->bytes[15] == 2u;'

# A listener that binds nothing is taken for a loopback one.
PLANT_NEED_TEXT='LOOPBACK:'
plant no-listener-taken-for-loopback src/server.c testEdge $LOOPBACK \
  '  if (listen_len == 0 || !parse_listen(listen, listen_len, &addr)) {
    return 0;
  }' \
  '  if (listen_len == 0) {
    return 1;
  }
  if (!parse_listen(listen, listen_len, &addr)) {
    return 0;
  }'

# Sign-out clears the cookie with attributes of its own, not the ones it was set with.
PLANT_NEED_TEXT='COOKIE:'
plant clearing-cookie-with-other-attributes src/server.c testEdge $COOKIES \
  '    if (cookie_line(server, "", 1, set_line, sizeof set_line) != 0) {' \
  '    if (snprintf(set_line, sizeof set_line,
            "%s=; Secure; HttpOnly; SameSite=Lax; Path=/; Max-Age=0",
            cookie_name(server)) < 0) {'

# The session is also read from the cookie of the other policy'"'"'s name.
PLANT_NEED_TEXT='COOKIE:'
plant either-cookie-name-is-a-session src/server.c testEdge $COOKIES \
  '    if (cookie_token(in.cookie, cookie_name(server), session_value,
            sizeof session_value) != 0) {' \
  '    if (cookie_token(in.cookie, cookie_name(server), session_value,
            sizeof session_value) != 0 &&
        cookie_token(in.cookie, "session", session_value, sizeof session_value) != 0) {'

# /logout does not ask for the token.
PLANT_NEED_TEXT='LOGOUT:'
plant logout-token-not-required src/admin.c testEdge $LOGOUT \
  '  if (!logout_allowed(catalog, request, keyring, root, root_len, session, body,' \
  '  if (0 && !logout_allowed(catalog, request, keyring, root, root_len, session, body,'

# /logout takes the token of any form of the session, not the one made for it.
PLANT_NEED_TEXT='LOGOUT:'
plant logout-takes-any-forms-token src/admin.c testEdge $LOGOUT \
  '      form_len == sizeof kLogoutPurpose - 1 &&
      memcmp(form_id, kLogoutPurpose, form_len) == 0) {' \
  '      form_len > 0) {'

# /logout does not open the token against the session.
PLANT_NEED_TEXT='LOGOUT:'
plant logout-token-not-opened src/admin.c testEdge $LOGOUT \
  '      gdefcore_form_check(catalog, request, keyring, root, root_len,
          (const char *)sraw, sraw_n, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&' \
  '      ((void)keyring, (void)root, (void)root_len, (void)sraw_n, 1) &&'

# The page that offers signing out carries no form.
PLANT_NEED_TEXT='LOGOUT:'
plant page-offers-no-signout-form src/admin.c testEdge $LOGOUT \
  '  form_len = strlen(out_form);
  /* The sign-out form goes last' \
  '  form_len = 0;
  /* The sign-out form goes last'


# Sign-out clears the cookie and does not end the session on the server.
PLANT_NEED_TEXT='SIGNOUT:'
plant signout-clears-the-cookie-but-does-not-revoke src/admin.c testEdge $REVOKES \
  '  rc = gdefcore_account_session_revoke(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);' \
  '  rc = GDEFCORE_OK;'

# A session that cannot be told from a live one is answered 403 instead of cleared.
PLANT_NEED_TEXT='DEAD:'
plant dead-session-signout-answered-403 src/admin.c testEdge $DEAD \
  '  if (rc != GDEFCORE_OK || gdefcore_request_principal(request, &who_n) == NULL) {
    wipe(raw, sizeof raw);
    return GDEFWEB_SIGNOUT_NO_SESSION;' \
  '  if (rc != GDEFCORE_OK || gdefcore_request_principal(request, &who_n) == NULL) {
    wipe(raw, sizeof raw);
    return GDEFWEB_SIGNOUT_REFUSED;'

# A dead session's sign-out is answered 303 but leaves the dead cookie in the browser.
PLANT_NEED_TEXT='DEAD:'
plant dead-session-cookie-not-cleared src/server.c testEdge $DEAD \
  '    case GDEFWEB_SIGNOUT_ENDED:
    case GDEFWEB_SIGNOUT_NO_SESSION:' \
  '    case GDEFWEB_SIGNOUT_ENDED:
      break;
    case GDEFWEB_SIGNOUT_NO_SESSION:
      reply_ex(server, sock, 303, "See Other", "", 0, NULL, NULL, in.method,
          in.path, client, "/login");
      goto out;'

# A dead session's sign-out skips the Origin check.
PLANT_NEED_TEXT='ORIGIN:'
plant dead-session-signout-skips-the-origin-check src/server.c testEdge $DEAD \
  '  if (state_change && !origin_good) {' \
  '  if (state_change && !origin_good &&
      !(span_eq(in.path, strlen(in.path), "/logout") &&
          gdefcore_request_principal(request, &(size_t){0}) == NULL)) {'

# A session the store cannot answer for is taken for a dead one and its cookie cleared.
PLANT_NEED_TEXT='FAILED:'
plant unreadable-store-taken-for-a-dead-session src/admin.c testEdge $FAILS \
  '  if (rc != GDEFCORE_OK && rc != GDEFCORE_ERR_INVALID) {' \
  '  if (0 && rc != GDEFCORE_OK && rc != GDEFCORE_ERR_INVALID) {'

# A sign-out whose revocation failed is reported as done.
PLANT_NEED_TEXT='FAILED:'
plant failed-revocation-reported-as-signed-out src/admin.c testEdge $FAILS \
  '  out = rc == GDEFCORE_OK ? GDEFWEB_SIGNOUT_ENDED : GDEFWEB_SIGNOUT_FAILED;' \
  '  out = GDEFWEB_SIGNOUT_ENDED;'

plant_finish
