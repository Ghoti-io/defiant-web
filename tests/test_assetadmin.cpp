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
 * The editor of the site's own stylesheets and scripts, through the real
 * server: /admin/stylesheets and /admin/scripts are two permissions, a form is
 * signed, a script is saved only with its name typed, a stale form writes
 * nothing, a refused form gives the text back, a file is public, and a name
 * that is not a file name never reaches a lookup.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/asset.h>
#include <ghoti.io/defiant-core/siteasset.h>

namespace {

/* application/x-www-form-urlencoded. */
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

class AssetAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  /* The kit grants the administrator the pages that need no permission a test
   * is about; the two of these are granted here, as the first start does. */
  void Open() {
    Site(id_);
    GDEFCORE_Catalog * catalog = NULL;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    for (const char * label :
        {GDEFCORE_ASSET_CSS_EDIT_LABEL, GDEFCORE_ASSET_JS_EDIT_LABEL}) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, "admin", 5, label, strlen(label), "core.role",
              9));
    }
    gdefcore_catalog_destroy(catalog);
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* A page the server answers as it answers one that does not exist: the empty
   * page, with no heading. */
  static bool NotThere(const Reply & r) {
    return r.status == 200 && r.body.find("<h1>") == std::string::npos;
  }

  /* A signed-in person who holds only the roles named, by what the labels grant. */
  std::string Person(const char * name, const std::vector<const char *> & labels) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Keyring * keyring;
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    const std::string role = std::string("role-") + name;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    EXPECT_NE(keyring, nullptr);
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_role_create(catalog, role.c_str(), role.size(), NULL));
    for (const char * label : labels) {
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(), label,
              strlen(label), "core.role", 9));
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

  /* The token of the form on @p page whose button says @p value. */
  static std::string TokenOf(const std::string & page, const char * value) {
    return FormToken(page, (std::string("value=\"") + value + "\"").c_str());
  }

  /* Post a form of @p path and give the reply as sent. */
  Reply Post(const std::string & path, const std::string & cookie,
      const std::string & token, const std::string & rest) {
    return PostRaw(path.c_str(), cookie, "token=" + token + rest);
  }

  std::string Served(const char * path) {
    Reply r = Get(path, std::string());
    return r.status == 200 ? r.body : "<" + std::to_string(r.status) + ">";
  }

  /* Create a file by the form, as the one signed in does; the reply as sent. */
  Reply Create(const char * base, const std::string & cookie, const std::string & name,
      const std::string & text, const std::string & confirm = std::string()) {
    Reply page = Get(base, cookie);
    std::string token = TokenOf(page.body, "create");
    return Post(base, cookie, token,
        "&do=create&name=" + Enc(name) + "&text=" + Enc(text) +
            (confirm.empty() ? "" : "&confirm=" + Enc(confirm)));
  }
};

}  // namespace

TEST_F(AssetAdmin, ThePagesAreThereOnlyForWhoHoldsTheirPermission) {
  Open();
  const Reply nobody_css = Get("/admin/stylesheets", std::string());
  const Reply fixed = Get("/admin/no-such-page", ada_);
  EXPECT_TRUE(NotThere(fixed)) << "CONTROL: what not being there looks like";
  EXPECT_TRUE(NotThere(nobody_css)) << "signed out";
  std::string css_only = Person("styler", {GDEFCORE_ASSET_CSS_EDIT_LABEL});
  std::string js_only = Person("scripter", {GDEFCORE_ASSET_JS_EDIT_LABEL});
  std::string neither = Person("plain", {});
  EXPECT_FALSE(NotThere(Get("/admin/stylesheets", ada_)));
  EXPECT_FALSE(NotThere(Get("/admin/scripts", ada_)));
  EXPECT_FALSE(NotThere(Get("/admin/stylesheets", css_only)));
  EXPECT_TRUE(NotThere(Get("/admin/scripts", css_only))) << "the other kind";
  EXPECT_FALSE(NotThere(Get("/admin/scripts", js_only)));
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets", js_only)));
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets", neither)));
  EXPECT_TRUE(NotThere(Get("/admin/scripts", neither)));
  EXPECT_EQ(fixed.body, Get("/admin/scripts", css_only).body);
  EXPECT_EQ(fixed.body, Get("/admin/stylesheets", neither).body);
  /* A post is refused the same way, and writes nothing. */
  const std::string token = TokenOf(Get("/admin/stylesheets", ada_).body, "create");
  Reply post = Post("/admin/stylesheets", neither, token, "&do=create&name=x.css&text=a%7B%7D");
  EXPECT_EQ(404, post.status);
  EXPECT_EQ("<404>", Served("/assets/site/x.css"));
}

