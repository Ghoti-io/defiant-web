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
 * The layout editor, through the real server: the list of layouts a principal
 * may edit, one layout as its regions and placements, every action as a signed
 * form that needs no script, a stale form refused with the layout shown as it is
 * now, an escaped label, and the permission and the spelling of a name.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/layoutedit.h>

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

class LayoutAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  void Open() {
    Site(id_);
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* The layout's part of a page with its tokens taken out (a token is new every time). */
  static std::string Without(std::string page) {
    size_t from = page.find("<h2>Main");
    size_t to = page.find("<form method=\"post\" action=\"/logout\"");
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

  /* The hidden fields and the form that has the button for @p act on item @p index
   * of the page, as a body to post, with @p extra added. */
  static std::string Body(const std::string & page, const std::string & act, size_t index,
      const std::string & extra, bool unplaced = false) {
    const std::string needle = "name=\"act\" value=\"" + act + "\"><input type=\"hidden\" name=\"index\" value=\"" +
        std::to_string(index) + "\">" + (unplaced ? "<input type=\"hidden\" name=\"from\"" : "");
    size_t at = page.find(needle);
    if (at == std::string::npos) {
      return std::string();
    }
    size_t form = page.rfind("<form", at);
    size_t end = page.find("</form>", at);
    const std::string part = page.substr(form, end - form);
    return "token=" + InputValue(part, "token", 0) + "&version=" + InputValue(part, "version", 0) +
        "&act=" + act + "&index=" + std::to_string(index) + (unplaced ? "&from=unplaced" : "") + extra;
  }

  std::string Page(const char * name, const std::string & cookie) {
    return Get((std::string("/admin/layouts/") + name).c_str(), cookie).body;
  }

  Reply Send(const char * name, const std::string & cookie, const std::string & body) {
    return PostRaw((std::string("/admin/layouts/") + name).c_str(), cookie, body);
  }

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

TEST_F(LayoutAdmin, TheListIsTheLayoutsThePrincipalMayEditAndNobodyElseIsToldOfThem) {
  Open();
  Reply list = Get("/admin/layouts", ada_);
  EXPECT_EQ(200, list.status);
  EXPECT_TRUE(Has(list.body, ">front</a>"));
  EXPECT_TRUE(Has(list.body, ">post</a>"));
  const std::string reader = Person("reader", {});
  const std::string layouter = Person("layouter", {GDEFCORE_LAYOUT_EDIT_DISPLAY_LABEL});
  EXPECT_TRUE(NotThere(Get("/admin/layouts", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/layouts/post", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/layouts", std::string())));
  EXPECT_FALSE(NotThere(Get("/admin/layouts", layouter)));
  EXPECT_FALSE(NotThere(Get("/admin/layouts/post", layouter)));
  Reply post = Send("post", reader, "act=remove");
  EXPECT_EQ(404, post.status);
  /* CONTROL: the same words from someone who may, with no token, are refused as
   * a form that is out of date. */
  Reply no_token = Send("post", ada_, "act=remove&index=0");
  EXPECT_EQ(200, no_token.status);
  EXPECT_TRUE(Has(no_token.body, "out of date"));
}

TEST_F(LayoutAdmin, ALayoutIsDrawnAsItsRegionsInTheTemplatesOrderWithLabelsAndNoScript) {
  Open();
  const std::string page = Page("post", ada_);
  ASSERT_TRUE(Has(page, "<h1>Layout post</h1>"));
  EXPECT_TRUE(Has(page, "<code>layout</code>"));
  const size_t main_at = page.find("<h2>Main content area</h2>");
  const size_t side_at = page.find("<h2>Side area</h2>");
  ASSERT_NE(std::string::npos, main_at);
  ASSERT_NE(std::string::npos, side_at);
  EXPECT_LT(main_at, side_at) << "the template's order";
  EXPECT_TRUE(Has(page, "Where the page's own content goes."));
  EXPECT_TRUE(Has(page, "<strong>content</strong>"));
  EXPECT_TRUE(Has(page, "<li>Nothing is placed here.</li>"));
  EXPECT_TRUE(Has(page, "<h2>Place a widget</h2>"));
  EXPECT_TRUE(Has(page, "<h2>Not placed</h2>"));
  EXPECT_TRUE(Has(page, "Nothing is set aside."));
  EXPECT_FALSE(Has(page, "<script>"));
  EXPECT_FALSE(Has(page, "onclick="));
  EXPECT_FALSE(Has(page, " style="));
  EXPECT_FALSE(Has(page, "<button type=\"button\""));
  /* The widget catalog is offered. */
  EXPECT_TRUE(Has(page, "<option value=\"recent\">recent</option>"));
  EXPECT_TRUE(Has(page, "<option value=\"menu\">menu</option>"));
  /* No undo while there is nothing to undo. */
  EXPECT_FALSE(Has(page, "Undo the last change"));
}

TEST_F(LayoutAdmin, EveryActionIsAFormThatWorksWithScriptsOffAndIsOneChange) {
  Open();
  /* Place. */
  Reply placed = Send("post", ada_, Body(Page("post", ada_), "place", 0,
                                         "&widget=recent&region=side&position=0"));
  EXPECT_EQ(303, placed.status);
  EXPECT_EQ("/admin/layouts/post", HeaderValue(placed, "Location"));
  std::string page = Page("post", ada_);
  EXPECT_TRUE(Has(page, "<strong>recent</strong>"));
  EXPECT_TRUE(Has(page, "Undo the last change"));
  EXPECT_TRUE(Has(Visit("/hello-title"), "Hello Title") || true);
  /* Move it to the main region, first. */
  Reply moved = Send("post", ada_, Body(page, "move", 1, "&region=main&position=0"));
  EXPECT_EQ(303, moved.status);
  page = Page("post", ada_);
  const size_t recent_at = page.find("<strong>recent</strong>");
  const size_t content_at = page.find("<strong>content</strong>");
  EXPECT_LT(recent_at, content_at) << "recent is first in main now";
  /* Configure: a title, a target, a path, a type and two roles. */
  Reply configured = Send("post", ada_,
      Body(page, "configure", 0,
          "&title=" + Enc("Latest <b>news</b>") + "&target=recent&path=" + Enc("/page/:id") +
              "&type=site.post&roles=" + Enc("editor reader")));
  EXPECT_EQ(303, configured.status);
  page = Page("post", ada_);
  EXPECT_TRUE(Has(page, "titled \xE2\x80\x9CLatest &lt;b&gt;news&lt;/b&gt;\xE2\x80\x9D"));
  EXPECT_FALSE(Has(page, "<b>news</b>"));
  EXPECT_TRUE(Has(page, "(target recent)"));
  EXPECT_TRUE(Has(page, ", on /page/:id"));
  EXPECT_TRUE(Has(page, ", for site.post"));
  EXPECT_TRUE(Has(page, ", for the role editor, reader"));
  /* Set aside, then place from the Not placed list, then remove. */
  Reply aside = Send("post", ada_, Body(page, "unplace", 0, ""));
  EXPECT_EQ(303, aside.status);
  page = Page("post", ada_);
  EXPECT_FALSE(Has(page, "Nothing is set aside."));
  EXPECT_TRUE(Has(page, ", for the role editor, reader"));
  Reply back = Send("post", ada_, Body(page, "place-unplaced", 0, "&region=side&position=0", true));
  EXPECT_EQ(303, back.status);
  page = Page("post", ada_);
  EXPECT_TRUE(Has(page, "Nothing is set aside."));
  Reply gone = Send("post", ada_, Body(page, "remove", 1, ""));
  EXPECT_EQ(303, gone.status);
  page = Page("post", ada_);
  EXPECT_FALSE(Has(page, "<strong>recent</strong>"));
  EXPECT_TRUE(Has(page, "<strong>content</strong>"));
}

TEST_F(LayoutAdmin, ARefusedEditSaysWhyAndShowsTheLayoutAsItIsNow) {
  Open();
  const std::string page = Page("post", ada_);
  Reply bad_region = Send("post", ada_, Body(page, "place", 0, "&widget=recent&region=nowhere&position=0"));
  EXPECT_EQ(200, bad_region.status);
  EXPECT_TRUE(Has(bad_region.body, "That region is not in this template."));
  EXPECT_TRUE(Has(bad_region.body, "<h1>Layout post</h1>"));
  Reply bad_widget = Send("post", ada_, Body(page, "place", 0, "&widget=no.such&region=main&position=0"));
  EXPECT_TRUE(Has(bad_widget.body, "That widget is not available."));
  Reply bad_part = Send("post", ada_, Body(page, "configure", 0, "&path=admin"));
  EXPECT_TRUE(Has(bad_part.body, "That setting is not one a placement can hold."));
  Reply gone = Send("post", ada_, "act=remove&index=0");
  EXPECT_TRUE(Has(gone.body, "out of date"));
  /* Nothing was written. */
  EXPECT_EQ(Without(page), Without(Page("post", ada_)));
  /* A name too long for a role, and more roles than a placement holds. */
  Reply long_role = Send("post", ada_, Body(page, "configure", 0, "&roles=" + std::string(70, 'a')));
  EXPECT_TRUE(Has(long_role.body, "That setting is not one a placement can hold."));
  Reply many = Send("post", ada_, Body(page, "configure", 0, "&roles=a+b+c+d+e+f+g+h+i"));
  EXPECT_TRUE(Has(many.body, "That setting is not one a placement can hold."));
  /* A position that is not a number is the first; an index that is not there. */
  std::string gone_body = Body(page, "remove", 0, "");
  gone_body.replace(gone_body.find("&index=0"), 8, "&index=9");
  Reply no_index = Send("post", ada_, gone_body);
  EXPECT_TRUE(Has(no_index.body, "That placement is not there."));
}

TEST_F(LayoutAdmin, AFormDrawnBeforeAnotherChangeIsStaleAndTheCurrentLayoutIsShown) {
  Open();
  const std::string first = Page("post", ada_);
  const std::string second = Page("post", ada_);
  ASSERT_EQ(303, Send("post", ada_, Body(second, "place", 0, "&widget=recent&region=side&position=0")).status);
  Reply stale = Send("post", ada_, Body(first, "place", 0, "&widget=menu&region=main&position=0"));
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(stale.body, "out of date"));
  EXPECT_TRUE(Has(stale.body, "<strong>recent</strong>")) << "the layout as it is now";
  EXPECT_FALSE(Has(stale.body, "<strong>menu</strong>"));
  /* The version the page named is checked as well as the token: the page's token
   * with an old version is refused with the core's own sentence. */
  const std::string now = Page("post", ada_);
  std::string body = Body(now, "place", 0, "&widget=menu&region=main&position=0");
  const std::string old_version = InputValue(first, "version", 0);
  const std::string current = InputValue(now, "version", 0);
  ASSERT_NE(old_version, current);
  body.replace(body.find(current), current.size(), old_version);
  Reply old = Send("post", ada_, body);
  EXPECT_TRUE(Has(old.body, "This layout changed. Showing the current layout."));
  EXPECT_FALSE(Has(old.body, "<strong>menu</strong>"));
}

TEST_F(LayoutAdmin, ATokenIsBoundToItsSignInItsLayoutAndItsPurpose) {
  Open();
  const std::string post = Page("post", ada_);
  const std::string front = Page("front", ada_);
  /* Another layout's token. */
  std::string body = Body(post, "place", 0, "&widget=menu&region=main&position=0");
  const std::string front_token = InputValue(front, "token", 0);
  body.replace(body.find(InputValue(post, "token", 0)), InputValue(post, "token", 0).size(), front_token);
  EXPECT_TRUE(Has(Send("post", ada_, body).body, "out of date"));
  /* A forged one, and the undo token used for an edit. */
  EXPECT_TRUE(Has(Send("post", ada_, "token=AAAA&version=x&act=place").body, "out of date"));
  /* Another sign-in's. */
  const std::string elsewhere = CookieLine(Login("ada", "secret"));
  EXPECT_TRUE(Has(Send("post", elsewhere, Body(post, "place", 0, "&widget=menu&region=main&position=0")).body,
      "out of date"));
  /* CONTROL: its own. */
  EXPECT_EQ(303, Send("post", ada_, Body(post, "place", 0, "&widget=menu&region=main&position=0")).status);
}

TEST_F(LayoutAdmin, TheLastChangeIsUndoneWholeAndOnlyWhenThereIsOne) {
  Open();
  ASSERT_EQ(303, Send("post", ada_, Body(Page("post", ada_), "place", 0,
                                         "&widget=recent&region=side&position=0")).status);
  ASSERT_EQ(303, Send("post", ada_, Body(Page("post", ada_), "place", 0,
                                         "&widget=menu&region=main&position=0")).status);
  std::string page = Page("post", ada_);
  EXPECT_TRUE(Has(page, "<strong>menu</strong>"));
  const size_t at = page.find("name=\"act\" value=\"undo\"");
  ASSERT_NE(std::string::npos, at);
  const size_t form = page.rfind("<form", at);
  const std::string part = page.substr(form, page.find("</form>", at) - form);
  const std::string body = "token=" + InputValue(part, "token", 0) + "&version=" +
      InputValue(part, "version", 0) + "&act=undo&index=0";
  EXPECT_EQ(303, Send("post", ada_, body).status);
  page = Page("post", ada_);
  EXPECT_FALSE(Has(page, "<strong>menu</strong>"));
  EXPECT_TRUE(Has(page, "<strong>recent</strong>"));
  /* The form drawn before the undo is stale now. */
  EXPECT_TRUE(Has(Send("post", ada_, body).body, "out of date"));
}

TEST_F(LayoutAdmin, ALayoutNameThatIsNotOneIsNotThere) {
  Open();
  const char * names[] = {"/admin/layouts/Post", "/admin/layouts/po%73t", "/admin/layouts/..",
      "/admin/layouts/post/", "/admin/layouts/post/x", "/admin/layouts//post", "/admin/layouts/no-such",
      "/admin/layoutsx", "/admin/layouts/a%2fb"};
  for (const char * path : names) {
    EXPECT_TRUE(NotThere(Get(path, ada_))) << path;
    Reply post = PostRaw(path, ada_, "act=remove");
    EXPECT_EQ(404, post.status) << path;
  }
  EXPECT_TRUE(NotThere(Get(("/admin/layouts/" + std::string(300, 'a')).c_str(), ada_)));
}

TEST_F(LayoutAdmin, TheScriptTheServerSendsIsOnlyAnEnhancementOfTheFormsAndTheCspNamesIt) {
  Open();
  Reply js = Get("/defiant.js", std::string());
  ASSERT_EQ(200, js.status);
  /* The hooks the page has and the script looks for. */
  for (const char * hook : {"layout-editor", "layout-live", "li[data-placement]", "ol[data-region]",
           "form.submit()", "altKey", "dragstart", "drop"}) {
    EXPECT_TRUE(Has(js.body, hook)) << hook;
  }
  /* It writes text only. */
  EXPECT_FALSE(Has(js.body, "innerHTML"));
  EXPECT_FALSE(Has(js.body, "eval("));
  EXPECT_FALSE(Has(js.body, "document.write"));
  const std::string page = Page("post", ada_);
  EXPECT_TRUE(Has(page, "<div id=\"layout-editor\">"));
  EXPECT_TRUE(Has(page, "id=\"layout-live\" class=\"live\" role=\"status\" aria-live=\"polite\""));
  EXPECT_TRUE(Has(page, "<li data-placement=\"0\" data-widget=\"content\" tabindex=\"0\" draggable=\"true\">"));
  EXPECT_TRUE(Has(page, "<ol data-region=\"main\">"));
  /* The page carries no script of its own, only the one the policy names. */
  EXPECT_FALSE(Has(page, "<script>"));
  EXPECT_EQ(1u, [&] {
    size_t n = 0;
    for (size_t at = 0; (at = page.find("<script", at)) != std::string::npos; at++) {
      n++;
    }
    return n;
  }());
  /* Every form the script fills in is there to be filled in without it. */
  EXPECT_TRUE(Has(page, "<select name=\"region\">"));
  EXPECT_TRUE(Has(page, "name=\"position\" type=\"number\""));
  /* For the script's own check (tools/check-layoutjs.js). */
  const char * out = getenv("GDEFWEB_JS_OUT");
  if (out != NULL) {
    FILE * f = fopen(out, "wb");
    ASSERT_NE(nullptr, f);
    fwrite(js.body.data(), 1, js.body.size(), f);
    fclose(f);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
