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
 * The content administration pages. Not a public header.
 */

#ifndef GHOTI_IO_GDEFWEB_CONTENT_H
#define GHOTI_IO_GDEFWEB_CONTENT_H

#include <ghoti.io/defiant-web/macros.h>
#include "admin.h"

#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/request.h>

#include <stddef.h>
#include <stdint.h>

/** Whether @p path is /admin/content or below it. */
int gdefweb_content_path(const char * path);

/**
 * @brief Serve a path under /admin/content: the types a person may manage, the
 * entities of one, and one entity's form. @p session is the raw session, as the
 * other admin pages are given it. A page that is not there, or not for this
 * person, is the answer unknown_admin() gives. Returns 0 with @p out filled, or
 * -1 for an error that is not the person's.
 */
int gdefweb_content_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out);

#endif
