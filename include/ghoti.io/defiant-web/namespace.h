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
 * @file namespace.h
 * @brief Maps every public name into this build's version namespace.
 * @stability free
 */

#ifndef GHOTI_IO_GDEFWEB_NAMESPACE_H
#define GHOTI_IO_GDEFWEB_NAMESPACE_H

#include <ghoti.io/defiant-web/libver.h>

/// @cond HIDDEN_SYMBOLS

#define GDEFWEB_Allocator GHOTIIO_DEFWEB(GDEFWEB_Allocator)
#define GDEFWEB_Result GHOTIIO_DEFWEB(GDEFWEB_Result)
#define GDEFWEB_Cookie_SameSite GHOTIIO_DEFWEB(GDEFWEB_Cookie_SameSite)
#define GDEFWEB_Cookie_Transport GHOTIIO_DEFWEB(GDEFWEB_Cookie_Transport)
#define GDEFWEB_Server GHOTIIO_DEFWEB(GDEFWEB_Server)
#define GDEFWEB_Server_Options GHOTIIO_DEFWEB(GDEFWEB_Server_Options)

#define gdefweb_allocator_default GHOTIIO_DEFWEB(gdefweb_allocator_default)
#define gdefweb_result_string GHOTIIO_DEFWEB(gdefweb_result_string)
#define gdefweb_server_accept_one GHOTIIO_DEFWEB(gdefweb_server_accept_one)
#define gdefweb_server_accepting GHOTIIO_DEFWEB(gdefweb_server_accepting)
#define gdefweb_server_create GHOTIIO_DEFWEB(gdefweb_server_create)
#define gdefweb_server_destroy GHOTIIO_DEFWEB(gdefweb_server_destroy)
#define gdefweb_server_done GHOTIIO_DEFWEB(gdefweb_server_done)
#define gdefweb_server_listen GHOTIIO_DEFWEB(gdefweb_server_listen)
#define gdefweb_server_local GHOTIIO_DEFWEB(gdefweb_server_local)
#define gdefweb_server_loop GHOTIIO_DEFWEB(gdefweb_server_loop)
#define gdefweb_server_options_default GHOTIIO_DEFWEB(gdefweb_server_options_default)
#define gdefweb_server_serving GHOTIIO_DEFWEB(gdefweb_server_serving)
#define gdefweb_server_set_host GHOTIIO_DEFWEB(gdefweb_server_set_host)
#define gdefweb_server_set_inst GHOTIIO_DEFWEB(gdefweb_server_set_inst)
#define GDEFWEB_Blob_Dir GHOTIIO_DEFWEB(GDEFWEB_Blob_Dir)
#define gdefweb_blob_dir_open GHOTIIO_DEFWEB(gdefweb_blob_dir_open)
#define gdefweb_blob_dir_store GHOTIIO_DEFWEB(gdefweb_blob_dir_store)
#define gdefweb_blob_dir_close GHOTIIO_DEFWEB(gdefweb_blob_dir_close)
#define gdefweb_uploads_bind GHOTIIO_DEFWEB(gdefweb_uploads_bind)
#define gdefweb_uploads_sweep GHOTIIO_DEFWEB(gdefweb_uploads_sweep)

/// @endcond

#endif /* GHOTI_IO_GDEFWEB_NAMESPACE_H */