TEST_F(AssetAdmin, AStylesheetIsMadeByAFormAndServedAtOnce) {
  Open();
  Reply made = Create("/admin/stylesheets", ada_, "site.css", "body { margin: 0 }\r\n");
  EXPECT_EQ(303, made.status) << made.body;
  EXPECT_EQ("/admin/stylesheets/site.css", HeaderValue(made, "Location"));
  EXPECT_EQ("body { margin: 0 }\n", Served("/assets/site/site.css"));
  Reply page = Get("/admin/stylesheets/site.css", ada_);
  EXPECT_EQ(200, page.status);
  EXPECT_TRUE(Has(page.body, "body { margin: 0 }"));
  EXPECT_TRUE(Has(page.body, "/assets/site/site.css?v="));
  EXPECT_TRUE(Has(page.body, "Anyone can read this file."));
  EXPECT_TRUE(Has(Get("/admin/stylesheets", ada_).body, ">site.css</a>"));
  /* The list of the other kind is not where it is. */
  EXPECT_FALSE(Has(Get("/admin/scripts", ada_).body, ">site.css</a>"));
  /* The files a module ships are listed read only; a text is escaped. */
  Create("/admin/stylesheets", ada_, "x.css", "</textarea><script>alert(1)</script>");
  Reply escaped = Get("/admin/stylesheets/x.css", ada_);
  EXPECT_FALSE(Has(escaped.body, "<script>alert"));
  EXPECT_TRUE(Has(escaped.body, "&lt;/textarea&gt;"));
}

TEST_F(AssetAdmin, ARefusedFormWritesNothingAndGivesTheTextBack) {
  Open();
  const std::string text = "p { color: red }";
  struct Case {
    const char * name;
    std::string body;
    const char * says;
  };
  const Case cases[] = {
      {"BAD NAME", text, "128 letters, digits"},
      {"..", text, "128 letters, digits"},
      {"a..b.css", text, "128 letters, digits"},
      {"ok.css", "", "empty, or holds a character"},
      {"ok.css", std::string("a\0b", 3), "empty, or holds a character"},
      {"ok.css", std::string(40 * 1024, 'a'), "too large"},
  };
  for (const Case & c : cases) {
    Reply r = Create("/admin/stylesheets", ada_, c.name, c.body);
    EXPECT_EQ(200, r.status) << c.name;
    EXPECT_TRUE(Has(r.body, c.says)) << c.name << ": " << c.says;
    EXPECT_EQ("<404>", Served("/assets/site/ok.css")) << c.name;
    EXPECT_EQ("<404>", Served("/assets/site/..")) << c.name;
  }
  /* The typed text comes back in the form. */
  Reply back = Create("/admin/stylesheets", ada_, "BAD NAME", text);
  EXPECT_TRUE(Has(back.body, "p { color: red }"));
  EXPECT_TRUE(Has(back.body, "value=\"BAD NAME\""));
  /* A name the site already uses, whichever kind it was. */
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "dup.txt", text).status);
  Reply dup = Create("/admin/stylesheets", ada_, "dup.txt", text);
  EXPECT_TRUE(Has(dup.body, "already has a file of that name"));
  Reply other_kind = Create("/admin/scripts", ada_, "dup.txt", "x()", "dup.txt");
  EXPECT_TRUE(Has(other_kind.body, "already has a file of that name"));
  EXPECT_EQ(text, Served("/assets/site/dup.txt"));
  /* CONTROL: the same form, made right. */
  EXPECT_EQ(303, Create("/admin/stylesheets", ada_, "ok.css", text).status);
}

