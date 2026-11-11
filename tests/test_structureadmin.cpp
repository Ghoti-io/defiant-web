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
 * The page structure editor, through the real server: the templates a principal
 * may edit, one structure as a preview and rows of cells, every action a signed
 * form that needs no script, a stale form refused, a rename that carries a
 * layout's placements, a removal that sets them aside and says so, a template
 * written by hand that is never edited by accident, and a new template.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/layoutedit.h>
#include <ghoti.io/defiant-core/siteasset.h>
#include <ghoti.io/defiant-core/structure.h>

namespace {

class StructureAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  void Open() {
    Site(id_);
    GrantTemplateKinds(id_, {"core.page"});
    {
      /* The generated stylesheet is a site file, so its permission is asked too. */
      GDEFCORE_Catalog * catalog = NULL;
      ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
      ASSERT_EQ(GDEFCORE_OK, gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_ASSET_CSS_EDIT_LABEL,
                                 strlen(GDEFCORE_ASSET_CSS_EDIT_LABEL), "core.role", 9));
      gdefcore_catalog_destroy(catalog);
    }
    ada_ = CookieLine(Login("ada", "secret"));
  }

  static std::string Notice(const Reply & r) {
    const size_t at = r.body.find("class=\"notice\">");
    return at == std::string::npos ? std::string("(no notice)") : r.body.substr(at, 200);
  }

  static bool NotThere(const Reply & r) {
    return r.status == 200 && r.body.find("<h1>") == std::string::npos;
  }

  std::string Page(const char * name, const std::string & cookie) {
    return Get((std::string("/admin/structures/") + name).c_str(), cookie).body;
  }

  /* The body of the @p nth form of @p page that carries the action @p act: its
   * hidden fields as they are, then @p extra. */
  static std::string Form(const std::string & page, const std::string & act, size_t nth,
      const std::string & extra) {
    const std::string needle = "name=\"act\" value=\"" + act + "\">";
    size_t at = std::string::npos;
    for (size_t k = 0, from = 0; k <= nth; k++) {
      at = page.find(needle, from);
      if (at == std::string::npos) {
        return std::string();
      }
      from = at + needle.size();
    }
    const size_t form = page.rfind("<form", at);
    const size_t end = page.find("</form>", at);
    const std::string part = page.substr(form, end - form);
    std::string body;
    const std::string key = "<input type=\"hidden\" name=\"";
    for (size_t p = part.find(key); p != std::string::npos; p = part.find(key, p + 1)) {
      const size_t n0 = p + key.size();
      const size_t n1 = part.find('"', n0);
      const std::string field = part.substr(n0, n1 - n0);
      const size_t v0 = part.find("value=\"", n1) + 7;
      const size_t v1 = part.find('"', v0);
      body += (body.empty() ? "" : "&") + field + "=" + part.substr(v0, v1 - v0);
    }
    return body + extra;
  }

  Reply Send(const char * name, const std::string & cookie, const std::string & body) {
    return PostRaw((std::string("/admin/structures/") + name).c_str(), cookie, body);
  }

  /* Draw the page, find the form and send it. */
  Reply Do(const char * name, const std::string & cookie, const std::string & act, size_t nth,
      const std::string & extra) {
    const std::string body = Form(Page(name, cookie), act, nth, extra);
    EXPECT_FALSE(body.empty()) << act;
    return Send(name, cookie, body);
  }

  std::string LayoutPage(const char * name) {
    return Get((std::string("/admin/layouts/") + name).c_str(), ada_).body;
  }

  /* The cells of the page's structure, as `region/width` in order. */
  static std::string Cells(const std::string & page) {
    std::string out;
    const std::string key = "<li data-cell=\"";
    for (size_t at = page.find(key); at != std::string::npos; at = page.find(key, at + 1)) {
      const size_t e = page.find('"', at + key.size());
      out += (out.empty() ? "" : " ") + page.substr(at + key.size(), e - at - key.size());
    }
    return out;
  }


  /* A widget placed in the side region of layout "post" by the layout editor's own form. */
  void PlaceInSide() {
    const std::string page = LayoutPage("post");
    const std::string needle = "name=\"act\" value=\"place\">";
    const size_t at = page.find(needle);
    ASSERT_NE(std::string::npos, at);
    const size_t form = page.rfind("<form", at);
    const std::string part = page.substr(form, page.find("</form>", at) - form);
    const std::string body = "token=" + InputValue(part, "token", 0) + "&version=" +
        InputValue(part, "version", 0) + "&act=place&index=0&widget=recent&region=side&position=0";
    ASSERT_EQ(303, PostRaw("/admin/layouts/post", ada_, body).status);
  }

  /* The body of the undo form of the page of /admin/undo that carries @p heading, or "". */
  std::string UndoBody(const std::string & cookie, const char * heading) {
    const std::string page = Get("/admin/undo", cookie).body;
    const size_t at = page.find(heading);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t form = page.find("<form", at);
    const std::string part = page.substr(form, page.find("</form>", form) - form);
    return "token=" + InputValue(part, "token", 0);
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

TEST_F(StructureAdmin, TheListIsTheTemplatesThePrincipalMayEditAndNobodyElseIsToldOfThem) {
  Open();
  Reply list = Get("/admin/structures", ada_);
  EXPECT_EQ(200, list.status);
  EXPECT_TRUE(Has(list.body, ">layout</a>"));
  const std::string reader = Person("reader", {});
  EXPECT_TRUE(NotThere(Get("/admin/structures", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/structures/layout", reader)));
  EXPECT_TRUE(NotThere(Get("/admin/structures", std::string())));
  EXPECT_EQ(404, Send("layout", reader, "act=custom").status);
  EXPECT_EQ(404, PostRaw("/admin/structures", reader, "name=x").status);
  /* A page that is not a template, and spellings that are not a name. */
  for (const char * path : {"/admin/structures/Layout", "/admin/structures/no-such",
           "/admin/structures/lay%6Fut", "/admin/structures/layout/", "/admin/structures/..",
           "/admin/structures/a%2fb", "/admin/structuresx"}) {
    EXPECT_TRUE(NotThere(Get(path, ada_))) << path;
    EXPECT_EQ(404, PostRaw(path, ada_, "act=custom").status) << path;
  }
}

TEST_F(StructureAdmin, ATemplateWithNoStructureStartsFromAPointAndIsDrawnAsPreviewAndRows) {
  Open();
  std::string page = Page("layout", ada_);
  EXPECT_TRUE(Has(page, "has no structure yet"));
  EXPECT_FALSE(Has(page, "id=\"structure-preview\""));
  EXPECT_TRUE(Has(page, "<option value=\"holy-grail\">"));
  Reply started = Do("layout", ada_, "start", 0, "&region=two-columns-right");
  EXPECT_EQ(303, started.status) << Notice(started);
  EXPECT_EQ("/admin/structures/layout", HeaderValue(started, "Location"));
  page = Page("layout", ada_);
  EXPECT_TRUE(Has(page, ">Row 1: stack</h2>"));
  EXPECT_EQ("main side", Cells(page));
  EXPECT_TRUE(Has(page, "<div id=\"structure-preview\" class=\"sp\">"));
  EXPECT_TRUE(Has(page, "<div class=\"sp-row\" data-bp=\"720\">"));
  EXPECT_TRUE(Has(page, "<div class=\"sp-cell w-8 st-1\">Main content area</div>"));
  EXPECT_TRUE(Has(page, "<input id=\"structure-width\" type=\"range\""));
  EXPECT_FALSE(Has(page, "<script>"));
  /* The generated pair is the structure's. */
  EXPECT_TRUE(Has(Get("/admin/stylesheets", ada_).body, "layout-layout.css"));
  /* The slider is the only script, and it writes no markup. */
  Reply js = Get("/defiant.js", std::string());
  EXPECT_TRUE(Has(js.body, "structure-preview"));
  EXPECT_TRUE(Has(js.body, "structure-width"));
}

TEST_F(StructureAdmin, EveryActionIsASignedFormThatWorksWithScriptsOff) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  /* Width, stacked place, a cell, a row, moves, a behavior. */
  EXPECT_EQ(303, Do("layout", ada_, "set-width", 0, "&width=6").status);
  EXPECT_TRUE(Has(Page("layout", ada_), "<div class=\"sp-cell w-6 st-1\">"));
  EXPECT_EQ(303, Do("layout", ada_, "add-cell", 0, "&region=extra&width=2").status);
  EXPECT_EQ("main side extra", Cells(Page("layout", ada_)));
  EXPECT_EQ(303, Do("layout", ada_, "set-stacked", 2, "&stacked=1").status);
  EXPECT_TRUE(Has(Page("layout", ada_), "<div class=\"sp-cell w-2 st-1\">"));
  EXPECT_EQ(303, Do("layout", ada_, "move-cell", 0, "").status) << "the first cell moves later";
  EXPECT_EQ("side main extra", Cells(Page("layout", ada_)));
  EXPECT_EQ(303, Do("layout", ada_, "add-row", 0, "&behavior=wrap&region=card").status);
  EXPECT_EQ("side main extra card", Cells(Page("layout", ada_)));
  EXPECT_EQ(303, Do("layout", ada_, "set-row", 0, "&behavior=fixed").status);
  EXPECT_TRUE(Has(Page("layout", ada_), ">Row 1: fixed</h2>"));
  EXPECT_EQ(303, Do("layout", ada_, "move-row", 0, "").status);
  EXPECT_EQ("card side main extra", Cells(Page("layout", ada_)));
  EXPECT_EQ(303, Do("layout", ada_, "remove-row", 0, "").status);
  EXPECT_EQ("side main extra", Cells(Page("layout", ada_)));
  EXPECT_EQ(303, Do("layout", ada_, "remove-cell", 0, "").status);
  EXPECT_EQ("main extra", Cells(Page("layout", ada_)));
  /* A refused edit says why and shows the structure as it is. */
  const std::string before = Cells(Page("layout", ada_));
  Reply refused = Do("layout", ada_, "add-cell", 0, "&region=main&width=1");
  EXPECT_EQ(200, refused.status);
  EXPECT_TRUE(Has(refused.body, "A region is used once in a structure."));
  EXPECT_EQ(before, Cells(Page("layout", ada_)));
  Reply wide = Do("layout", ada_, "add-cell", 0, "&region=wide&width=12");
  EXPECT_TRUE(Has(wide.body, "more than 12 columns"));
}

TEST_F(StructureAdmin, ARenameCarriesALayoutsPlacementsAndARemovalSetsThemAsideAndSaysSo) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  /* Put a widget in the layout's side region through the layout editor's own form. */
  {
    const std::string page = LayoutPage("post");
    const std::string needle = "name=\"act\" value=\"place\">";
    const size_t at = page.find(needle);
    ASSERT_NE(std::string::npos, at);
    const size_t form = page.rfind("<form", at);
    const std::string part = page.substr(form, page.find("</form>", at) - form);
    const std::string body = "token=" + InputValue(part, "token", 0) + "&version=" +
        InputValue(part, "version", 0) + "&act=place&index=0&widget=recent&region=side&position=0";
    ASSERT_EQ(303, PostRaw("/admin/layouts/post", ada_, body).status);
  }
  EXPECT_TRUE(Has(LayoutPage("post"), "<strong>recent</strong>"));
  ASSERT_EQ(303, Do("layout", ada_, "rename-cell", 1, "&region=rail").status);
  EXPECT_EQ("main rail", Cells(Page("layout", ada_)));
  std::string layout = LayoutPage("post");
  EXPECT_TRUE(Has(layout, "<ol data-region=\"rail\">"));
  EXPECT_TRUE(Has(layout, "<strong>recent</strong>"));
  EXPECT_TRUE(Has(layout, "Nothing is set aside."));
  /* Removing the cell: the placement is set aside, and the page says so. */
  Reply removed = Do("layout", ada_, "remove-cell", 1, "");
  EXPECT_EQ(303, removed.status);
  const std::string after = Get("/admin/structures/layout", ada_).body;
  EXPECT_TRUE(Has(after, "Placements set aside, because their region is gone: post recent rail"));
  layout = LayoutPage("post");
  EXPECT_FALSE(Has(layout, "<ol data-region=\"rail\">"));
  EXPECT_TRUE(Has(layout, "<h2>Not placed</h2>"));
  EXPECT_FALSE(Has(layout, "Nothing is set aside."));
  EXPECT_TRUE(Has(layout, "<strong>recent</strong>"));
}


TEST_F(StructureAdmin, TheUndoPageTakesBackTheLastStructureChangeWholeAndOnlyOnce) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  PlaceInSide();
  const std::string before = Cells(Page("layout", ada_));
  EXPECT_EQ("main side", before);
  ASSERT_EQ(303, Do("layout", ada_, "remove-cell", 1, "").status);
  EXPECT_EQ("main", Cells(Page("layout", ada_)));
  EXPECT_TRUE(Has(LayoutPage("post"), "<h2>Not placed</h2>"));
  const std::string form = UndoBody(ada_, "<h2>Page structure</h2>");
  ASSERT_FALSE(form.empty()) << Get("/admin/undo", ada_).body;
  Reply undone = PostRaw("/admin/undo", ada_, form);
  EXPECT_EQ(303, undone.status);
  /* The answer is said on the next page drawn (any page shows it once), so it is read first. */
  EXPECT_TRUE(Has(Get("/admin/undo", ada_).body, "The last page structure change was undone."));
  EXPECT_EQ(before, Cells(Page("layout", ada_))) << "the structure is back";
  const std::string layout = LayoutPage("post");
  EXPECT_TRUE(Has(layout, "<strong>recent</strong>")) << layout;
  EXPECT_TRUE(Has(layout, "Nothing is set aside.")) << "the placement is back in its region";
  /* A change is undone once: the same form again takes nothing back. */
  EXPECT_TRUE(UndoBody(ada_, "<h2>Page structure</h2>").empty());
  Reply again = PostRaw("/admin/undo", ada_, form);
  EXPECT_EQ(303, again.status);
  EXPECT_TRUE(Has(Get("/admin/undo", ada_).body, "There is no page structure change to undo."));
  EXPECT_EQ(before, Cells(Page("layout", ada_)));
}

TEST_F(StructureAdmin, AnUndoIsRefusedOnceALayoutItWroteIsWrittenAgainAndSaysSo) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  PlaceInSide();
  ASSERT_EQ(303, Do("layout", ada_, "remove-cell", 1, "").status);
  const std::string form = UndoBody(ada_, "<h2>Page structure</h2>");
  ASSERT_FALSE(form.empty());
  /* The layout is edited by hand after the change: place the set-aside widget again. */
  {
    const std::string page = LayoutPage("post");
    const std::string needle = "name=\"act\" value=\"place\">";
    const size_t at = page.find(needle);
    ASSERT_NE(std::string::npos, at);
    const size_t f = page.rfind("<form", at);
    const std::string part = page.substr(f, page.find("</form>", at) - f);
    const std::string body = "token=" + InputValue(part, "token", 0) + "&version=" +
        InputValue(part, "version", 0) + "&act=place&index=0&widget=menu&region=main&position=0";
    ASSERT_EQ(303, PostRaw("/admin/layouts/post", ada_, body).status);
  }
  EXPECT_EQ(303, PostRaw("/admin/undo", ada_, form).status);
  EXPECT_TRUE(Has(Get("/admin/undo", ada_).body,
      "That page was changed after that edit, so the edit cannot be undone."));
  EXPECT_EQ("main", Cells(Page("layout", ada_))) << "nothing was undone";
  /* The layout is as it was left (a page's tokens are new each time, so it is read, not compared). */
  const std::string layout = LayoutPage("post");
  EXPECT_TRUE(Has(layout, "<strong>menu</strong>")) << "the later write stands";
  EXPECT_TRUE(Has(layout, "<h2>Not placed</h2>"));
  EXPECT_FALSE(Has(layout, "Nothing is set aside.")) << "the set-aside placement was not brought back";
}

