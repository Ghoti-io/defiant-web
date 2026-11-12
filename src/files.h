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
 * @file files.h
 * @brief The upload page of the admin and the public address of a kept file.
 * Not a public header.
 */

#ifndef GHOTI_IO_GDEFWEB_FILES_H
#define GHOTI_IO_GDEFWEB_FILES_H

#include <ghoti.io/defiant-web/macros.h>
#include "admin.h"

#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/request.h>

#include <stddef.h>
#include <stdint.h>

/** Whether @p path is /admin/files. */
int gdefweb_files_path(const char * path);

/** Whether uploads are on (gdefweb_uploads_bind()). */
int gdefweb_files_enabled(void);

/**
 * The most bytes a request body may have when it posts a file: the policy's
 * file size and the room a form's other parts and boundaries need, or 0 when
 * uploads are off. The server holds a body to this before it holds it.
 */
size_t gdefweb_files_body_cap(void);

/**
 * @brief Serve /admin/files: the form and the files this person uploaded
 * (GET), and a file posted as multipart/form-data (POST). Returns 0 with
 * @p out filled, or -1 for an error that is not the person's.
 */
int gdefweb_files_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * ctype, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out);

/** Whether @p path is /files/<id> or /files/<id>/<name>. */
int gdefweb_files_public_path(const char * path);

/**
 * @brief A file for the request's principal. A file that is not there, or that
 * the principal may not have, is one answer (404). The body is the caller's to
 * free; @p out->csp and @p out->disposition are set.
 */
int gdefweb_files_public(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, const char * path, struct GDEFWEB_Admin_Reply * out);

#endif
