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
 * The settings pages (site admin story 8), through the real server: the core
 * generates the form of a group, the server draws the page around it, signs it
 * and reads the post; a module places its settings page and its tabs in the
 * admin menu with routes of its own; a changed title shows on the next public
 * request; and every refusal has a control in the same test.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-core/settings.h>
#include <ghoti.io/defiant-test/defiant-test.h>

namespace {

#define SETADM_STR2(x) #x
#define SETADM_STR(x) SETADM_STR2(x)
const char kToken[] = SETADM_STR(GHOTIIO_DEFCORE_NAME);
const char kSecretKey[] = "sk_live_TOPSECRET_web";

void NoRoute(const GDEFCORE_Route_Call * call) { (void)call; }

/* The module `shop`: a settings page at /admin/settings/shop with a tab for its
 * limits and one for its keys, a link in the admin menu, and settings in three
 * groups, one of which (extras) has no page. */
struct Shop {
  GDEFCORE_Module_Manifest man;
  GDEFCORE_Module_Route_Handler handler;
  GDEFCORE_Module_Service service;
  GDEFCORE_Module_Route routes[3];
  GDEFCORE_Module_Link link;
  GDEFCORE_Module_Setting settings[5];

  static void Str(const char ** p, size_t * n, const char * s) {
    *p = s;
    *n = strlen(s);
  }

  void Build() {
    memset(&man, 0, sizeof man);
    memset(&handler, 0, sizeof handler);
    memset(&service, 0, sizeof service);
    memset(routes, 0, sizeof routes);
    memset(&link, 0, sizeof link);
    memset(settings, 0, sizeof settings);
    handler.size = sizeof handler;
    handler.call_fn = NoRoute;
    service.size = sizeof service;
    service.name = "shop.route";
    service.name_len = 10;
    service.version = GDEFCORE_MODULE_ROUTE_HANDLER_VERSION;
    service.table = &handler;
    for (int i = 0; i < 3; i++) {
      routes[i].size = sizeof routes[i];
      routes[i].handler = "shop.route";
      routes[i].handler_len = 10;
      routes[i].permission = GDEFCORE_SETTINGS_EDIT;
      routes[i].permission_len = strlen(GDEFCORE_SETTINGS_EDIT);
    }
    Str(&routes[0].name, &routes[0].name_len, "shop.settings");
    Str(&routes[0].path, &routes[0].path_len, "/admin/settings/shop");
    Str(&routes[0].title, &routes[0].title_len, "Shop settings");
    routes[0].kind = GDEFCORE_ROUTE_PAGE;
    Str(&routes[1].name, &routes[1].name_len, "shop.settings-limits");
    Str(&routes[1].path, &routes[1].path_len, "/admin/settings/shop/limits");
    Str(&routes[1].title, &routes[1].title_len, "Limits");
    routes[1].kind = GDEFCORE_ROUTE_TAB;
    Str(&routes[1].parent, &routes[1].parent_len, "shop.settings");
    routes[1].is_default = 1;
    Str(&routes[2].name, &routes[2].name_len, "shop.settings-keys");
    Str(&routes[2].path, &routes[2].path_len, "/admin/settings/shop/keys");
    Str(&routes[2].title, &routes[2].title_len, "Keys");
    routes[2].kind = GDEFCORE_ROUTE_TAB;
    Str(&routes[2].parent, &routes[2].parent_len, "shop.settings");
    routes[2].weight = 1;
    link.size = sizeof link;
    Str(&link.name, &link.name_len, "shop.l-settings");
    Str(&link.menu, &link.menu_len, "admin");
    Str(&link.label, &link.label_len, "Shop settings");
    Str(&link.route, &link.route_len, "shop.settings");
    link.weight = 6;
    link.kind = GDEFCORE_ROUTE_PAGE;
    for (int i = 0; i < 5; i++) {
      settings[i].size = sizeof settings[i];
    }
    Str(&settings[0].name, &settings[0].name_len, "shop.limit");
    Str(&settings[0].type, &settings[0].type_len, "number");
    Str(&settings[0].default_value, &settings[0].default_len, "10");
    Str(&settings[0].label, &settings[0].label_len, "Order limit");
    Str(&settings[0].description, &settings[0].description_len, "How many a customer may order.");
    Str(&settings[0].group, &settings[0].group_len, "limits");
    Str(&settings[0].minimum, &settings[0].minimum_len, "1");
    Str(&settings[0].maximum, &settings[0].maximum_len, "100");
    Str(&settings[1].name, &settings[1].name_len, "shop.sale");
    Str(&settings[1].type, &settings[1].type_len, "boolean");
    Str(&settings[1].default_value, &settings[1].default_len, "false");
    Str(&settings[1].label, &settings[1].label_len, "On sale");
    Str(&settings[1].group, &settings[1].group_len, "limits");
    Str(&settings[2].name, &settings[2].name_len, "shop.api_key");
    Str(&settings[2].type, &settings[2].type_len, "text");
    Str(&settings[2].label, &settings[2].label_len, "API key");
    Str(&settings[2].group, &settings[2].group_len, "keys");
    settings[2].environment_local = 1;
    settings[2].secret = 1;
    Str(&settings[3].name, &settings[3].name_len, "shop.banner");
    Str(&settings[3].type, &settings[3].type_len, "text");
    Str(&settings[3].label, &settings[3].label_len, "Banner");
    Str(&settings[3].group, &settings[3].group_len, "keys");
    settings[3].environment_local = 1;
    Str(&settings[4].name, &settings[4].name_len, "shop.motto");
    Str(&settings[4].type, &settings[4].type_len, "text");
    Str(&settings[4].default_value, &settings[4].default_len, "none");
    Str(&settings[4].label, &settings[4].label_len, "Motto");
    Str(&settings[4].group, &settings[4].group_len, "extras");
    man.size = sizeof man;
    man.core_token = kToken;
    man.core_token_len = strlen(kToken);
    man.name = "shop";
    man.name_len = 4;
    man.schema_version = 1;
    man.services = &service;
    man.nservices = 1;
    man.routes = routes;
    man.nroutes = 3;
    man.links = &link;
    man.nlinks = 1;
    man.settings = settings;
    man.nsettings = 5;
  }
};

Shop g_shop;

extern "C" const GDEFCORE_Module_Manifest * ShopEntry(void) { return &g_shop.man; }

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

class SettingsAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string cookie_;

