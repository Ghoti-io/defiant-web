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
 * The admin pages as routes, links and the widgets that draw them.
 */

#include "nav.h"

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/adminarea.h>
#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-core/module.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/settings.h>
#include <ghoti.io/defiant-core/siteasset.h>
#include <ghoti.io/defiant-core/template.h>
#include <ghoti.io/defiant-core/view.h>

#include <string.h>

/* The permission each page asks. Every page has one, and the route's own check
 * is the only authorization: a session says who someone is, never what they may
 * do. Where a page already meant a permission it keeps it (templates, menus,
 * settings, and the layouts the front page editor edits); the pages that had
 * none ask the ones the core declares for them (adminarea.h). */
#define PERM_AREA GDEFCORE_ADMIN_USE
#define PERM_POST GDEFCORE_POST_WRITE
#define PERM_FRONT GDEFCORE_LAYOUT_EDIT
#define PERM_UNDO GDEFCORE_ADMIN_UNDO
#define PERM_ACCESS GDEFCORE_ACCESS_READ
#define PERM_TEMPLATES GDEFCORE_TEMPLATE_EDIT
#define PERM_MENUS GDEFCORE_MENU_EDIT
#define PERM_SETTINGS GDEFCORE_SETTINGS_EDIT
#define PERM_CSS GDEFCORE_ASSET_CSS_EDIT
#define PERM_JS GDEFCORE_ASSET_JS_EDIT
#define PERM_VIEWS GDEFCORE_VIEW_EDIT

struct PageRoute {
  const char * name;
  const char * path;
  const char * title;
  const char * permission;
  int kind;
  const char * parent;
  int is_default;
  int weight;
};

