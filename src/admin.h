/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Web.
 *
 * Ghoti.io Defiant Web is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Web is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Server-rendered admin pages. Not a public header.
 */

#ifndef GHOTI_IO_GDEFWEB_ADMIN_H
#define GHOTI_IO_GDEFWEB_ADMIN_H

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/request.h>
#include <ghoti.io/defiant-web/allocator.h>

#include <stddef.h>

struct GDEFWEB_Admin_Reply {
  int status;
  const char * reason;
  const char * type;
  const char * location;
  char location_buf[512];
  char * body;
  size_t len;
};

const char * gdefweb_admin_csp(void);

/* The sign-in page. @p failed adds the one sentence a refusal shows. */
const char * gdefweb_admin_login_page(int failed);

int gdefweb_admin_static(const char * path, const char ** body, size_t * len,
    const char ** type);

int gdefweb_admin_path(const char * path);

/* In these, @p session is the value of the session cookie, which the server
 * took from the request under the name its cookie policy gives (server.c), and
 * not the Cookie header: a cookie of any other name is not a session.
 *
 * Bind the queue of the session to the request, so the
 * messages widget of a page (a public one with a layout that places it) shows
 * the messages the session was left. A value that carries no live session
 * binds nothing, and the widget draws nothing. */
void gdefweb_admin_bind_messages(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session);

/* Whether the session cookie carries a live session, and so who the request is
 * from: the principal is bound to the request. That is authentication and only
 * that. What the person may do is decided by each page's own route check
 * (nav.h), never by a role they hold. */
int gdefweb_admin_authenticated(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session);

/* What a POST to /logout comes to. The Origin is the caller's check and has been
 * made. */
enum GDEFWEB_Signout {
  /* The session was live and is now ended on the server: the caller clears the
   * cookie and sends the person to the sign-in page. */
  GDEFWEB_SIGNOUT_ENDED = 0,
  /* The cookie names no live session (absent, damaged, expired, revoked, or
   * ended by an older epoch). There is nothing to end and nothing was changed;
   * the caller still clears the cookie, so a dead one is not left in the
   * browser. No token is asked for, because there is no session to forge. */
  GDEFWEB_SIGNOUT_NO_SESSION = 1,
  /* The session is live and the request does not carry its signed sign-out
   * token: answer 403 and change nothing. */
  GDEFWEB_SIGNOUT_REFUSED = 2,
  /* The session could not be ended, or could not be told from a live one (the
   * storage failed): nothing is known to have changed, the cookie is not
   * cleared, and the person is told they are still signed in. */
  GDEFWEB_SIGNOUT_FAILED = 3
};

/* Sign out: end the one session the cookie names, and no other sign-in of the
 * person, through gdefcore_account_session_revoke(). A live session needs the
 * signed token its own page carried. */
enum GDEFWEB_Signout gdefweb_admin_signout(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, const char * body, size_t body_len,
    const char * ctype);

int gdefweb_admin_serve(const GDEFWEB_Allocator * alloc,
    GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * method, const char * path, const char * session,
    const char * body, size_t body_len, const char * ctype,
    struct GDEFWEB_Admin_Reply * out);

#endif /* GHOTI_IO_GDEFWEB_ADMIN_H */
