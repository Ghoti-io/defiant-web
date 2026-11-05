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
 * Editing menus by drag and by keyboard (site admin story 10), through the real
 * server. Every action is a form that works with scripts off; a drag and a key
 * make the post the move form makes, which is one signed post, one changeset,
 * and one undo. A stale form, a cycle and a missing permission are refused with
 * nothing written.
 *
 * There is no headless browser in the build image, so the editor is tested in
 * two halves that meet at the form: the posts the script makes (it fills in the
 * page's one move form and submits it) are made here from that form, and the
 * script's key map, its form fields and its handling of text are checked as
 * source, by name. Every refusal has a control in the same test.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-test/defiant-test.h>

#include <regex>

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

class MenuEditor : public Edge {
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

  /* The admin role granted the permissions to edit menu entries and overrides. */
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

  std::string Page() { return Get("/admin/menus", cookie_).body; }

  static std::string MenuOf(const std::string & page) {
    size_t at = page.find("<nav class=\"admin-menu\">");
    if (at == std::string::npos) {
      return std::string();
    }
    return page.substr(at, page.find("</nav>", at) - at);
  }

  /* The form of @p entry's row whose button has the action @p action. */
  static std::string RowForm(const std::string & page, const std::string & entry,
      const std::string & action) {
    size_t row = page.find("<tr id=\"e-" + entry + "\"");
    size_t button;
    size_t form;
    size_t end;
    if (row == std::string::npos) {
      return std::string();
    }
    button = page.find("name=\"action\" value=\"" + action + "\"", row);
    if (button == std::string::npos || button > page.find("</tr>", row)) {
      return std::string();
    }
    form = page.rfind("<form", button);
    end = page.find("</form>", button);
    return page.substr(form, end - form);
  }

  /* The form with the id @p id (the one move form, the script's). */
  static std::string FormById(const std::string & page, const std::string & id) {
    size_t at = page.find("<form method=\"post\" action=\"/admin/menus\" id=\"" + id + "\"");
    if (at == std::string::npos) {
      return std::string();
    }
    return page.substr(at, page.find("</form>", at) - at);
  }

  /* The body a browser posts for a form: its hidden fields, then @p extra. */
  static std::string Body(const std::string & form, const std::string & extra) {
    return "token=" + InputValue(form, "token", 0) + "&version=" +
           InputValue(form, "version", 0) + extra;
  }

  /* The post of the button @p action in @p entry's row, with the page's own
   * token and version. */
  std::string RowBody(const std::string & page, const std::string & entry,
      const std::string & action, const std::string & extra = std::string()) {
    std::string form = RowForm(page, entry, action);
    EXPECT_FALSE(form.empty()) << entry << " " << action;
    return Body(form, "&name=" + Enc(entry) + "&action=" + action + extra);
  }

  /* The post a drag makes: the script fills in the move form's name, parent
   * and position and submits it. */
  std::string DragBody(const std::string & page, const std::string & entry,
      const std::string & parent, size_t position) {
    std::string form = FormById(page, "menu-move-form");
    EXPECT_FALSE(form.empty());
    return Body(form, "&action=move&name=" + Enc(entry) + "&parent=" + Enc(parent) +
                          "&position=" + std::to_string(position));
  }

  /* Post and follow the redirect, as a browser does. */
  Reply Post(const std::string & body) { return PostForm("/admin/menus", cookie_, body); }

  /* The editor's rows of one menu in the order shown: "<name> under <parent>". */
  static std::string Tree(const std::string & page, const std::string & menu = "admin") {
    std::string out;
    size_t at = 0;
    while ((at = page.find("<tr id=\"e-", at)) != std::string::npos) {
      size_t name_end = page.find('"', at + 10);
      std::string name = page.substr(at + 10, name_end - (at + 10));
      size_t m = page.find("data-menu=\"", name_end);
      size_t p = page.find("data-parent=\"", name_end);
      std::string menu_name = page.substr(m + 11, page.find('"', m + 11) - (m + 11));
      std::string parent = page.substr(p + 13, page.find('"', p + 13) - (p + 13));
      if (menu_name == menu) {
        out += name + (parent.empty() ? "" : " under " + parent) + "\n";
      }
      at = name_end;
    }
    return out;
  }

  /* The text of every message the page shows. */
  static std::string Messages(const std::string & page) {
    std::string out;
    size_t at = 0;
    while ((at = page.find("class=\"message", at)) != std::string::npos) {
      size_t from = page.find('>', at) + 1;
      out += page.substr(from, page.find("</", from) - from) + "|";
      at = from;
    }
    return out;
  }

  /* How many entities of @p type the store has, and how many revisions they hold
   * in all: written rows only (an entity cleared is not counted). */
  std::string Stored(const char * type) {
    GDEFCORE_Catalog * catalog = Catalog();
    char ** ids = NULL;
    size_t n = 0;
    size_t live = 0;
    size_t revisions = 0;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_entity_ids(catalog, type, strlen(type), &ids, &n));
    for (size_t i = 0; i < n; i++) {
      char ** t = NULL;
      size_t * l = NULL;
      size_t c = 0;
      size_t r = 0;
      if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
              strlen(ids[i]), "en", 2, "name", 4, &t, &l, &c) == GDEFCORE_OK) {
        live += c > 0 ? 1 : 0;
        gdefcore_values_free(catalog, t, l, c);
      }
      EXPECT_EQ(GDEFCORE_OK, gdefcore_entity_revision_count(catalog, ids[i],
                                 strlen(ids[i]), &r));
      revisions += r;
    }
    gdefcore_ids_free(catalog, ids, n);
    gdefcore_catalog_destroy(catalog);
    return std::to_string(live) + " live, " + std::to_string(revisions) + " revisions";
  }

  std::string Script() { return Get("/defiant.js", std::string()).body; }

  /* The form that adds an entry: the last one on the page but the sign-out form. */
  static std::string AddForm(const std::string & whole) {
    std::string page = WithoutSignout(whole);
    size_t at = page.rfind("<form");
    return page.substr(at, page.find("</form>", at) - at);
  }

  /* The button of the existing undo page that takes back the last menu edit. */
  Reply UndoLast() {
    std::string undo = Get("/admin/undo", cookie_).body;
    size_t at = undo.find("<h2>Menus</h2>");
    EXPECT_NE(std::string::npos, at) << undo;
    return PostForm("/admin/undo", cookie_,
        "token=" + InputValue(at == std::string::npos ? undo : undo.substr(at), "token", 0));
  }
};