TEST_F(StructureAdmin, TheStructureUndoIsOfferedOnlyToWhoMayEditAndAnotherPrincipalsFormDoesNothing) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  ASSERT_EQ(303, Do("layout", ada_, "remove-cell", 1, "").status);
  /* A principal who may use the undo page and edit nothing sees no such form. */
  const std::string bob = Person("bob", {GDEFCORE_ADMIN_USE_LABEL, GDEFCORE_ADMIN_UNDO_LABEL});
  EXPECT_EQ(200, Get("/admin/undo", bob).status);
  EXPECT_FALSE(Has(Get("/admin/undo", bob).body, "<h2>Page structure</h2>"));
  EXPECT_FALSE(Has(Get("/admin/undo", bob).body, "Undo the last page structure change"));
  /* CONTROL: the editor sees it. */
  const std::string form = UndoBody(ada_, "<h2>Page structure</h2>");
  ASSERT_FALSE(form.empty());
  /* Ada's signed form, posted in Bob's session, is not Ada's to use. */
  PostRaw("/admin/undo", bob, form);
  EXPECT_EQ("main", Cells(Page("layout", ada_))) << "nothing was undone";
  EXPECT_FALSE(UndoBody(ada_, "<h2>Page structure</h2>").empty()) << "the record was not taken";
}