TEST_F(AssetAdmin, ASignedFormIsRequiredAndIsBoundToItsSessionItsPurposeAndItsFile) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "one.css", "a{}").status);
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "two.css", "b{}").status);
  const std::string page = Get("/admin/stylesheets/one.css", ada_).body;
  const std::string save = TokenOf(page, "save");
  const std::string drop = TokenOf(page, "delete");
  ASSERT_FALSE(save.empty());
  ASSERT_FALSE(drop.empty());
  const char * bad_ones[] = {"", "x", "AAAA", "%00"};
  for (const char * bad : bad_ones) {
    Reply r = Post("/admin/stylesheets/one.css", ada_, bad, "&do=save&text=zzz");
    EXPECT_EQ(200, r.status) << bad;
    EXPECT_TRUE(Has(r.body, "out of date")) << bad;
  }
  /* No token at all. */
  Reply none = PostRaw("/admin/stylesheets/one.css", ada_, "do=save&text=zzz");
  EXPECT_TRUE(Has(none.body, "out of date"));
  /* The delete form's token is not a save token, nor another file's. */
  Reply wrong_purpose = Post("/admin/stylesheets/one.css", ada_, drop, "&do=save&text=zzz");
  EXPECT_TRUE(Has(wrong_purpose.body, "out of date"));
  const std::string two_save = TokenOf(Get("/admin/stylesheets/two.css", ada_).body, "save");
  Reply wrong_file = Post("/admin/stylesheets/one.css", ada_, two_save, "&do=save&text=zzz");
  EXPECT_TRUE(Has(wrong_file.body, "out of date"));
  /* A token made for another sign-in. */
  std::string other = CookieLine(Login("ada", "secret"));
  Reply elsewhere = Post("/admin/stylesheets/one.css", other, save, "&do=save&text=zzz");
  EXPECT_TRUE(Has(elsewhere.body, "out of date"));
  EXPECT_EQ("a{}", Served("/assets/site/one.css"));
  EXPECT_EQ("b{}", Served("/assets/site/two.css"));
  /* CONTROL: the right token. */
  Reply ok = Post("/admin/stylesheets/one.css", ada_, save, "&do=save&text=" + Enc("c{}"));
  EXPECT_EQ(303, ok.status);
  EXPECT_EQ("c{}", Served("/assets/site/one.css"));
}

TEST_F(AssetAdmin, AFormOpenedBeforeAnotherSaveIsStaleAndWritesNothing) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "stale.css", "v1{}").status);
  const std::string first = TokenOf(Get("/admin/stylesheets/stale.css", ada_).body, "save");
  const std::string second = TokenOf(Get("/admin/stylesheets/stale.css", ada_).body, "save");
  ASSERT_EQ(303, Post("/admin/stylesheets/stale.css", ada_, second,
                     "&do=save&text=" + Enc("v2{}")).status);
  Reply stale = Post("/admin/stylesheets/stale.css", ada_, first,
      "&do=save&text=" + Enc("mine{}"));
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(stale.body, "changed while you were editing"));
  EXPECT_TRUE(Has(stale.body, "mine{}")) << "the text typed is given back";
  EXPECT_EQ("v2{}", Served("/assets/site/stale.css"));
}

