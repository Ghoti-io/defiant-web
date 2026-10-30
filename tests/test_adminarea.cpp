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
 * The admin area gated by permissions, not by a role (site admin story 13),
 * through the real server. A session proves who someone is; the route's own
 * check says what they may do; the role named `admin` is asked nothing. One
 * test per row of the story's matrix, and every refusal has a control in the
 * same test (the same request, made by someone who holds the grant).
 *
 * "Reachable" means the page answers differently from a page that does not
 * exist, for the same cookie: a refusal is that answer.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-core/settings.h>

#include <map>
#include <set>

namespace {

const char * const kPages[] = {"/admin", "/admin/more", "/admin/post", "/admin/front",
    "/admin/undo", "/admin/access", "/admin/templates", "/admin/menus",
    "/admin/settings"};

struct Grant {
  const char * label;
  std::set<std::string> opens;
};

/* Every permission a page asks and the pages it alone opens. */
const std::vector<Grant> & Table() {
  static const std::vector<Grant> table = {
      {GDEFCORE_ADMIN_USE_LABEL, {"/admin", "/admin/more"}},
      {GDEFCORE_POST_WRITE_LABEL, {"/admin/post"}},
      {GDEFCORE_LAYOUT_EDIT_DISPLAY_LABEL, {"/admin/front"}},
      {GDEFCORE_LAYOUT_EDIT_RULE_LABEL, {"/admin/front"}},
      {GDEFCORE_ADMIN_UNDO_LABEL, {"/admin/undo"}},
      {GDEFCORE_ACCESS_READ_LABEL, {"/admin/access"}},
      {GDEFCORE_TEMPLATE_EDIT_LABEL, {"/admin/templates"}},
      {GDEFCORE_MENU_EDIT_ENTRY_LABEL, {"/admin/menus"}},
      {GDEFCORE_MENU_EDIT_OVERRIDE_LABEL, {"/admin/menus"}},
      {GDEFCORE_SETTINGS_EDIT_LABEL, {"/admin/settings"}},
      {GDEFCORE_ROLE_REVERT_LABEL, {}},
  };
  return table;
}

class AdminArea : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;

  /* A site the way the program starts one: the kit, then the first
   * administrator's grants (@p seed), or nothing granted at all. */
  void Open(bool seed) {
    Listen(NULL);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id_));
    InstallKit(id_, false);
    if (seed) {
      GDEFCORE_Catalog * catalog = Catalog();
      ASSERT_EQ(GDEFCORE_OK, gdefcore_admin_seed(catalog));
      gdefcore_catalog_destroy(catalog);
    }
    Publish(id_, "hello-title", "Hello Title");
    UseHost();
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    if (host_ != NULL) {
      gdefcore_catalog_bind_host(catalog, host_);
    }
    return catalog;
  }

  /* A role holding @p labels, and a person who has it and no other. */
  void Person(const char * user, const char * role, const std::vector<std::string> & labels) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Keyring * keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    ASSERT_NE(keyring, nullptr);
    if (role != NULL && gdefcore_access_role_id(catalog, role, strlen(role), principal) !=
                            GDEFCORE_OK) {
      ASSERT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, role, strlen(role), NULL));
      for (const std::string & label : labels) {
        ASSERT_EQ(GDEFCORE_OK,
            gdefcore_access_grant(catalog, role, strlen(role), label.data(), label.size(),
                "core.role", 9));
      }
    }
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    if (role != NULL) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_principal_add_role(catalog, principal, strlen(principal), role,
              strlen(role)));
    }
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal,
            strlen(principal), user, strlen(user), "pw-for-tests", 12, account));
    gdefcore_catalog_destroy(catalog);
  }

  void Revoke(const char * role, const char * label) {
    GDEFCORE_Catalog * catalog = Catalog();
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_revoke(catalog, role, strlen(role), label, strlen(label)));
    gdefcore_catalog_destroy(catalog);
  }

  Reply SignIn(const char * user, std::string * cookie) {
    Reply login = Login(user, user == std::string("ada") ? "secret" : "pw-for-tests");
    *cookie = CookieLine(login);
    return login;
  }

  /* The page answers as no page that does not exist answers. */
  bool Reachable(const std::string & path, const std::string & cookie) {
    Reply got = Get(path.c_str(), cookie);
    Reply none = Get("/admin/no-such-page-at-all", cookie);
    return got.status != none.status || got.body != none.body;
  }

  std::set<std::string> Reached(const std::string & cookie) {
    std::set<std::string> out;
    for (const char * path : kPages) {
      if (Reachable(path, cookie)) {
        out.insert(path);
      }
    }
    return out;
  }

  static std::string Join(const std::set<std::string> & set) {
    std::string out;
    for (const std::string & item : set) {
      out += item + " ";
    }
    return out.empty() ? "(none)" : out;
  }

  static std::set<std::string> MoreLinks(const std::string & page) {
    std::set<std::string> out;
    size_t at = 0;
    while ((at = page.find("<li><a href=\"", at)) != std::string::npos) {
      size_t from = at + 13;
      out.insert(page.substr(from, page.find('"', from) - from));
      at = from;
    }
    return out;
  }

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
};

}  // namespace

