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
 * The content administration (web content admin story 1), through the real
 * server. The types here are defined by the test, at run time, and are named
 * nowhere in the library: the pages are made from what a type declares, and
 * what a person may do on them is what the core's access answers say.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/entity.h>

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

const char kContent[] = "site.recipe";
const char kConfig[] = "site.banner";
const char kHostile[] = "<script>alert(1)</script>\"><img src=x onerror=1>";

class ContentAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    return catalog;
  }

  static GDEFCORE_Field Field(const char * name, int required, int many, size_t max_len) {
    GDEFCORE_Field f;
    memset(&f, 0, sizeof f);
    f.size = sizeof f;
    f.name = name;
    f.name_len = strlen(name);
    f.required = required;
    f.many = many;
    f.max_len = max_len;
    return f;
  }

  /* A permission of one action on one type, granted to the roles named. */
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

  /* The site, the two types (nothing in the library names them), and the
   * administrator's grants on both. */
  void Open() {
    Site(id_);
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Field recipe[3] = {Field("title", 1, 0, 80), Field("notes", 0, 1, 0),
        Field("summary", 0, 0, 400)};
    GDEFCORE_Field banner[1] = {Field("text", 1, 0, 0)};
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog, kContent, strlen(kContent), GDEFCORE_TYPE_CONTENT,
            recipe, 3));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog, kConfig, strlen(kConfig), GDEFCORE_TYPE_CONFIG,
            banner, 1));
    for (const char * type : {kContent, kConfig}) {
      for (const char * action : {"create", "read", "update", "delete"}) {
        Allow(catalog, type, action, {"admin"});
      }
    }
    gdefcore_catalog_destroy(catalog);
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* A signed-in person who holds exactly these permissions on the content type. */
  std::string Person(const char * name, const std::vector<const char *> & actions,
      const char * type = kContent, bool area = true) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Keyring * keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    const std::string role = std::string("role-") + name;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_role_create(catalog, role.c_str(), role.size(), NULL));
    for (const char * action : actions) {
      const std::string label = std::string(action) + " " + type;
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(), label.c_str(),
              label.size(), "core.role", 9));
    }
    /* Whoever may use the admin area, as the kit's editors may. */
    if (area) {
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(),
              GDEFCORE_ADMIN_USE_LABEL, strlen(GDEFCORE_ADMIN_USE_LABEL), "core.role", 9));
    }
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_principal_add_role(catalog, principal, strlen(principal),
            role.c_str(), role.size()));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal,
            strlen(principal), name, strlen(name), "pw-pw-pw", 8, account));
    gdefcore_catalog_destroy(catalog);
    return CookieLine(Login(name, "pw-pw-pw"));
  }

  /* A page the server answers as it answers one that does not exist. */
  static bool NotThere(const Reply & r) {
    return r.status == 200 && r.body.find("<h1>") == std::string::npos;
  }

  static std::string TokenOf(const std::string & page, const char * value) {
    return FormToken(page, (std::string("value=\"") + value + "\"").c_str());
  }

  Reply Post(const std::string & path, const std::string & cookie, const std::string & token,
      const std::string & rest) {
    return PostRaw(path.c_str(), cookie, "token=" + token + rest);
  }

  /* The "New" button of a type's list, then the form it leads to; the path of it. */
  std::string StartNew(const std::string & cookie, const char * type = kContent) {
    const std::string base = std::string("/admin/content/") + type;
    Reply list = Get(base.c_str(), cookie);
    const std::string token = TokenOf(list.body, "new");
    Reply made = Post(base, cookie, token, "&do=new");
    EXPECT_EQ(303, made.status) << list.body;
    return HeaderValue(made, "Location");
  }

  /* Make one entity of the content type by the forms; the path of its page. */
  std::string Make(const std::string & cookie, const std::string & title,
      const std::string & notes = std::string()) {
    const std::string path = StartNew(cookie);
    Reply form = Get(path.c_str(), cookie);
    EXPECT_EQ(200, form.status) << path;
    Reply saved = Post(path, cookie, TokenOf(form.body, "save"),
        "&do=save&title=" + Enc(title) + "&notes=" + Enc(notes));
    EXPECT_EQ(303, saved.status) << saved.body;
    return path;
  }
};

}  // namespace