TEST_F(StructureAdmin, TheEditorCanBeDraggedAndOperatedByKeysAndEveryFormIsStillThereWithoutTheScript) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  Reply js = Get("/defiant.js", std::string());
  ASSERT_EQ(200, js.status);
  for (const char * hook : {"structure-editor", "structure-live", "li[data-cell]", "ol[data-row]",
           "h2[data-rowhead]", "move-cell", "move-row", "form.submit()", "altKey", "dragstart", "drop"}) {
    EXPECT_TRUE(Has(js.body, hook)) << hook;
  }
  EXPECT_FALSE(Has(js.body, "innerHTML"));
  EXPECT_FALSE(Has(js.body, "eval("));
  const std::string page = Page("layout", ada_);
  EXPECT_TRUE(Has(page, "id=\"structure-live\" class=\"live\" role=\"status\" aria-live=\"polite\""));
  EXPECT_TRUE(Has(page, "<h2 data-rowhead=\"0\" tabindex=\"0\" draggable=\"true\">Row 1: "));
  EXPECT_TRUE(Has(page, "<ol data-row=\"0\">"));
  EXPECT_TRUE(Has(page, "<li data-cell=\"main\" tabindex=\"0\" draggable=\"true\">"));
  EXPECT_TRUE(Has(page, "<li data-cell=\"side\" tabindex=\"0\" draggable=\"true\">"));
  /* The page carries no script of its own, only the one the policy names. */
  EXPECT_FALSE(Has(page, "<script>"));
  /* Every form the script fills in is there to be filled in without it: a cell's own move
   * form, with the fields the script sets. */
  EXPECT_TRUE(Has(page, "name=\"act\" value=\"move-cell\">"));
  EXPECT_TRUE(Has(page, "name=\"to_row\""));
  EXPECT_TRUE(Has(page, "name=\"to_cell\""));
  /* With a second row, the row's own move form is there too. */
  ASSERT_EQ(303, Do("layout", ada_, "add-row", 0, "&behavior=wrap&region=card").status);
  EXPECT_TRUE(Has(Page("layout", ada_), "name=\"act\" value=\"move-row\">"));
  /* The script itself is checked by tools/check-structurejs.js, from the layout test's copy. */
}