  void Open() {
    Site(id_);
    UseHost();
    g_shop = Shop();
    g_shop.Build();
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, ShopEntry));
    cookie_ = CookieLine(Login("ada", "secret"));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  void EnableShop() {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, "shop", 4));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "shop", 4));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  /* The admin role granted the permission to edit settings. */
  void GrantSettings() {
    GDEFCORE_Catalog * catalog = Catalog();
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_SETTINGS_EDIT_LABEL,
            strlen(GDEFCORE_SETTINGS_EDIT_LABEL), "core.role", 9));
    gdefcore_catalog_destroy(catalog);
  }

  uint64_t Epoch() {
    GDEFCORE_Catalog * catalog = Catalog();
    uint64_t e = 0;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_epoch(catalog, &e));
    gdefcore_catalog_destroy(catalog);
    return e;
  }

  /* The front page draws the site name, as a site that wants it shown does. */
  void PlaceSiteName() {
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
    const char * panes[2] = {"main sitename", "side recent"};
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

  static std::string MenuOf(const std::string & page) {
    size_t at = page.find("<nav class=\"admin-menu\">");
    if (at == std::string::npos) {
      return std::string();
    }
    return page.substr(at, page.find("</nav>", at) - at);
  }

  /* The post body for a form drawn on @p page: its token and version, and the
   * fields. */
  std::string Body(const std::string & page, const std::string & fields) {
    return "token=" + InputValue(page, "token", 0) + "&version=" +
           InputValue(page, "version", 0) + fields;
  }

  /* The title the public front page shows. */
  std::string SiteName() {
    std::string page = Get("/", std::string()).body;
    size_t at = page.find("<span class=\"site-name\">");
    if (at == std::string::npos) {
      return "<none>";
    }
    at += 24;
    return page.substr(at, page.find("</span>", at) - at);
  }
};

}  // namespace

