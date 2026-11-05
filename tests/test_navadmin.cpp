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
 * The admin area's navigation (site admin story 6), through the real server:
 * the menu, the breadcrumbs and the tabs are the core's widgets, drawn from the
 * admin pages as routes, and what a page offers is what its route's own check
 * lets the administrator follow. The menus page changes them.
 *
 * Every refusal has a control in the same test: the same request that is
 * refused for one administrator is shown to succeed for another. Every action
 * is a plain form: nothing here runs a script.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-test/defiant-test.h>

namespace {

/* The page the way the response scanner reads it: tokens are opaque. */
bool ScansClean(const std::string & page) {
  std::string text = page;
  size_t at;
  while ((at = text.find("name=\"token\" value=\"")) != std::string::npos) {
    size_t end = text.find('"', at + 20);
    text.erase(at, (end == std::string::npos ? text.size() : end) - at);
  }
  while ((at = text.find("charset=\"utf-8\"")) != std::string::npos) {
    text.erase(at, 15);
  }
  return gdeftest_response_has_internal_id(text.data(), text.size(), NULL, 0) == 0;
}

std::string Enc(const std::string & in) {
  std::string out;
  for (unsigned char c : in) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof buf, "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

class NavAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string cookie_;

  void Open() {
    Site(id_);
    UseHost();
    cookie_ = CookieLine(Login("ada", "secret"));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  /* The admin role granted the permissions to edit menu entries and overrides:
   * @p entries and @p overrides say which. */
  void GrantMenus(bool entries = true, bool overrides = true) {
    GDEFCORE_Catalog * catalog = Catalog();
    if (entries) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_MENU_EDIT_ENTRY_LABEL,
              strlen(GDEFCORE_MENU_EDIT_ENTRY_LABEL), "core.role", 9));
    }
    if (overrides) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, "admin", 5,
              GDEFCORE_MENU_EDIT_OVERRIDE_LABEL,
              strlen(GDEFCORE_MENU_EDIT_OVERRIDE_LABEL), "core.role", 9));
    }
    gdefcore_catalog_destroy(catalog);
  }

  /* The links of the menu on a page, in order: what is inside <nav class="admin-menu">. */
  static std::string MenuOf(const std::string & page) {
    size_t at = page.find("<nav class=\"admin-menu\">");
    if (at == std::string::npos) {
      return std::string();
    }
    return page.substr(at, page.find("</nav>", at) - at);
  }

  std::string Posted(const char * form_button, const std::string & fields,
      const std::string & page) {
    return "token=" + FormToken(page, form_button) + fields;
  }

  /* Post a form of the menus page and follow the redirect, as a browser does. */
  Reply PostMenus(const std::string & token, const std::string & fields) {
    return PostForm("/admin/menus", cookie_, "token=" + token + fields);
  }

  std::string MenusPage() { return Get("/admin/menus", cookie_).body; }

  /* The version of the menus a page of the editor was drawn for, as a field. */
  static std::string Ver(const std::string & page) {
    return "&version=" + InputValue(page, "version", 0);
  }
};

}  // namespace

TEST_F(NavAdmin, TheMenuIsTheCoresAndShowsWhatTheRoutesAllow) {
  Open();
  Reply home = Get("/admin", cookie_);
  EXPECT_EQ(200, home.status);
  std::string menu = MenuOf(home.body);
  ASSERT_FALSE(menu.empty()) << home.body;
  EXPECT_TRUE(Has(menu, "<ul class=\"menu\">"));
  /* The pages every administrator reaches, in their order, nested under Admin. */
  for (const char * link : {"href=\"/admin\">Admin", "href=\"/admin/post\">Write a post",
           "href=\"/admin/front\">Change the front page", "href=\"/admin/undo\">Undo",
           "href=\"/admin/more\">More", "href=\"/admin/access\">What is granted"}) {
    EXPECT_TRUE(Has(menu, link)) << link << "\n" << menu;
  }
  EXPECT_LT(menu.find("Write a post"), menu.find("Change the front page"));
  EXPECT_LT(menu.find("More"), menu.find("What is granted"));
  EXPECT_TRUE(Has(menu, "<li class=\"depth2\"><a href=\"/admin/access\">"));
  /* The templates and the menus are entries only for who may edit them: the
   * route's own check, and nothing granted yet. */
  EXPECT_FALSE(Has(menu, "href=\"/admin/templates\""));
  EXPECT_FALSE(Has(menu, "href=\"/admin/menus\""));
  /* CONTROL: once granted, the same pages carry them. */
  GrantTemplateKinds(id_, {"core.pane"});
  GrantMenus();
  menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, "href=\"/admin/templates\">Edit templates")) << menu;
  EXPECT_TRUE(Has(menu, "href=\"/admin/menus\">Menus")) << menu;
  /* Every page has it, and it is the same menu with the page marked. */
  Reply more = Get("/admin/more", cookie_);
  EXPECT_TRUE(Has(MenuOf(more.body), "<li class=\"depth1 current\"><a "
                                     "href=\"/admin/more\">More"))
      << MenuOf(more.body);
  EXPECT_TRUE(Has(more.body, "<h2>core</h2>")) << "the directory is the menu, grouped by module";
  EXPECT_FALSE(Has(WithoutSignout(more.body), "<form"));
  EXPECT_TRUE(ScansClean(home.body));
  ExpectSecurity(home);
}