TEST_F(AssetAdmin, AScriptIsSavedOnlyWithItsNameTypedAndAStylesheetIsNot) {
  Open();
  /* Creating one. */
  for (const char * typed : {"", "other.js", "App.js", "app.js "}) {
    Reply r = Create("/admin/scripts", ada_, "app.js", "run()", typed);
    EXPECT_EQ(200, r.status) << "[" << typed << "]";
    EXPECT_TRUE(Has(r.body, "name typed does not match")) << "[" << typed << "]";
    EXPECT_EQ("<404>", Served("/assets/site/app.js"));
  }
  Reply made = Create("/admin/scripts", ada_, "app.js", "run()", "app.js");
  EXPECT_EQ(303, made.status);
  EXPECT_EQ("run()", Served("/assets/site/app.js"));
  Reply js = Get("/assets/site/app.js", std::string());
  EXPECT_EQ("text/javascript; charset=utf-8", HeaderValue(js, "Content-Type"));
  /* Editing one. */
  const std::string page = Get("/admin/scripts/app.js", ada_).body;
  EXPECT_TRUE(Has(page, "type its name to confirm"));
  Reply no = Post("/admin/scripts/app.js", ada_, TokenOf(page, "save"),
      "&do=save&text=" + Enc("evil()"));
  EXPECT_TRUE(Has(no.body, "name typed does not match"));
  EXPECT_TRUE(Has(no.body, "evil()"));
  EXPECT_EQ("run()", Served("/assets/site/app.js"));
  Reply yes = Post("/admin/scripts/app.js", ada_, TokenOf(page, "save"),
      "&do=save&confirm=app.js&text=" + Enc("fine()"));
  EXPECT_EQ(303, yes.status);
  EXPECT_EQ("fine()", Served("/assets/site/app.js"));
  /* A stylesheet asks for no typing to save, and a delete asks for it in both. */
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "s.css", "a{}").status);
  const std::string spage = Get("/admin/stylesheets/s.css", ada_).body;
  EXPECT_FALSE(Has(spage, "To save a script"));
  Reply saved = Post("/admin/stylesheets/s.css", ada_, TokenOf(spage, "save"),
      "&do=save&text=" + Enc("b{}"));
  EXPECT_EQ(303, saved.status);
  Reply keep = Post("/admin/stylesheets/s.css", ada_,
      TokenOf(Get("/admin/stylesheets/s.css", ada_).body, "delete"),
      "&do=delete&confirm=nope.css");
  EXPECT_TRUE(Has(keep.body, "name typed does not match"));
  EXPECT_EQ("b{}", Served("/assets/site/s.css"));
}

TEST_F(AssetAdmin, ADeleteTakesTheFileAwayAndItsAddressWithIt) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "gone.css", "a{}").status);
  EXPECT_EQ("a{}", Served("/assets/site/gone.css"));
  const std::string page = Get("/admin/stylesheets/gone.css", ada_).body;
  Reply drop = Post("/admin/stylesheets/gone.css", ada_, TokenOf(page, "delete"),
      "&do=delete&confirm=gone.css");
  EXPECT_EQ(303, drop.status);
  EXPECT_EQ("/admin/stylesheets", HeaderValue(drop, "Location"));
  EXPECT_EQ("<404>", Served("/assets/site/gone.css"));
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets/gone.css", ada_)));
  EXPECT_FALSE(Has(Get("/admin/stylesheets", ada_).body, ">gone.css</a>"));
  /* The name is free again, for either kind. */
  EXPECT_EQ(303, Create("/admin/scripts", ada_, "gone.css", "x()", "gone.css").status);
}

TEST_F(AssetAdmin, TheHistoryListsTheChangesAndTheLatestIsRevertedWithTheNameTyped) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "h.css", "one{}").status);
  Reply history = Get("/admin/stylesheets/h.css/history", ada_);
  EXPECT_EQ(200, history.status);
  EXPECT_TRUE(Has(history.body, "no earlier version"));
  ASSERT_EQ(303, Post("/admin/stylesheets/h.css", ada_,
                     TokenOf(Get("/admin/stylesheets/h.css", ada_).body, "save"),
                     "&do=save&text=" + Enc("two{}")).status);
  history = Get("/admin/stylesheets/h.css/history", ada_);
  EXPECT_TRUE(Has(history.body, "The current version, by"));
  const std::string token = TokenOf(history.body, "revert");
  ASSERT_FALSE(token.empty());
  Reply no = Post("/admin/stylesheets/h.css/history", ada_, token, "&do=revert&confirm=");
  EXPECT_TRUE(Has(no.body, "name typed does not match"));
  EXPECT_EQ("two{}", Served("/assets/site/h.css"));
  Reply bad = Post("/admin/stylesheets/h.css/history", ada_, "AAAA", "&do=revert&confirm=h.css");
  EXPECT_TRUE(Has(bad.body, "out of date"));
  Reply yes = Post("/admin/stylesheets/h.css/history", ada_, token,
      "&do=revert&confirm=h.css");
  EXPECT_EQ(303, yes.status);
  EXPECT_EQ("one{}", Served("/assets/site/h.css"));
}