/* ---- Admin role, Fresh install ------------------------------------------------ */

TEST_F(AdminArea, TheFirstAdministratorOfAFreshInstallReachesEveryAdminPage) {
  Open(true);
  Reply login = SignIn("ada", &ada_);
  EXPECT_EQ(303, login.status);
  EXPECT_FALSE(CookiePair(login, "__Host-admin").empty()) << "no admin cookie for the first administrator";
  EXPECT_EQ("/admin", HeaderValue(login, "Location"));
  std::set<std::string> want(std::begin(kPages), std::end(kPages));
  EXPECT_EQ(Join(want), Join(Reached(ada_))) << "FRESH: a page the first administrator cannot reach";
  /* The role holds grants; it is not a bypass: the matrix lists them. */
  Reply access = Get("/admin/access", ada_);
  EXPECT_TRUE(Has(access.body, "role admin")) << access.body;
  EXPECT_TRUE(Has(access.body, GDEFCORE_ADMIN_USE_LABEL));
}

TEST_F(AdminArea, AFreshInstallWithNothingGrantedOpensNoAdminPageToTheRoleNamedAdmin) {
  Open(false);
  Reply login = SignIn("ada", &ada_);
  EXPECT_EQ(303, login.status);
  /* The role is held and nothing is granted: no admin cookie, and nowhere to land. */
  EXPECT_TRUE(CookiePair(login, "__Host-admin").empty()) << "ROLE-NAME: an admin cookie for a role with no grants";
  EXPECT_EQ("/", HeaderValue(login, "Location"));
  EXPECT_EQ("(none)", Join(Reached(ada_))) << "ROLE-NAME: a role named admin with no grants reached a page";
  /* CONTROL: the same person, once the role holds one grant. */
  GDEFCORE_Catalog * catalog = Catalog();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_admin_declare(catalog));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant(catalog, "admin", 5, GDEFCORE_ADMIN_USE_LABEL,
          strlen(GDEFCORE_ADMIN_USE_LABEL), "core.role", 9));
  gdefcore_catalog_destroy(catalog);
  login = SignIn("ada", &ada_);
  EXPECT_FALSE(CookiePair(login, "__Host-admin").empty());
  EXPECT_EQ("/admin /admin/more ", Join(Reached(ada_)));
}

/* ---- One grant ------------------------------------------------------------------ */