TEST_F(NavAdmin, BreadcrumbsAndTabsAreDrawnForTheTemplatePage) {
  Open();
  GrantTemplateKinds(id_, {"core.pane"});
  std::string home = Get("/admin", cookie_).body;
  /* A page with a trail of one and no tabs has neither. */
  EXPECT_FALSE(Has(home, "admin-crumbs"));
  EXPECT_FALSE(Has(home, "admin-tabs"));
  std::string edit = Get("/admin/templates/recent", cookie_).body;
  EXPECT_TRUE(Has(edit, "<nav class=\"admin-crumbs\"><ol class=\"breadcrumbs\">")) << edit;
  EXPECT_TRUE(Has(edit, "<a href=\"/admin\">Admin</a>"));
  EXPECT_TRUE(Has(edit, "<a href=\"/admin/templates\">Edit templates</a>"));
  EXPECT_TRUE(Has(edit, "<nav class=\"admin-tabs\"><ul class=\"tabs\">"));
  EXPECT_TRUE(Has(edit, "<li class=\"current\"><a href=\"/admin/templates/recent\">Edit</a></li>"))
      << edit;
  EXPECT_TRUE(Has(edit, "<li><a href=\"/admin/templates/recent/history\">History</a></li>"));
  std::string history = Get("/admin/templates/recent/history", cookie_).body;
  EXPECT_TRUE(Has(history, "<li><a href=\"/admin/templates/recent\">Edit</a></li>")) << history;
  EXPECT_TRUE(Has(history, "<li class=\"current\"><a href=\"/admin/templates/recent/history\">"
                           "History</a></li>"))
      << history;
  /* A name that needs encoding keeps its encoding in the tabs. */
  EXPECT_TRUE(ScansClean(edit));
  EXPECT_TRUE(ScansClean(history));
}

TEST_F(NavAdmin, TheMenusPageIsThereOnlyForWhoMayFollowItsRoute) {
  Open();
  Reply none = Get("/admin/nothing", cookie_);
  Reply refused = Get("/admin/menus", cookie_);
  EXPECT_EQ(none.status, refused.status);
  EXPECT_EQ(none.body, refused.body);
  Reply post_none = PostRaw("/admin/nothing", cookie_, "token=x");
  Reply post_refused = PostRaw("/admin/menus", cookie_, "token=x&name=core.l-admin-undo&hidden=hide");
  EXPECT_EQ(post_none.status, post_refused.status);
  EXPECT_EQ(post_none.body, post_refused.body);
  /* CONTROL: granted, the same URL is a page. */
  GrantMenus();
  Reply page = Get("/admin/menus", cookie_);
  EXPECT_EQ(200, page.status);
  EXPECT_NE(none.body, page.body);
  EXPECT_TRUE(Has(page.body, "<h1>Menus</h1>"));
  EXPECT_TRUE(Has(page.body, "<td>core.l-admin-undo</td>"));
  EXPECT_TRUE(Has(page.body, "<form method=\"post\" action=\"/admin/menus\">"));
  EXPECT_FALSE(Has(page.body, "<style"));
  EXPECT_FALSE(Has(page.body, " style="));
  EXPECT_FALSE(Has(page.body, "<script>"));
  EXPECT_TRUE(ScansClean(page.body)) << page.body;
  ExpectSecurity(page);
  /* Not the visitor, nor an account with no role. */
  AddUser(id_, "bob", "hunter2");
  Reply bob = Login("bob", "hunter2");
  for (const std::string & who : {std::string(), CookieLine(bob)}) {
    Reply got = Get("/admin/menus", who);
    Reply unknown = Get("/no-such-path", who);
    EXPECT_EQ(unknown.status, got.status);
    EXPECT_EQ(unknown.body, got.body);
  }
}

