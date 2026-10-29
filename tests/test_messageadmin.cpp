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
 * The admin directory and the messages (site admin story 9), through the real
 * server: the More screen is the admin menu grouped by the module that declared
 * each entry, with an entry the viewer's own route check refuses absent; a save
 * answers with a redirect and leaves one message, which the next page the same
 * session is shown says once, escaped, and no different session is shown; and a
 * public page that places the messages widget shows it to the session too.
 *
 * Every refusal has a control in the same test (the granted viewer, the first
 * page, the session it was left).
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-core/message.h>
#include <ghoti.io/defiant-core/settings.h>
#include <ghoti.io/defiant-test/defiant-test.h>

namespace {

#define MSGADM_STR2(x) #x
#define MSGADM_STR(x) MSGADM_STR2(x)
const char kToken[] = MSGADM_STR(GHOTIIO_DEFCORE_NAME);

void NoRoute(const GDEFCORE_Route_Call * call) { (void)call; }

/* The module `blog`: a page of its own that anyone in the admin area may
 * follow, a settings page that needs the permission to edit settings, and a
 * number setting whose label is hostile. */
struct Blog {
  GDEFCORE_Module_Manifest man;
  GDEFCORE_Module_Route_Handler handler;
  GDEFCORE_Module_Service service;
  GDEFCORE_Module_Route routes[3];
  GDEFCORE_Module_Link links[2];
  GDEFCORE_Module_Setting setting;

  static void Str(const char ** p, size_t * n, const char * s) {
    *p = s;
    *n = strlen(s);
  }

  void Build() {
    memset(&man, 0, sizeof man);
    memset(&handler, 0, sizeof handler);
    memset(&service, 0, sizeof service);
    memset(routes, 0, sizeof routes);
    memset(links, 0, sizeof links);
    memset(&setting, 0, sizeof setting);
    handler.size = sizeof handler;
    handler.call_fn = NoRoute;
    service.size = sizeof service;
    service.name = "blog.route";
    service.name_len = 10;
    service.version = GDEFCORE_MODULE_ROUTE_HANDLER_VERSION;
    service.table = &handler;
    for (int i = 0; i < 3; i++) {
      routes[i].size = sizeof routes[i];
      routes[i].handler = "blog.route";
      routes[i].handler_len = 10;
    }
    Str(&routes[0].name, &routes[0].name_len, "blog.index");
    Str(&routes[0].path, &routes[0].path_len, "/admin/blog");
    Str(&routes[0].title, &routes[0].title_len, "Blog");
    routes[0].kind = GDEFCORE_ROUTE_PAGE;
    Str(&routes[1].name, &routes[1].name_len, "blog.settings");
    Str(&routes[1].path, &routes[1].path_len, "/admin/settings/blog");
    Str(&routes[1].title, &routes[1].title_len, "Blog settings");
    routes[1].kind = GDEFCORE_ROUTE_PAGE;
    Str(&routes[2].name, &routes[2].name_len, "blog.settings-general");
    Str(&routes[2].path, &routes[2].path_len, "/admin/settings/blog/general");
    Str(&routes[2].title, &routes[2].title_len, "General");
    routes[2].kind = GDEFCORE_ROUTE_TAB;
    Str(&routes[2].parent, &routes[2].parent_len, "blog.settings");
    routes[2].is_default = 1;
    for (int i = 1; i < 3; i++) {
      routes[i].permission = GDEFCORE_SETTINGS_EDIT;
      routes[i].permission_len = strlen(GDEFCORE_SETTINGS_EDIT);
    }
    for (int i = 0; i < 2; i++) {
      links[i].size = sizeof links[i];
      Str(&links[i].menu, &links[i].menu_len, "admin");
      links[i].kind = GDEFCORE_ROUTE_PAGE;
    }
    Str(&links[0].name, &links[0].name_len, "blog.l-index");
    Str(&links[0].label, &links[0].label_len, "Blog <b>tools</b>");
    Str(&links[0].route, &links[0].route_len, "blog.index");
    links[0].weight = 7;
    Str(&links[1].name, &links[1].name_len, "blog.l-settings");
    Str(&links[1].label, &links[1].label_len, "Blog settings");
    Str(&links[1].route, &links[1].route_len, "blog.settings");
    links[1].weight = 8;
    setting.size = sizeof setting;
    Str(&setting.name, &setting.name_len, "blog.limit");
    Str(&setting.type, &setting.type_len, "number");
    Str(&setting.default_value, &setting.default_len, "5");
    Str(&setting.label, &setting.label_len, "Limit <script>x</script>");
    Str(&setting.group, &setting.group_len, "general");
    Str(&setting.minimum, &setting.minimum_len, "1");
    Str(&setting.maximum, &setting.maximum_len, "10");
    man.size = sizeof man;
    man.core_token = kToken;
    man.core_token_len = strlen(kToken);
    man.name = "blog";
    man.name_len = 4;
    man.schema_version = 1;
    man.services = &service;
    man.nservices = 1;
    man.routes = routes;
    man.nroutes = 3;
    man.links = links;
    man.nlinks = 2;
    man.settings = &setting;
    man.nsettings = 1;
  }
};

Blog g_blog;

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

/* A page as a browser reads it: digits written as character references are the
 * digits. */
std::string Decoded(const std::string & html) {
  std::string out;
  for (size_t i = 0; i < html.size();) {
    if (html.compare(i, 2, "&#") == 0 && i + 5 <= html.size() && html[i + 4] == ';') {
      int code = atoi(html.substr(i + 2, 2).c_str());
      if (code >= 48 && code <= 57) {
        out += (char)code;
        i += 5;
        continue;
      }
    }
    out += html[i++];
  }
  return out;
}

extern "C" const GDEFCORE_Module_Manifest * BlogEntry(void) { return &g_blog.man; }

class MessageAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string cookie_;