TEST_F(AdminArea, OneGrantReachesThatPageAndNothingElse) {
  Open(true);
  Person("carol", "menu-keepers",
      {GDEFCORE_MENU_EDIT_ENTRY_LABEL, GDEFCORE_MENU_EDIT_OVERRIDE_LABEL});
  std::string carol;
  Reply login = SignIn("carol", &carol);
  EXPECT_EQ(303, login.status);
  EXPECT_FALSE(CookiePair(login, "__Host-admin").empty())
      << "ONE-GRANT: no admin cookie for a person with an admin page";
  EXPECT_EQ("/admin/menus", HeaderValue(login, "Location")) << "ONE-GRANT: sign-in lands on the page";
  Reply menus = Get("/admin/menus", carol);
  EXPECT_EQ(200, menus.status);
  EXPECT_TRUE(Has(menus.body, "<h1>Menus</h1>")) << "ONE-GRANT: the menu editor is not there for them";
  EXPECT_EQ("/admin/menus ", Join(Reached(carol)))
      << "ONE-GRANT: a person with only the menu grant reached another page";
  /* Her navigation does not carry a link home that is not hers. */
  EXPECT_FALSE(Has(menus.body, "href=\"/admin\">")) << menus.body;
  /* CONTROL: the administrator, who holds the grants, reaches all of it. */
  EXPECT_EQ(Join(std::set<std::string>(std::begin(kPages), std::end(kPages))),
      Join(Reached((SignIn("ada", &ada_), ada_))));
}

TEST_F(AdminArea, EveryGrantOpensTheOnePageItGovernsAndNoOther) {
  Open(true);
  size_t n = 0;
  for (const Grant & grant : Table()) {
    std::string user = "p" + std::to_string(n++);
    std::string role = "only-" + user;
    std::string cookie;
    Person(user.c_str(), role.c_str(), {grant.label});
    Reply login = SignIn(user.c_str(), &cookie);
    EXPECT_EQ(303, login.status) << grant.label;
    /* CONTROL: the page the grant is for is there, with the right landing. */
    EXPECT_EQ(Join(grant.opens), Join(Reached(cookie)))
        << "OPENS: '" << grant.label << "' opens a page it does not govern, or not its own";
    if (grant.opens.empty()) {
      EXPECT_TRUE(CookiePair(login, "__Host-admin").empty()) << grant.label;
      EXPECT_EQ("/", HeaderValue(login, "Location")) << grant.label;
    }
  }
}

/* ---- Revoked -------------------------------------------------------------------- */

TEST_F(AdminArea, RevokingTheAreaGrantHidesMoreAndTheMenuAndTheCookieAloneOpensNothing) {
  Open(true);
  SignIn("ada", &ada_);
  std::set<std::string> all(std::begin(kPages), std::end(kPages));
  ASSERT_EQ(Join(all), Join(Reached(ada_)));
  EXPECT_TRUE(Has(Get("/admin/post", ada_).body, "href=\"/admin/more\">More"));
  Revoke("admin", GDEFCORE_ADMIN_USE_LABEL);
  all.erase("/admin");
  all.erase("/admin/more");
  EXPECT_EQ(Join(all), Join(Reached(ada_))) << "REVOKED: the area grant is revoked and its pages answer";
  /* The menu hangs under the admin home, so it goes with it. */
  Reply post = Get("/admin/post", ada_);
  EXPECT_EQ(200, post.status);
  EXPECT_FALSE(Has(post.body, "href=\"/admin/more\"")) << "REVOKED: More is still listed";
  EXPECT_FALSE(Has(post.body, "href=\"/admin/undo\"")) << "REVOKED: the menu is still drawn";
  /* Everything else goes too: the cookie and the role named admin remain. */
  for (const Grant & grant : Table()) {
    if (std::string(grant.label) != GDEFCORE_ADMIN_USE_LABEL) {
      GDEFCORE_Catalog * catalog = Catalog();
      (void)gdefcore_access_revoke(catalog, "admin", 5, grant.label, strlen(grant.label));
      gdefcore_catalog_destroy(catalog);
    }
  }
  EXPECT_EQ("(none)", Join(Reached(ada_)))
      << "COOKIE: a session cookie and a role name opened a page";
  GDEFCORE_Catalog * catalog = Catalog();
  char ** roles = NULL;
  size_t count = 0;
  char admin[GDEFCORE_PUBLIC_ID_LEN + 1];
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_admin_principal(catalog, admin));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_principal_roles(catalog, admin, strlen(admin), &roles, &count));
  bool has_admin = false;
  for (size_t i = 0; i < count; i++) {
    has_admin = has_admin || strcmp(roles[i], "admin") == 0;
  }
  gdefcore_access_free(catalog, roles, count);
  gdefcore_catalog_destroy(catalog);
  EXPECT_TRUE(has_admin) << "the test is about a person who still holds the role";
  /* A write is refused the same way. */
  Reply none = PostRaw("/admin/no-such-page-at-all", ada_, "token=x");
  Reply post_write = PostRaw("/admin/post", ada_, "token=x");
  EXPECT_EQ(none.status, post_write.status);
  EXPECT_EQ(none.body, post_write.body);
}