TEST_F(NavAdmin, HidingAnEntryRemovesItFromTheMenuAndNotThePage) {
  Open();
  GrantMenus();
  std::string page = MenusPage();
  EXPECT_TRUE(Has(MenuOf(Get("/admin/more", cookie_).body), "href=\"/admin/undo\""));
  /* Hide the undo link: the first row's override form is the same form for
   * every generated entry, and the name says which entry. */
  Reply done = PostMenus(FormToken(page, ">Change the menu</button>"),
      "&name=core.l-admin-undo&hidden=hide&label=&weight=&parent=&menu=");
  EXPECT_EQ(200, done.status);
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  EXPECT_FALSE(Has(MenuOf(Get("/admin/more", cookie_).body), "href=\"/admin/undo\""));
  /* Hiding a link is not denying the page: it is served. */
  Reply undo = Get("/admin/undo", cookie_);
  EXPECT_EQ(200, undo.status);
  EXPECT_TRUE(Has(undo.body, "<form"));
  /* The menus page still lists it, marked hidden and changed. */
  page = MenusPage();
  EXPECT_TRUE(Has(page, "hidden")) << page;
  EXPECT_TRUE(Has(page, ">Reset</button>"));
  /* Reset brings it back. */
  std::string reset = FormToken(page, ">Reset</button>");
  ASSERT_FALSE(reset.empty());
  done = PostMenus(reset, "&name=core.l-admin-undo");
  EXPECT_EQ(200, done.status);
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
}

TEST_F(NavAdmin, RenamingAndReweightingKeepsWhatWasSetBefore) {
  Open();
  GrantMenus();
  std::string page = MenusPage();
  std::string token = FormToken(page, ">Change the menu</button>");
  PostMenus(token, "&name=core.l-admin-more&label=Elsewhere&hidden=&menu=");
  std::string menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, "href=\"/admin/more\">Elsewhere")) << menu;
  /* A second change of another field leaves the first in place: the override
   * is what is set, and the form sets only what is filled in. A weight or a
   * parent in the post is not read: where an entry stands is the moves'. */
  PostMenus(token, "&name=core.l-admin-more&label=&weight=-50&parent=core.l-admin-post&hidden=hide&menu=");
  menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_FALSE(Has(menu, "href=\"/admin/more\"")) << "hidden now";
  EXPECT_TRUE(Has(MenusPage(), "Elsewhere</td>")) << "and still called what it was set to";
  /* Only the changed fields are stored: the label and the hidden flag, and not
   * a parent, a menu or a weight. */
  GDEFCORE_Catalog * catalog = Catalog();
  char ** ids = NULL;
  size_t n = 0;
  size_t live = 0;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_ids(catalog, GDEFCORE_MENU_OVERRIDE_TYPE,
                             strlen(GDEFCORE_MENU_OVERRIDE_TYPE), &ids, &n));
  for (size_t i = 0; i < n; i++) {
    char ** t = NULL;
    size_t * l = NULL;
    size_t c = 0;
    if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
            strlen(ids[i]), "en", 2, "name", 4, &t, &l, &c) != GDEFCORE_OK || c == 0) {
      continue;
    }
    gdefcore_values_free(catalog, t, l, c);
    live += 1;
    for (const char * field : {"parent", "menu", "weight"}) {
      t = NULL;
      l = NULL;
      c = 0;
      GDEFCORE_Result got = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
          ids[i], strlen(ids[i]), "en", 2, field, strlen(field), &t, &l, &c);
      /* A field with no value is a miss. */
      EXPECT_TRUE(got == GDEFCORE_ERR_INVALID || (got == GDEFCORE_OK && c == 0))
          << field << " is not stored";
      if (got == GDEFCORE_OK) {
        gdefcore_values_free(catalog, t, l, c);
      }
    }
    for (const char * field : {"label", "hidden"}) {
      GDEFCORE_Result got = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
          ids[i], strlen(ids[i]), "en", 2, field, strlen(field), &t, &l, &c);
      EXPECT_TRUE(got == GDEFCORE_OK && c == 1) << field << " is stored";
      if (got == GDEFCORE_OK) {
        gdefcore_values_free(catalog, t, l, c);
      }
    }
  }
  EXPECT_EQ(1u, live);
  gdefcore_ids_free(catalog, ids, n);
  gdefcore_catalog_destroy(catalog);
}