TEST_F(SettingsAdmin, TheSettingsPagesAreThereOnlyForWhoMayFollowTheirRoute) {
  Open();
  EnableShop();
  Reply none = Get("/admin/nothing", cookie_);
  for (const char * path : {"/admin/settings", "/admin/settings/core", "/admin/settings/core/site",
           "/admin/settings/shop", "/admin/settings/shop/limits"}) {
    Reply refused = Get(path, cookie_);
    EXPECT_EQ(none.status, refused.status) << path;
    EXPECT_EQ(none.body, refused.body) << path;
  }
  Reply post_none = PostRaw("/admin/nothing", cookie_, "token=x");
  Reply post_refused = PostRaw("/admin/settings/core/site", cookie_, "token=x&s.core.site_title=Hacked");
  EXPECT_EQ(post_none.status, post_refused.status);
  EXPECT_EQ(post_none.body, post_refused.body);
  EXPECT_EQ("<none>", SiteName());
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/settings\""));
  /* CONTROL: granted, the same URLs are pages, and the menu carries them. */
  GrantSettings();
  for (const char * path : {"/admin/settings", "/admin/settings/core", "/admin/settings/core/site",
           "/admin/settings/shop", "/admin/settings/shop/limits", "/admin/settings/shop/keys"}) {
    Reply page = Get(path, cookie_);
    EXPECT_EQ(200, page.status) << path;
    EXPECT_NE(none.body, page.body) << path;
    EXPECT_TRUE(ScansClean(page.body)) << path << "\n" << page.body;
    ExpectSecurity(page);
  }
  std::string menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, "href=\"/admin/settings\">Settings")) << menu;
  EXPECT_TRUE(Has(menu, "href=\"/admin/settings/core\">Site settings")) << menu;
  EXPECT_TRUE(Has(menu, "href=\"/admin/settings/shop\">Shop settings")) << menu;
  /* A group no route placed has no page, and the index says so. */
  Reply extras = Get("/admin/settings/shop/extras", cookie_);
  EXPECT_EQ(none.status, extras.status);
  EXPECT_EQ(none.body, extras.body);
  Reply index = Get("/admin/settings", cookie_);
  EXPECT_TRUE(Has(index.body, "<a href=\"/admin/settings/shop/limits\">shop.limits</a>")) << index.body;
  EXPECT_TRUE(Has(index.body, "shop.extras (no page in the admin menu)")) << index.body;
  /* Not the visitor, nor an account with no role. */
  AddUser(id_, "bob", "hunter2");
  Reply bob = Login("bob", "hunter2");
  for (const std::string & who : {std::string(), CookieLine(bob)}) {
    Reply got = Get("/admin/settings/core/site", who);
    Reply unknown = Get("/no-such-path", who);
    EXPECT_EQ(unknown.status, got.status);
    EXPECT_EQ(unknown.body, got.body);
  }
}

TEST_F(SettingsAdmin, AChangedTitleShowsOnTheNextPublicRequestAndNothingIsRebuilt) {
  Open();
  GrantSettings();
  PlaceSiteName();
  EXPECT_EQ("My site", SiteName());
  std::string page = Get("/admin/settings/core/site", cookie_).body;
  EXPECT_TRUE(Has(page, "name=\"s.core.site_title\"")) << page;
  EXPECT_TRUE(Has(page, "<form method=\"post\" class=\"settings-form\" action=\"/admin/settings/core/site\">"));
  uint64_t before = Epoch();
  Reply done = PostForm("/admin/settings/core/site", cookie_,
      Body(page, "&s.core.site_title=" + Enc("Gadget <Works>") + "&s.core.tagline=" + Enc("Tools & more")));
  EXPECT_EQ(200, done.status);
  EXPECT_TRUE(Has(done.body, "<p class=\"notice\">Saved.</p>")) << done.body;
  EXPECT_GT(Epoch(), before);
  EXPECT_EQ("Gadget &lt;Works&gt;", SiteName());
  EXPECT_TRUE(Has(Get("/", std::string()).body, "<span class=\"tagline\">Tools &amp; more</span>"));
  /* The form shows what is stored, escaped. */
  std::string again = Decoded(Get("/admin/settings/core/site", cookie_).body);
  EXPECT_TRUE(Has(again, "value=\"Gadget &lt;Works&gt;\"")) << again;
  EXPECT_TRUE(ScansClean(done.body));
  ExpectSecurity(done);
}

TEST_F(SettingsAdmin, SavingWhatIsThereWritesNothingAndSaysSo) {
  Open();
  GrantSettings();
  std::string page = Get("/admin/settings/core/site", cookie_).body;
  uint64_t before = Epoch();
  Reply done = PostForm("/admin/settings/core/site", cookie_,
      Body(page, "&s.core.site_title=" + Enc("My site") + "&s.core.tagline="));
  EXPECT_TRUE(Has(done.body, "<p class=\"notice\">Nothing changed.</p>")) << done.body;
  EXPECT_EQ(before, Epoch());
  /* CONTROL: a change is a save. */
  page = Get("/admin/settings/core/site", cookie_).body;
  done = PostForm("/admin/settings/core/site", cookie_,
      Body(page, "&s.core.site_title=" + Enc("Different") + "&s.core.tagline="));
  EXPECT_TRUE(Has(done.body, "<p class=\"notice\">Saved.</p>")) << done.body;
  EXPECT_GT(Epoch(), before);
}

