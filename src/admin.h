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
  char * body;
  size_t len;
};

const char * gdefweb_admin_csp(void);

int gdefweb_admin_static(const char * path, const char ** body, size_t * len,
    const char ** type);

int gdefweb_admin_path(const char * path);

int gdefweb_admin_is_admin(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request);

int gdefweb_admin_allowed(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * cookie);

int gdefweb_admin_serve(const GDEFWEB_Allocator * alloc,
    GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * method, const char * path, const char * cookie,
    const char * body, size_t body_len, const char * ctype,
    struct GDEFWEB_Admin_Reply * out);

#endif /* GHOTI_IO_GDEFWEB_ADMIN_H */
