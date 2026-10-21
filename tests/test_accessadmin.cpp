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
 * The access matrix page (access-conditions story A4), through the real
 * server: /admin/access lists what each role and each enabled module grants,
 * to an administrator and to no one else, and prints nothing a role's,
 * module's or condition's author wrote without escaping it.
 *
 * Each refusal has a control beside it (the administrator is shown the very
 * rows the others are not), so a page that showed nothing to anyone would not
 * pass.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/condition.h>
#include <ghoti.io/defiant-test/defiant-test.h>

#include <algorithm>

#define AA_STR2(x) #x
#define AA_STR(x) AA_STR2(x)

namespace {

const char kCoreToken[] = AA_STR(GHOTIIO_DEFCORE_NAME);

/* What an author might write to break a page that printed it raw. */
const char kHostileRole[] = "<b id=\"role\">boss</b>";
const char kHostilePermission[] = "Read <i>posts</i> & \"more\"";
const char kHostileLabel[] = "acc.<img src=x onerror=x>";
const char kHostileConstant[] = "<script>alert(x)</script>";

GDEFCORE_Module_Manifest g_man;
GDEFCORE_Module_Perm g_perm;
GDEFCORE_Module_Grant g_grant;
std::string g_condition;

extern "C" const GDEFCORE_Module_Manifest * AccEntry(void) { return &g_man; }

/* The page the way the response scanner reads it: declaring the encoding is
 * not naming a row. */
bool ScansClean(const std::string & page) {
  std::string text = page;
  size_t at;
  while ((at = text.find("charset=\"utf-8\"")) != std::string::npos) {
    text.erase(at, 15);
  }
  return gdeftest_response_has_internal_id(text.data(), text.size(), NULL, 0) == 0;
}

class AccessAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string admin_;
  std::string bob_;

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  /* A site with a role and a module that grant, every name hostile, ada the
   * administrator and bob with no role. */
  void Open() {
    GDEFCORE_Catalog * catalog;
    GDEFCORE_Permission read;
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Condition * parsed = NULL;
    char * words = NULL;
    size_t words_len = 0;
    std::vector<std::string> lines;
    std::string text = "acc";
    Site(id_);
    UseHost();
    AddUser(id_, "bob", "hunter2");
    catalog = Catalog();
    memset(&read, 0, sizeof read);
    read.size = sizeof read;
    read.label = kHostilePermission;
    read.label_len = strlen(kHostilePermission);
    read.module_name = "site";
    read.module_name_len = 4;
    read.action = "read";
    read.action_len = 4;
    read.condition = GDEFCORE_ACCESS_ANY;
    read.type_name = "site.post";
    read.type_name_len = 9;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog, &read));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_role_create(catalog, kHostileRole, strlen(kHostileRole),
            NULL));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, kHostileRole, strlen(kHostileRole),
            kHostilePermission, strlen(kHostilePermission), "core.role", 9));
    /* A module whose permission, action and condition are hostile as well. */
    g_condition = std::string("(= field.title \"") + kHostileConstant + "\")";
    memset(&g_perm, 0, sizeof g_perm);
    g_perm.size = sizeof g_perm;
    g_perm.label = kHostileLabel;
    g_perm.label_len = strlen(kHostileLabel);
    g_perm.action = "read";
    g_perm.action_len = 4;
    g_perm.condition = GDEFCORE_ACCESS_ANY;
    g_perm.type_name = "site.post";
    g_perm.type_name_len = 9;
    memset(&g_grant, 0, sizeof g_grant);
    g_grant.size = sizeof g_grant;
    g_grant.permission = kHostileLabel;
    g_grant.permission_len = strlen(kHostileLabel);
    g_grant.type_name = "site.post";
    g_grant.type_name_len = 9;
    g_grant.condition = g_condition.c_str();
    g_grant.condition_len = g_condition.size();
    memset(&g_man, 0, sizeof g_man);
    g_man.size = sizeof g_man;
    g_man.core_token = kCoreToken;
    g_man.core_token_len = strlen(kCoreToken);
    g_man.schema_version = 1;
    g_man.name = "acc";
    g_man.name_len = 3;
    g_man.label = "acc";
    g_man.label_len = 3;
    g_man.perms = &g_perm;
    g_man.nperms = 1;
    g_man.grants = &g_grant;
    g_man.ngrants = 1;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, AccEntry));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_condition_parse(g_condition.data(), g_condition.size(), &parsed,
            NULL));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_condition_explain(parsed, &words, &words_len));
    lines.push_back(std::string("Module acc grants ") + kHostileLabel +
                    " (low risk): " + std::string(words, words_len));
    lines.push_back(kHostileLabel);
    std::sort(lines.begin(), lines.end());
    for (const std::string & line : lines) {
      text += "\n" + line;
    }
    gdefcore_condition_text_free(words);
    gdefcore_condition_free(parsed);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, "acc", 3));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, text.data(), text.size()));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
    admin_ = CookieLine(Login("ada", "secret"));
    bob_ = CookieLine(Login("bob", "hunter2"));
  }
};

}  // namespace

