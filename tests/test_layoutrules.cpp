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
 * The selector rules and the checker, through the real server: the rules in the
 * order the engine uses with the number it ranks them by, a rule added, changed
 * and removed by a signed form, a form about a rule that has changed since refused,
 * and the question "which layout does this path get?" answered with the offers in
 * order and why each other lost.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/layoutwhy.h>

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

class LayoutRules : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  void Open() {
    Site(id_);
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* The table of rules with its tokens taken out (a token is new every time). */
  static std::string Rows(std::string page) {
    size_t from = page.find("<table");
    size_t to = page.find("</table>");
    page = page.substr(from, to == std::string::npos ? std::string::npos : to - from);
    const std::string key = "name=\"token\" value=\"";
    for (size_t at = page.find(key); at != std::string::npos; at = page.find(key, at)) {
      size_t end = page.find('"', at + key.size());
      page.erase(at + key.size(), end - at - key.size());
      at += key.size();
    }
    return page;
  }

  static bool NotThere(const Reply & r) {
    return r.status == 200 && r.body.find("<h1>") == std::string::npos;
  }

  Reply Page() { return Get("/admin/layout-rules", ada_); }

  /* The token of the first form on the page that has @p act, as a body start. */
  static std::string Start(const std::string & page, const std::string & act) {
    const std::string needle = "name=\"act\" value=\"" + act + "\"";
    size_t at = page.find(needle);
    if (at == std::string::npos) {
      return std::string();
    }
    size_t form = page.rfind("<form", at);
    return "token=" + InputValue(page.substr(form, at - form), "token", 0) + "&act=" + act;
  }

  /* The row of rule @p name on the page, with its was fingerprint, as a body. */
  static std::string Edit(const std::string & page, const std::string & act, const std::string & name) {
    const std::string needle = "name=\"name\" value=\"" + name + "\"><input type=\"hidden\" name=\"was\" value=\"";
    size_t at = page.find("name=\"act\" value=\"" + act + "\"><input type=\"hidden\" name=\"name\" value=\"" + name + "\">");
    if (at == std::string::npos) {
      return std::string();
    }
    size_t form = page.rfind("<form", at);
    const std::string part = page.substr(form, page.find("</form>", at) - form);
    return "token=" + InputValue(part, "token", 0) + "&act=" + act + "&name=" + Enc(name) +
        "&was=" + InputValue(part, "was", 0);
  }

  Reply Send(const std::string & body) { return PostRaw("/admin/layout-rules", ada_, body); }

  std::string Person(const char * name, const std::vector<const char *> & labels) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Keyring * keyring;
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    const std::string role = std::string("role-") + name;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, role.c_str(), role.size(), NULL));
    for (const char * label : labels) {
      EXPECT_EQ(GDEFCORE_OK, gdefcore_access_grant(catalog, role.c_str(), role.size(), label,
                                 strlen(label), "core.role", 9));
    }
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_add_role(catalog, principal, strlen(principal),
                               role.c_str(), role.size()));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal,
                               strlen(principal), name, strlen(name), "pw-pw-pw", 8, account));
    gdefcore_catalog_destroy(catalog);
    return CookieLine(Login(name, "pw-pw-pw"));
  }
};

}  // namespace