const char kMore[] = "core.l-admin-more";

}  // namespace

/* ---- every action is a form --------------------------------------------------- */

TEST_F(MenuEditor, EveryActionIsAFormThatWorksWithScriptsOff) {
  Open();
  GrantMenus();
  std::string page = Page();
  /* The page has the buttons for every action, as submit buttons of forms that
   * carry a token and the version: nothing waits for a script. */
  for (const char * action : {"up", "down", "indent", "outdent", "rename", "hide"}) {
    std::string form = RowForm(page, kMore, action);
    ASSERT_FALSE(form.empty()) << action;
    EXPECT_TRUE(Has(form, ("<button type=\"submit\" name=\"action\" value=\"" +
                              std::string(action) + "\">")
                              .c_str()))
        << action << "\n" << form;
    EXPECT_TRUE(Has(form, "method=\"post\" action=\"/admin/menus\""));
    EXPECT_FALSE(InputValue(form, "token", 0).empty());
    EXPECT_EQ(size_t(GDEFCORE_MENU_VERSION_LEN), InputValue(form, "version", 0).size());
  }
  EXPECT_TRUE(Has(page, "<button type=\"submit\" name=\"action\" value=\"add\">"));
  EXPECT_FALSE(FormById(page, "menu-move-form").empty()) << "the form a drag uses is a form";
  EXPECT_FALSE(Has(page, "<button type=\"button\""));
  EXPECT_FALSE(Has(page, "onclick="));
  EXPECT_FALSE(Has(page, "<script>"));
  EXPECT_FALSE(Has(page, " style="));
  EXPECT_TRUE(ScansClean(page)) << page;
  /* Up: a post, a redirect to the page with the entry's fragment, a message. */
  Reply sent = PostRaw("/admin/menus", cookie_, RowBody(page, kMore, "up"));
  EXPECT_EQ(303, sent.status);
  EXPECT_EQ(std::string("/admin/menus#e-") + kMore, HeaderValue(sent, "Location"));
  std::string after = Get("/admin/menus", cookie_).body;
  EXPECT_TRUE(Has(Messages(after), "Moved core.l-admin-more up.")) << Messages(after);
  EXPECT_NE(Tree(page), Tree(after));
  EXPECT_LT(Tree(after).find(kMore), Tree(after).find("core.l-admin-settings under"))
      << "up is before the entry that was above it";
  /* The rest, each as a plain post. */
  for (const char * action : {"down", "indent", "outdent"}) {
    std::string before = Tree(Page());
    Reply done = Post(RowBody(Page(), kMore, action));
    EXPECT_EQ(200, done.status) << action;
    EXPECT_NE(before, Tree(done.body)) << action << " moved it";
  }
  /* Rename and hide a generated entry, then show it. */
  Reply renamed = Post(RowBody(Page(), kMore, "rename", "&label=Elsewhere"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/more\">Elsewhere"));
  EXPECT_TRUE(Has(Messages(renamed.body), "Renamed core.l-admin-more.")) << Messages(renamed.body);
  Post(RowBody(Page(), kMore, "hide"));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/more\""));
  EXPECT_TRUE(Has(Page(), "value=\"show\""));
  Post(RowBody(Page(), kMore, "show"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/more\">Elsewhere"));
  /* A site entry: add, rename, hide, show, remove. */
  Post(Body(AddForm(Page()),
      "&action=add&suffix=docs&menu=admin&label=Docs&route=core.admin-more&parent="));
  page = Page();
  EXPECT_TRUE(Has(page, "id=\"e-site.docs\"")) << page;
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), ">Docs</a>"));
  Post(RowBody(page, "site.docs", "rename", "&label=Manual"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), ">Manual</a>"));
  Post(RowBody(Page(), "site.docs", "hide"));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "Manual"));
  Post(RowBody(Page(), "site.docs", "show"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), ">Manual</a>"));
  /* A generated entry has no Remove: it is hidden instead. A site entry has. */
  EXPECT_TRUE(RowForm(Page(), kMore, "remove").empty());
  Post(RowBody(Page(), "site.docs", "remove"));
  EXPECT_FALSE(Has(Page(), "id=\"e-site.docs\""));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "Manual"));
}