TEST_F(AccessAdmin, TheAdministratorSeesRoleAndModuleRows) {
  Open();
  Reply page = Get("/admin/access", admin_);
  EXPECT_EQ(200, page.status);
  EXPECT_TRUE(Has(page.body, "<h1>Access</h1>"));
  EXPECT_TRUE(Has(page.body, "<table>"));
  EXPECT_TRUE(Has(page.body, "<th>Granted by</th>"));
  /* Both kinds of source, named. */
  EXPECT_TRUE(Has(page.body, "<td>module acc</td>")) << page.body;
  EXPECT_TRUE(Has(page.body, "<td>role admin</td>") ||
              Has(page.body, "<td>role "))
      << page.body;
  /* Each has its type, its risk and a condition in words. */
  EXPECT_TRUE(Has(page.body, "<td>site.post</td>")) << page.body;
  EXPECT_TRUE(Has(page.body, "<td class=\"risk-low\">low</td>")) << page.body;
  EXPECT_TRUE(Has(page.body, "<td class=\"risk-medium\">medium</td>")) << page.body;
  EXPECT_TRUE(Has(page.body, "the type is site.post")) << page.body;
  ExpectSecurity(page);
  /* It is a link from the More page, and the page itself links home. */
  EXPECT_TRUE(Has(Get("/admin/more", admin_).body, "href=\"/admin/access\""));
  EXPECT_TRUE(Has(page.body, "href=\"/admin\""));
  EXPECT_FALSE(Has(page.body, "<form"));
  EXPECT_FALSE(Has(page.body, " style="));
  /* No internal id is shown (the response scanner's rule). */
  EXPECT_TRUE(ScansClean(page.body));
}

TEST_F(AccessAdmin, HostileTextIsEscaped) {
  Open();
  Reply page = Get("/admin/access", admin_);
  ASSERT_EQ(200, page.status);
  /* The control: every hostile text is on the page, as text... */
  EXPECT_TRUE(Has(page.body, "boss&lt;/b&gt;")) << page.body;
  EXPECT_TRUE(Has(page.body, "Read &lt;i&gt;posts&lt;/i&gt; &amp; &quot;more&quot;"))
      << page.body;
  EXPECT_TRUE(Has(page.body, "acc.&lt;img src=x onerror=x&gt;")) << page.body;
  EXPECT_TRUE(Has(page.body, "&lt;script&gt;alert(x)&lt;/script&gt;")) << page.body;
  /* ...and none of it is markup. */
  EXPECT_FALSE(Has(page.body, "<b id=")) << page.body;
  EXPECT_FALSE(Has(page.body, "<i>posts")) << page.body;
  EXPECT_FALSE(Has(page.body, "<img src=x")) << page.body;
  EXPECT_FALSE(Has(page.body, "<script>alert")) << page.body;
  EXPECT_FALSE(Has(page.body, "<script>")) << page.body;
}

TEST_F(AccessAdmin, NoOneElseSeesIt) {
  Open();
  Reply seen = Get("/admin/access", admin_);
  ASSERT_EQ(200, seen.status);
  ASSERT_TRUE(Has(seen.body, "module acc"));
  /* The pages that do not exist, for each visitor, are what they are told. */
  const std::string cookies[] = {std::string(), bob_,
      "Cookie: __Host-admin=not-a-token\r\n"};
  for (const std::string & cookie : cookies) {
    Reply none = Get("/admin/access", cookie);
    Reply missing = Get("/admin/nothing", cookie);
    EXPECT_EQ(missing.status, none.status) << cookie;
    EXPECT_EQ(missing.body, none.body) << cookie;
    EXPECT_FALSE(Has(none.body, "Granted by")) << cookie;
    EXPECT_FALSE(Has(none.body, "module acc")) << cookie;
    EXPECT_FALSE(Has(none.body, "alert")) << cookie;
  }
}

TEST_F(AccessAdmin, ReadOnly) {
  Open();
  Reply page = Get("/admin/access", admin_);
  ASSERT_EQ(200, page.status);
  Reply posted = PostRaw("/admin/access", admin_, "x=1");
  EXPECT_NE(200, posted.status);
  EXPECT_FALSE(Has(posted.body, "Granted by"));
}

namespace {

/* A module whose access callback decides a field (access story A6). */
GDEFCORE_Module_Manifest g_cb_man;
GDEFCORE_Module_Perm g_cb_perm;
GDEFCORE_Module_Callback g_cb;
GDEFCORE_Module_Service g_cb_service;
GDEFCORE_Module_Access_Callback g_cb_table;

extern "C" GDEFCORE_Result AccPaid(GDEFCORE_Callback_Call * call,
    GDEFCORE_Request * request, const GDEFCORE_Callback_Question * q,
    GDEFCORE_Callback_Answer * a, void * user) {
  (void)call;
  (void)request;
  (void)q;
  (void)a;
  (void)user;
  return GDEFCORE_OK;
}

extern "C" const GDEFCORE_Module_Manifest * AccCbEntry(void) {
  return &g_cb_man;
}

}  // namespace