TEST_F(LayoutRules, TheRulesAreInTheEnginesOrderWithTheRankItGivesEachAndOnlyForWhoMayEditThem) {
  Open();
  Reply page = Page();
  EXPECT_EQ(200, page.status);
  EXPECT_TRUE(Has(page.body, "<h1>Layout rules</h1>"));
  EXPECT_TRUE(Has(page.body, "<td>site.front</td>"));
  EXPECT_TRUE(Has(page.body, "<td>site.post</td>"));
  /* The kit's front page rule is for the root alone, exact; the post rule is a
   * pattern of its own: the order is rank, then weight, then name. */
  ASSERT_EQ(303, Send(Start(page.body, "add") + "&name=docs&path=" + Enc("/docs/*") +
                      "&layout=post&weight=0").status);
  ASSERT_EQ(303, Send(Start(Page().body, "add") + "&name=zdeep&path=" + Enc("/docs/a/b/*") +
                      "&layout=front&weight=0").status);
  page = Page();
  const size_t deep = page.body.find("<td>site.zdeep</td>");
  const size_t docs = page.body.find("<td>site.docs</td>");
  ASSERT_NE(std::string::npos, deep);
  ASSERT_NE(std::string::npos, docs);
  EXPECT_LT(deep, docs) << "the more specific first, though its name is later";
  /* The rank is a plain number in its own cell. */
  EXPECT_TRUE(Has(page.body, "</td><td>front</td><td></td><td>no</td><td>0</td><td>"));
  /* Someone who may not edit layouts is told nothing of it. */
  const std::string reader = Person("reader", {});
  EXPECT_TRUE(NotThere(Get("/admin/layout-rules", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/layout-rules", std::string())));
  EXPECT_EQ(404, PostRaw("/admin/layout-rules", reader, "act=add").status);
}

TEST_F(LayoutRules, ARuleIsAddedChangedAndRemovedByASignedFormAndAStaleOneIsRefused) {
  Open();
  ASSERT_EQ(303, Send(Start(Page().body, "add") + "&name=docs&path=" + Enc("/docs/*") +
                      "&layout=post&weight=3").status);
  std::string page = Page().body;
  EXPECT_TRUE(Has(page, "<td>site.docs</td><td>/docs/*</td><td>post</td>"));
  EXPECT_TRUE(Has(page, "<td>3</td>"));
  /* Change it. */
  const std::string drawn = Edit(page, "edit", "site.docs");
  ASSERT_FALSE(drawn.empty());
  ASSERT_EQ(303, Send(drawn + "&path=" + Enc("/docs/*") + "&layout=front&type=&weight=7").status);
  page = Page().body;
  EXPECT_TRUE(Has(page, "<td>site.docs</td><td>/docs/*</td><td>front</td>"));
  EXPECT_TRUE(Has(page, "<td>7</td>"));
  /* The form drawn before that change is stale, for a change and for a removal. */
  Reply stale = Send(drawn + "&path=" + Enc("/other") + "&layout=post&weight=0");
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(stale.body, "That rule changed"));
  EXPECT_TRUE(Has(Page().body, "<td>/docs/*</td>")) << "nothing was written";
  /* Remove it. */
  ASSERT_EQ(303, Send(Edit(Page().body, "remove", "site.docs")).status);
  EXPECT_FALSE(Has(Page().body, "<td>site.docs</td>"));
  /* A rule that is not there. */
  EXPECT_TRUE(Has(Send(Edit(Page().body, "remove", "site.front") + "x").body, "That rule changed"));
  EXPECT_TRUE(Has(Page().body, "<td>site.front</td>"));
}

TEST_F(LayoutRules, TheTypeOfARuleIsAChoiceOfTheTypesTheSiteHasAndAKeptTypeIsNeverDroppedUnseen) {
  Open();
  /* A content type the site defines is offered; a config type is not. */
  {
    GDEFCORE_Catalog * catalog = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    GDEFCORE_Field field;
    memset(&field, 0, sizeof field);
    field.size = sizeof field;
    field.name = "title";
    field.name_len = 5;
    field.required = 1;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_type_define(catalog, "site.note", 9, GDEFCORE_TYPE_CONTENT, &field, 1));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_type_define(catalog, "site.setting", 12, GDEFCORE_TYPE_CONFIG, &field, 1));
    gdefcore_catalog_destroy(catalog);
  }
  const std::string page = Page().body;
  const size_t form = page.find("<h2>A new rule</h2>");
  ASSERT_NE(std::string::npos, form);
  const std::string add = page.substr(form, page.find("</form>", form) - form);
  EXPECT_TRUE(Has(add, "<select name=\"type\"><option value=\"\">Any type</option>")) << add;
  EXPECT_TRUE(Has(add, "<option value=\"site.note\">site.note</option>")) << add;
  EXPECT_FALSE(Has(add, "site.setting")) << "a config type is not a type of page";
  EXPECT_FALSE(Has(add, "<input name=\"type\"")) << "it is a choice, not typed text";
  /* A rule for the type is saved by the form's value and shown selected when changed. */
  ASSERT_EQ(303, Send(Start(page, "add") + "&name=notes&path=" + Enc("/notes/*") +
                      "&layout=post&type=site.note").status);
  const std::string drawn = Page().body;
  EXPECT_TRUE(Has(drawn, "<option value=\"site.note\" selected>site.note</option>"));
  /* A rule whose type is not defined (its module gone) keeps it, said so, rather than dropping it. */
  ASSERT_EQ(303, Send(Start(drawn, "add") + "&name=old&path=" + Enc("/old/*") +
                      "&layout=post&type=site.gone").status);
  EXPECT_TRUE(Has(Page().body, "<option value=\"site.gone\" selected>site.gone (not defined)</option>"));
  /* CONTROL: the same page without that rule names no such type. */
  ASSERT_EQ(303, Send(Edit(Page().body, "remove", "site.old")).status);
  EXPECT_FALSE(Has(Page().body, "site.gone"));
}