TEST_F(ContentAdmin, ATypeTheLibraryNeverHeardOfHasItsListCreateEditAndDeletePages) {
  Open();
  /* The index offers both defined types, the list is there, and there is a way in. */
  Reply index = Get("/admin/content", ada_);
  EXPECT_EQ(200, index.status);
  EXPECT_TRUE(Has(index.body, "href=\"/admin/content/site.recipe\""));
  EXPECT_TRUE(Has(index.body, "href=\"/admin/content/site.banner\""));
  Reply list = Get("/admin/content/site.recipe", ada_);
  EXPECT_FALSE(NotThere(list));
  EXPECT_TRUE(Has(list.body, "Nothing here yet.")) << list.body;
  EXPECT_FALSE(NotThere(Get("/admin/content/site.banner", ada_))) << "the other type as well";
  /* Create: the button reserves an entity and leads to its form. */
  const std::string path = StartNew(ada_);
  EXPECT_TRUE(Has(path, "/admin/content/site.recipe/")) << "[" << path << "]";
  Reply form = Get(path.c_str(), ada_);
  EXPECT_EQ(200, form.status);
  EXPECT_TRUE(Has(form.body, "name=\"title\""));
  EXPECT_TRUE(Has(form.body, "name=\"notes\""));
  EXPECT_TRUE(Has(form.body, "<textarea name=\"summary\"")) << "a long field is a text area";
  EXPECT_FALSE(Has(form.body, "Delete")) << "nothing is stored yet, so nothing to delete";
  Reply saved = Post(path, ada_, TokenOf(form.body, "save"),
      "&do=save&title=Soup&notes=" + Enc("stock\r\nsalt\r\n\r\npepper") + "&summary=");
  EXPECT_EQ(303, saved.status) << saved.body;
  EXPECT_EQ("/admin/content/site.recipe", HeaderValue(saved, "Location"));
  /* The list shows it, with a way to edit it. */
  Reply listed = Get("/admin/content/site.recipe", ada_);
  EXPECT_TRUE(Has(listed.body, ">Soup</td>"));
  EXPECT_TRUE(Has(listed.body, path.c_str()));
  /* Edit: the form shows what is stored, one value of a many field to a line. */
  Reply edit = Get(path.c_str(), ada_);
  EXPECT_TRUE(Has(edit.body, "value=\"Soup\""));
  EXPECT_TRUE(Has(edit.body, "stock\nsalt\npepper")) << edit.body;
  Reply changed = Post(path, ada_, TokenOf(edit.body, "save"),
      "&do=save&title=Broth&notes=stock");
  EXPECT_EQ(303, changed.status);
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, ">Broth</td>"));
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">Soup</td>"));
  /* Delete: refused without the box ticked, then done, and the list is empty. */
  Reply page = Get(path.c_str(), ada_);
  const std::string drop = TokenOf(page.body, "delete");
  ASSERT_FALSE(drop.empty());
  Reply unticked = Post(path, ada_, drop, "&do=delete&confirm=no");
  EXPECT_EQ(200, unticked.status);
  EXPECT_TRUE(Has(unticked.body, "Nothing was changed"));
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, ">Broth</td>"));
  Reply deleted = Post(path, ada_, drop, "&do=delete&confirm=yes");
  EXPECT_EQ(303, deleted.status);
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">Broth</td>"));
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, "Nothing here yet."));
}

