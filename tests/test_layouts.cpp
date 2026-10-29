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
 * Layouts chosen through the route trie (site admin story 7), through the real
 * server: the server builds the trie of the modules enabled for the tenant
 * (their routes and their contributors, and the kernel's own contributor that
 * offers the site's rules), the page is drawn by the layout the trie chooses,
 * and a module's widget type and selector change a public page with no change
 * to the server. A layout the site changes shows on the next request.
 *
 * Every refusal has a control in the same test.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>

namespace {

#define LAYOUTS_STR2(x) #x
#define LAYOUTS_STR(x) LAYOUTS_STR2(x)
const char kToken[] = LAYOUTS_STR(GHOTIIO_DEFCORE_NAME);

struct Probe {
  int widget_calls = 0;
  int selector_calls = 0;
};

GDEFCORE_Result StampFn(GDEFCORE_Request *, const char *, size_t,
    GDEFCORE_Widget_Sink * sink, void * user) {
  ((Probe *)user)->widget_calls += 1;
  return gdefcore_widget_bind_text(sink, "title", 5, "Stamped by news", 15);
}

void SkinFn(const GDEFCORE_Contributor_Call * call) {
  GDEFCORE_Offer offer;
  ((Probe *)call->data)->selector_calls += 1;
  memset(&offer, 0, sizeof offer);
  offer.size = sizeof offer;
  offer.value = "skinned";
  offer.value_len = 7;
  gdefcore_contributor_offer(call, &offer);
}

/* The module `news`: a contributor on /hello-title and a widget type that needs
 * a permission nobody holds until it is granted. */
struct News {
  Probe probe;
  GDEFCORE_Module_Manifest man;
  GDEFCORE_Module_Contributor_Handler handler;
  GDEFCORE_Module_Widget_Provider provider;
  GDEFCORE_Module_Service services[2];
  GDEFCORE_Module_Contributor contributor;
  GDEFCORE_Module_Widget widget;
  GDEFCORE_Module_Perm perm;

  void Build() {
    memset(&man, 0, sizeof man);
    memset(&handler, 0, sizeof handler);
    memset(&provider, 0, sizeof provider);
    memset(services, 0, sizeof services);
    memset(&contributor, 0, sizeof contributor);
    memset(&widget, 0, sizeof widget);
    memset(&perm, 0, sizeof perm);
    handler.size = sizeof handler;
    handler.fn = SkinFn;
    handler.data = &probe;
    provider.size = sizeof provider;
    provider.fn = StampFn;
    provider.user = &probe;
    services[0].size = sizeof services[0];
    services[0].name = "news.c.skin";
    services[0].name_len = 11;
    services[0].version = GDEFCORE_MODULE_CONTRIBUTOR_HANDLER_VERSION;
    services[0].table = &handler;
    services[1].size = sizeof services[1];
    services[1].name = "news.w.stamp";
    services[1].name_len = 12;
    services[1].version = GDEFCORE_MODULE_WIDGET_PROVIDER_VERSION;
    services[1].table = &provider;
    contributor.size = sizeof contributor;
    contributor.name = "news.skin";
    contributor.name_len = 9;
    contributor.kind = GDEFCORE_CONTRIBUTOR_LAYOUT;
    contributor.kind_len = 6;
    contributor.path = "/hello-title";
    contributor.path_len = 12;
    contributor.handler = "news.c.skin";
    contributor.handler_len = 11;
    widget.size = sizeof widget;
    widget.name = "news.stamp";
    widget.name_len = 10;
    widget.kind = "core.pane";
    widget.kind_len = 9;
    widget.permission = "news.read";
    widget.permission_len = 9;
    widget.provider = "news.w.stamp";
    widget.provider_len = 12;
    perm.size = sizeof perm;
    perm.label = "Read news";
    perm.label_len = 9;
    perm.action = "news.read";
    perm.action_len = 9;
    perm.condition = GDEFCORE_ACCESS_ANY;
    perm.type_name = "site.post";
    perm.type_name_len = 9;
    man.size = sizeof man;
    man.core_token = kToken;
    man.core_token_len = strlen(kToken);
    man.name = "news";
    man.name_len = 4;
    man.schema_version = 1;
    man.services = services;
    man.nservices = 2;
    man.perms = &perm;
    man.nperms = 1;
    man.contributors = &contributor;
    man.ncontributors = 1;
    man.widgets = &widget;
    man.nwidgets = 1;
  }
};

News g_news;

extern "C" const GDEFCORE_Module_Manifest * NewsEntry(void) { return &g_news.man; }

class Layouts : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];

  void Open() {
    Site(id_);
    UseHost();
    g_news = News();
    g_news.Build();
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, NewsEntry));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  GDEFCORE_Result ApplyChangeset(GDEFCORE_Changeset * cs, const char * confirm) {
    GDEFCORE_Result rc = gdefcore_changeset_apply(cs, confirm, strlen(confirm));
    gdefcore_changeset_destroy(cs);
    return rc;
  }

  void EnableNews() {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, "news", 4));
    ASSERT_EQ(GDEFCORE_OK, ApplyChangeset(cs, "news\nRead news"));
    gdefcore_catalog_destroy(catalog);
  }

  GDEFCORE_Result DisableNews() {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_module_disable(host_, cs, "news", 4));
    GDEFCORE_Result rc = ApplyChangeset(cs, "news");
    gdefcore_catalog_destroy(catalog);
    return rc;
  }

  /* The id of the entity of @p type called @p name, or "". */
  static std::string FindNamed(GDEFCORE_Catalog * catalog, const char * type,
      const char * name) {
    char ** ids = NULL;
    size_t n = 0;
    std::string found;
    if (gdefcore_entity_ids(catalog, type, strlen(type), &ids, &n) != GDEFCORE_OK) {
      return found;
    }
    for (size_t i = 0; i < n && found.empty(); i++) {
      char ** t = NULL;
      size_t * l = NULL;
      size_t c = 0;
      if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
              strlen(ids[i]), "en", 2, "name", 4, &t, &l, &c) != GDEFCORE_OK) {
        continue;
      }
      if (c > 0 && std::string(t[0], l[0]) == name) {
        found = ids[i];
      }
      gdefcore_values_free(catalog, t, l, c);
    }
    gdefcore_ids_free(catalog, ids, n);
    return found;
  }

  /* A template of kind core.pane, made. */
  void PutTemplate(const char * name, const char * body) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    GDEFCORE_Value v[3];
    const char * slots[3];
    size_t lens[3];
    OneValue(&v[0], "name", name, &slots[0], &lens[0]);
    OneValue(&v[1], "body", body, &slots[1], &lens[1]);
    OneValue(&v[2], "kind", "core.pane", &slots[2], &lens[2]);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(catalog, "core.template", 13, tid, NULL));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_save(cs, tid, strlen(tid), "en", 2, v, 3));
    ASSERT_EQ(GDEFCORE_OK, ApplyChangeset(cs, "core.template"));
    gdefcore_catalog_destroy(catalog);
  }

  /* The layout `skinned`, or `name`, with @p panes. */
  GDEFCORE_Result PutLayout(const char * name, std::vector<const char *> panes) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    char lid[GDEFCORE_PUBLIC_ID_LEN + 1];
    GDEFCORE_Value v[3];
    const char * slots[2];
    size_t lens[2];
    std::vector<size_t> plens;
    OneValue(&v[0], "name", name, &slots[0], &lens[0]);
    OneValue(&v[1], "layout", "layout", &slots[1], &lens[1]);
    for (const char * p : panes) {
      plens.push_back(strlen(p));
    }
    memset(&v[2], 0, sizeof v[2]);
    v[2].size = sizeof v[2];
    v[2].field = "panes";
    v[2].field_len = 5;
    v[2].texts = panes.data();
    v[2].text_lens = plens.data();
    v[2].count = panes.size();
    std::string have = FindNamed(catalog, "core.display", name);
    if (have.empty()) {
      EXPECT_EQ(GDEFCORE_OK, gdefcore_entity_create(catalog, "core.display", 12, lid, NULL));
    } else {
      memcpy(lid, have.c_str(), GDEFCORE_PUBLIC_ID_LEN + 1);
    }
    EXPECT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    GDEFCORE_Result rc = gdefcore_entity_save(cs, lid, strlen(lid), "en", 2, v, 3);
    if (rc == GDEFCORE_OK) {
      rc = ApplyChangeset(cs, "core.display");
    } else {
      gdefcore_changeset_destroy(cs);
    }
    gdefcore_catalog_destroy(catalog);
    return rc;
  }

  GDEFCORE_Result PutRule(const char * name, const char * path, const char * layout,
      int exact = 0) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Layout_Rule rule;
    memset(&rule, 0, sizeof rule);
    rule.size = sizeof rule;
    rule.path = path;
    rule.path_len = strlen(path);
    rule.layout = layout;
    rule.layout_len = strlen(layout);
    rule.exact = exact;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    GDEFCORE_Result rc = gdefcore_layout_stage_rule(cs, name, strlen(name), &rule);
    if (rc == GDEFCORE_OK) {
      rc = ApplyChangeset(cs, "core.layout-rule");
    } else {
      gdefcore_changeset_destroy(cs);
    }
    gdefcore_catalog_destroy(catalog);
    return rc;
  }

  void GrantNews() {
    GDEFCORE_Catalog * catalog = Catalog();
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "anonymous", 9, "Read news", 9,
            GDEFCORE_ACCESS_CONFIRM_ANONYMOUS,
            strlen(GDEFCORE_ACCESS_CONFIRM_ANONYMOUS)));
    gdefcore_catalog_destroy(catalog);
  }
};

}  // namespace

