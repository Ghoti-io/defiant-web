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

/* Bind the queue of the session the cookie carries to the request, so the
 * messages widget of a page (a public one with a layout that places it) shows
 * the messages the session was left. A cookie that carries no live session
 * binds nothing, and the widget draws nothing. */
void gdefweb_admin_bind_messages(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * cookie);

/* Whether the session cookie carries a live session, and so who the request is
 * from: the principal is bound to the request. That is authentication and only
 * that. What the person may do is decided by each page's own route check
 * (nav.h), never by a role they hold. */
int gdefweb_admin_authenticated(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * cookie);

int gdefweb_admin_serve(const GDEFWEB_Allocator * alloc,
    GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * method, const char * path, const char * cookie,
    const char * body, size_t body_len, const char * ctype,
    struct GDEFWEB_Admin_Reply * out);

#endif /* GHOTI_IO_GDEFWEB_ADMIN_H */