  void Open() {
    Site(id_);
    UseHost();
    g_blog = Blog();
    g_blog.Build();
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, BlogEntry));
    cookie_ = CookieLine(Login("ada", "secret"));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  void Switch(bool on) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, on ? gdefcore_module_enable(host_, cs, "blog", 4)
                              : gdefcore_module_disable(host_, cs, "blog", 4));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "blog", 4));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  void GrantSettings() {
    GDEFCORE_Catalog * catalog = Catalog();
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_SETTINGS_EDIT_LABEL,
            strlen(GDEFCORE_SETTINGS_EDIT_LABEL), "core.role", 9));
    gdefcore_catalog_destroy(catalog);
  }

  /* The front page draws the messages widget, as a site that wants it shown does. */
  void PlaceMessages() {
    GDEFCORE_Catalog * catalog = Catalog();
    char ** ids = NULL;
    size_t n = 0;
    std::string front;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_ids(catalog, "core.display", 12, &ids, &n));
    for (size_t i = 0; i < n && front.empty(); i++) {
      char ** t = NULL;
      size_t * l = NULL;
      size_t c = 0;
      if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
              strlen(ids[i]), "en", 2, "name", 4, &t, &l, &c) == GDEFCORE_OK) {
        if (c > 0 && std::string(t[0], l[0]) == "front") {
          front = ids[i];
        }
        gdefcore_values_free(catalog, t, l, c);
      }
    }
    gdefcore_ids_free(catalog, ids, n);
    ASSERT_FALSE(front.empty());
    const char * panes[2] = {"main messages", "side recent"};
    size_t lens[2] = {strlen(panes[0]), strlen(panes[1])};
    GDEFCORE_Value v;
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    v.field = "panes";
    v.field_len = 5;
    v.texts = panes;
    v.text_lens = lens;
    v.count = 2;
    GDEFCORE_Changeset * cs = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_save(cs, front.data(), front.size(), "en", 2, &v, 1));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "core.display", 12));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  /* The part of the More screen after its heading: the directory itself. */
  std::string MoreOf(const std::string & cookie) {
    std::string page = Get("/admin/more", cookie).body;
    size_t at = page.find("<h1>More</h1>");
    return at == std::string::npos ? std::string("<no directory>") : page.substr(at);
  }

  std::string Body(const std::string & page, const std::string & fields) {
    return "token=" + InputValue(page, "token", 0) + "&version=" +
           InputValue(page, "version", 0) + fields;
  }

  static bool Said(const std::string & page, const char * kind, const char * text) {
    return page.find(std::string("<div class=\"message ") + kind + "\">" + text + "</div>") !=
           std::string::npos;
  }
};

}  // namespace