TEST_F(ContentAdmin, ARequiredFieldAndALimitRefuseTheFormAndGiveWhatWasTypedBack) {
  Open();
  const std::string path = StartNew(ada_);
  Reply form = Get(path.c_str(), ada_);
  Reply empty = Post(path, ada_, TokenOf(form.body, "save"), "&do=save&title=&notes=a");
  EXPECT_EQ(200, empty.status);
  EXPECT_TRUE(Has(empty.body, "does not fit the field"));
  EXPECT_TRUE(Has(empty.body, ">a</textarea>")) << "what was typed comes back";
  Reply form2 = Get(path.c_str(), ada_);
  Reply long_title = Post(path, ada_, TokenOf(form2.body, "save"),
      "&do=save&title=" + Enc(std::string(200, 'x')));
  EXPECT_EQ(200, long_title.status);
  EXPECT_TRUE(Has(long_title.body, "does not fit the field"));
  EXPECT_TRUE(Has(long_title.body, std::string(200, 'x').c_str()));
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, "Nothing here yet."));
  /* CONTROL: the same form with a value that fits. */
  Reply form3 = Get(path.c_str(), ada_);
  EXPECT_EQ(303, Post(path, ada_, TokenOf(form3.body, "save"), "&do=save&title=ok").status);
}

TEST_F(ContentAdmin, AValueIsInertInTheListTheFormAndTheMessages) {
  Open();
  const std::string path = Make(ada_, kHostile, kHostile);
  Reply listed = Get("/admin/content/site.recipe", ada_);
  EXPECT_FALSE(Has(listed.body, "<script>alert(1)"));
  EXPECT_FALSE(Has(listed.body, "<img src=x"));
  EXPECT_TRUE(Has(listed.body, "&lt;script&gt;alert(1)&lt;/script&gt;"));
  Reply edit = Get(path.c_str(), ada_);
  EXPECT_FALSE(Has(edit.body, "<script>alert(1)"));
  EXPECT_FALSE(Has(edit.body, "\"><img"));
  EXPECT_TRUE(Has(edit.body, "&lt;script&gt;alert(1)&lt;/script&gt;&quot;&gt;&lt;img"));
}