/* ---- a drag is one post, one changeset, one undo ------------------------------ */

TEST_F(MenuEditor, ADragIsOneSignedPostOneChangesetAndTheExistingUndoTakesItBack) {
  Open();
  GrantMenus();
  std::string before = Tree(Page());
  EXPECT_EQ("0 live, 0 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  /* To the first place under Admin: room before the first, so only the entry
   * that moved is written. */
  Reply one = Post(DragBody(Page(), kMore, "core.l-admin", 0));
  EXPECT_EQ(200, one.status);
  EXPECT_NE(before, Tree(one.body));
  EXPECT_LT(Tree(one.body).find(kMore), Tree(one.body).find("core.l-admin-post under"));
  EXPECT_EQ("1 live, 1 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  EXPECT_EQ("1 live, 1 revisions", Stored(GDEFCORE_MENU_CHANGE_TYPE));
  EXPECT_TRUE(Has(Messages(one.body), "Moved core.l-admin-more.")) << Messages(one.body);
  /* The existing undo page takes it back, whole. */
  Reply undone = UndoLast();
  EXPECT_EQ(200, undone.status);
  EXPECT_TRUE(Has(Messages(undone.body), "The last menu change was undone.")) << Messages(undone.body);
  EXPECT_EQ(before, Tree(Page())) << "the earlier tree";
  EXPECT_EQ("0 live, 1 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  /* A drag among entries with no room between them numbers the siblings again,
   * in the same one changeset: seven entries written, and one undo. */
  Reply many = Post(DragBody(Page(), kMore, "core.l-admin", 5));
  EXPECT_EQ(200, many.status);
  EXPECT_NE(before, Tree(many.body));
  EXPECT_EQ("7 live, 8 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE))
      << "(the one the first drag made and its undo cleared is the first of the eight)";
  UndoLast();
  EXPECT_EQ(before, Tree(Page()));
  EXPECT_EQ("0 live, 8 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  /* An undo is once. */
  Reply again = UndoLast();
  EXPECT_TRUE(Has(Messages(again.body), GDEFCORE_MENU_REFUSED_NOTHING)) << Messages(again.body);
  /* Hiding is one edit too, and its undo shows the entry again. */
  Post(RowBody(Page(), "core.l-admin-undo", "hide"));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  UndoLast();
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  /* An undo does not overwrite a later edit: after a second edit only the second
   * is taken back, and the first stands. */
  Post(RowBody(Page(), "core.l-admin-undo", "rename", "&label=First"));
  Post(RowBody(Page(), "core.l-admin-more", "rename", "&label=Second"));
  UndoLast();
  std::string menu = MenuOf(Get("/admin", cookie_).body);
  EXPECT_TRUE(Has(menu, ">First</a>")) << menu;
  EXPECT_FALSE(Has(menu, ">Second</a>"));
}

/* ---- the keys make the posts the drag makes -------------------------------------- */

TEST_F(MenuEditor, TheEditorIsOperableWithTheKeyboardAlone) {
  Open();
  GrantMenus();
  std::string page = Page();
  std::string script = Script();
  /* The script's key map, read from the source the page loads: Alt+Shift and an
   * arrow key, each naming the action of the button of that name. */
  std::smatch m;
  ASSERT_TRUE(std::regex_search(script, m, std::regex("var KEYS = \\{([^}]*)\\};"))) << script;
  std::map<std::string, std::string> keys;
  std::string body = m[1];
  const std::regex pair_re("\"(Arrow[A-Za-z]+)\": \"([a-z]+)\"");
  for (std::sregex_iterator it(body.begin(), body.end(), pair_re), end; it != end; ++it) {
    keys[(*it)[1]] = (*it)[2];
  }
  EXPECT_EQ((std::map<std::string, std::string>{{"ArrowDown", "down"}, {"ArrowLeft", "outdent"},
                {"ArrowRight", "indent"}, {"ArrowUp", "up"}}),
      keys);
  EXPECT_TRUE(Has(script, "event.altKey && event.shiftKey")) << "the modifier chord";
  EXPECT_TRUE(Has(script, "table.addEventListener(\"keydown\"")) << "keys are handled on the editor";
  /* The page says what the keys are, in words. */
  for (const char * chord : {"Alt+Shift+Up", "Alt+Shift+Down", "Alt+Shift+Right", "Alt+Shift+Left"}) {
    EXPECT_TRUE(Has(page, chord)) << chord;
  }
  /* Every entry is focusable, and the live region the script speaks in is there. */
  size_t rows = 0;
  size_t focusable = 0;
  for (size_t at = 0; (at = page.find("<tr id=\"e-", at)) != std::string::npos; at += 1) {
    std::string tag = page.substr(at, page.find('>', at) - at);
    rows += 1;
    focusable += Has(tag, "tabindex=\"&#48;\"") ? 1 : 0;
  }
  EXPECT_GT(rows, 5u);
  EXPECT_EQ(rows, focusable);
  EXPECT_TRUE(Has(page, "id=\"menu-live\" class=\"live\" role=\"status\" aria-live=\"polite\""));
  /* The fields the script fills in are fields of the form it submits. */
  std::string form = FormById(page, "menu-move-form");
  for (const char * name : {"action", "name", "parent", "position"}) {
    EXPECT_TRUE(Has(form, (std::string("name=\"") + name + "\"").c_str())) << name;
    EXPECT_TRUE(Has(script, (std::string("field(\"") + name + "\")").c_str())) << name;
  }
  /* The posts the keys make: the key's action and the entry's name, on the move
   * form. Each is a move of that entry. */
  const std::string menus = "core.l-admin-menus";
  for (const auto & key : keys) {
    std::string now = Page();
    Reply keyed = Post(Body(FormById(now, "menu-move-form"),
        "&action=" + key.second + "&name=" + Enc(menus)));
    EXPECT_EQ(200, keyed.status) << key.second;
    EXPECT_TRUE(Has(Messages(keyed.body), "Moved core.l-admin-menus")) << key.first << " " << Messages(keyed.body);
  }
  /* Up by a key and by a drag to the place above give the same tree. */
  const std::string front = "core.l-admin-front";
  std::string start = Tree(Page());
  Post(Body(FormById(Page(), "menu-move-form"), "&action=up&name=" + Enc(front)));
  std::string by_key = Tree(Page());
  EXPECT_NE(start, by_key);
  UndoLast();
  EXPECT_EQ(start, Tree(Page()));
  Post(DragBody(Page(), front, "core.l-admin", 0));
  EXPECT_EQ(by_key, Tree(Page())) << "the drag to the first place is the key up from the second";
  /* Only the keys the form has: a post of anything else is the form being out of date. */
  Reply odd = Post(Body(FormById(Page(), "menu-move-form"), "&action=levitate&name=" + Enc(front)));
  EXPECT_TRUE(Has(Messages(odd.body), "out of date")) << Messages(odd.body);
}

/* ---- stale, cycle, permission --------------------------------------------------- */

TEST_F(MenuEditor, AFormBasedOnAnOlderTreeIsRefusedWithAMessageAndTheCurrentTree) {
  Open();
  GrantMenus();
  std::string old_page = Page();
  /* Someone changes the menu after this page was drawn. */
  Post(RowBody(Page(), "core.l-admin-undo", "rename", "&label=Take+back"));
  std::string now = Page();
  EXPECT_NE(InputValue(old_page, "version", 0), InputValue(now, "version", 0));
  const std::string tree = Tree(now);
  const std::string stored = Stored(GDEFCORE_MENU_OVERRIDE_TYPE);
  Reply stale = Post(DragBody(old_page, kMore, "core.l-admin", 0));
  EXPECT_EQ(200, stale.status);
  EXPECT_TRUE(Has(Messages(stale.body), GDEFCORE_MENU_REFUSED_STALE))
      << "a stale form was accepted: " << Messages(stale.body);
  EXPECT_EQ(tree, Tree(stale.body)) << "the page that answers shows the tree as it is";
  EXPECT_EQ(InputValue(now, "version", 0), InputValue(stale.body, "version", 0));
  EXPECT_EQ(stored, Stored(GDEFCORE_MENU_OVERRIDE_TYPE)) << "nothing was written";
  for (const char * action : {"up", "hide"}) {
    Reply other = Post(RowBody(old_page, kMore, action));
    EXPECT_TRUE(Has(Messages(other.body), GDEFCORE_MENU_REFUSED_STALE)) << action;
  }
  EXPECT_EQ(stored, Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  /* CONTROL: the same drag from the page as it is now is made. */
  Reply fresh = Post(DragBody(Page(), kMore, "core.l-admin", 0));
  EXPECT_NE(tree, Tree(fresh.body));
  EXPECT_FALSE(Has(Messages(fresh.body), GDEFCORE_MENU_REFUSED_STALE));
}

TEST_F(MenuEditor, AMoveUnderItsOwnSubtreeOrUnderNothingIsRefusedAndNothingIsWritten) {
  Open();
  GrantMenus();
  Post(Body(AddForm(Page()),
      "&action=add&suffix=p&menu=admin&label=P&route=core.admin-more&parent="));
  Post(Body(AddForm(Page()),
      "&action=add&suffix=q&menu=admin&label=Q&route=core.admin-more&parent=site.p"));
  const std::string tree = Tree(Page());
  const std::string stored = Stored(GDEFCORE_MENU_ENTRY_TYPE);
  EXPECT_TRUE(Has(tree, "site.q under site.p")) << tree;
  Reply cycle = Post(DragBody(Page(), "site.p", "site.q", 0));
  EXPECT_TRUE(Has(Messages(cycle.body), GDEFCORE_MENU_REFUSED_CYCLE)) << Messages(cycle.body);
  EXPECT_EQ(tree, Tree(cycle.body));
  Reply itself = Post(DragBody(Page(), "site.p", "site.p", 0));
  EXPECT_TRUE(Has(Messages(itself.body), GDEFCORE_MENU_REFUSED_CYCLE));
  Reply missing = Post(DragBody(Page(), "site.q", "site.nothing", 0));
  EXPECT_TRUE(Has(Messages(missing.body), GDEFCORE_MENU_REFUSED_PARENT)) << Messages(missing.body);
  Reply far = Post(DragBody(Page(), "site.q", "", 99));
  EXPECT_TRUE(Has(Messages(far.body), GDEFCORE_MENU_REFUSED_PLACE)) << Messages(far.body);
  std::string negative = DragBody(Page(), "site.q", "", 0);
  negative.replace(negative.find("&position=0"), 11, "&position=-3");
  Reply bad = Post(negative);
  EXPECT_TRUE(Has(Messages(bad.body), "That change was not made.")) << Messages(bad.body);
  EXPECT_EQ(tree, Tree(Page()));
  EXPECT_EQ(stored, Stored(GDEFCORE_MENU_ENTRY_TYPE));
  /* CONTROL: q out from under p is a move like any other. */
  Reply fine = Post(DragBody(Page(), "site.q", "", 0));
  EXPECT_NE(tree, Tree(fine.body));
  EXPECT_FALSE(Has(Tree(fine.body), "site.q under"));
}

TEST_F(MenuEditor, WhatThePermissionDoesNotCoverAndAFormNotSignedForThisWriteNothing) {
  Open();
  /* May edit overrides, and not entries. */
  GrantMenus(false, true);
  std::string page = Page();
  std::string tree = Tree(page);
  /* Adding a site entry is an entry write: refused, said, nothing stored. */
  Reply add = Post(Body(AddForm(page),
      "&action=add&suffix=docs&menu=admin&label=Docs&route=core.admin-more&parent="));
  EXPECT_TRUE(Has(Messages(add.body), "That edit is not allowed.")) << Messages(add.body);
  EXPECT_EQ(tree, Tree(add.body));
  EXPECT_EQ("0 live, 0 revisions", Stored(GDEFCORE_MENU_ENTRY_TYPE));
  /* CONTROL: a generated entry is an override, which the grant covers. */
  Reply moved = Post(DragBody(Page(), kMore, "core.l-admin", 0));
  EXPECT_NE(tree, Tree(moved.body));
  /* A form not signed for this: no token, a damaged one, one for another
   * purpose, one for another login. */
  page = Page();
  const std::string good = DragBody(page, "core.l-admin-undo", "core.l-admin", 0);
  const std::string rest = good.substr(good.find("&version"));
  const std::string base = Tree(page);
  const std::string stored = Stored(GDEFCORE_MENU_OVERRIDE_TYPE);
  PostRaw("/admin/menus", cookie_, rest);
  EXPECT_EQ(base, Tree(Page())) << "no token";
  {
    std::string damaged = good;
    size_t at = damaged.find("token=") + 6 + 40;
    damaged[at] = damaged[at] == 'A' ? 'B' : 'A';
    PostRaw("/admin/menus", cookie_, damaged);
    EXPECT_EQ(base, Tree(Page())) << "a damaged token";
  }
  PostRaw("/admin/menus", cookie_, "token=not-a-token" + rest);
  EXPECT_EQ(base, Tree(Page())) << "a token that is not one";
  /* The hide form's token is not a move's. */
  std::string other = RowForm(Page(), kMore, "hide");
  ASSERT_FALSE(other.empty());
  Reply swapped = PostForm("/admin/menus", cookie_, "token=" + InputValue(other, "token", 0) + rest);
  EXPECT_TRUE(Has(Messages(swapped.body), "out of date")) << Messages(swapped.body);
  EXPECT_EQ(base, Tree(Page())) << "a token for another purpose";
  Reply other_login = Login("ada", "secret");
  PostRaw("/admin/menus", CookieLine(other_login), good);
  EXPECT_EQ(base, Tree(Page())) << "another login's cookie";
  EXPECT_EQ(403, PostRaw("/admin/menus", cookie_, good, "http://evil.example").status);
  EXPECT_EQ(stored, Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  /* CONTROL: the right token in the right session does it. */
  Reply ok = Post(good);
  EXPECT_NE(base, Tree(ok.body));
}

TEST_F(MenuEditor, HidingRemovesTheEntryFromEveryMenuAndNotThePage) {
  Open();
  GrantMenus();
  EXPECT_TRUE(Has(MenuOf(Get("/admin/more", cookie_).body), "href=\"/admin/undo\""));
  Post(RowBody(Page(), "core.l-admin-undo", "hide"));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  EXPECT_FALSE(Has(MenuOf(Get("/admin/more", cookie_).body), "href=\"/admin/undo\""));
  Reply undo = Get("/admin/undo", cookie_);
  EXPECT_EQ(200, undo.status) << "hiding a link is not denying the page";
  EXPECT_TRUE(Has(undo.body, "<form"));
  EXPECT_TRUE(Has(Page(), "hidden")) << "the editor still lists it, marked";
  EXPECT_EQ("1 live, 1 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE));
  Post(RowBody(Page(), "core.l-admin-undo", "show"));
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "href=\"/admin/undo\""));
  EXPECT_EQ("0 live, 1 revisions", Stored(GDEFCORE_MENU_OVERRIDE_TYPE))
      << "shown again leaves no override standing";
}

/* ---- text is text, in the page and in the script ---------------------------------- */

TEST_F(MenuEditor, ALabelWithMarkupIsShownAsTextInThePageAndTheScriptNeverMakesMarkup) {
  Open();
  GrantMenus();
  const std::string hostile = "<img src=x onerror=alert(x)>";
  Reply added = Post(Body(AddForm(Page()),
      "&action=add&suffix=evil&menu=admin&label=" + Enc(hostile) +
          "&route=core.admin-more&parent="));
  std::string page = added.body;
  EXPECT_FALSE(Has(page, "<img src=x")) << page;
  EXPECT_TRUE(Has(page, "&lt;img src=x onerror=alert(x)&gt;"));
  EXPECT_TRUE(Has(page, "data-label=\"&lt;img src=x onerror=alert(x)&gt;\""))
      << "the script reads the label from an attribute that is escaped";
  EXPECT_TRUE(Has(MenuOf(Get("/admin", cookie_).body), "&lt;img src=x onerror=alert(x)&gt;"));
  EXPECT_FALSE(Has(MenuOf(Get("/admin", cookie_).body), "<img"));
  EXPECT_TRUE(ScansClean(page));
  /* A generated entry renamed to markup, and the message that says so. */
  Reply renamed = Post(RowBody(Page(), kMore, "rename", "&label=" + Enc("<b>x</b>")));
  EXPECT_FALSE(Has(renamed.body, "<b>x</b>"));
  EXPECT_TRUE(Has(renamed.body, "&lt;b&gt;x&lt;/b&gt;"));
  /* The script: what it says, it says as text, and no text of the page reaches
   * the page again as markup. */
  std::string script = Script();
  EXPECT_TRUE(Has(script, "live.textContent = text")) << "what it says, it says as text";
  for (const char * sink : {"innerHTML", "outerHTML", "insertAdjacentHTML", "document.write",
           "eval(", "new Function", "createContextualFragment", "srcdoc", "setAttribute(\"on"}) {
    EXPECT_FALSE(Has(script, sink)) << sink;
  }
}

/* ---- the one script, and the undo page --------------------------------------------- */

TEST_F(MenuEditor, TheScriptIsTheOneHashedScriptTheLibraryAlreadyServes) {
  Open();
  GrantMenus();
  Reply menus = Get("/admin/menus", cookie_);
  std::string page = menus.body;
  size_t first = page.find("<script");
  ASSERT_NE(std::string::npos, first);
  EXPECT_EQ(std::string::npos, page.find("<script", first + 1)) << "exactly one script";
  EXPECT_TRUE(Has(page, "<script defer src=\"/defiant.js\" integrity=\"sha256-"));
  std::string csp = HeaderValue(menus, "Content-Security-Policy");
  EXPECT_TRUE(Has(csp, "script-src 'sha256-"));
  EXPECT_EQ(std::string::npos, csp.find("unsafe-inline"));
  /* The hash in the page and the policy is the hash of the body served. */
  std::string integrity = page.substr(page.find("integrity=\"") + 11);
  integrity = integrity.substr(0, integrity.find('"'));
  EXPECT_TRUE(Has(csp, ("'" + integrity + "'").c_str())) << integrity << "\n" << csp;
  std::string script = Script();
  EXPECT_TRUE(Has(script, "menu-editor"));
  EXPECT_TRUE(Has(script, "getElementById(\"panes\")")) << "the pane editor's script is the same one";
  ExpectSecurity(menus);
}

TEST_F(MenuEditor, TheUndoPageOffersTheMenuUndoOnlyToWhoMayEditMenus) {
  Open();
  std::string none = Get("/admin/undo", cookie_).body;
  EXPECT_FALSE(Has(none, "Undo the last menu change"));
  GrantMenus();
  std::string some = Get("/admin/undo", cookie_).body;
  EXPECT_TRUE(Has(some, "Undo the last menu change"));
  EXPECT_TRUE(ScansClean(some));
  /* With nothing done, the button says so and writes nothing. */
  Reply nothing = UndoLast();
  EXPECT_TRUE(Has(Messages(nothing.body), GDEFCORE_MENU_REFUSED_NOTHING)) << Messages(nothing.body);
  /* Not with a token that is not this form's. */
  Reply forged = PostRaw("/admin/undo", cookie_, "token=" + InputValue(Page(), "token", 0));
  EXPECT_EQ(303, forged.status);
  EXPECT_EQ("0 live, 0 revisions", Stored(GDEFCORE_MENU_CHANGE_TYPE));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