TEST_F(MessageAdmin, TheMoreScreenIsTheAdminMenuGroupedByModuleAndEntriesTheViewerMayNotFollowAreAbsent) {
  Open();
  std::string more = MoreOf(cookie_);
  /* The kernel's own pages, under its heading; what needs a permission nobody
   * granted is not there at all, not greyed. */
  EXPECT_TRUE(Has(more, "<h2>core</h2>")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/post\">Write a post</a>")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/undo\">Undo</a>")) << more;
  EXPECT_FALSE(Has(more, "href=\"/admin/settings\"")) << more;
  EXPECT_FALSE(Has(more, "href=\"/admin/menus\"")) << more;
  EXPECT_FALSE(Has(more, "href=\"/admin/templates\"")) << more;
  EXPECT_FALSE(Has(more, "href=\"/admin/more\"")) << "the page itself is not offered to its reader";
  EXPECT_FALSE(Has(more, "blog")) << "a module that is not enabled has no heading";
  EXPECT_FALSE(Has(more, "Server")) << "no heading of the kernel's own invention";
  /* A module enabled brings its heading, with no change to the kernel: its
   * entries, escaped, and only the ones its routes let this viewer follow. */
  Switch(true);
  more = MoreOf(cookie_);
  EXPECT_TRUE(Has(more, "<h2>blog</h2>")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/blog\">Blog &lt;b&gt;tools&lt;/b&gt;</a>")) << more;
  EXPECT_FALSE(Has(more, "<b>tools</b>")) << more;
  EXPECT_FALSE(Has(more, "Blog settings")) << "its settings page needs a permission nobody granted";
  EXPECT_FALSE(Has(more, "href=\"/admin/settings\"")) << more;
  EXPECT_LT(more.find("<h2>core</h2>"), more.find("<h2>blog</h2>")) << "the kernel first";
  EXPECT_TRUE(ScansClean(more));
  /* CONTROL: granted, the settings entries are there, the kernel's and the module's. */
  GrantSettings();
  more = MoreOf(cookie_);
  EXPECT_TRUE(Has(more, "<a href=\"/admin/settings\">Settings</a>")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/settings/blog\">Blog settings</a>")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/blog\">Blog &lt;b&gt;tools&lt;/b&gt;</a>")) << more;
  /* Disabled, the module takes its heading with it. */
  Switch(false);
  more = MoreOf(cookie_);
  EXPECT_FALSE(Has(more, "blog")) << more;
  EXPECT_TRUE(Has(more, "<a href=\"/admin/settings\">Settings</a>")) << more;
  /* Not for a visitor, and not for an account that holds no role. */
  Reply visitor = Get("/admin/more", std::string());
  Reply unknown = Get("/no-such-path", std::string());
  EXPECT_EQ(unknown.status, visitor.status);
  EXPECT_EQ(unknown.body, visitor.body);
}

TEST_F(MessageAdmin, ASaveAnswersWithARedirectAndLeavesOneMessageShownOnceToTheSessionThatSaved) {
  Open();
  GrantSettings();
  const char * path = "/admin/settings/core/site";
  std::string page = Get(path, cookie_).body;
  EXPECT_FALSE(Has(page, "admin-messages")) << "nothing yet";
  Reply sent = PostRaw(path, cookie_, Body(page, "&s.core.site_title=Gadget&s.core.tagline="));
  EXPECT_EQ(303, sent.status);
  EXPECT_EQ(path, HeaderValue(sent, "Location"));
  EXPECT_TRUE(sent.body.empty());
  /* A second session of the same account, a visitor, and another page of the
   * first session's: the message waits for the page it was left for. */
  Reply other = Login("ada", "secret");
  std::string other_cookie = CookieLine(other);
  ASSERT_NE(cookie_, other_cookie);
  EXPECT_FALSE(Has(Get(path, other_cookie).body, "message")) << "a different session";
  EXPECT_FALSE(Has(Visit(path), "Saved."));
  EXPECT_FALSE(Has(Visit("/"), "Saved."));
  /* The session that saved is shown it, in the page the redirect names. */
  Reply landed = Get(path, cookie_);
  EXPECT_TRUE(Said(landed.body, "status", "Saved.")) << landed.body;
  EXPECT_TRUE(Has(landed.body, "<div class=\"admin-messages\">")) << landed.body;
  EXPECT_LT(landed.body.find("admin-messages"), landed.body.find("admin-menu"));
  EXPECT_TRUE(ScansClean(landed.body));
  ExpectSecurity(landed);
  /* Once: a refresh, the next page, the page after. */
  for (const char * where : {path, "/admin", "/admin/more", path}) {
    EXPECT_FALSE(Has(Get(where, cookie_).body, "Saved.")) << where;
    EXPECT_FALSE(Has(Get(where, cookie_).body, "admin-messages")) << where;
  }
  /* Saving what is there says so, and says it once too. */
  page = Get(path, cookie_).body;
  sent = PostRaw(path, cookie_, Body(page, "&s.core.site_title=Gadget&s.core.tagline="));
  EXPECT_EQ(303, sent.status);
  EXPECT_TRUE(Said(Get(path, cookie_).body, "status", "Nothing changed."));
  EXPECT_FALSE(Has(Get(path, cookie_).body, "Nothing changed."));
}