TEST_F(ContentAdmin, WhoMayDoWhatDecidesWhatIsThereAndAnythingElseIsNotThere) {
  Open();
  const std::string made = Make(ada_, "Soup");
  Reply gone = Get("/admin/content/site.nosuch", ada_);
  EXPECT_TRUE(NotThere(gone)) << "CONTROL: what not being there looks like";
  EXPECT_TRUE(NotThere(Get("/admin/content/core.role", ada_))) << "a built-in type";
  std::string reader = Person("reader", {"read"});
  std::string maker = Person("maker", {"create", "read"});
  std::string editor = Person("editor", {"update", "read"});
  std::string cleaner = Person("cleaner", {"delete", "read"});
  std::string nothing = Person("nothing", {});
  /* A person with only read has nothing to manage: the type is not there. */
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe", reader)));
  EXPECT_EQ(gone.body, Get("/admin/content/site.recipe", reader).body);
  EXPECT_EQ(gone.body, Get("/admin/content/site.recipe", nothing).body);
  EXPECT_FALSE(Has(Get("/admin/content", reader).body, "site.recipe"));
  EXPECT_TRUE(Has(Get("/admin/content", reader).body, "There is no type you may manage."));
  EXPECT_FALSE(Has(Get("/admin/content", ada_).body, "There is no type you may manage."));
  /* Signed out: no admin page at all. */
  Reply out = Get("/admin/content/site.recipe", std::string());
  EXPECT_TRUE(out.status != 200 || NotThere(out));
  /* Create without update: the list and the button, no edit link, no way into
   * an entity made by someone else. */
  Reply make_list = Get("/admin/content/site.recipe", maker);
  EXPECT_FALSE(NotThere(make_list));
  EXPECT_TRUE(Has(make_list.body, "value=\"new\""));
  EXPECT_FALSE(Has(make_list.body, ">Edit</a>"));
  EXPECT_TRUE(NotThere(Get(made.c_str(), maker)));
  EXPECT_EQ(303, Post("/admin/content/site.recipe", maker,
                     TokenOf(make_list.body, "new"), "&do=new").status);
  /* Update without create: the edit link, no button; a new entity cannot be made. */
  Reply edit_list = Get("/admin/content/site.recipe", editor);
  EXPECT_FALSE(NotThere(edit_list));
  EXPECT_TRUE(Has(edit_list.body, ">Edit</a>"));
  EXPECT_FALSE(Has(edit_list.body, "value=\"new\""));
  Reply as_editor = Get(made.c_str(), editor);
  EXPECT_FALSE(NotThere(as_editor));
  EXPECT_FALSE(Has(as_editor.body, "value=\"delete\"")) << "no delete grant, no delete form";
  Reply not_made = PostRaw("/admin/content/site.recipe", editor,
      "token=" + TokenOf(Get("/admin/content/site.recipe", ada_).body, "new") + "&do=new");
  EXPECT_EQ(404, not_made.status) << "a token of another sign-in, and no grant";
  /* A token of the button is not a grant: someone who may create banners holds
   * such a token, and may update recipes (so the type is there for them), but may
   * not create one. */
  std::string mixed = Person("mixed", {"update", "read"});
  {
    GDEFCORE_Catalog * catalog = Catalog();
    const std::string role = "role-mixed";
    const std::string label = "create site.banner";
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, role.c_str(), role.size(), label.c_str(), label.size(),
            "core.role", 9));
    gdefcore_catalog_destroy(catalog);
  }
  const std::string banner_list = Get("/admin/content/site.banner", mixed).body;
  const std::string borrowed = TokenOf(banner_list, "new");
  ASSERT_FALSE(borrowed.empty());
  EXPECT_EQ(404, Post("/admin/content/site.recipe", mixed, borrowed, "&do=new").status)
      << "a create grant on another type";
  /* Delete without update: the list is there and so is the entity's page. */
  Reply clean_page = Get(made.c_str(), cleaner);
  EXPECT_TRUE(Has(clean_page.body, "value=\"delete\""));
  EXPECT_FALSE(Has(clean_page.body, "value=\"save\"")) << "cannot update";
  /* A post of someone who may not is refused as a page that is not there, and writes nothing. */
  Reply form = Get(made.c_str(), ada_);
  Reply refused = Post(made, nothing, TokenOf(form.body, "save"), "&do=save&title=Mine");
  EXPECT_EQ(404, refused.status);
  Reply refused_reader = Post(made, reader, TokenOf(form.body, "save"), "&do=save&title=Mine");
  EXPECT_EQ(404, refused_reader.status);
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, ">Soup</td>"));
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">Mine</td>"));
}

