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
 * @file server.h
 * @brief One HTTP listener for an installation.
 * @stability free
 *
 * The listener reads `DEFIANT_LISTEN` and `DEFIANT_TRUSTED_PROXIES` from a
 * configuration the caller already loaded. An empty listen value binds
 * nothing. The installation is open when a caller attaches one. Health
 * paths are answered before a tenant is resolved.
 */

#ifndef GHOTI_IO_GDEFWEB_SERVER_H
#define GHOTI_IO_GDEFWEB_SERVER_H

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-web/core.h>
#include <ghoti.io/defiant-core/inst.h>
#include <ghoti.io/cutil/loop.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One process listener. Opaque. */
typedef struct GDEFWEB_Server GDEFWEB_Server;

/**
 * @brief Create a server from bootstrap settings.
 *
 * Copies `DEFIANT_LISTEN` and `DEFIANT_TRUSTED_PROXIES`. Does not bind.
 * @p config is not retained. @p allocator NULL uses the default.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_create(
    const GDEFCORE_Inst_Config * config, const GDEFWEB_Allocator * allocator,
    GDEFWEB_Server ** out);

/** @brief Free a server. NULL is ignored. Closes the listener. */
GDEFWEB_API void gdefweb_server_destroy(GDEFWEB_Server * server);

/**
 * @brief Attach an open installation, or clear it.
 *
 * NULL @p inst means the installation is not open. @p root is
 * ::GDEFCORE_ACCOUNT_ROOT_LEN bytes when @p inst is not NULL. The
 * installation and the stores it borrows must outlive the server.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_set_inst(GDEFWEB_Server * server,
    GDEFCORE_Inst * inst, const uint8_t * root, size_t root_len);

/**
 * @brief Bind `DEFIANT_LISTEN`. An empty value binds nothing and succeeds.
 *
 * A later call replaces the listener.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_listen(GDEFWEB_Server * server);

/** @brief Non-zero while a listener is accepting. */
GDEFWEB_API int gdefweb_server_accepting(const GDEFWEB_Server * server);

/** @brief The bound address, including the port the OS chose. */
GDEFWEB_API GDEFWEB_Result gdefweb_server_local(const GDEFWEB_Server * server,
    GCU_Socket_Address * out);

/**
 * @brief The server's loop. The caller runs it on the thread that created
 * the server.
 */
GDEFWEB_API GCU_Loop * gdefweb_server_loop(GDEFWEB_Server * server);

/**
 * @brief Accept one connection on a fiber and serve one request.
 *
 * Returns once the fiber is parked on accept. Pump the loop until
 * gdefweb_server_done() is non-zero.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_accept_one(GDEFWEB_Server * server);

/** @brief Non-zero after the connection started by accept_one has finished. */
GDEFWEB_API int gdefweb_server_done(const GDEFWEB_Server * server);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFWEB_SERVER_H */