/* An access callback is a row of its module, said to be decided per entity
 * and rated high; the module's plain grant beside it keeps its own rating. */
TEST_F(AccessAdmin, ACallbackIsShownAsDecidedPerEntityAndRatedHigh) {
  Open();
  GDEFCORE_Catalog * catalog = Catalog();
  GDEFCORE_Changeset * cs = NULL;
  memset(&g_cb_perm, 0, sizeof g_cb_perm);
  g_cb_perm.size = sizeof g_cb_perm;
  g_cb_perm.label = "Read paid body";
  g_cb_perm.label_len = 14;
  g_cb_perm.action = "read";
  g_cb_perm.action_len = 4;
  g_cb_perm.condition = GDEFCORE_ACCESS_ANY;
  g_cb_perm.type_name = "site.post";
  g_cb_perm.type_name_len = 9;
  g_cb_perm.field = "body";
  g_cb_perm.field_len = 4;
  memset(&g_cb_table, 0, sizeof g_cb_table);
  g_cb_table.size = sizeof g_cb_table;
  g_cb_table.fn = AccPaid;
  memset(&g_cb_service, 0, sizeof g_cb_service);
  g_cb_service.size = sizeof g_cb_service;
  g_cb_service.name = "paywall.paid";
  g_cb_service.name_len = 12;
  g_cb_service.version = GDEFCORE_MODULE_ACCESS_CALLBACK_VERSION;
  g_cb_service.table = &g_cb_table;
  memset(&g_cb, 0, sizeof g_cb);
  g_cb.size = sizeof g_cb;
  g_cb.name = "paid";
  g_cb.name_len = 4;
  g_cb.scope = GDEFCORE_CALLBACK_FIELD;
  g_cb.permission = "Read paid body";
  g_cb.permission_len = 14;
  g_cb.type_name = "site.post";
  g_cb.type_name_len = 9;
  g_cb.field = "body";
  g_cb.field_len = 4;
  g_cb.service = "paywall.paid";
  g_cb.service_len = 12;
  g_cb.fuel = 10;
  g_cb.timeout_ms = 50;
  g_cb.cache = GDEFCORE_CALLBACK_CACHE_TOKEN;
  g_cb.flags = GDEFCORE_CALLBACK_LOCKED;
  memset(&g_cb_man, 0, sizeof g_cb_man);
  g_cb_man.size = sizeof g_cb_man;
  g_cb_man.core_token = kCoreToken;
  g_cb_man.core_token_len = strlen(kCoreToken);
  g_cb_man.schema_version = 1;
  g_cb_man.name = "paywall";
  g_cb_man.name_len = 7;
  g_cb_man.label = "paywall";
  g_cb_man.label_len = 7;
  g_cb_man.perms = &g_cb_perm;
  g_cb_man.nperms = 1;
  g_cb_man.services = &g_cb_service;
  g_cb_man.nservices = 1;
  g_cb_man.callbacks = &g_cb;
  g_cb_man.ncallbacks = 1;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, AccCbEntry));
  const std::string confirm =
      "paywall\nModule paywall decides Read paid body per entity with its "
      "callback paid (field scope, high risk): its breadth cannot be judged.\n"
      "Read paid body";
  ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, "paywall", 7));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_changeset_apply(cs, confirm.data(), confirm.size()));
  gdefcore_changeset_destroy(cs);
  gdefcore_catalog_destroy(catalog);
  Reply page = Get("/admin/access", admin_);
  ASSERT_EQ(200, page.status);
  size_t at = page.body.find("<td>module paywall</td>");
  ASSERT_NE(std::string::npos, at) << page.body;
  std::string row = page.body.substr(at, page.body.find("</tr>", at) - at);
  EXPECT_TRUE(Has(row, "<td>site.post, field body</td>")) << row;
  EXPECT_TRUE(Has(row, "evaluated per entity by its callback paid")) << row;
  EXPECT_TRUE(Has(row, "<td class=\"risk-high\">high</td>")) << row;
  /* Control: the plain module grant beside it keeps its low rating. */
  at = page.body.find("<td>module acc</td>");
  ASSERT_NE(std::string::npos, at) << page.body;
  row = page.body.substr(at, page.body.find("</tr>", at) - at);
  EXPECT_TRUE(Has(row, "<td class=\"risk-low\">low</td>")) << row;
  Reply none = Get("/admin/access", bob_);
  EXPECT_FALSE(Has(none.body, "paywall"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