TEST_F(NavAdmin, ASiteEntryIsAddedChangedAndRemoved) {
  Open();
  GrantMenus();
  std::string page = MenusPage();
  PostMenus(FormToken(page, ">Add</button>"),
      "&action=add" + Ver(page) +
          "&suffix=docs&menu=admin&label=Docs&route=core.admin-more&parent=core.l-admin");
  std::string menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, "<a href=\"/admin/more\">Docs</a>")) << menu;
  page = MenusPage();
  EXPECT_TRUE(Has(page, "<td>site.docs</td>"));
  /* Edited as it is. */
  PostMenus(FormToken(page, ">Save</button>"),
      "&name=site.docs&label=Manual&menu=admin&route=core.admin-more&parent=core.l-admin&weight=7");
  menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, ">Manual</a>")) << menu;
  EXPECT_FALSE(Has(menu, ">Docs</a>"));
  /* Removed. */
  page = MenusPage();
  PostMenus(FormToken(page, ">Remove</button>"), "&action=remove" + Ver(page) + "&name=site.docs");
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "Manual"));
  /* A label with markup in it is shown as text, in the menu and on the page. */
  page = MenusPage();
  PostMenus(FormToken(page, ">Add</button>"),
      "&action=add" + Ver(page) + "&suffix=evil&menu=admin&label=" +
          Enc("<script>alert(1)</script>") + "&route=core.admin-more&parent=");
  EXPECT_FALSE(Has(Get("/admin", cookie_).body, "<script>alert"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "&lt;script&gt;alert(1)&lt;/script&gt;"));
  EXPECT_FALSE(Has(MenusPage(), "<script>alert"));
  EXPECT_TRUE(Has(MenusPage(), "&lt;script&gt;"));
}

TEST_F(NavAdmin, WhatThePermissionDoesNotCoverIsRefusedAndSaid) {
  Open();
  /* May edit entries, and not overrides. */
  GrantMenus(true, false);
  std::string page = MenusPage();
  EXPECT_EQ(200, Get("/admin/menus", cookie_).status);
  Reply done = PostMenus(FormToken(page, ">Change the menu</button>"),
      "&name=core.l-admin-undo&hidden=hide&label=&weight=&parent=&menu=");
  EXPECT_EQ(200, done.status);
  EXPECT_TRUE(Has(done.body, "That change was not made.")) << done.body;
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  /* CONTROL: an entry, which it does cover, is made. */
  PostMenus(FormToken(page, ">Add</button>"),
      "&action=add" + Ver(page) + "&suffix=ok&menu=admin&label=Fine&route=core.admin-more&parent=");
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), ">Fine</a>"));
}

