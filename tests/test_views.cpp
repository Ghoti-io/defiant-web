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
 * Views in the browser (web content admin story 5), through the real server:
 * the editor under /admin/views, a view's public page under /v/<name>, the
 * permission that decides who may write a view, and what a hostile value does
 * in each. The types are defined by the test and named nowhere in the library.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/view.h>
#include <ghoti.io/defiant-storage/value.h>

namespace {

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

const char kType[] = "site.recipe";
const char kHostile[] = "<script>alert(1)</script>\"><img src=x onerror=1>";

class ViewsAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    return catalog;
  }

  static GDEFCORE_Field Field(const char * name, int required, int many) {
    GDEFCORE_Field f;
    memset(&f, 0, sizeof f);
    f.size = sizeof f;
    f.name = name;
    f.name_len = strlen(name);
    f.required = required;
    f.many = many;
    return f;
  }

  static void Allow(GDEFCORE_Catalog * catalog, const char * type, const char * action,
      const std::vector<std::string> & roles) {
    const std::string label = std::string(action) + " " + type;
    GDEFCORE_Permission p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.label = label.c_str();
    p.label_len = label.size();
    p.module_name = "site";
    p.module_name_len = 4;
    p.action = action;
    p.action_len = strlen(action);
    p.condition = GDEFCORE_ACCESS_ANY;
    p.type_name = type;
    p.type_name_len = strlen(type);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog, &p));
    for (const std::string & role : roles) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(), label.c_str(),
              label.size(), "core.role", 9));
    }
  }

  void Open() {
    Site(id_);
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Field recipe[3] = {Field("title", 1, 0), Field("course", 0, 0), Field("notes", 0, 1)};
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog, kType, strlen(kType), GDEFCORE_TYPE_CONTENT, recipe, 3));
    for (const char * action : {"create", "read", "update", "delete"}) {
      Allow(catalog, kType, action, {"admin"});
    }
    /* A program's first start declares and grants this; the fixture leaves it to the test. */
    ASSERT_EQ(GDEFCORE_OK, gdefcore_view_declare(catalog));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_view_declare_edit(catalog));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_VIEW_EDIT_LABEL,
            strlen(GDEFCORE_VIEW_EDIT_LABEL), "core.role", 9));
    gdefcore_catalog_destroy(catalog);
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* A signed-in person with the admin area and, if asked, the permission to edit views. */
  std::string Person(const char * name, bool may_edit_views) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Keyring * keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    const std::string role = std::string("role-") + name;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, role.c_str(), role.size(), NULL));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, role.c_str(), role.size(), GDEFCORE_ADMIN_USE_LABEL,
            strlen(GDEFCORE_ADMIN_USE_LABEL), "core.role", 9));
    if (may_edit_views) {
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(), GDEFCORE_VIEW_EDIT_LABEL,
              strlen(GDEFCORE_VIEW_EDIT_LABEL), "core.role", 9));
    }
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_principal_add_role(catalog, principal, strlen(principal), role.c_str(),
            role.size()));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal, strlen(principal),
            name, strlen(name), "pw-pw-pw", 8, account));
    gdefcore_catalog_destroy(catalog);
    return CookieLine(Login(name, "pw-pw-pw"));
  }

  static bool NotThere(const Reply & r) {
    return r.status == 200 && r.body.find("<h1>") == std::string::npos;
  }

  static std::string TokenOf(const std::string & page, const char * value) {
    return FormToken(page, (std::string("value=\"") + value + "\"").c_str());
  }

  /* A recipe, made through the content forms. */
  void Recipe(const std::string & title, const std::string & course = "main",
      const std::string & notes = std::string()) {
    Reply list = Get("/admin/content/site.recipe", ada_);
    Reply made = PostRaw("/admin/content/site.recipe", ada_,
        "token=" + TokenOf(list.body, "new") + "&do=new");
    ASSERT_EQ(303, made.status) << list.body;
    const std::string path = HeaderValue(made, "Location");
    Reply form = Get(path.c_str(), ada_);
    Reply saved = PostRaw(path.c_str(), ada_,
        "token=" + TokenOf(form.body, "save") + "&do=save&title=" + Enc(title) +
            "&course=" + Enc(course) + "&notes=" + Enc(notes));
    ASSERT_EQ(303, saved.status) << saved.body;
  }

  static size_t Count(const std::string & hay, const char * needle) {
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
      n++;
    }
    return n;
  }

  /* The form of a new view, then its post; the reply of the post. */
  Reply SaveNew(const std::string & cookie, const std::string & rest) {
    Reply form = Get("/admin/views/new", cookie);
    return PostRaw("/admin/views/new", cookie,
        "token=" + TokenOf(form.body, "save") + "&do=save" + rest);
  }

  const std::string kRecipes = "&name=recipes&type=site.recipe&col0_field=title&col0_label=Title"
                               "&col1_field=course&format=table&limit=25";
};

}  // namespace

