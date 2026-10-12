/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Web.
 *
 * Ghoti.io Defiant Web is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Web is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file core.h
 * @brief Result codes for the HTTP adapter.
 * @stability free
 */

#ifndef GHOTI_IO_GDEFWEB_CORE_H
#define GHOTI_IO_GDEFWEB_CORE_H

#include <ghoti.io/defiant-web/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of an adapter call.
 */
typedef enum GDEFWEB_Result {
  GDEFWEB_OK = 0,          ///< Succeeded.
  GDEFWEB_ERR_IO,          ///< A stream or sink reported an error.
  GDEFWEB_ERR_FORMAT,      ///< Well formed input that is not this format.
  GDEFWEB_ERR_UNSUPPORTED, ///< Unknown format name, or a newer version.
  GDEFWEB_ERR_LIMIT,       ///< A limit was exceeded. Nothing is truncated.
  GDEFWEB_ERR_CORRUPT,     ///< This format, but the bytes are wrong.
  GDEFWEB_ERR_OOM,         ///< The allocator returned NULL.
  GDEFWEB_ERR_INVALID,     ///< A caller argument is wrong, or a missing row.
  GDEFWEB_ERR_INTERNAL,    ///< A library invariant failed.
  GDEFWEB_ERR_BUSY,        ///< A call cannot proceed because one is already running.
  GDEFWEB_RESULT_COUNT     ///< Not a result. Closes the string table.
} GDEFWEB_Result;

/**
 * @brief A static description of a result. Never NULL.
 *
 * @param result The code, including out-of-range values.
 * @return A string the caller does not free.
 */
GDEFWEB_API const char * gdefweb_result_string(GDEFWEB_Result result);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFWEB_CORE_H */