TEST_F(NavAdmin, AFormNotSignedForThisWritesNothing) {
  Open();
  GrantMenus();
  std::string page = MenusPage();
  std::string token = FormToken(page, ">Change the menu</button>");
  const std::string fields =
      "&name=core.l-admin-undo&hidden=hide&label=&weight=&parent=&menu=";
  /* No token, a damaged one, and one for another login: each leaves the menu
   * as it was. */
  auto undo_link = [&]() {
    return Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\"");
  };
  PostRaw("/admin/menus", cookie_, fields.substr(1));
  EXPECT_TRUE(undo_link()) << "no token";
  {
    std::string damaged = token;
    damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
    PostRaw("/admin/menus", cookie_, "token=" + damaged + fields);
  }
  EXPECT_TRUE(undo_link()) << "a damaged token";
  PostRaw("/admin/menus", cookie_, "token=not-a-token" + fields);
  EXPECT_TRUE(undo_link()) << "a token that is not one";
  Reply other = Login("ada", "secret");
  std::string other_cookie = CookieLine(other);
  PostRaw("/admin/menus", other_cookie, "token=" + token + fields);
  EXPECT_TRUE(undo_link()) << "another login's cookie";
  /* A token for one purpose does not do another's: the reset token is not an
   * override. */
  PostRaw("/admin/menus", cookie_, "token=" + FormToken(MenusPage(), ">Add</button>") +
      "&name=core.l-admin-undo&hidden=hide");
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  /* A cross-origin post is refused before it is read. */
  EXPECT_EQ(403, PostRaw("/admin/menus", cookie_, "token=" + token + fields,
                     "http://evil.example")
                     .status);
  /* CONTROL: the right token, in the right session, does it. */
  PostRaw("/admin/menus", cookie_, "token=" + token + fields);
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
}

TEST_F(NavAdmin, ASiteRestylesTheAdminMenuLikeAnyTemplate) {
  Open();
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "<ul class=\"menu\">"));
  /* The menu is drawn by the stored template `menu`: changed, the pages follow. */
  GDEFCORE_Catalog * catalog = Catalog();
  char ** ids = NULL;
  size_t n = 0;
  std::string id;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_ids(catalog, "core.template", 13, &ids, &n));
  for (size_t i = 0; i < n && id.empty(); i++) {
    char ** t = NULL;
    size_t * l = NULL;
    size_t c = 0;
    if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
            strlen(ids[i]), "en", 2, "name", 4, &t, &l, &c) == GDEFCORE_OK) {
      if (c > 0 && std::string(t[0], l[0]) == "menu") {
        id = ids[i];
      }
      gdefcore_values_free(catalog, t, l, c);
    }
  }
  gdefcore_ids_free(catalog, ids, n);
  ASSERT_FALSE(id.empty());
  {
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Value value;
    static const char kBody[] =
        "<% use items; use url; %><div class=\"mine\"><% for (i : items) { %>"
        "<a href=\"<%= url(i.href) %>\"><%= i.label %></a><% } %></div>";
    const char * slot = kBody;
    size_t len = sizeof kBody - 1;
    memset(&value, 0, sizeof value);
    value.size = sizeof value;
    value.field = "body";
    value.field_len = 4;
    value.texts = &slot;
    value.text_lens = &len;
    value.count = 1;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_save(cs, id.c_str(), id.size(), "en", 2,
                               &value, 1));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "core.template", 13));
    gdefcore_changeset_destroy(cs);
  }
  gdefcore_catalog_destroy(catalog);
  std::string menu = MenuOf(Get("/admin/more", cookie_).body);
  EXPECT_TRUE(Has(menu, "<div class=\"mine\"><a href=\"/admin\">Admin</a><a href=\"/admin/post\">"))
      << menu;
  EXPECT_FALSE(Has(menu, "<ul class=\"menu\">"));
}

TEST_F(NavAdmin, TheVisitorIsShownNoAdminEntryAnywhere) {
  Open();
  GrantMenus();
  GrantTemplateKinds(id_, {"core.pane"});
  std::string page = MenusPage();
  PostMenus(FormToken(page, ">Add</button>"),
      "&action=add" + Ver(page) +
          "&suffix=public&menu=main&label=Public&route=core.admin-more&parent=");
  /* The front page, a post and the sign-in page carry no admin link, with
   * the entry and the permissions all in place. */
  for (const char * path : {"/", "/hello-title", "/login", "/no-such"}) {
    std::string body = Visit(path);
    EXPECT_FALSE(Has(body, "/admin")) << path << "\n" << body;
    EXPECT_FALSE(Has(body, "Write a post")) << path;
    EXPECT_FALSE(Has(body, "class=\"menu\"")) << path;
  }
  /* The admin pages themselves are not there for a visitor. */
  Reply unknown = Get("/no-such-path", std::string());
  for (const char * path : {"/admin", "/admin/more", "/admin/menus", "/admin/templates",
           "/admin/templates/recent", "/admin/access"}) {
    Reply got = Get(path, std::string());
    EXPECT_EQ(unknown.status, got.status) << path;
    EXPECT_EQ(unknown.body, got.body) << path;
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