TEST_F(SettingsAdmin, AValueOutOfBoundsIsRefusedWithAMessageAndNothingIsWritten) {
  Open();
  EnableShop();
  GrantSettings();
  std::string page = Get("/admin/settings/shop/limits", cookie_).body;
  uint64_t before = Epoch();
  Reply bad = PostForm("/admin/settings/shop/limits", cookie_,
      Body(page, "&s.shop.limit=500&s.shop.sale=true"));
  EXPECT_TRUE(Has(bad.body, "Order limit is above the highest value allowed")) << bad.body;
  EXPECT_TRUE(Has(bad.body, "notice error"));
  EXPECT_EQ(before, Epoch());
  Reply text = PostForm("/admin/settings/shop/limits", cookie_,
      Body(page, "&s.shop.limit=ten&s.shop.sale=true"));
  EXPECT_TRUE(Has(text.body, "Order limit must be a number")) << text.body;
  EXPECT_EQ(before, Epoch());
  /* CONTROL: inside the bounds. */
  Reply ok = PostForm("/admin/settings/shop/limits", cookie_,
      Body(page, "&s.shop.limit=50&s.shop.sale=true"));
  EXPECT_TRUE(Has(ok.body, "Saved.")) << ok.body;
  EXPECT_TRUE(Has(Decoded(ok.body), "value=\"50\"")) << ok.body;
}

TEST_F(SettingsAdmin, AWriteNeedsTheSignedFormAndTheRightOne) {
  Open();
  EnableShop();
  GrantSettings();
  std::string page = Get("/admin/settings/core/site", cookie_).body;
  uint64_t before = Epoch();
  /* No token, a forged one, and a token made for another purpose. */
  Reply none = PostForm("/admin/settings/core/site", cookie_,
      "version=" + InputValue(page, "version", 0) + "&s.core.site_title=NoToken");
  /* A token the server made, with its tag damaged: the signature is checked. */
  std::string damaged = InputValue(page, "token", 0);
  ASSERT_GT(damaged.size(), 8u);
  for (size_t i = damaged.size() - 4; i < damaged.size(); i++) {
    damaged[i] = damaged[i] == 'A' ? 'B' : 'A';
  }
  Reply forged = PostForm("/admin/settings/core/site", cookie_,
      "token=" + damaged + "&version=" + InputValue(page, "version", 0) +
          "&s.core.site_title=Forged");
  EXPECT_TRUE(Has(none.body, "out of date")) << none.body;
  EXPECT_TRUE(Has(forged.body, "out of date")) << forged.body;
  EXPECT_EQ(before, Epoch());
  EXPECT_EQ("<none>", SiteName());
  /* The same session's token for the menus page is not one for settings. */
  GDEFCORE_Catalog * catalog = Catalog();
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_MENU_EDIT_ENTRY_LABEL,
          strlen(GDEFCORE_MENU_EDIT_ENTRY_LABEL), "core.role", 9));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_MENU_EDIT_OVERRIDE_LABEL,
          strlen(GDEFCORE_MENU_EDIT_OVERRIDE_LABEL), "core.role", 9));
  gdefcore_catalog_destroy(catalog);
  before = Epoch();
  std::string menus = Get("/admin/menus", cookie_).body;
  std::string other = InputValue(menus, "token", 0);
  ASSERT_FALSE(other.empty()) << menus;
  Reply wrong = PostForm("/admin/settings/core/site", cookie_,
      "token=" + other + "&version=" + InputValue(page, "version", 0) + "&s.core.tagline=Wrong");
  EXPECT_TRUE(Has(wrong.body, "out of date")) << wrong.body;
  EXPECT_EQ(before, Epoch());
  /* Not from another origin. */
  Reply cross = PostRaw("/admin/settings/core/site", cookie_, Body(page, "&s.core.tagline=Cross"),
      "http://evil.example");
  EXPECT_NE(200, cross.status);
  EXPECT_EQ(before, Epoch());
  /* CONTROL: the form it was given. */
  Reply fine = PostForm("/admin/settings/core/site", cookie_, Body(page, "&s.core.tagline=Fine"));
  EXPECT_TRUE(Has(fine.body, "Saved.")) << fine.body;
  EXPECT_GT(Epoch(), before);
}

