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
 * AD-25's cross-tenant attack at the web level. Two tenants share one server,
 * one process and one thread, and the tests send requests to both in turn:
 * each tenant's session cookie and signed form token is presented at the
 * other, the host header and the forwarded host choose the tenant, and no
 * page of one shows anything of the other. Every "not accepted" is judged
 * against the same request made with no cookie at all, so that a refusal is
 * measured against what a stranger gets, not against a status someone
 * remembered. Each cross-tenant check is paired with the same request at the
 * token's own tenant, which must be accepted: a test that refuses everything
 * proves nothing.
 *
 * tools/check-plants.sh plants "a tenant-A token accepted at tenant B" and
 * "the forwarded host trusted from a stranger" in scratch copies of the
 * server and requires Tenants.SessionAtOtherTenant and Tenants.HostSelects to
 * fail by name.
 */

#include "tenants_fixture.h"

namespace {

class Tenants : public Pair {
 protected:
  bool IsAdminPage(const Reply & reply) {
    return reply.status == 200 && (Has(reply.body, "href=\"/admin/post\"") ||
                                       Has(reply.body, "href=\"/admin\""));
  }

  /* A response to a request that carried a cookie the host did not accept
   * must be the one a stranger gets, byte for byte. */
  void ExpectStranger(const char * host, const char * path, const Reply & got,
      const char * what) {
    Reply stranger = GetAt(host, path, std::string());
    EXPECT_EQ(stranger.status, got.status) << what << " " << path;
    EXPECT_EQ(stranger.body, got.body) << what << " " << path;
    EXPECT_FALSE(IsAdminPage(got)) << what << " " << path;
  }

  /* Strings that belong to one tenant alone. */
  std::vector<std::string> Marks(int tenant) {
    return tenant == 0
        ? std::vector<std::string>{"Alpha Title", "alpha-post", kHostA, a_}
        : std::vector<std::string>{"Beta Title", "beta-post", kHostB, b_};
  }
};

}  // namespace

TEST_F(Tenants, HostSelects) {
  Open();
  const char * const hosts[2] = {kHostA, kHostB};
  const char * const aliases[2] = {"/alpha-post", "/beta-post"};
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < 2; i++) {
      int other = 1 - i;
      Reply mine = GetAt(hosts[i], aliases[i], std::string());
      Reply theirs = GetAt(hosts[i], aliases[other], std::string());
      EXPECT_EQ(200, mine.status);
      EXPECT_TRUE(Has(mine.body, i == 0 ? "Alpha Title" : "Beta Title"));
      EXPECT_FALSE(Has(mine.body, other == 0 ? "Alpha Title" : "Beta Title"));
      /* The other tenant's alias names nothing here. */
      EXPECT_EQ(0u, theirs.body.size()) << hosts[i] << aliases[other];
      /* The front page is this tenant's. */
      Reply front = GetAt(hosts[i], "/", std::string());
      EXPECT_TRUE(Has(front.body, i == 0 ? "Alpha Title" : "Beta Title"));
      EXPECT_FALSE(Has(front.body, other == 0 ? "Alpha Title" : "Beta Title"));
    }
  }
  /* Without a trusted proxy a forwarded host is ignored, whoever sends it. */
  Reply forwarded = Ask(Req("GET", "/", kHostA, "X-Forwarded-Host: beta.example\r\n", NULL), 20000);
  EXPECT_EQ(200, forwarded.status);
  EXPECT_TRUE(Has(forwarded.body, "Alpha Title"));
  EXPECT_FALSE(Has(forwarded.body, "Beta Title"));
  /* A host with no tenant gets the fixed 404, the same for each spelling. */
  Reply none = GetAt("gamma.example", "/", std::string());
  Reply none2 = GetAt("alpha.example.evil.example", "/", std::string());
  EXPECT_EQ(404, none.status);
  EXPECT_EQ(none.body, none2.body);
  EXPECT_EQ(404, none2.status);
}

TEST_F(Tenants, ForwardedHostWhenTrusted) {
  Open("127.0.0.1");
  Reply a = Ask(Req("GET", "/", kHostA, NULL, NULL), 20000);
  Reply b = Ask(Req("GET", "/", kHostA, "X-Forwarded-Host: beta.example\r\n", NULL), 20000);
  Reply a_again = Ask(Req("GET", "/", kHostB, "X-Forwarded-Host: alpha.example\r\n", NULL), 20000);
  EXPECT_TRUE(Has(a.body, "Alpha Title"));
  EXPECT_FALSE(Has(a.body, "Beta Title"));
  EXPECT_TRUE(Has(b.body, "Beta Title"));
  EXPECT_FALSE(Has(b.body, "Alpha Title"));
  EXPECT_TRUE(Has(a_again.body, "Alpha Title"));
}

TEST_F(Tenants, SessionAtOtherTenant) {
  Open();
  std::string ca = CookiesFor(kHostA, "ada", "secret");
  std::string cb = CookiesFor(kHostB, "ada", "secret");
  ASSERT_FALSE(ca.empty());
  ASSERT_FALSE(cb.empty());
  EXPECT_NE(ca, cb);
  const char * const paths[] = {"/admin", "/admin/post", "/admin/front",
      "/admin/undo", "/admin/more"};
  /* Interleaved: A's cookie at A, at B, B's at B, at A, and round again. */
  for (int round = 0; round < 3; round++) {
    for (const char * path : paths) {
      EXPECT_TRUE(IsAdminPage(GetAt(kHostA, path, ca))) << "control A " << path;
      ExpectStranger(kHostB, path, GetAt(kHostB, path, ca), "A's session at B");
      EXPECT_TRUE(IsAdminPage(GetAt(kHostB, path, cb))) << "control B " << path;
      ExpectStranger(kHostA, path, GetAt(kHostA, path, cb), "B's session at A");
    }
  }
  /* The Lax session cookie alone, which is all a plain page reads. */
  std::string lax_a = "Cookie: " + CookiePair(LoginAt(kHostA, "ada", "secret"), "__Host-session") + "\r\n";
  ExpectStranger(kHostB, "/admin", GetAt(kHostB, "/admin", lax_a), "A's lax session at B");
  /* A's cookie sent to B's sign-in page does not skip the form. */
  Reply login_b = GetAt(kHostB, "/login", ca);
  Reply login_b_stranger = GetAt(kHostB, "/login", std::string());
  EXPECT_EQ(login_b_stranger.body, login_b.body);
}

