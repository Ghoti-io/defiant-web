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
 * The admin area's navigation. Not a public header.
 *
 * The admin pages are routes in a route table of their own, with the
 * permission each already depends on, and links in the `admin` menu. The core
 * draws the menu, the breadcrumbs and the tabs from them (menu.h), so what a
 * page offers is what its route's own check allows, and a site restyles it
 * like any template.
 */

#ifndef GHOTI_IO_GDEFWEB_NAV_H
#define GHOTI_IO_GDEFWEB_NAV_H

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/request.h>

#include <stddef.h>

/** The marker a page leaves where its navigation goes. */
#define GDEFWEB_NAV_MARK "<!--nav-->"

/**
 * @brief The route table of the admin pages for one request: the routes, their
 * links, and the access check of @p catalog as the route check. NULL when
 * memory runs out. Free with gdefweb_nav_close().
 */
GDEFCORE_Routes * gdefweb_nav_open(GDEFCORE_Catalog * catalog);

/** @brief Free the table. NULL is ignored. */
void gdefweb_nav_close(GDEFCORE_Routes * routes);

/**
 * @brief Whether the request may follow the route @p machine at @p path: the
 * route's own check, for the values the path matched. A path that is not the
 * route's, and a route the table does not have, are no.
 */
int gdefweb_nav_follows(GDEFCORE_Routes * routes, GDEFCORE_Request * request,
    const char * path, const char * machine);

/**
 * @brief The navigation of the page at @p path as HTML: the admin menu, and
 * the breadcrumbs and tabs when the page has them, each drawn by the core's
 * widget. If the menu cannot be drawn (the site has no templates for it) the
 * result is a single link home, never nothing. Free with the allocator.
 */
char * gdefweb_nav_html(const GDEFWEB_Allocator * alloc,
    GDEFCORE_Catalog * catalog, GDEFCORE_Routes * routes,
    GDEFCORE_Request * request, const char * path, size_t * out_len);

#endif /* GHOTI_IO_GDEFWEB_NAV_H */