TEST_F(LayoutRules, ARuleThatIsNotOneIsRefusedWithAnoticeAndNothingIsWritten) {
  Open();
  const std::string before = Page().body;
  const char * bad[] = {
      "&name=&path=%2Fa&layout=post", "&name=Docs&path=%2Fa&layout=post",
      "&name=docs&path=docs&layout=post", "&name=docs&path=%2Fa%2F%3ABad&layout=post",
      "&name=docs&path=%2Fa&layout=", "&name=docs&path=%2Fa&layout=Not+A+Layout",
      "&name=do+cs&path=%2Fa&layout=post", "&name=docs&path=%2Fa&layout=post&type=bad",
      "&name=docs&path=%2Fa&layout=post&weight=x"};
  for (const char * body : bad) {
    Reply r = Send(Start(Page().body, "add") + body);
    EXPECT_EQ(200, r.status) << body;
    EXPECT_TRUE(Has(r.body, "refused") || Has(r.body, "not understood")) << body;
    EXPECT_FALSE(Has(r.body, "<td>site.docs</td>")) << body;
  }
  EXPECT_EQ(Rows(before), Rows(Page().body)) << "nothing was written";
  /* CONTROL: the same words made right. */
  EXPECT_EQ(303, Send(Start(Page().body, "add") + "&name=docs&path=%2Fa&layout=post").status);
}

TEST_F(LayoutRules, ATokenIsBoundToItsSignInAndItsPurposeAndAForgedOneWritesNothing) {
  Open();
  const std::string page = Page().body;
  const std::string start = Start(page, "add");
  EXPECT_TRUE(Has(Send("token=AAAA&act=add&name=docs&path=%2Fa&layout=post").body, "out of date"));
  EXPECT_TRUE(Has(Send("act=add&name=docs&path=%2Fa&layout=post").body, "out of date"));
  const std::string elsewhere = CookieLine(Login("ada", "secret"));
  EXPECT_TRUE(Has(PostRaw("/admin/layout-rules", elsewhere, start + "&name=docs&path=%2Fa&layout=post").body,
      "out of date"));
  EXPECT_FALSE(Has(Page().body, "<td>site.docs</td>"));
  EXPECT_EQ(303, Send(start + "&name=docs&path=%2Fa&layout=post").status);
}

TEST_F(LayoutRules, TheCheckerSaysWhichLayoutAPathGetsAndWhyEachOtherLost) {
  Open();
  ASSERT_EQ(303, Send(Start(Page().body, "add") + "&name=special&path=" + Enc("/hello-title") +
                      "&layout=front&exact=1").status);
  /* The front page, as a visitor. */
  Reply front = Send(Start(Page().body, "check") + "&path=%2F&as=anonymous");
  EXPECT_EQ(200, front.status);
  EXPECT_TRUE(Has(front.body, "The page gets the layout <strong>front</strong>."));
  EXPECT_TRUE(Has(front.body, "<td>front</td><td>site.front</td>"));
  EXPECT_TRUE(Has(front.body, "chosen"));
  /* A post: the exact rule wins over the kit's, and the kit's rule says why it lost. */
  Reply post = Send(Start(Page().body, "check") + "&path=" + Enc("/hello-title") + "&as=anonymous");
  EXPECT_TRUE(Has(post.body, "The page gets the layout <strong>front</strong>."));
  EXPECT_TRUE(Has(post.body, "<td>front</td><td>site.special</td>"));
  EXPECT_TRUE(Has(post.body, "a less specific pattern"));
  /* As the person signed in. */
  Reply me = Send(Start(Page().body, "check") + "&path=%2F&as=me");
  EXPECT_TRUE(Has(me.body, "The page gets the layout <strong>front</strong>."));
  EXPECT_TRUE(Has(me.body, "selected>me</option>"));
  /* A path that is not a path, and the one the checker asks again with. */
  Reply bad = Send(Start(Page().body, "check") + "&path=no-slash&as=anonymous");
  EXPECT_TRUE(Has(bad.body, "A path starts with a slash."));
  EXPECT_FALSE(Has(bad.body, "The page gets the layout"));
  /* The renderer agrees: the page it draws for that path is the layout named. */
  EXPECT_TRUE(Has(Get("/hello-title", std::string()).body, "class=\"main\""));
  /* An escaped path comes back escaped. */
  Reply odd = Send(Start(Page().body, "check") + "&path=" + Enc("/<b>x</b>") + "&as=anonymous");
  EXPECT_FALSE(Has(odd.body, "<b>x</b>"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