TEST_F(Layouts, TheKitsPagesAreDrawnByTheLayoutsTheTrieChooses) {
  Open();
  Reply front = Get("/", std::string());
  Reply post = Get("/hello-title", std::string());
  EXPECT_EQ(200, front.status);
  EXPECT_TRUE(Has(front.body, "class=\"recent\"")) << front.body;
  EXPECT_TRUE(Has(front.body, "Hello Title"));
  EXPECT_TRUE(Has(post.body, "<article class=\"post\"><h1>Hello Title</h1>")) << post.body;
  EXPECT_FALSE(Has(post.body, "class=\"recent\""));
  /* CONTROL: a path no rule reaches is what an unknown path is. */
  Reply unknown = Get("/no-such-page", std::string());
  EXPECT_FALSE(Has(unknown.body, "article"));
  EXPECT_FALSE(Has(unknown.body, "Hello Title"));
}

TEST_F(Layouts, AModulesSelectorAndWidgetChangeAPublicPageWithNoChangeToTheServer) {
  Open();
  const std::string plain = Get("/hello-title", std::string()).body;
  EnableNews();
  PutTemplate("news.stamp", "<% use title; %><aside class=\"stamp\"><%= title %></aside>");
  ASSERT_EQ(GDEFCORE_OK, PutLayout("skinned", {"main content", "side news.stamp"}));
  Reply skinned = Get("/hello-title", std::string());
  /* The module's selector chose the layout; its widget needs a permission the
   * visitor does not hold, so it draws nothing and the provider is not asked. */
  EXPECT_GT(g_news.probe.selector_calls, 0);
  EXPECT_EQ(0, g_news.probe.widget_calls);
  EXPECT_FALSE(Has(skinned.body, "stamp")) << skinned.body;
  EXPECT_TRUE(Has(skinned.body, "<article class=\"post\"><h1>Hello Title</h1>"));
  /* CONTROL: granted to the visitor, the widget is drawn, on the next request. */
  GrantNews();
  Reply granted = Get("/hello-title", std::string());
  EXPECT_TRUE(Has(granted.body, "<aside class=\"stamp\">Stamped by news</aside>")) << granted.body;
  EXPECT_EQ(1, g_news.probe.widget_calls);
  EXPECT_TRUE(Has(granted.body, "<article class=\"post\"><h1>Hello Title</h1>"))
      << "the main content is the same";
  /* Another page is not touched. */
  Reply front = Get("/", std::string());
  EXPECT_FALSE(Has(front.body, "stamp")) << front.body;
  /* The site's own rule beats the module's on the same path, and it takes
   * effect on the next request: no restart. */
  ASSERT_EQ(GDEFCORE_OK, PutLayout("plain", {"main content"}));
  ASSERT_EQ(GDEFCORE_OK, PutRule("site.hello", "/hello-title", "plain", 1));
  Reply site = Get("/hello-title", std::string());
  EXPECT_FALSE(Has(site.body, "stamp")) << site.body;
  EXPECT_TRUE(Has(site.body, "<article class=\"post\">"));
  EXPECT_EQ(plain.find("class=\"recent\""), std::string::npos);
  /* The module cannot be turned off while a layout places its widget; once
   * none does, it can, and the page is served all the same. */
  EXPECT_NE(GDEFCORE_OK, DisableNews());
  ASSERT_EQ(GDEFCORE_OK, PutLayout("skinned", {"main content"}));
  EXPECT_EQ(GDEFCORE_OK, DisableNews());
  Reply off = Get("/hello-title", std::string());
  EXPECT_EQ(200, off.status);
  EXPECT_TRUE(Has(off.body, "<article class=\"post\"><h1>Hello Title</h1>")) << off.body;
  EXPECT_EQ(0, g_news.probe.widget_calls - 1) << "the provider is not asked again";
}

TEST_F(Layouts, AnUnknownRegionIsRefusedAndTheSiteKeepsServing) {
  Open();
  EXPECT_NE(GDEFCORE_OK, PutLayout("broken", {"main content", "footer sitename"}));
  ASSERT_EQ(GDEFCORE_OK, PutRule("site.hello", "/hello-title", "broken", 1));
  Reply post = Get("/hello-title", std::string());
  /* The rule names a layout that does not exist, so it is passed over. */
  EXPECT_TRUE(Has(post.body, "<article class=\"post\"><h1>Hello Title</h1>")) << post.body;
  /* CONTROL: a layout that fits is applied. */
  ASSERT_EQ(GDEFCORE_OK, PutLayout("broken", {"main title"}));
  Reply fixed = Get("/hello-title", std::string());
  EXPECT_TRUE(Has(fixed.body, "<h1>Hello Title</h1>")) << fixed.body;
  EXPECT_FALSE(Has(fixed.body, "<article")) << fixed.body;
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
