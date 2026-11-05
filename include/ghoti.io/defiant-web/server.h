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
 *
 * The session cookie is secure unless the caller names otherwise. See
 * ::GDEFWEB_Server_Options.
 */

#ifndef GHOTI_IO_GDEFWEB_SERVER_H
#define GHOTI_IO_GDEFWEB_SERVER_H

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-web/core.h>
#include <ghoti.io/defiant-core/inst.h>
#include <ghoti.io/defiant-core/module.h>
#include <ghoti.io/cutil/loop.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One process listener. Opaque. */
typedef struct GDEFWEB_Server GDEFWEB_Server;

/**
 * @brief The SameSite attribute of the session cookie.
 *
 * Strict is the default, and with it a browser does not send the cookie on a
 * request that a link on another site started. A person who follows a link
 * from a mail or a chat to a page of this site therefore arrives signed out on
 * that first request, and signed in on every request after it, because the
 * next one starts from this site. A deployment that needs the first request
 * signed in chooses ::GDEFWEB_COOKIE_SAME_SITE_LAX, which sends the cookie on
 * a top-level navigation by a safe method (a GET) from anywhere. Every
 * state-changing request is checked for its Origin and its signed form token
 * either way, so Lax is not what keeps a write from being forged.
 */
typedef enum GDEFWEB_Cookie_SameSite {
  GDEFWEB_COOKIE_SAME_SITE_STRICT = 0, ///< `SameSite=Strict`. The default.
  GDEFWEB_COOKIE_SAME_SITE_LAX = 1,    ///< `SameSite=Lax`.
} GDEFWEB_Cookie_SameSite;

/**
 * @brief Whether the session cookie needs a secure connection.
 *
 * ::GDEFWEB_COOKIE_TRANSPORT_SECURE is the default: the cookie is named
 * `__Host-session` and carries `Secure`, `HttpOnly` and `Path=/`, and no
 * `Domain`, so a browser keeps it to this host and to HTTPS. A server behind
 * a proxy that ends TLS is in this mode too, because the browser's side of the
 * connection is HTTPS.
 *
 * ::GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK is for development over plain
 * HTTP. A browser ignores a `__Host-` cookie that is not `Secure`, so this
 * mode drops both, and the cookie is named `session`. Because it can be read
 * from a network that is not private, gdefweb_server_create() accepts it only
 * when `DEFIANT_LISTEN` names a loopback address (127.0.0.0/8 or ::1), and
 * otherwise fails with ::GDEFWEB_ERR_INSECURE.
 */
typedef enum GDEFWEB_Cookie_Transport {
  GDEFWEB_COOKIE_TRANSPORT_SECURE = 0,         ///< `__Host-session`, `Secure`. The default.
  GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK = 1, ///< `session`, no `Secure`; loopback only.
} GDEFWEB_Cookie_Transport;

/**
 * @brief The creation options of a server.
 *
 * Start from gdefweb_server_options_default() and change only what a
 * deployment needs. Zero values of the enumerations are the secure choices,
 * and a NULL options pointer to gdefweb_server_create() means all of them.
 * Accepted only at exactly its own size.
 */
typedef struct GDEFWEB_Server_Options {
  size_t size;                         ///< `sizeof(GDEFWEB_Server_Options)`.
  GDEFWEB_Cookie_SameSite same_site;   ///< The cookie's SameSite attribute.
  GDEFWEB_Cookie_Transport transport;  ///< Secure, or plain HTTP on loopback.
} GDEFWEB_Server_Options;

/** @brief Fill @p out with the secure options: Strict, and Secure. */
GDEFWEB_API void gdefweb_server_options_default(GDEFWEB_Server_Options * out);

/**
 * @brief Create a server from bootstrap settings.
 *
 * Copies `DEFIANT_LISTEN` and `DEFIANT_TRUSTED_PROXIES`. Does not bind.
 * @p config is not retained. @p options NULL means the secure options
 * (gdefweb_server_options_default()); it is not retained either, and a struct
 * of any size but its own, or a value outside an enumeration, is
 * ::GDEFWEB_ERR_INVALID. The plain-HTTP cookie mode with a listener that is
 * not a loopback address (or none) is ::GDEFWEB_ERR_INSECURE and creates
 * nothing. @p allocator NULL uses the default.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_create(
    const GDEFCORE_Inst_Config * config, const GDEFWEB_Server_Options * options,
    const GDEFWEB_Allocator * allocator, GDEFWEB_Server ** out);

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
 * @brief Give every request the module host of this process, or clear it.
 *
 * The host is bound to the catalog each request opens, so the grants of the
 * enabled modules are among those the template editor's decisions weigh and
 * the contracts it validates against include the modules' contributions. The
 * host is borrowed and must outlive the server. A server with none runs no
 * module code.
 */
GDEFWEB_API GDEFWEB_Result gdefweb_server_set_host(GDEFWEB_Server * server,
    GDEFCORE_Host * host);

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

/**
 * @brief Non-zero while a connection is being served, not while the fiber is
 * only waiting to accept one.
 */
GDEFWEB_API int gdefweb_server_serving(const GDEFWEB_Server * server);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFWEB_SERVER_H */