TEST_F(ViewsAdmin, AnEditorBuildsAViewAndItsPublicPageShowsTheRowsThePrincipalMaySee) {
  Open();
  Recipe("Soup");
  Recipe("Stew");
  { Reply l = Get("/admin/views", ada_); EXPECT_TRUE(Has(l.body, "There is no view yet.")) << l.body; }
  Reply made = SaveNew(ada_, kRecipes);
  ASSERT_EQ(303, made.status) << made.body;
  EXPECT_EQ("/admin/views/recipes", HeaderValue(made, "Location"));
  EXPECT_TRUE(Has(Get("/admin/views", ada_).body, "href=\"/admin/views/recipes\""));
  Reply edit = Get("/admin/views/recipes", ada_);
  EXPECT_TRUE(Has(edit.body, "name=\"name\" value=\"recipes\" readonly"));
  EXPECT_TRUE(Has(edit.body, "name=\"col0_field\" value=\"title\""));
  EXPECT_TRUE(Has(edit.body, "<h2>Fields of site.recipe</h2>")) << "the type's fields are listed";
  EXPECT_TRUE(Has(edit.body, "join count")) << "with the formatters of a many-valued field";
  Reply page = Get("/v/recipes", ada_);
  EXPECT_EQ(200, page.status);
  EXPECT_TRUE(Has(page.body, "<td>Soup</td>")) << page.body;
  EXPECT_TRUE(Has(page.body, "<td>Stew</td>"));
  EXPECT_TRUE(Has(page.body, "<th>Title</th>"));
  Reply visitor = Get("/v/recipes", "");
  EXPECT_EQ(200, visitor.status);
  EXPECT_FALSE(Has(visitor.body, "Soup")) << "a visitor sees the rows the access filter lets them";
  EXPECT_EQ(404, Get("/v/nothing", ada_).status);
  EXPECT_EQ(404, Get("/v/Bad%20Name", ada_).status);
}

TEST_F(ViewsAdmin, ARefusedSaveShowsTheCoresReasonAndGivesWhatWasTypedBack) {
  Open();
  Reply bad = SaveNew(ada_, "&name=recipes&type=site.recipe&col0_field=nope&format=table&limit=25");
  EXPECT_EQ(200, bad.status);
  EXPECT_TRUE(Has(bad.body, "Not saved: ")) << bad.body;
  EXPECT_TRUE(Has(bad.body, "nope")) << "the reason names the field";
  EXPECT_TRUE(Has(bad.body, "name=\"col0_field\" value=\"nope\"")) << "what was typed comes back";
  EXPECT_TRUE(NotThere(Get("/admin/views/recipes", ada_))) << "nothing was stored";
  Reply cond = SaveNew(ada_, "&name=recipes&type=site.recipe&condition=%28%3D+field.nope+%22x%22%29"
                             "&col0_field=title&format=table&limit=25");
  EXPECT_EQ(200, cond.status);
  EXPECT_TRUE(Has(cond.body, "Not saved: "));
  Reply limit = SaveNew(ada_, "&name=recipes&type=site.recipe&col0_field=title&format=table&limit=abc");
  EXPECT_TRUE(Has(limit.body, "Not saved: "));
  /* A name that is already a view is not silently replaced from the new form. */
  ASSERT_EQ(303, SaveNew(ada_, kRecipes).status);
  Reply again = SaveNew(ada_, kRecipes);
  EXPECT_EQ(200, again.status);
  EXPECT_TRUE(Has(again.body, "a view of that name exists already"));
}