TEST_F(ContentAdmin, EveryFormIsSignedForItsSessionItsPurposeAndItsEntity) {
  Open();
  const std::string one = Make(ada_, "One");
  const std::string two = Make(ada_, "Two");
  const std::string page = Get(one.c_str(), ada_).body;
  const std::string save = TokenOf(page, "save");
  const std::string drop = TokenOf(page, "delete");
  ASSERT_FALSE(save.empty());
  ASSERT_FALSE(drop.empty());
  for (const char * bad : {"", "x", "AAAA", "%00"}) {
    Reply r = Post(one, ada_, bad, "&do=save&title=Zzz");
    EXPECT_EQ(200, r.status) << bad;
    EXPECT_TRUE(Has(r.body, "was changed while you were editing")) << bad;
  }
  Reply none = PostRaw(one.c_str(), ada_, "do=save&title=Zzz");
  EXPECT_TRUE(Has(none.body, "was changed while you were editing"));
  /* The delete token is not a save token, nor the other entity's token. */
  EXPECT_TRUE(Has(Post(one, ada_, drop, "&do=save&title=Zzz").body, "while you were editing"));
  const std::string two_save = TokenOf(Get(two.c_str(), ada_).body, "save");
  EXPECT_TRUE(Has(Post(one, ada_, two_save, "&do=save&title=Zzz").body, "while you were editing"));
  EXPECT_TRUE(Has(Post(one, ada_, save, "&do=delete&confirm=yes").body, "while you were editing"));
  /* A token made for another sign-in. */
  std::string other = CookieLine(Login("ada", "secret"));
  EXPECT_TRUE(Has(Post(one, other, save, "&do=save&title=Zzz").body, "while you were editing"));
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">Zzz</td>"));
  /* A page of a type whose token was made for another path, or an Origin that is not ours. */
  Reply cross = PostRaw(one.c_str(), ada_, "token=" + save + "&do=save&title=Zzz",
      "http://evil.example");
  EXPECT_NE(303, cross.status);
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">Zzz</td>"));
  /* CONTROL: the right token. */
  EXPECT_EQ(303, Post(one, ada_, save, "&do=save&title=Fine").status);
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, ">Fine</td>"));
}

TEST_F(ContentAdmin, AFormOpenedBeforeAnotherSaveIsStaleAndWritesNothing) {
  Open();
  const std::string path = Make(ada_, "v1");
  const std::string first = TokenOf(Get(path.c_str(), ada_).body, "save");
  const std::string second = TokenOf(Get(path.c_str(), ada_).body, "save");
  ASSERT_EQ(303, Post(path, ada_, second, "&do=save&title=v2").status);
  Reply stale = Post(path, ada_, first, "&do=save&title=mine");
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(stale.body, "was changed while you were editing"));
  EXPECT_TRUE(Has(stale.body, "value=\"mine\"")) << "the text typed is given back";
  EXPECT_FALSE(Has(Get("/admin/content/site.recipe", ada_).body, ">mine</td>"));
  EXPECT_TRUE(Has(Get("/admin/content/site.recipe", ada_).body, ">v2</td>"));
  /* The same token twice: the second finds the entity moved. */
  ASSERT_EQ(303, Post(path, ada_, TokenOf(Get(path.c_str(), ada_).body, "save"),
                     "&do=save&title=v3").status);
}

TEST_F(ContentAdmin, ConfigurationIsChangedAndDeletedOnlyWithTheTypeNameTyped) {
  Open();
  const std::string path = StartNew(ada_, kConfig);
  Reply form = Get(path.c_str(), ada_);
  EXPECT_TRUE(Has(form.body, "type the type's name to confirm"));
  Reply unconfirmed = Post(path, ada_, TokenOf(form.body, "save"), "&do=save&text=Hello");
  EXPECT_EQ(200, unconfirmed.status);
  EXPECT_TRUE(Has(unconfirmed.body, "type name typed does not match"));
  EXPECT_TRUE(Has(Get("/admin/content/site.banner", ada_).body, "Nothing here yet."));
  Reply wrong = Post(path, ada_, TokenOf(Get(path.c_str(), ada_).body, "save"),
      "&do=save&text=Hello&confirm=site.recipe");
  EXPECT_EQ(200, wrong.status);
  Reply right = Post(path, ada_, TokenOf(Get(path.c_str(), ada_).body, "save"),
      "&do=save&text=Hello&confirm=site.banner");
  EXPECT_EQ(303, right.status) << right.body;
  EXPECT_TRUE(Has(Get("/admin/content/site.banner", ada_).body, ">Hello</td>"));
  Reply page = Get(path.c_str(), ada_);
  Reply wrong_drop = Post(path, ada_, TokenOf(page.body, "delete"), "&do=delete&confirm=yes");
  EXPECT_EQ(200, wrong_drop.status);
  EXPECT_TRUE(Has(Get("/admin/content/site.banner", ada_).body, ">Hello</td>"));
  Reply dropped = Post(path, ada_, TokenOf(Get(path.c_str(), ada_).body, "delete"),
      "&do=delete&confirm=site.banner");
  EXPECT_EQ(303, dropped.status);
  EXPECT_FALSE(Has(Get("/admin/content/site.banner", ada_).body, ">Hello</td>"));
}