TEST_F(SettingsAdmin, AFormDrawnBeforeAnotherSaveIsStale) {
  Open();
  GrantSettings();
  std::string first = Get("/admin/settings/core/site", cookie_).body;
  std::string second = Get("/admin/settings/core/site", cookie_).body;
  Reply a = PostForm("/admin/settings/core/site", cookie_, Body(first, "&s.core.tagline=One"));
  EXPECT_TRUE(Has(a.body, "Saved.")) << a.body;
  uint64_t before = Epoch();
  Reply b = PostForm("/admin/settings/core/site", cookie_, Body(second, "&s.core.tagline=Two"));
  EXPECT_TRUE(Has(b.body, "changed since the form was drawn")) << b.body;
  EXPECT_EQ(before, Epoch());
  EXPECT_TRUE(Has(Get("/admin/settings/core/site", cookie_).body, "value=\"One\""));
}

TEST_F(SettingsAdmin, AModulePlacesItsPageAndTabsAndALocalSecretIsNeverShownAgain) {
  Open();
  EnableShop();
  GrantSettings();
  std::string page = Get("/admin/settings/shop/keys", cookie_).body;
  /* The tabs are the module's own routes. */
  EXPECT_TRUE(Has(page, "<nav class=\"admin-tabs\"><ul class=\"tabs\">")) << page;
  EXPECT_TRUE(Has(page, "<li><a href=\"/admin/settings/shop\">Limits</a></li>"))
      << "the default tab answers its parent's path: " << page;
  EXPECT_TRUE(Has(page, "<li class=\"current\"><a href=\"/admin/settings/shop/keys\">Keys</a></li>")) << page;
  EXPECT_TRUE(Has(page, "Not set"));
  Reply saved = PostForm("/admin/settings/shop/keys", cookie_,
      Body(page, std::string("&s.shop.api_key=") + kSecretKey + "&s.shop.banner=" + Enc("Sale <now>")));
  EXPECT_TRUE(Has(saved.body, "Saved.")) << saved.body;
  EXPECT_FALSE(Has(saved.body, "TOPSECRET")) << saved.body;
  EXPECT_TRUE(Has(saved.body, ">Set<"));
  EXPECT_TRUE(Has(Decoded(saved.body), ">Sale &lt;now&gt;</textarea>")) << saved.body;
  EXPECT_TRUE(ScansClean(saved.body));
  /* Neither the page after, nor a leaving blank, shows or removes it. */
  std::string after = Get("/admin/settings/shop/keys", cookie_).body;
  EXPECT_FALSE(Has(after, "TOPSECRET"));
  Reply blank = PostForm("/admin/settings/shop/keys", cookie_,
      Body(after, "&s.shop.api_key=&s.shop.banner=" + Enc("Sale <now>")));
  EXPECT_TRUE(Has(blank.body, "Nothing changed.")) << blank.body;
  GDEFCORE_Catalog * catalog = Catalog();
  char * v = NULL;
  size_t n = 0;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_settings_get(catalog, "shop.api_key", 12, &v, &n, NULL));
  EXPECT_EQ(kSecretKey, std::string(v, n));
  gdefcore_text_free(catalog, v);
  gdefcore_catalog_destroy(catalog);
  /* Clear takes it away. */
  std::string again = Get("/admin/settings/shop/keys", cookie_).body;
  Reply cleared = PostForm("/admin/settings/shop/keys", cookie_,
      Body(again, "&s.shop.api_key=&c.shop.api_key=clear&s.shop.banner=" + Enc("Sale <now>")));
  EXPECT_TRUE(Has(cleared.body, "Saved.")) << cleared.body;
  EXPECT_TRUE(Has(cleared.body, "Not set"));
  /* The module disabled takes its page with it. */
  GDEFCORE_Catalog * c2 = Catalog();
  GDEFCORE_Changeset * cs = NULL;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(c2, NULL, 0, &cs));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_module_disable(host_, cs, "shop", 4));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "shop", 4));
  gdefcore_changeset_destroy(cs);
  gdefcore_catalog_destroy(c2);
  Reply gone = Get("/admin/settings/shop/keys", cookie_);
  Reply none = Get("/admin/nothing", cookie_);
  EXPECT_EQ(none.body, gone.body);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