/* ---- Anonymous, no session ------------------------------------------------------ */

TEST_F(AdminArea, AnonymousAndASessionWithNoGrantsReachNoAdminPage) {
  Open(true);
  Person("bob", NULL, {});
  std::string bob;
  Reply login = SignIn("bob", &bob);
  EXPECT_EQ(303, login.status);
  EXPECT_TRUE(CookiePair(login, "__Host-admin").empty());
  EXPECT_EQ("/", HeaderValue(login, "Location"));
  for (const std::string & who : {std::string(), bob}) {
    Reply unknown = Get("/no-such-path", who);
    for (const char * path : kPages) {
      Reply got = Get(path, who);
      EXPECT_EQ(unknown.status, got.status) << path;
      EXPECT_EQ(unknown.body, got.body) << path;
      Reply written = PostRaw(path, who, "token=x");
      Reply written_none = PostRaw("/no-such-path", who, "token=x");
      EXPECT_EQ(written_none.status, written.status) << path;
    }
  }
  /* CONTROL: the administrator is shown them. */
  SignIn("ada", &ada_);
  EXPECT_TRUE(Reachable("/admin/post", ada_));
}

/* ---- Menu and More -------------------------------------------------------------- */

TEST_F(AdminArea, MoreListsOnlyWhatTheViewerMayReach) {
  Open(true);
  Person("usher", "ushers", {GDEFCORE_ADMIN_USE_LABEL});
  Person("dave", "menu-and-area", {GDEFCORE_ADMIN_USE_LABEL, GDEFCORE_MENU_EDIT_ENTRY_LABEL});
  std::string usher;
  std::string dave;
  SignIn("usher", &usher);
  SignIn("dave", &dave);
  Reply usher_more = Get("/admin/more", usher);
  ASSERT_EQ(200, usher_more.status);
  std::set<std::string> links = MoreLinks(usher_more.body);
  EXPECT_EQ("/admin ", Join(links)) << "MORE: lists a page the viewer may not reach\n" << usher_more.body;
  /* CONTROL: with the grant for one more page, that page is listed, and only it. */
  Reply dave_more = Get("/admin/more", dave);
  EXPECT_EQ("/admin /admin/menus ", Join(MoreLinks(dave_more.body)))
      << "MORE: not the viewer's pages\n" << dave_more.body;
  /* The administrator's lists them all, the ones that have a page of their own. */
  SignIn("ada", &ada_);
  std::set<std::string> everything = MoreLinks(Get("/admin/more", ada_).body);
  for (const char * path : {"/admin/post", "/admin/front", "/admin/undo", "/admin/templates",
           "/admin/menus", "/admin/settings", "/admin/access"}) {
    EXPECT_EQ(1u, everything.count(path)) << path;
  }
}

/* ---- The undo page: only what the viewer holds the permission of ---------------- */