static const struct PageRoute kPages[] = {
    {"core.admin", "/admin", "Admin", PERM_AREA, GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-post", "/admin/post", "Write a post", PERM_POST,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-front", "/admin/front", "Change the front page", PERM_FRONT,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-content", "/admin/content", "Content", PERM_AREA, GDEFCORE_ROUTE_PAGE,
        NULL, 0, 0},
    {"core.admin-content-type", "/admin/content/:type", "Content of a type", PERM_AREA,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-content-entity", "/admin/content/:type/:id", "Content", PERM_AREA,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-content-page", "/admin/content/:type/page/:number", "Content",
        PERM_AREA, GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-views", "/admin/views", "Views", PERM_VIEWS, GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-view", "/admin/views/:name", "View", PERM_VIEWS, GDEFCORE_ROUTE_PAGE, NULL, 0,
        0},
    {"core.admin-undo", "/admin/undo", "Undo", PERM_UNDO, GDEFCORE_ROUTE_PAGE, NULL,
        0, 0},
    {"core.admin-templates", "/admin/templates", "Edit templates", PERM_TEMPLATES,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-template", "/admin/templates/:name", "Template", PERM_TEMPLATES,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-template-edit", "/admin/templates/:name/edit", "Edit",
        PERM_TEMPLATES, GDEFCORE_ROUTE_TAB, "core.admin-template", 1, 0},
    {"core.admin-template-history", "/admin/templates/:name/history", "History",
        PERM_TEMPLATES, GDEFCORE_ROUTE_TAB, "core.admin-template", 0, 1},
    {"core.admin-menus", "/admin/menus", "Menus", PERM_MENUS, GDEFCORE_ROUTE_PAGE,
        NULL, 0, 0},
    {"core.admin-layouts", "/admin/layouts", "Layouts", PERM_FRONT, GDEFCORE_ROUTE_PAGE, NULL,
        0, 0},
    {"core.admin-layout", "/admin/layouts/:name", "Layout", PERM_FRONT, GDEFCORE_ROUTE_PAGE,
        NULL, 0, 0},
    {"core.admin-structures", "/admin/structures", "Page structures", PERM_TEMPLATES,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-structure", "/admin/structures/:name", "Page structure", PERM_TEMPLATES,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-layout-rules", "/admin/layout-rules", "Layout rules", PERM_FRONT,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-stylesheets", "/admin/stylesheets", "Stylesheets", PERM_CSS,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-stylesheet", "/admin/stylesheets/:name", "Stylesheet", PERM_CSS,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-stylesheet-edit", "/admin/stylesheets/:name/edit", "Edit", PERM_CSS,
        GDEFCORE_ROUTE_TAB, "core.admin-stylesheet", 1, 0},
    {"core.admin-stylesheet-history", "/admin/stylesheets/:name/history", "History",
        PERM_CSS, GDEFCORE_ROUTE_TAB, "core.admin-stylesheet", 0, 1},
    {"core.admin-scripts", "/admin/scripts", "Scripts", PERM_JS, GDEFCORE_ROUTE_PAGE,
        NULL, 0, 0},
    {"core.admin-script", "/admin/scripts/:name", "Script", PERM_JS,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-script-edit", "/admin/scripts/:name/edit", "Edit", PERM_JS,
        GDEFCORE_ROUTE_TAB, "core.admin-script", 1, 0},
    {"core.admin-script-history", "/admin/scripts/:name/history", "History", PERM_JS,
        GDEFCORE_ROUTE_TAB, "core.admin-script", 0, 1},
    {"core.admin-settings", "/admin/settings", "Settings", PERM_SETTINGS,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-settings-core", "/admin/settings/core", "Site settings",
        PERM_SETTINGS, GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
    {"core.admin-settings-core-site", "/admin/settings/core/site", "Site",
        PERM_SETTINGS, GDEFCORE_ROUTE_TAB, "core.admin-settings-core", 1, 0},
    {"core.admin-more", "/admin/more", "More", PERM_AREA, GDEFCORE_ROUTE_PAGE, NULL,
        0, 0},
    {"core.admin-access", "/admin/access", "What is granted", PERM_ACCESS,
        GDEFCORE_ROUTE_PAGE, NULL, 0, 0},
};

struct PageLink {
  const char * name;
  const char * label;
  const char * route;
  const char * parent;
  int weight;
};

static const struct PageLink kLinks[] = {
    {"core.l-admin", "Admin", "core.admin", NULL, -100},
    {"core.l-admin-post", "Write a post", "core.admin-post", "core.l-admin", 0},
    {"core.l-admin-front", "Change the front page", "core.admin-front",
        "core.l-admin", 1},
    {"core.l-admin-content", "Content", "core.admin-content", "core.l-admin", 10},
    {"core.l-admin-content-type", "Content of a type", "core.admin-content-type",
        "core.l-admin-content", 0},
    {"core.l-admin-views", "Views", "core.admin-views", "core.l-admin", 11},
    {"core.l-admin-undo", "Undo", "core.admin-undo", "core.l-admin", 2},
    {"core.l-admin-templates", "Edit templates", "core.admin-templates",
        "core.l-admin", 3},
    /* Has a path only on the page of one template, which is where it shows. */
    {"core.l-admin-template", "Template", "core.admin-template",
        "core.l-admin-templates", 0},
    {"core.l-admin-menus", "Menus", "core.admin-menus", "core.l-admin", 4},
    {"core.l-admin-layouts", "Layouts", "core.admin-layouts", "core.l-admin", 4},
    {"core.l-admin-layout", "Layout", "core.admin-layout", "core.l-admin-layouts", 0},
    {"core.l-admin-layout-rules", "Layout rules", "core.admin-layout-rules", "core.l-admin-layouts", 1},
    {"core.l-admin-structures", "Page structures", "core.admin-structures", "core.l-admin", 5},
    {"core.l-admin-structure", "Page structure", "core.admin-structure",
        "core.l-admin-structures", 0},
    {"core.l-admin-stylesheets", "Stylesheets", "core.admin-stylesheets",
        "core.l-admin", 6},
    {"core.l-admin-stylesheet", "Stylesheet", "core.admin-stylesheet",
        "core.l-admin-stylesheets", 0},
    {"core.l-admin-scripts", "Scripts", "core.admin-scripts", "core.l-admin", 7},
    {"core.l-admin-script", "Script", "core.admin-script", "core.l-admin-scripts", 0},
    {"core.l-admin-settings", "Settings", "core.admin-settings", "core.l-admin", 5},
    {"core.l-admin-settings-core", "Site settings", "core.admin-settings-core",
        "core.l-admin-settings", 0},
    {"core.l-admin-more", "More", "core.admin-more", "core.l-admin", 9},
    {"core.l-admin-access", "What is granted", "core.admin-access",
        "core.l-admin-more", 0},
};

/* A page needs no handler of its own here: the server serves it. */
static void page_call(const GDEFCORE_Route_Call * call) {
  (void)call;
}

GDEFCORE_Routes * gdefweb_nav_open(GDEFCORE_Catalog * catalog) {
  GDEFCORE_Routes * routes = NULL;
  size_t i;
  if (catalog == NULL || gdefcore_routes_create(NULL, &routes) != GDEFCORE_OK) {
    return NULL;
  }
  if (gdefcore_routes_set_check(routes, gdefcore_access_route_check, catalog) !=
      GDEFCORE_OK) {
    gdefcore_routes_destroy(routes);
    return NULL;
  }
  for (i = 0; i < sizeof kPages / sizeof kPages[0]; i++) {
    GDEFCORE_Route_Spec spec;
    memset(&spec, 0, sizeof spec);
    spec.size = sizeof spec;
    spec.machine = kPages[i].name;
    spec.machine_len = strlen(kPages[i].name);
    spec.path = kPages[i].path;
    spec.path_len = strlen(kPages[i].path);
    spec.call_fn = page_call;
    spec.title = kPages[i].title;
    spec.title_len = strlen(kPages[i].title);
    spec.permission = kPages[i].permission;
    spec.permission_len =
        kPages[i].permission == NULL ? 0 : strlen(kPages[i].permission);
    spec.kind = kPages[i].kind;
    spec.parent = kPages[i].parent;
    spec.parent_len = kPages[i].parent == NULL ? 0 : strlen(kPages[i].parent);
    spec.is_default = kPages[i].is_default;
    spec.weight = kPages[i].weight;
    if (gdefcore_route_add_spec(routes, &spec, NULL, NULL) != GDEFCORE_OK) {
      gdefcore_routes_destroy(routes);
      return NULL;
    }
  }
  for (i = 0; i < sizeof kLinks / sizeof kLinks[0]; i++) {
    GDEFCORE_Route_Link link;
    memset(&link, 0, sizeof link);
    link.size = sizeof link;
    link.name = kLinks[i].name;
    link.name_len = strlen(kLinks[i].name);
    link.menu = "admin";
    link.menu_len = 5;
    link.label = kLinks[i].label;
    link.label_len = strlen(kLinks[i].label);
    link.route = kLinks[i].route;
    link.route_len = strlen(kLinks[i].route);
    link.weight = kLinks[i].weight;
    link.parent = kLinks[i].parent;
    link.parent_len = kLinks[i].parent == NULL ? 0 : strlen(kLinks[i].parent);
    link.kind = GDEFCORE_ROUTE_PAGE;
    if (gdefcore_routes_add_link(routes, &link) != GDEFCORE_OK) {
      gdefcore_routes_destroy(routes);
      return NULL;
    }
  }
  /* What the enabled modules placed in the admin area: their routes (a settings
   * page and its tabs among them); their links come with them. A module whose
   * routes cannot be added changes nothing of the kernel's pages. */
  {
    GDEFCORE_Host * host = gdefcore_catalog_host(catalog);
    size_t tenant_len = 0;
    const uint8_t * tenant = gdefcore_catalog_tenant(catalog, &tenant_len);
    char * err = NULL;
    size_t err_len = 0;
    if (host != NULL && tenant != NULL) {
      (void)gdefcore_module_add_routes(host, tenant, tenant_len, NULL, routes, &err,
          &err_len);
      gdefcore_routes_free(routes, err);
    }
  }
  return routes;
}

void gdefweb_nav_close(GDEFCORE_Routes * routes) {
  gdefcore_routes_destroy(routes);
}

int gdefweb_nav_follows(GDEFCORE_Routes * routes, GDEFCORE_Request * request,
    const char * path, const char * machine) {
  GDEFCORE_Route_Match * match = NULL;
  int allowed = 0;
  if (routes == NULL || request == NULL || path == NULL || machine == NULL ||
      gdefcore_routes_match(routes, request, path, strlen(path), &match) !=
          GDEFCORE_OK) {
    return 0;
  }
  /* The route's own check, for the page's values. A route the match left out
   * was refused by the same check, and the question asked again is the same. */
  if (gdefcore_routes_permits(routes, request, machine, strlen(machine), match,
          &allowed) != GDEFCORE_OK) {
    allowed = 0;
  }
  gdefcore_route_match_free(match);
  return allowed;
}

int gdefweb_nav_landing(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    char * out, size_t cap) {
  GDEFCORE_Routes * routes;
  size_t i;
  int found = 0;
  if (out == NULL || cap == 0) {
    return 0;
  }
  out[0] = '\0';
  if (catalog == NULL || request == NULL) {
    return 0;
  }
  routes = gdefweb_nav_open(catalog);
  if (routes == NULL) {
    return 0;
  }
  for (i = 0; i < sizeof kPages / sizeof kPages[0] && !found; i++) {
    size_t n = strlen(kPages[i].path);
    if (kPages[i].kind != GDEFCORE_ROUTE_PAGE || strchr(kPages[i].path, ':') != NULL ||
        n + 1 > cap) {
      continue;
    }
    if (gdefweb_nav_follows(routes, request, kPages[i].path, kPages[i].name)) {
      memcpy(out, kPages[i].path, n + 1);
      found = 1;
    }
  }
  gdefweb_nav_close(routes);
  return found;
}

/* Append @p s to the result, growing it. */
static int grow_add(const GDEFWEB_Allocator * alloc, char ** p, size_t * n,
    size_t * cap, const char * s, size_t len) {
  if (*n + len + 1 > *cap) {
    size_t next = *cap == 0 ? 512 : *cap * 2;
    char * grown;
    while (next < *n + len + 1) {
      next *= 2;
    }
    grown = (char *)gcu_allocator_realloc(alloc, *p, next);
    if (grown == NULL) {
      return -1;
    }
    *p = grown;
    *cap = next;
  }
  memcpy(*p + *n, s, len);
  *n += len;
  (*p)[*n] = '\0';
  return 0;
}

/* One widget drawn into the result, in an element of the class @p cls, if it
 * has anything to show. A failure to draw adds nothing, and neither does a
 * widget that drew nothing and is @p optional (the messages widget, which
 * claims the session's messages when it draws and draws nothing when there are
 * none). */
static int add_widget(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Routes * routes, GDEFCORE_Request * request, const char * kind,
    const char * target, const char * path, const char * tag, const char * cls,
    int optional, char ** p, size_t * n, size_t * cap) {
  char * html = NULL;
  size_t html_len = 0;
  int rc = 0;
  if (gdefcore_render_widget(catalog, routes, request, NULL, kind, strlen(kind),
          target, strlen(target), path, strlen(path), GDEFCORE_RENDER_PAGE_FUEL,
          &html, &html_len) != GDEFCORE_OK) {
    return 0;
  }
  if (optional && html_len == 0) {
    gdefcore_text_free(catalog, html);
    return 0;
  }
  if (grow_add(alloc, p, n, cap, "<", 1) != 0 ||
      grow_add(alloc, p, n, cap, tag, strlen(tag)) != 0 ||
      grow_add(alloc, p, n, cap, " class=\"", 8) != 0 ||
      grow_add(alloc, p, n, cap, cls, strlen(cls)) != 0 ||
      grow_add(alloc, p, n, cap, "\">", 2) != 0 ||
      grow_add(alloc, p, n, cap, html, html_len) != 0 ||
      grow_add(alloc, p, n, cap, "</", 2) != 0 ||
      grow_add(alloc, p, n, cap, tag, strlen(tag)) != 0 ||
      grow_add(alloc, p, n, cap, ">", 1) != 0) {
    rc = -1;
  }
  gdefcore_text_free(catalog, html);
  return rc;
}

/* How many items the core would put in a tab strip or a trail for this page. */
static size_t count_items(GDEFCORE_Catalog * catalog, GDEFCORE_Routes * routes,
    GDEFCORE_Request * request, const char * path, int tabs) {
  GDEFCORE_Route_Match * match = NULL;
  GDEFCORE_Menu * menu = NULL;
  size_t n = 0;
  if (gdefcore_routes_match(routes, request, path, strlen(path), &match) !=
      GDEFCORE_OK) {
    return 0;
  }
  if ((tabs ? gdefcore_menu_tabs(catalog, routes, request, match, &menu)
            : gdefcore_menu_breadcrumbs(catalog, routes, request, match, "admin", 5,
                  &menu)) == GDEFCORE_OK) {
    n = gdefcore_menu_count(menu);
  }
  gdefcore_menu_free(menu);
  gdefcore_route_match_free(match);
  return n;
}

char * gdefweb_nav_html(const GDEFWEB_Allocator * alloc,
    GDEFCORE_Catalog * catalog, GDEFCORE_Routes * routes,
    GDEFCORE_Request * request, const char * path, size_t * out_len) {
  char * p = NULL;
  size_t n = 0;
  size_t cap = 0;
  size_t base;
  if (alloc == NULL || catalog == NULL || routes == NULL || request == NULL ||
      path == NULL || out_len == NULL) {
    return NULL;
  }
  /* The messages the session was left come first, and are claimed by drawing
   * them: a page that is drawn shows them once. */
  if (add_widget(alloc, catalog, routes, request, GDEFCORE_WIDGET_MESSAGES, "", path,
          "div", "admin-messages", 1, &p, &n, &cap) != 0) {
    gcu_allocator_free(alloc, p);
    return NULL;
  }
  base = n;
  if (add_widget(alloc, catalog, routes, request, GDEFCORE_WIDGET_MENU, "admin",
          path, "nav", "admin-menu", 0, &p, &n, &cap) != 0) {
    gcu_allocator_free(alloc, p);
    return NULL;
  }
  if (n > base &&
      count_items(catalog, routes, request, path, 0) > 1 &&
      add_widget(alloc, catalog, routes, request, GDEFCORE_WIDGET_BREADCRUMBS, "",
          path, "nav", "admin-crumbs", 0, &p, &n, &cap) != 0) {
    gcu_allocator_free(alloc, p);
    return NULL;
  }
  if (n > base && count_items(catalog, routes, request, path, 1) > 0 &&
      add_widget(alloc, catalog, routes, request, GDEFCORE_WIDGET_TABS, "", path,
          "nav", "admin-tabs", 0, &p, &n, &cap) != 0) {
    gcu_allocator_free(alloc, p);
    return NULL;
  }
  if (n == base) {
    /* No templates to draw with: the one way home every page had. */
    static const char kHome[] = "<p><a href=\"/admin\">Home</a></p>";
    static const char kSite[] = "<p><a href=\"/\">Home</a></p>";
    /* Home is the admin home only for who may follow it. */
    int admin_home = gdefweb_nav_follows(routes, request, "/admin", "core.admin");
    if (grow_add(alloc, &p, &n, &cap, admin_home ? kHome : kSite,
            (admin_home ? sizeof kHome : sizeof kSite) - 1) != 0) {
      gcu_allocator_free(alloc, p);
      return NULL;
    }
  }
  *out_len = n;
  return p;
}