TEST_F(StructureAdmin, AFormDrawnBeforeAnotherChangeAndATokenOfAnotherFormWriteNothing) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  const std::string old_page = Page("layout", ada_);
  ASSERT_EQ(303, Do("layout", ada_, "set-width", 0, "&width=6").status);
  const std::string stale = Form(old_page, "set-width", 0, "&width=5");
  Reply refused = Send("layout", ada_, stale);
  EXPECT_EQ(200, refused.status);
  EXPECT_TRUE(Has(refused.body, "out of date"));
  EXPECT_TRUE(Has(Page("layout", ada_), "<div class=\"sp-cell w-6 st-1\">"));
  /* No token, a token of the layout editor's, and a token from another sign-in. */
  EXPECT_TRUE(Has(Send("layout", ada_, "act=custom&version=x&confirm=layout").body, "out of date"));
  {
    const std::string page = LayoutPage("post");
    const size_t at = page.find("name=\"act\" value=\"place\">");
    const size_t form = page.rfind("<form", at);
    const std::string part = page.substr(form, page.find("</form>", at) - form);
    const std::string body = "token=" + InputValue(part, "token", 0) +
        "&version=x&act=remove-row&row=0";
    EXPECT_TRUE(Has(Send("layout", ada_, body).body, "out of date"));
  }
  const std::string elsewhere = CookieLine(Login("ada", "secret"));
  const std::string forged = Form(Page("layout", elsewhere), "set-width", 0, "&width=3");
  EXPECT_TRUE(Has(Send("layout", ada_, forged).body, "out of date"));
  EXPECT_TRUE(Has(Page("layout", ada_), "<div class=\"sp-cell w-6 st-1\">"));
}