TEST_F(Tenants, FormAtOtherTenant) {
  Open();
  std::string ca = CookiesFor(kHostA, "ada", "secret");
  std::string cb = CookiesFor(kHostB, "ada", "secret");
  ASSERT_FALSE(ca.empty());
  ASSERT_FALSE(cb.empty());
  std::string token_a = InputValue(GetAt(kHostA, "/admin/post", ca).body, "token", 0);
  std::string token_b = InputValue(GetAt(kHostB, "/admin/post", cb).body, "token", 0);
  ASSERT_FALSE(token_a.empty());
  ASSERT_FALSE(token_b.empty());
  const std::string fields = "&summary=s&body=b";
  /* A's form under B's session and under A's own session, sent to B. */
  PostFollow(kHostB, "/admin/post", cb, Form(token_a, ("&title=Cross+One" + fields).c_str()));
  PostFollow(kHostB, "/admin/post", ca, Form(token_a, ("&title=Cross+Two" + fields).c_str()));
  /* And B's form to A. */
  PostFollow(kHostA, "/admin/post", ca, Form(token_b, ("&title=Cross+Three" + fields).c_str()));
  PostFollow(kHostA, "/admin/post", cb, Form(token_b, ("&title=Cross+Four" + fields).c_str()));
  for (const char * host : {kHostA, kHostB}) {
    std::string front = GetAt(host, "/", std::string()).body;
    for (const char * title : {"Cross One", "Cross Two", "Cross Three", "Cross Four"}) {
      EXPECT_FALSE(Has(front, title)) << host << " shows " << title;
    }
  }
  /* The controls: each form at its own tenant, with its own session. */
  Reply done_a = PostFollow(kHostA, "/admin/post", ca, Form(token_a, ("&title=Alpha+Only" + fields).c_str()));
  Reply done_b = PostFollow(kHostB, "/admin/post", cb, Form(token_b, ("&title=Beta+Only" + fields).c_str()));
  EXPECT_TRUE(Has(done_a.body, "Saved"));
  EXPECT_TRUE(Has(done_b.body, "Saved"));
  std::string front_a = GetAt(kHostA, "/", std::string()).body;
  std::string front_b = GetAt(kHostB, "/", std::string()).body;
  EXPECT_TRUE(Has(front_a, "Alpha Only"));
  EXPECT_FALSE(Has(front_a, "Beta Only"));
  EXPECT_TRUE(Has(front_b, "Beta Only"));
  EXPECT_FALSE(Has(front_b, "Alpha Only"));
}

TEST_F(Tenants, NoPageLeaks) {
  Open();
  std::string cookies[2] = {CookiesFor(kHostA, "ada", "secret"),
      CookiesFor(kHostB, "ada", "secret")};
  ASSERT_FALSE(cookies[0].empty());
  ASSERT_FALSE(cookies[1].empty());
  const char * const hosts[2] = {kHostA, kHostB};
  const char * const paths[] = {"/", "/alpha-post", "/beta-post", "/no-such",
      "/login", "/admin", "/admin/post", "/admin/front", "/admin/undo",
      "/admin/more", "/admin/post/saved"};
  size_t seen = 0;
  for (int round = 0; round < 2; round++) {
    for (int i = 0; i < 2; i++) {
      int other = 1 - i;
      /* Write something that only this tenant has, then read everything. */
      if (round == 0) {
        std::string token = InputValue(GetAt(hosts[i], "/admin/post", cookies[i]).body, "token", 0);
        ASSERT_FALSE(token.empty());
        PostFollow(hosts[i], "/admin/post", cookies[i],
            Form(token, i == 0 ? "&title=Only+In+Alpha&summary=s&body=b"
                               : "&title=Only+In+Beta&summary=s&body=b"));
      }
      for (const std::string & cookie : {std::string(), cookies[i]}) {
        for (const char * path : paths) {
          Reply got = GetAt(hosts[i], path, cookie);
          seen += 1;
          for (const std::string & mark : Marks(other)) {
            /* The other tenant's alias is a path the request itself names;
             * the page must not echo it back, nor anything else of theirs. */
            if (mark == "alpha-post" || mark == "beta-post") {
              continue;
            }
            EXPECT_FALSE(Has(got.body, mark.c_str()))
                << hosts[i] << " " << path << " shows " << mark;
          }
          EXPECT_FALSE(Has(got.body, i == 0 ? "Only In Beta" : "Only In Alpha"))
              << hosts[i] << " " << path;
          for (const std::string & line : HeaderAll(got, "Location")) {
            for (const std::string & mark : Marks(other)) {
              EXPECT_EQ(std::string::npos, line.find(mark)) << hosts[i] << " " << path;
            }
          }
        }
      }
    }
  }
  EXPECT_GT(seen, (size_t)80);
  /* The controls: what was written is there, at its own tenant. */
  EXPECT_TRUE(Has(GetAt(kHostA, "/", std::string()).body, "Only In Alpha"));
  EXPECT_TRUE(Has(GetAt(kHostB, "/", std::string()).body, "Only In Beta"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