TEST_F(MessageAdmin, ARefusalAndAnErrorAreMessagesToo) {
  Open();
  Switch(true);
  const char * path = "/admin/settings/blog/general";
  /* Before the permission is granted the page is one that does not exist, a
   * post to it is told so, and it leaves no message. */
  Reply post_none = PostRaw("/admin/nothing", cookie_, "token=x&s.blog.limit=2");
  Reply refused = PostRaw(path, cookie_, "token=x&s.blog.limit=2");
  EXPECT_EQ(post_none.status, refused.status);
  EXPECT_EQ(post_none.body, refused.body);
  EXPECT_FALSE(Has(Get("/admin", cookie_).body, "admin-messages"));
  GrantSettings();
  std::string page = Get(path, cookie_).body;
  /* A value the core refuses: its words, with the setting's label, escaped. */
  Reply sent = PostRaw(path, cookie_, Body(page, "&s.blog.limit=500"));
  EXPECT_EQ(303, sent.status);
  std::string landed = Get(path, cookie_).body;
  EXPECT_TRUE(Has(landed, "<div class=\"message error\">")) << landed;
  EXPECT_TRUE(Has(landed, "Limit &lt;script&gt;x&lt;/script&gt; is above the highest value allowed")) << landed;
  EXPECT_FALSE(Has(landed, "<script>x")) << "message text is never markup";
  EXPECT_TRUE(Has(Decoded(landed), "value=\"5\"")) << "nothing was written";
  EXPECT_FALSE(Has(Get(path, cookie_).body, "highest value allowed"));
  /* A form that is out of date, and one that was never signed. */
  Reply a = PostRaw(path, cookie_, Body(page, "&s.blog.limit=6"));
  EXPECT_EQ(303, a.status);
  EXPECT_TRUE(Said(Get(path, cookie_).body, "status", "Saved."));
  Reply stale = PostRaw(path, cookie_, Body(page, "&s.blog.limit=7"));
  EXPECT_EQ(303, stale.status);
  std::string after = Get(path, cookie_).body;
  EXPECT_TRUE(Has(after, "<div class=\"message error\">")) << after;
  EXPECT_TRUE(Has(after, "changed since the form was drawn")) << after;
  Reply unsigned_post = PostRaw(path, cookie_, "s.blog.limit=8");
  EXPECT_EQ(303, unsigned_post.status);
  EXPECT_TRUE(Said(Get(path, cookie_).body, "error", "That form is out of date. Nothing was saved."));
  /* CONTROL: the fresh form saves. */
  page = Get(path, cookie_).body;
  PostRaw(path, cookie_, Body(page, "&s.blog.limit=9"));
  EXPECT_TRUE(Said(Get(path, cookie_).body, "status", "Saved."));
}

TEST_F(MessageAdmin, APublicPageThatPlacesTheWidgetShowsTheSessionItsMessageOnce) {
  Open();
  GrantSettings();
  PlaceMessages();
  const char * path = "/admin/settings/core/site";
  std::string page = Get(path, cookie_).body;
  PostRaw(path, cookie_, Body(page, "&s.core.site_title=Gadget&s.core.tagline=Words"));
  /* A visitor, and a different session, are shown nothing of it, and do not use it up. */
  EXPECT_FALSE(Has(Visit("/"), "Saved."));
  EXPECT_FALSE(Has(Get("/", CookieLine(Login("ada", "secret"))).body, "Saved."));
  Reply front = Get("/", cookie_);
  EXPECT_TRUE(Said(front.body, "status", "Saved.")) << front.body;
  EXPECT_FALSE(Has(Get("/", cookie_).body, "Saved."));
  EXPECT_FALSE(Has(Get(path, cookie_).body, "Saved.")) << "it was shown, so it is gone";
  /* A page that draws a queue is not one a visitor is handed. */
  EXPECT_FALSE(Has(Visit("/"), "class=\"message"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