TEST_F(StructureAdmin, ATemplateWrittenByHandNeedsItsNameAndIsOnlyReplacedByANewStart) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  /* The wrong name writes nothing. */
  Reply wrong = Do("layout", ada_, "custom", 0, "&confirm=other");
  EXPECT_EQ(200, wrong.status);
  EXPECT_TRUE(Has(wrong.body, "The name typed does not match"));
  EXPECT_EQ("main side", Cells(Page("layout", ada_)));
  ASSERT_EQ(303, Do("layout", ada_, "custom", 0, "&confirm=layout").status);
  const std::string page = Page("layout", ada_);
  EXPECT_TRUE(Has(page, "written by hand"));
  EXPECT_EQ("", Cells(page));
  EXPECT_FALSE(Has(page, "id=\"structure-preview\""));
  EXPECT_FALSE(Has(page, "name=\"act\" value=\"custom\""));
  /* A start takes over. */
  ASSERT_EQ(303, Do("layout", ada_, "start-new", 0, "&region=main").status);
  EXPECT_EQ("main", Cells(Page("layout", ada_)));
}

TEST_F(StructureAdmin, ANewTemplateIsMadeFromANameAndARegionAndABadNameIsRefused) {
  Open();
  const std::string list = Get("/admin/structures", ada_).body;
  const size_t at = list.find("name=\"token\" value=\"");
  ASSERT_NE(std::string::npos, at);
  const std::string token = InputValue(list, "token", 0);
  Reply made = PostRaw("/admin/structures", ada_, "token=" + token + "&name=landing&region=main");
  EXPECT_EQ(303, made.status);
  EXPECT_EQ("/admin/structures/landing", HeaderValue(made, "Location"));
  EXPECT_EQ("main", Cells(Page("landing", ada_)));
  EXPECT_TRUE(Has(Get("/admin/structures", ada_).body, ">landing</a>"));
  for (const char * bad : {"Landing", "landing", "9x", "a_b", ""}) {
    Reply r = PostRaw("/admin/structures", ada_, "token=" + InputValue(Get("/admin/structures", ada_).body, "token", 0) +
                                                      "&name=" + bad + "&region=main");
    EXPECT_EQ(200, r.status) << bad;
    EXPECT_TRUE(Has(r.body, "is not one that exists")) << bad;
  }
  EXPECT_TRUE(Has(PostRaw("/admin/structures", ada_, "name=other&region=main").body, "out of date"));
  EXPECT_TRUE(NotThere(Get("/admin/structures/other", ada_)));
}

TEST_F(StructureAdmin, ThePageEscapesEveryNameItShows) {
  Open();
  ASSERT_EQ(303, Do("layout", ada_, "start", 0, "&region=two-columns-right").status);
  /* A region name the document would refuse never reaches the page, and a refusal's own text is escaped. */
  Reply bad = Do("layout", ada_, "rename-cell", 0, "&region=%3Cb%3Ex%3C%2Fb%3E");
  EXPECT_EQ(200, bad.status);
  EXPECT_FALSE(Has(bad.body, "<b>x</b>"));
  EXPECT_EQ("main side", Cells(Page("layout", ada_)));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