TEST_F(AdminArea, UndoOffersAndUndoesOnlyWhatTheViewerHoldsThePermissionOf) {
  Open(true);
  Person("undoer", "undoers", {GDEFCORE_ADMIN_UNDO_LABEL});
  std::string undoer;
  SignIn("undoer", &undoer);
  Reply bare = Get("/admin/undo", undoer);
  EXPECT_EQ(200, bare.status);
  EXPECT_TRUE(Has(bare.body, "<h1>") || Has(bare.body, "Undo") || !bare.body.empty());
  EXPECT_FALSE(Has(bare.body, "<form")) << "UNDO: the page offers what the viewer may not undo\n" << bare.body;
  /* CONTROL: with the permission to write posts, the post is offered. */
  Person("writer", "writers", {GDEFCORE_ADMIN_UNDO_LABEL, GDEFCORE_POST_WRITE_LABEL});
  std::string writer;
  SignIn("writer", &writer);
  Reply posts = Get("/admin/undo", writer);
  EXPECT_TRUE(Has(posts.body, "Post Hello Title")) << posts.body;
  EXPECT_FALSE(Has(posts.body, "Role ")) << "UNDO: roles offered without the permission\n" << posts.body;
  /* A role is undone only by who may undo roles, and the refusal says nothing of why. */
  GDEFCORE_Catalog * catalog = Catalog();
  for (const char * role : {"extras-one", "extras-two"}) {
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, role, strlen(role), NULL));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, role, strlen(role), GDEFCORE_POST_WRITE_LABEL,
            strlen(GDEFCORE_POST_WRITE_LABEL), "core.role", 9));
  }
  gdefcore_catalog_destroy(catalog);
  SignIn("ada", &ada_);
  std::string page = Get("/admin/undo", ada_).body;
  std::string first = UndoToken(page, "Role extras-one");
  std::string second = UndoToken(page, "Role extras-two");
  ASSERT_FALSE(first.empty()) << page;
  ASSERT_FALSE(second.empty());
  Reply done = PostForm("/admin/undo", ada_, "token=" + first);
  EXPECT_TRUE(Has(Messages(done.body), "Undone.")) << "CONTROL: " << Messages(done.body);
  Revoke("admin", GDEFCORE_ROLE_REVERT_LABEL);
  EXPECT_FALSE(Has(Get("/admin/undo", ada_).body, "Role extras-two"))
      << "UNDO: roles offered without the permission";
  Reply refused = PostForm("/admin/undo", ada_, "token=" + second);
  std::string said = Messages(refused.body);
  EXPECT_TRUE(Has(said, "That cannot be undone here.")) << "UNDO: " << said;
  for (const char * naming : {"core.", GDEFCORE_ROLE_REVERT_LABEL, "permission", "Undo changes to"}) {
    EXPECT_FALSE(Has(refused.body, naming)) << "NAMES: the refusal names '" << naming << "'\n" << said;
  }
  EXPECT_FALSE(Has(said, "Undone.")) << "UNDO: a role was undone without the permission";
}

/* ---- The front page: the write asks what the page asked ----------------------- */

TEST_F(AdminArea, TheFrontPageWriteAsksTheLayoutDecisionOfTheDisplay) {
  Open(true);
  Person("layouter", "layouters", {GDEFCORE_LAYOUT_EDIT_DISPLAY_LABEL});
  Person("ruler", "rulers", {GDEFCORE_LAYOUT_EDIT_RULE_LABEL});
  std::string layouter;
  std::string ruler;
  SignIn("layouter", &layouter);
  SignIn("ruler", &ruler);
  /* Both reach the page: the permission is the action, and both labels carry it. */
  ASSERT_TRUE(Reachable("/admin/front", layouter));
  ASSERT_TRUE(Reachable("/admin/front", ruler));
  std::string page = Get("/admin/front", ruler).body;
  ASSERT_TRUE(Has(page, "side recent")) << page;
  Reply no = PostForm("/admin/front", ruler, "token=" + FormToken(page, "Remove recent posts"));
  EXPECT_TRUE(Has(Messages(no.body), "That change was not made."))
      << "FRONT: " << Messages(no.body);
  EXPECT_TRUE(Has(no.body, "side recent")) << "FRONT: the rules-only grant changed the display";
  /* CONTROL: who may edit the display does. */
  page = Get("/admin/front", layouter).body;
  Reply yes = PostForm("/admin/front", layouter, "token=" + FormToken(page, "Remove recent posts"));
  EXPECT_TRUE(Has(Messages(yes.body), "Saved.")) << Messages(yes.body);
  EXPECT_FALSE(Has(yes.body, "side recent"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