TEST_F(ContentAdmin, ALongListIsPagedByPath) {
  Open();
  for (int i = 0; i < 27; i++) {
    Make(ada_, "item-" + std::to_string(100 + i));
  }
  Reply first = Get("/admin/content/site.recipe", ada_);
  EXPECT_TRUE(Has(first.body, "href=\"/admin/content/site.recipe/page/2\""));
  EXPECT_FALSE(Has(first.body, "rel=\"prev\""));
  Reply second = Get("/admin/content/site.recipe/page/2", ada_);
  EXPECT_FALSE(NotThere(second));
  EXPECT_TRUE(Has(second.body, "rel=\"prev\""));
  EXPECT_FALSE(Has(second.body, "rel=\"next\""));
  size_t rows_first = 0;
  size_t rows_second = 0;
  for (size_t at = 0; (at = first.body.find(">Edit</a>", at)) != std::string::npos; at++) {
    rows_first++;
  }
  for (size_t at = 0; (at = second.body.find(">Edit</a>", at)) != std::string::npos; at++) {
    rows_second++;
  }
  EXPECT_EQ(25u, rows_first);
  EXPECT_EQ(2u, rows_second);
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe/page/0", ada_)));
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe/page/x", ada_)));
}

TEST_F(ContentAdmin, AnEntityOfAnotherTypeIsNotThereUnderThisOne) {
  Open();
  const std::string recipe = Make(ada_, "Soup");
  const std::string id = recipe.substr(recipe.rfind('/') + 1);
  EXPECT_FALSE(NotThere(Get(recipe.c_str(), ada_)));
  EXPECT_TRUE(NotThere(Get(("/admin/content/site.banner/" + id).c_str(), ada_)));
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe/not-an-id", ada_)));
  EXPECT_TRUE(NotThere(Get((recipe + "/extra").c_str(), ada_)));
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe/..", ada_)));
}

TEST_F(ContentAdmin, TheAdminAreaPermissionIsStillWhatOpensThePages) {
  Open();
  const std::string made = Make(ada_, "Soup");
  /* Every grant on the type, and no right to the area: nothing is there. */
  std::string outsider = Person("outsider", {"create", "read", "update", "delete"}, kContent,
      false);
  EXPECT_TRUE(NotThere(Get("/admin/content", outsider)));
  EXPECT_TRUE(NotThere(Get("/admin/content/site.recipe", outsider)));
  EXPECT_TRUE(NotThere(Get(made.c_str(), outsider)));
  /* CONTROL: the same grants with the area. */
  std::string insider = Person("insider", {"create", "read", "update", "delete"});
  EXPECT_FALSE(NotThere(Get("/admin/content/site.recipe", insider)));
}

TEST_F(ContentAdmin, EmptyingAFieldClearsItAndTheMenuLeadsToTheAdmin) {
  Open();
  const std::string path = Make(ada_, "Soup", "stock\nsalt");
  Reply form = Get(path.c_str(), ada_);
  EXPECT_TRUE(Has(form.body, "stock\nsalt"));
  ASSERT_EQ(303, Post(path, ada_, TokenOf(form.body, "save"),
                     "&do=save&title=Soup&notes=&summary=").status);
  Reply after = Get(path.c_str(), ada_);
  EXPECT_FALSE(Has(after.body, "stock"));
  EXPECT_TRUE(Has(after.body, "value=\"Soup\""));
  /* The admin menu carries the way in. */
  EXPECT_TRUE(Has(Get("/admin", ada_).body, "href=\"/admin/content\""));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
