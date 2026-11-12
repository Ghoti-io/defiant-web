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
 * @file multipart.h
 * @brief A strict reader of a multipart/form-data body. Not a public header.
 *
 * It reads a body already held in memory (the server caps what it holds before
 * it holds it) and gives each part's name, file name, declared type and bytes
 * as spans of that body: nothing is copied and nothing is decoded. It refuses,
 * and gives nothing, for a boundary that is not an RFC 2046 boundary, more than
 * ::GDEFWEB_MP_MAX_PARTS parts, a part head over ::GDEFWEB_MP_MAX_HEAD bytes or
 * with a header it cannot read, a name or file name with a quote, a control
 * character or a length over its cap, a body with text before the first
 * boundary, and a body that does not end with the closing boundary.
 */

#ifndef GHOTI_IO_GDEFWEB_MULTIPART_H
#define GHOTI_IO_GDEFWEB_MULTIPART_H

#include <ghoti.io/defiant-web/macros.h>

#include <stddef.h>
#include <stdint.h>

/** Parts one body may have, at most. */
#define GDEFWEB_MP_MAX_PARTS 8
/** Bytes of one part's header lines, at most. */
#define GDEFWEB_MP_MAX_HEAD 1024
/** Bytes of a boundary, at most (RFC 2046). */
#define GDEFWEB_MP_MAX_BOUNDARY 70
/** Bytes of a part's name, at most. */
#define GDEFWEB_MP_MAX_NAME 64
/** Bytes of a part's file name, at most. */
#define GDEFWEB_MP_MAX_FILENAME 255

struct GDEFWEB_Mp_Part {
  const char * name;        /**< `name` of the Content-Disposition. */
  size_t name_len;
  const char * filename;    /**< `filename`, or NULL when the part has none. */
  size_t filename_len;
  const char * ctype;       /**< The part's Content-Type, or NULL. */
  size_t ctype_len;
  const uint8_t * data;     /**< The part's content. */
  size_t len;
};

struct GDEFWEB_Mp {
  struct GDEFWEB_Mp_Part parts[GDEFWEB_MP_MAX_PARTS];
  size_t count;
};

/**
 * @brief Read @p body, which @p content_type (the request's Content-Type)
 * says is multipart/form-data.
 *
 * @return 0 with @p out filled, or -1 for anything refused above (@p out is
 * then empty).
 */
int gdefweb_mp_parse(const char * content_type, const char * body, size_t len,
    struct GDEFWEB_Mp * out);

/**
 * @brief The first part named @p name, or NULL. A name that two parts carry is
 * ambiguous: NULL, so a caller cannot be made to read the wrong one.
 */
const struct GDEFWEB_Mp_Part * gdefweb_mp_find(const struct GDEFWEB_Mp * mp,
    const char * name);

#endif