TEST_F(ViewsAdmin, WhoMayEditViewsDecidesWhatIsThereAndAnAreaWithoutItSeesNothing) {
  Open();
  const std::string reader = Person("nina", false);
  const std::string maker = Person("max", true);
  EXPECT_TRUE(NotThere(Get("/admin/views", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/views/new", reader)));
  Reply form = Get("/admin/views/new", maker);
  ASSERT_FALSE(NotThere(form)) << "a holder of the permission has the page";
  EXPECT_EQ(404, PostRaw("/admin/views/new", reader,
      "token=" + TokenOf(form.body, "save") + "&do=save" + kRecipes).status)
      << "a token taken from another person's form is no way in";
  EXPECT_TRUE(NotThere(Get("/admin/views/recipes", maker))) << "and nothing was written";
  EXPECT_EQ(303, PostRaw("/admin/views/new", maker,
      "token=" + TokenOf(form.body, "save") + "&do=save" + kRecipes).status);
  EXPECT_TRUE(NotThere(Get("/admin/views/recipes", reader)));
  EXPECT_FALSE(NotThere(Get("/admin/views/recipes", maker)));
  EXPECT_TRUE(NotThere(Get("/admin/views", "")));
  /* The menu does not offer the page to one who cannot follow it. */
  EXPECT_FALSE(Has(Get("/admin", reader).body, "/admin/views"));
  EXPECT_TRUE(Has(Get("/admin", maker).body, "/admin/views"));
}

TEST_F(ViewsAdmin, AHostileValueIsInertInThePageThePreviewTheListAndTheFormats) {
  Open();
  Recipe(kHostile, kHostile);
  ASSERT_EQ(303, SaveNew(ada_, "&name=bad&type=site.recipe&col0_field=title&col0_label=%3Cb%3EHead"
                               "&col1_field=notes&col1_fmt=join&format=table&limit=25").status);
  Reply page = Get("/v/bad", ada_);
  EXPECT_FALSE(Has(page.body, "<script>alert")) << page.body;
  EXPECT_FALSE(Has(page.body, "<b>Head"));
  EXPECT_TRUE(Has(page.body, "&lt;script&gt;"));
  Reply edit = Get("/admin/views/bad", ada_);
  Reply prev = PostRaw("/admin/views/bad", ada_,
      "token=" + TokenOf(edit.body, "preview") + "&do=preview");
  EXPECT_EQ(200, prev.status);
  EXPECT_TRUE(Has(prev.body, "<h2>Preview</h2>"));
  EXPECT_FALSE(Has(prev.body, "<script>alert"));
  /* A hostile name is not a view's name and a hostile condition is refused, not run. */
  EXPECT_FALSE(Has(Get("/admin/views/%3Cscript%3E", ada_).body, "<script>"));
  Reply hostile_cond = SaveNew(ada_, "&name=x&type=site.recipe&condition=" + Enc(kHostile) +
                                     "&col0_field=title&format=table&limit=25");
  EXPECT_FALSE(Has(hostile_cond.body, "<script>alert(1)</script>\"><img"))
      << "the condition comes back escaped";
  /* A list, a CSV and a JSON of the same rows. */
  ASSERT_EQ(303, SaveNew(ada_, "&name=bad-list&type=site.recipe&col0_field=title&format=list&limit=25").status);
  Reply list = Get("/v/bad-list", ada_);
  EXPECT_TRUE(Has(list.body, "<li>&lt;script&gt;")) << list.body;
  ASSERT_EQ(303, SaveNew(ada_, "&name=bad-csv&type=site.recipe&col0_field=title&format=csv&limit=25").status);
  Reply csv = Get("/v/bad-csv", ada_);
  EXPECT_TRUE(Has(HeaderValue(csv, "Content-Type"), "text/csv"));
  EXPECT_EQ("nosniff", HeaderValue(csv, "X-Content-Type-Options"));
  ASSERT_EQ(303, SaveNew(ada_, "&name=bad-json&type=site.recipe&col0_field=title&format=json&limit=25").status);
  Reply json = Get("/v/bad-json", ada_);
  EXPECT_TRUE(Has(HeaderValue(json, "Content-Type"), "application/json"));
  EXPECT_EQ("nosniff", HeaderValue(json, "X-Content-Type-Options"));
  EXPECT_FALSE(Has(json.body, "<script>")) << json.body;
}

TEST_F(ViewsAdmin, AnArgumentNarrowsTheRowsFromTheUrlAndAPreviewTakesExampleValues) {
  Open();
  Recipe("Soup", "starter");
  Recipe("Stew", "main");
  Recipe("Roast", "main");
  ASSERT_EQ(303, SaveNew(ada_, "&name=bycourse&type=site.recipe"
                               "&condition=%28%3D+field.course+%24c%29&col0_field=title"
                               "&arg0_name=c&arg0_source=exposed&arg0_fixed=main"
                               "&format=table&limit=25").status);
  Reply defaulted = Get("/v/bycourse", ada_);
  EXPECT_TRUE(Has(defaulted.body, "<td>Stew</td>")) << "the default of the argument";
  EXPECT_FALSE(Has(defaulted.body, "<td>Soup</td>"));
  Reply starters = Get("/v/bycourse?c=starter", ada_);
  EXPECT_TRUE(Has(starters.body, "<td>Soup</td>"));
  EXPECT_FALSE(Has(starters.body, "<td>Stew</td>"));
  EXPECT_TRUE(Has(starters.body, "<form method=\"get\">")) << "an exposed argument is a form";
  /* A value that tries to be part of the condition is only a value. */
  Reply sly = Get("/v/bycourse?c=%22%29%20%28%3D%20field.title%20%22Soup", ada_);
  EXPECT_FALSE(Has(sly.body, "<td>Soup</td>")) << sly.body;
  Reply edit = Get("/admin/views/bycourse", ada_);
  Reply prev = PostRaw("/admin/views/bycourse", ada_,
      "token=" + TokenOf(edit.body, "preview") + "&do=preview&p_c=starter");
  EXPECT_TRUE(Has(prev.body, "<td>Soup</td>")) << prev.body;
  EXPECT_FALSE(Has(prev.body, "<td>Stew</td>"));
}

TEST_F(ViewsAdmin, APagePagesAndADeleteNeedsItsNameTypedAndAFreshForm) {
  Open();
  for (int i = 0; i < 5; i++) {
    Recipe("Dish" + std::to_string(i));
  }
  ASSERT_EQ(303, SaveNew(ada_, "&name=small&type=site.recipe&col0_field=title&format=table&limit=2").status);
  Reply first = Get("/v/small", ada_);
  EXPECT_TRUE(Has(first.body, "page=2")) << first.body;
  EXPECT_FALSE(Has(first.body, "Previous"));
  Reply last = Get("/v/small?page=3", ada_);
  EXPECT_TRUE(Has(last.body, "Previous"));
  EXPECT_FALSE(Has(last.body, "page=4"));
  EXPECT_EQ(1u, Count(last.body, "<td>")) << "the last page holds the one row left";
  EXPECT_EQ(2u, Count(first.body, "<td>"));
  Reply edit = Get("/admin/views/small", ada_);
  const std::string drop = TokenOf(edit.body, "delete");
  Reply wrong = PostRaw("/admin/views/small", ada_, "token=" + drop + "&do=delete&confirm=other");
  EXPECT_EQ(200, wrong.status);
  EXPECT_TRUE(Has(wrong.body, "Nothing was changed"));
  EXPECT_FALSE(NotThere(Get("/admin/views/small", ada_)));
  /* A save changes the view, so a form drawn before it is out of date. */
  Reply saved = PostRaw("/admin/views/small", ada_, "token=" + TokenOf(edit.body, "save") +
      "&do=save&name=small&type=site.recipe&col0_field=title&format=table&limit=3");
  ASSERT_EQ(303, saved.status) << saved.body;
  Reply stale = PostRaw("/admin/views/small", ada_, "token=" + drop + "&do=delete&confirm=small");
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(stale.body, "out of date"));
  EXPECT_FALSE(NotThere(Get("/admin/views/small", ada_)));
  Reply fresh = Get("/admin/views/small", ada_);
  Reply gone = PostRaw("/admin/views/small", ada_,
      "token=" + TokenOf(fresh.body, "delete") + "&do=delete&confirm=small");
  EXPECT_EQ(303, gone.status);
  EXPECT_TRUE(NotThere(Get("/admin/views/small", ada_)));
  EXPECT_EQ(404, Get("/v/small", ada_).status);
}

/* The content form (story 2) shows the core's own refusal of a save, with the path of a nested field. */
TEST_F(ViewsAdmin, TheContentFormShowsTheCoresRefusalOfANestedFieldByItsPath) {
  Open();
  GDEFCORE_Catalog * catalog = Catalog();
  GDEFCORE_Field sku[1] = {Field("sku", 0, 0)};
  sku[0].max_len = 3;
  GDEFCORE_Field order[2] = {Field("title", 1, 0), Field("lines", 0, 1)};
  order[1].type = GDEFSTOR_FIELD_GROUP;
  order[1].children = sku;
  order[1].child_count = 1;
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_type_define(catalog, "site.order", 10, GDEFCORE_TYPE_CONTENT, order, 2));
  for (const char * action : {"create", "read", "update", "delete"}) {
    Allow(catalog, "site.order", action, {"admin"});
  }
  gdefcore_catalog_destroy(catalog);
  Reply list = Get("/admin/content/site.order", ada_);
  Reply made = PostRaw("/admin/content/site.order", ada_,
      "token=" + TokenOf(list.body, "new") + "&do=new");
  ASSERT_EQ(303, made.status) << list.body;
  const std::string path = HeaderValue(made, "Location");
  Reply form = Get(path.c_str(), ada_);
  Reply saved = PostRaw(path.c_str(), ada_,
      "token=" + TokenOf(form.body, "save") + "&do=save&title=T&lines-0--i=0&lines-0-sku=ABCDEFG");
  EXPECT_EQ(200, saved.status) << "a value over its field's length is refused, not stored";
  EXPECT_TRUE(Has(saved.body, "lines.sku")) << saved.body;
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
