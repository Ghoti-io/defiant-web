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
 * @file uploads.h
 * @brief Where uploaded files are kept, and the policy they are held to.
 * @stability free
 *
 * An editor uploads a file at `/admin/files` and a visitor is given one at
 * `/files/<id>/<name>`; the library that holds the rules is defiant-core
 * (upload.h). This header is the host's side: a directory to keep the bytes
 * in, and the one place the server is told that uploads are on.
 *
 * Uploads are off until gdefweb_uploads_bind() is called: `/admin/files` then
 * says so and takes nothing, `/files/...` answers as a page that is not there,
 * and a request body keeps the small cap every other form has.
 */

#ifndef GHOTI_IO_GDEFWEB_UPLOADS_H
#define GHOTI_IO_GDEFWEB_UPLOADS_H

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-web/core.h>
#include <ghoti.io/defiant-core/upload.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A directory that keeps files by tenant and digest. Opaque. */
typedef struct GDEFWEB_Blob_Dir GDEFWEB_Blob_Dir;

/**
 * @brief Open @p root, a directory that exists, as a place to keep files.
 *
 * Files are kept at `<root>/<tenant, hex>/<first two hex of the digest>/<digest>`,
 * so two tenants' identical bytes are two files, and nothing a caller gives
 * (a name, a type) is ever part of a path. A file is written whole and moved
 * into place, so a reader sees all of it or none. The directory's permissions
 * are the host's to set: files are made private to the account running the
 * server.
 *
 * @return ::GDEFWEB_OK; ::GDEFWEB_ERR_INVALID for a root that is not a
 *   directory; ::GDEFWEB_ERR_OOM.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_blob_dir_open(const char * root, GDEFWEB_Blob_Dir ** out);

/** @brief The store @p dir makes, for gdefcore_upload_stage() and its kin. It
 * lives as long as @p dir. */
GDEFWEB_API const GDEFCORE_Blob_Store * gdefweb_blob_dir_store(const GDEFWEB_Blob_Dir * dir);

/** @brief Free @p dir. The files stay. NULL is ignored. */
GDEFWEB_API void gdefweb_blob_dir_close(GDEFWEB_Blob_Dir * dir);

/**
 * @brief Turn uploads on: keep files in @p blobs, hold them to @p policy.
 *
 * Both are borrowed and must outlive every request. Call it before the server
 * accepts, and again with NULL and NULL, while no request is running, to turn
 * uploads off. The binding belongs to the process: every tenant's catalog is
 * handed to the one store, which keeps each tenant's files apart by the tenant
 * bytes it is given.
 *
 * @return ::GDEFWEB_OK, or ::GDEFWEB_ERR_INVALID for a store or policy of the
 *   wrong size (a bind of NULL and NULL turns uploads off).
 */
GDEFWEB_API GDEFWEB_Result gdefweb_uploads_bind(const GDEFCORE_Blob_Store * blobs,
    const GDEFCORE_Upload_Policy * policy);

/**
 * @brief Run the upload sweep once for @p catalog's tenant, with the store and
 * the policy bound by gdefweb_uploads_bind().
 *
 * This is the one call a host's own timer makes to keep files tidy, beside
 * gdefcore_schedule_run() for scheduled publishing. Neither library owns a
 * timer: the core's schedule holds one job row per entity to publish, which is
 * not what a recurring tidy-up is, and a server that started its own thread
 * would decide the cadence for every host. A sweep is safe at any time and as
 * often as the host likes (it changes nothing that is not due), and from more
 * than one host at once: a file is promoted, staged again or removed under
 * the store's own transaction rules. Each out count may be NULL.
 *
 * @param now_ms The time, in milliseconds since 1970 UTC.
 * @return ::GDEFWEB_OK; ::GDEFWEB_ERR_INVALID when uploads are not bound or
 *   @p catalog is NULL; ::GDEFWEB_ERR_OOM; otherwise ::GDEFWEB_ERR_IO.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_uploads_sweep(GDEFCORE_Catalog * catalog,
    uint64_t now_ms, size_t * out_promoted, size_t * out_demoted, size_t * out_removed);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFWEB_UPLOADS_H */