TEST_F(AssetAdmin, AFileOfTheOtherKindOrASpellingThatIsNotAFileNameIsNotThere) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "only.css", "a{}").status);
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets/none.css", ada_)));
  /* The stylesheet is not a script, and its name opened on the other page is a
   * file that is not there. */
  EXPECT_TRUE(NotThere(Get("/admin/scripts/only.css", ada_)));
  const char * not_names[] = {"/admin/stylesheets/ONLY.CSS", "/admin/stylesheets/only%2ecss",
      "/admin/stylesheets/%6fnly.css", "/admin/stylesheets/..", "/admin/stylesheets/%2e%2e",
      "/admin/stylesheets/a..b", "/admin/stylesheets/only.css/edit",
      "/admin/stylesheets/only.css/", "/admin/stylesheets//only.css",
      "/admin/stylesheets/only.css/history/x", "/admin/stylesheets/only%00.css",
      "/admin/stylesheets/a%2fb", "/admin/stylesheetsx"};
  for (const char * path : not_names) {
    EXPECT_TRUE(NotThere(Get(path, ada_))) << path;
    Reply post = PostRaw(path, ada_, "do=save&text=x");
    EXPECT_EQ(404, post.status) << path;
  }
  const std::string long_name(300, 'a');
  EXPECT_TRUE(NotThere(Get(("/admin/stylesheets/" + long_name).c_str(), ada_)));
  EXPECT_EQ("a{}", Served("/assets/site/only.css"));
  /* The signed-out visitor is told nothing of any of it. */
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets/only.css", std::string())));
}

TEST_F(AssetAdmin, ANarrowGrantEditsOnlyTheFilesItNamesAndTheRestAreNotThere) {
  Open();
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "mine.css", "a{}").status);
  ASSERT_EQ(303, Create("/admin/stylesheets", ada_, "yours.css", "b{}").status);
  GDEFCORE_Catalog * catalog = NULL;
  GDEFCORE_Keyring * keyring;
  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  char account[GDEFCORE_PUBLIC_ID_LEN + 1];
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
  keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, "narrow", 6, NULL));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant_when(catalog, "narrow", 6, GDEFCORE_ASSET_CSS_EDIT_LABEL,
          strlen(GDEFCORE_ASSET_CSS_EDIT_LABEL), "(= field.name \"mine.css\")", 25,
          "core.role", 9));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_principal_add_role(catalog, principal,
                             strlen(principal), "narrow", 6));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_account_bind(catalog, keyring, root_, sizeof root_,
                             principal, strlen(principal), "nara", 4, "pw-pw-pw", 8,
                             account));
  gdefcore_catalog_destroy(catalog);
  const std::string nara = CookieLine(Login("nara", "pw-pw-pw"));
  Reply list = Get("/admin/stylesheets", nara);
  EXPECT_EQ(200, list.status);
  EXPECT_TRUE(Has(list.body, ">mine.css</a>"));
  EXPECT_FALSE(Has(list.body, "yours.css"));
  EXPECT_FALSE(NotThere(Get("/admin/stylesheets/mine.css", nara)));
  EXPECT_TRUE(NotThere(Get("/admin/stylesheets/yours.css", nara)));
  Reply write = Post("/admin/stylesheets/mine.css", nara,
      TokenOf(Get("/admin/stylesheets/mine.css", nara).body, "save"),
      "&do=save&text=" + Enc("n{}"));
  EXPECT_EQ(303, write.status);
  EXPECT_EQ("n{}", Served("/assets/site/mine.css"));
  EXPECT_EQ("b{}", Served("/assets/site/yours.css"));
  /* A new file is a name the grant does not cover, so the apply refuses it. */
  Reply made = Create("/admin/stylesheets", nara, "third.css", "c{}");
  EXPECT_EQ(200, made.status);
  EXPECT_TRUE(Has(made.body, "That edit is not allowed.") ||
              Has(made.body, "was refused"));
  EXPECT_EQ("<404>", Served("/assets/site/third.css"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
