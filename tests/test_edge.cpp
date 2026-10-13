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
 * Story 16's matrix, on the memory backend. One test per row.
 */

#include "edge_fixture.h"


TEST_F(Edge, EmptyListenBindsNothing) {
  GDEFCORE_Inst_Config * config = NULL;
  SetEnv("DEFIANT_LISTEN", "");
  UnsetEnv("DEFIANT_TRUSTED_PROXIES");
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, NULL, &server_));
  gdefcore_inst_config_destroy(config);
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_listen(server_));
  EXPECT_EQ(0, gdefweb_server_accepting(server_));
}

TEST_F(Edge, FrontPage) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply alive;
  Reply ready;
  Reply page;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_default(inst_, id));
  InstallKit(id);
  Listen(NULL);
  alive = Ask(Req("GET", "/alive", "not-a-tenant.example",
                  "X-Forwarded-Host: other.example\r\n", NULL),
      5000);
  ready = Ask(Req("GET", "/ready", "also-not.example", NULL, NULL), 5000);
  page = Ask(Req("GET", "/", "127.0.0.1", NULL, NULL), 15000);
  EXPECT_EQ(200, alive.status);
  EXPECT_EQ(200, ready.status);
  EXPECT_EQ(200, page.status);
  EXPECT_NE(std::string::npos, page.body.find("class=\"main\""));
  ExpectSecurity(alive);
  ExpectSecurity(ready);
  ExpectSecurity(page);
}

TEST_F(Edge, Alias) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply hit;
  Reply miss;
  Reply miss_again;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  hit = Ask(Req("GET", "/hello-title", "site.example", NULL, NULL), 20000);
  miss = Ask(Req("GET", "/no-such", "site.example", NULL, NULL), 15000);
  miss_again = Ask(Req("GET", "/also-missing", "site.example", NULL, NULL), 15000);
  EXPECT_EQ(200, hit.status);
  EXPECT_NE(std::string::npos, hit.body.find("Hello Title"));
  EXPECT_EQ(hit.status, miss.status);
  EXPECT_EQ(0u, miss.body.size());
  EXPECT_EQ(miss.status, miss_again.status);
  EXPECT_EQ(0u, miss_again.body.size());
  ExpectSecurity(hit);
  ExpectSecurity(miss);
}

TEST_F(Edge, UnknownHost) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply reply;
  std::string host(257, 'a');
  Reply bad;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "known.example", 13, "", 0, id));
  reply = Ask(Req("GET", "/", "missing.example", NULL, NULL), 5000);
  bad = Ask(Req("GET", "/", host.c_str(), NULL, NULL), 5000);
  EXPECT_EQ(404, reply.status);
  EXPECT_EQ(std::string("Not found\n"), reply.body);
  EXPECT_EQ(reply.body, bad.body);
  EXPECT_EQ(reply.status, bad.status);
  EXPECT_EQ(std::string::npos, reply.body.find("missing.example"));
  EXPECT_EQ(std::string::npos, reply.body.find(id));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_default(inst_, other));
  EXPECT_EQ(std::string::npos, reply.body.find(other));
  ExpectSecurity(reply);
}

TEST_F(Edge, UntrustedForward) {
  char acme[GDEFCORE_PUBLIC_ID_LEN + 1];
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply reply;
  std::string log;
  Listen("203.0.113.5");
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "acme.example", 12, "", 0, acme));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "other.example", 13, "", 0, other));
  InstallKit(acme);
  InstallKit(other);
  Publish(acme, "acme-post", "Acme Title");
  Publish(other, "other-post", "Other Title");
  fflush(stdout);
  testing::internal::CaptureStdout();
  reply = Ask(Req("GET", "/?password=hunter2", "acme.example",
                  "X-Forwarded-Host: other.example\r\n"
                  "X-Forwarded-For: 203.0.113.9\r\n",
                  NULL),
      20000);
  fflush(stdout);
  log = testing::internal::GetCapturedStdout();
  EXPECT_EQ(200, reply.status);
  EXPECT_NE(std::string::npos, reply.body.find("Acme Title"));
  EXPECT_EQ(std::string::npos, reply.body.find("Other Title"));
  EXPECT_NE(std::string::npos, log.find("client=127.0.0.0"));
  EXPECT_EQ(std::string::npos, log.find("203.0.113.9"));
  EXPECT_EQ(std::string::npos, log.find("hunter2"));
  EXPECT_EQ(std::string::npos, log.find("?"));
  EXPECT_EQ(std::string::npos, log.find("password"));
}

TEST_F(Edge, TrustedForward) {
  char acme[GDEFCORE_PUBLIC_ID_LEN + 1];
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply reply;
  std::string log;
  Listen("127.0.0.1");
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "acme.example", 12, "", 0, acme));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "other.example", 13, "", 0, other));
  InstallKit(acme);
  InstallKit(other);
  Publish(acme, "acme-post", "Acme Title");
  Publish(other, "other-post", "Other Title");
  fflush(stdout);
  testing::internal::CaptureStdout();
  reply = Ask(Req("GET", "/", "acme.example",
                  "X-Forwarded-Host: other.example\r\n"
                  "X-Forwarded-For: 203.0.113.9\r\n",
                  NULL),
      20000);
  fflush(stdout);
  log = testing::internal::GetCapturedStdout();
  EXPECT_EQ(200, reply.status);
  EXPECT_NE(std::string::npos, reply.body.find("Other Title"));
  EXPECT_EQ(std::string::npos, reply.body.find("Acme Title"));
  EXPECT_NE(std::string::npos, log.find("client=203.0.0.0"));
  EXPECT_EQ(std::string::npos, log.find("203.0.113.9"));
}

TEST_F(Edge, ReadyBeforeOpen) {
  GDEFCORE_Inst_Config * config = NULL;
  Reply closed;
  Reply open;
  SetEnv("DEFIANT_LISTEN", "127.0.0.1:0");
  UnsetEnv("DEFIANT_TRUSTED_PROXIES");
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, NULL, &server_));
  gdefcore_inst_config_destroy(config);
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_listen(server_));
  closed = Ask(Req("GET", "/ready", "anywhere.example",
                   "X-Forwarded-Host: ignored.example\r\n", NULL),
      5000);
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_set_inst(server_, inst_, root_, sizeof root_));
  open = Ask(Req("GET", "/ready", "anywhere.example", NULL, NULL), 5000);
  EXPECT_EQ(503, closed.status);
  EXPECT_EQ(200, open.status);
}

TEST_F(Edge, Cookie) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply anon;
  Reply logged;
  Reply bad;
  std::string cookie;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  anon = Ask(Req("GET", "/hello-title", "site.example", NULL, NULL), 20000);
  logged = Ask(Req("POST", "/login", "site.example",
                   "Origin: http://site.example\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\n",
                   "username=ada&password=secret"),
      20000);
  cookie = HeaderValue(logged, "Set-Cookie");
  EXPECT_NE(std::string::npos, cookie.find("__Host-session="));
  EXPECT_NE(std::string::npos, cookie.find("Secure"));
  EXPECT_NE(std::string::npos, cookie.find("HttpOnly"));
  EXPECT_NE(std::string::npos, cookie.find("SameSite=Lax"));
  EXPECT_NE(std::string::npos, cookie.find("Path=/"));
  EXPECT_EQ(std::string::npos, cookie.find("Domain="));
  logged = Ask(Req("GET", "/hello-title", "site.example",
                   (std::string("Cookie: ") + cookie + "\r\n").c_str(), NULL),
      20000);
  bad = Ask(Req("GET", "/hello-title", "site.example",
                "Cookie: __Host-session=not-a-token\r\n", NULL),
      15000);
  EXPECT_EQ(200, anon.status);
  EXPECT_NE(std::string::npos, anon.body.find("Hello Title"));
  EXPECT_EQ(200, logged.status);
  EXPECT_EQ(std::string::npos, logged.body.find("Hello Title"));
  EXPECT_EQ(anon.status, bad.status);
  EXPECT_EQ(anon.body, bad.body);
  {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Request * request = NULL;
    GDEFCORE_Keyring * keyring = NULL;
    uint8_t storage[16];
    unsigned char raw[768];
    size_t raw_n = 0;
    size_t at = cookie.find("__Host-session=");
    size_t end;
    std::string token;
    const char * principal = NULL;
    size_t principal_n = 0;
    ASSERT_NE(std::string::npos, at);
    token = cookie.substr(at + strlen("__Host-session="));
    end = token.find(';');
    if (end != std::string::npos) {
      token = token.substr(0, end);
    }
    ASSERT_EQ(0, B64Url(token, raw, sizeof raw, &raw_n));
    ASSERT_GT(raw_n, 0u);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_storage_key(inst_, id, strlen(id), storage));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    ASSERT_NE(keyring, nullptr);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_request_create(storage, sizeof storage, 0, 0, NULL, &request));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_check(catalog, request, keyring, root_, sizeof root_,
            (const char *)raw, raw_n));
    principal = gdefcore_request_principal(request, &principal_n);
    EXPECT_NE(principal, nullptr);
    EXPECT_EQ(GDEFCORE_PUBLIC_ID_LEN, principal_n);
    {
      char ** roles = NULL;
      size_t role_count = 0;
      size_t ri;
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_principal_roles(catalog, principal, principal_n, &roles,
              &role_count));
      ASSERT_GT(role_count, 0u);
      for (ri = 0; ri < role_count; ri++) {
        const char * confirm = "core.role";
        if (strcmp(roles[ri], "authenticated") == 0) {
          confirm = GDEFCORE_ACCESS_CONFIRM_AUTHENTICATED;
        } else if (strcmp(roles[ri], "anonymous") == 0) {
          confirm = GDEFCORE_ACCESS_CONFIRM_ANONYMOUS;
        }
        ASSERT_EQ(GDEFCORE_OK,
            gdefcore_access_grant(catalog, roles[ri], strlen(roles[ri]),
                "Read posts", 10, confirm, strlen(confirm)));
      }
      gdefcore_access_free(catalog, roles, role_count);
    }
    gdefcore_request_destroy(request);
    gdefcore_catalog_destroy(catalog);
  }
  logged = Ask(Req("GET", "/hello-title", "site.example",
                   (std::string("Cookie: ") + cookie + "\r\n").c_str(), NULL),
      20000);
  EXPECT_EQ(200, logged.status);
  EXPECT_GT(logged.body.size(), 0u);
  EXPECT_NE(std::string::npos, logged.body.find("class=\"post\""));
  EXPECT_NE(std::string::npos, logged.body.find("Hello Title"));
}

TEST_F(Edge, Inactive) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char quiet[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Catalog * catalog = NULL;
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  std::string found;
  const char * fields[] = {"storage", "placement", "edition", "status",
      "resolution", "is_default"};
  std::string values[6];
  GDEFCORE_Value staged[6];
  const char * texts[6];
  size_t lens[6];
  GDEFCORE_Changeset * changeset = NULL;
  Reply unknown;
  Reply inactive;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "quiet.example", 13, "", 0, quiet));
  gdefcore_inst_destroy(inst_);
  inst_ = NULL;
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_catalog_open(registry_, kRegistry, sizeof kRegistry, NULL, NULL,
          &catalog));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_entity_ids(catalog, "core.tenant", 11, &ids, &count));
  for (i = 0; i < count; i++) {
    char ** texts_out = NULL;
    size_t * lens_out = NULL;
    size_t n = 0;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
            GDEFCORE_PUBLIC_ID_LEN, "en", 2, "resolution", 10, &texts_out,
            &lens_out, &n));
    if (n == 1 && lens_out[0] == 13 &&
        memcmp(texts_out[0], "quiet.example", 13) == 0) {
      found = ids[i];
    }
    gdefcore_values_free(catalog, texts_out, lens_out, n);
  }
  ASSERT_FALSE(found.empty());
  for (i = 0; i < 6; i++) {
    char ** texts_out = NULL;
    size_t * lens_out = NULL;
    size_t n = 0;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
            found.c_str(), GDEFCORE_PUBLIC_ID_LEN, "en", 2, fields[i],
            strlen(fields[i]), &texts_out, &lens_out, &n));
    if (n == 1) {
      values[i].assign(texts_out[0], lens_out[0]);
    }
    gdefcore_values_free(catalog, texts_out, lens_out, n);
  }
  values[3] = "closed";
  for (i = 0; i < 6; i++) {
    texts[i] = values[i].c_str();
    lens[i] = values[i].size();
    memset(&staged[i], 0, sizeof staged[i]);
    staged[i].size = sizeof staged[i];
    staged[i].field = fields[i];
    staged[i].field_len = strlen(fields[i]);
    staged[i].texts = &texts[i];
    staged[i].text_lens = &lens[i];
    staged[i].count = 1;
  }
  ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &changeset));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_entity_save(changeset, found.c_str(), GDEFCORE_PUBLIC_ID_LEN, "en",
          2, staged, 6));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_changeset_apply(changeset, "core.tenant", 11));
  gdefcore_changeset_destroy(changeset);
  gdefcore_ids_free(catalog, ids, count);
  gdefcore_catalog_destroy(catalog);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_open(registry_, site_, root_, sizeof root_, "", 0, NULL,
          &inst_));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_set_inst(server_, inst_, root_, sizeof root_));
  unknown = Ask(Req("GET", "/", "missing.example", NULL, NULL), 5000);
  inactive = Ask(Req("GET", "/", "quiet.example", NULL, NULL), 5000);
  EXPECT_EQ(unknown.status, inactive.status);
  EXPECT_EQ(unknown.body, inactive.body);
  EXPECT_EQ(std::string("Not found\n"), inactive.body);
  EXPECT_EQ(std::string::npos, inactive.body.find("quiet.example"));
  EXPECT_EQ(std::string::npos, inactive.body.find(quiet));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_default(inst_, id));
  EXPECT_EQ(std::string::npos, inactive.body.find(id));
}

TEST_F(Edge, Login) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply ok;
  Reply bad;
  Reply foreign;
  std::string cookie;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  foreign = Ask(Req("POST", "/login", "site.example",
                    "Origin: http://evil.example\r\n"
                    "Content-Type: application/x-www-form-urlencoded\r\n",
                    "username=ada&password=secret"),
      5000);
  EXPECT_EQ(403, foreign.status);
  EXPECT_EQ(std::string::npos, HeaderValue(foreign, "Set-Cookie").find("__Host-session="));
  bad = Ask(Req("POST", "/login", "site.example",
                "Origin: http://site.example\r\n"
                "Content-Type: application/x-www-form-urlencoded\r\n",
                "username=ada&password=wrong"),
      20000);
  ok = Ask(Req("POST", "/login", "site.example",
               "Origin: http://site.example\r\n"
               "Content-Type: application/x-www-form-urlencoded\r\n",
               "username=ada&password=secret"),
      20000);
  cookie = HeaderValue(ok, "Set-Cookie");
  EXPECT_EQ(std::string::npos, HeaderValue(bad, "Set-Cookie").find("__Host-session="));
  EXPECT_NE(std::string::npos, cookie.find("__Host-session="));
  EXPECT_NE(std::string::npos, cookie.find("Secure"));
  EXPECT_NE(std::string::npos, cookie.find("HttpOnly"));
  EXPECT_NE(std::string::npos, cookie.find("SameSite=Lax"));
  EXPECT_NE(std::string::npos, cookie.find("Path=/"));
}

TEST_F(Edge, Logout) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply logged;
  Reply refused;
  Reply cleared;
  std::string cookie;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  logged = Ask(Req("POST", "/login", "site.example",
                   "Origin: http://site.example\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\n",
                   "username=ada&password=secret"),
      20000);
  cookie = HeaderValue(logged, "Set-Cookie");
  refused = Ask(Req("POST", "/logout", "site.example",
                    (std::string("Cookie: ") + cookie + "\r\n").c_str(), ""),
      5000);
  EXPECT_EQ(403, refused.status);
  EXPECT_EQ(std::string::npos, HeaderValue(refused, "Set-Cookie").find("Max-Age=0"));
  refused = Ask(Req("POST", "/logout", "site.example",
                    (std::string("Origin: http://evil.example\r\nCookie: ") +
                        cookie + "\r\n")
                        .c_str(),
                    ""),
      5000);
  cleared = Ask(Req("POST", "/logout", "site.example",
                    (std::string("Origin: http://site.example\r\nCookie: ") +
                        cookie + "\r\n")
                        .c_str(),
                    ""),
      5000);
  EXPECT_EQ(std::string::npos, HeaderValue(refused, "Set-Cookie").find("Max-Age=0"));
  EXPECT_EQ(403, refused.status);
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("__Host-session="));
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("Max-Age=0"));
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("Secure"));
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("HttpOnly"));
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("SameSite=Lax"));
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("Path=/"));
}

TEST_F(Edge, Throttle) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply ninth;
  Reply later;
  Reply locked;
  Reply cleared;
  Reply shared;
  Reply other_addr;
  int i;
  auto post = [&](const char * user, const char * pass, const char * xff) {
    std::string extra =
        "Origin: http://site.example\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n";
    std::string body = std::string("username=") + user + "&password=" + pass;
    if (xff != NULL) {
      extra += std::string("X-Forwarded-For: ") + xff + "\r\n";
    }
    return Ask(Req("POST", "/login", "site.example", extra.c_str(), body.c_str()),
        30000);
  };
  Listen("127.0.0.1");
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Keyring * keyring = NULL;
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    ASSERT_NE(keyring, nullptr);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_create(catalog, keyring, root_, sizeof root_, "bea", 3,
            "secret", 6, principal, account));
    gdefcore_catalog_destroy(catalog);
  }
  for (i = 0; i < 8; i++) {
    Reply fail = post("ada", "wrong", NULL);
    EXPECT_EQ(401, fail.status);
    EXPECT_EQ(std::string::npos, HeaderValue(fail, "Set-Cookie").find("__Host-session="));
  }
  ninth = post("ada", "secret", NULL);
  EXPECT_EQ(429, ninth.status);
  EXPECT_EQ(std::string::npos, HeaderValue(ninth, "Set-Cookie").find("__Host-session="));
  usleep(1100 * 1000);
  later = post("ada", "wrong", NULL);
  EXPECT_EQ(401, later.status);
  locked = post("ada", "wrong", NULL);
  EXPECT_EQ(429, locked.status);
  for (i = 0; i < 7; i++) {
    Reply fail = post("bea", "wrong", "203.0.113.10");
    EXPECT_EQ(401, fail.status);
  }
  cleared = post("bea", "secret", "203.0.113.10");
  EXPECT_NE(std::string::npos, HeaderValue(cleared, "Set-Cookie").find("__Host-session="));
  for (i = 0; i < 8; i++) {
    Reply fail = post("bea", "wrong", "203.0.113.20");
    EXPECT_EQ(401, fail.status) << i;
  }
  for (i = 0; i < 8; i++) {
    Reply fail = post("cara", "wrong", "198.51.100.7");
    EXPECT_EQ(401, fail.status);
  }
  shared = post("dora", "wrong", "198.51.100.7");
  EXPECT_EQ(429, shared.status);
  other_addr = post("dora", "wrong", "198.51.100.8");
  EXPECT_EQ(401, other_addr.status);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST_F(Edge, AdminFirstScreen) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  Reply home = Get("/admin", cookie);
  EXPECT_EQ(200, home.status);
  EXPECT_TRUE(Has(home.body, "href=\"/admin/post\""));
  EXPECT_TRUE(Has(home.body, "href=\"/admin/front\""));
  EXPECT_TRUE(Has(home.body, "href=\"/admin/undo\""));
  EXPECT_TRUE(Has(home.body, "href=\"/admin/more\""));
  EXPECT_FALSE(Has(home.body, "<form"));
  EXPECT_FALSE(Has(home.body, "<input"));
  ExpectSecurity(home);
  for (const char * path : {"/admin/post", "/admin/front", "/admin/undo", "/admin/more"}) {
    Reply page = Get(path, cookie);
    EXPECT_EQ(200, page.status) << path;
    EXPECT_TRUE(Has(page.body, "href=\"/admin\"")) << path;
    EXPECT_FALSE(Has(page.body, "<style")) << path;
    EXPECT_FALSE(Has(page.body, " style=")) << path;
    EXPECT_FALSE(Has(page.body, "<script>")) << path;
    EXPECT_TRUE(Has(page.body, "href=\"/defiant.css\"")) << path;
    /* Run in <head>, the script would look for #panes before it exists. */
    EXPECT_TRUE(Has(page.body, "<script defer src=\"/defiant.js\"")) << path;
    ExpectSecurity(page);
  }
  Reply more = Get("/admin/more", cookie);
  EXPECT_TRUE(Has(more.body, "Server"));
  EXPECT_FALSE(Has(more.body, "enant"));
  EXPECT_FALSE(Has(more.body, "<form"));
  Reply post = Get("/admin/post", cookie);
  EXPECT_TRUE(Has(post.body, "<form"));
  EXPECT_FALSE(Has(post.body, "id=\"panes\""));
  Reply front = Get("/admin/front", cookie);
  EXPECT_TRUE(Has(front.body, "side view recent"));
  EXPECT_FALSE(Has(front.body, "name=\"title\""));
  Reply undo = Get("/admin/undo", cookie);
  EXPECT_FALSE(Has(undo.body, "rase"));
}

TEST_F(Edge, AdminHidden) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  AddUser(id, "bob", "hunter2");
  Reply admin = Login("ada", "secret");
  Reply bob = Login("bob", "hunter2");
  std::string lax_only = "Cookie: " + CookiePair(admin, "__Host-session") + "\r\n";
  std::string bob_cookie = CookieLine(bob);
  /* Bob's session token offered as the Strict cookie as well. */
  std::string bob_forged = "Cookie: " + CookiePair(bob, "__Host-session") + "; __Host-admin=" +
      CookiePair(bob, "__Host-session").substr(strlen("__Host-session=")) + "\r\n";
  std::string bad_cookie = "Cookie: __Host-admin=not-a-token\r\n";
  EXPECT_FALSE(CookiePair(admin, "__Host-admin").empty());
  EXPECT_TRUE(CookiePair(bob, "__Host-admin").empty());
  const char * paths[] = {"/admin", "/admin/post", "/admin/front", "/admin/undo",
      "/admin/more", "/admin/nothing"};
  std::string cookies[] = {std::string(), lax_only, bob_cookie, bob_forged, bad_cookie};
  for (const std::string & cookie : cookies) {
    Reply unknown = Get("/no-such-path", cookie);
    for (const char * path : paths) {
      Reply got = Get(path, cookie);
      EXPECT_EQ(unknown.status, got.status) << path << " " << cookie;
      EXPECT_EQ(unknown.body, got.body) << path << " " << cookie;
      EXPECT_FALSE(Has(got.body, "dmin")) << path;
    }
    Reply unknown_post = PostForm("/no-such-path", cookie, "x=1");
    Reply got = PostForm("/admin/post", cookie, "title=Sneak&body=Sneak&summary=x");
    EXPECT_EQ(unknown_post.status, got.status) << cookie;
    EXPECT_EQ(unknown_post.body, got.body) << cookie;
  }
  EXPECT_FALSE(Has(Visit("/"), "Sneak"));
}

TEST_F(Edge, AdminCookies) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  AddUser(id, "bob", "hunter2");
  Reply admin = Login("ada", "secret");
  Reply bob = Login("bob", "hunter2");
  std::string session = SetCookieLine(admin, "__Host-session");
  std::string strict = SetCookieLine(admin, "__Host-admin");
  EXPECT_TRUE(Has(session, "SameSite=Lax"));
  EXPECT_TRUE(Has(strict, "SameSite=Strict"));
  EXPECT_TRUE(Has(strict, "Secure"));
  EXPECT_TRUE(Has(strict, "HttpOnly"));
  EXPECT_TRUE(Has(strict, "Path=/"));
  EXPECT_FALSE(Has(strict, "Domain="));
  EXPECT_EQ(CookiePair(admin, "__Host-session").substr(strlen("__Host-session=")),
      CookiePair(admin, "__Host-admin").substr(strlen("__Host-admin=")));
  EXPECT_FALSE(SetCookieLine(bob, "__Host-session").empty());
  EXPECT_TRUE(SetCookieLine(bob, "__Host-admin").empty());
  Reply out = Ask(Req("POST", "/logout", "site.example",
                      (std::string("Origin: http://site.example\r\n") + CookieLine(admin))
                          .c_str(),
                      ""),
      5000);
  EXPECT_TRUE(Has(SetCookieLine(out, "__Host-session"), "Max-Age=0"));
  EXPECT_TRUE(Has(SetCookieLine(out, "__Host-admin"), "Max-Age=0"));
  EXPECT_TRUE(Has(SetCookieLine(out, "__Host-admin"), "SameSite=Strict"));
}

TEST_F(Edge, AdminAssets) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  Reply js = Get("/defiant.js", std::string());
  Reply css = Get("/defiant.css", std::string());
  EXPECT_EQ(200, js.status);
  EXPECT_EQ(200, css.status);
  EXPECT_TRUE(Has(HeaderValue(js, "Content-Type"), "javascript"));
  EXPECT_TRUE(Has(HeaderValue(css, "Content-Type"), "text/css"));
  EXPECT_TRUE(Has(css.body, "color-scheme: light dark"));
  /* An empty region still has room to drop a pane on. */
  EXPECT_TRUE(Has(css.body, "[data-region]"));
  EXPECT_TRUE(Has(css.body, "min-height"));
  EXPECT_TRUE(Has(js.body, "panes"));
  EXPECT_FALSE(js.body.empty());
  /* The CSP names the hash of exactly these bytes, and nothing else. */
  unsigned char dig[GSEC_SHA256_DIGEST_LEN];
  ASSERT_EQ(GSEC_OK, gsec_sha256(js.body.data(), js.body.size(), dig));
  static const char tab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string b64;
  size_t i = 0;
  for (; i + 3 <= sizeof dig; i += 3) {
    unsigned v = (dig[i] << 16) | (dig[i + 1] << 8) | dig[i + 2];
    b64 += tab[(v >> 18) & 63];
    b64 += tab[(v >> 12) & 63];
    b64 += tab[(v >> 6) & 63];
    b64 += tab[v & 63];
  }
  unsigned v = (dig[i] << 16) | (dig[i + 1] << 8);
  b64 += tab[(v >> 18) & 63];
  b64 += tab[(v >> 12) & 63];
  b64 += tab[(v >> 6) & 63];
  b64 += '=';
  std::string csp = HeaderValue(js, "Content-Security-Policy");
  EXPECT_TRUE(Has(csp, ("script-src 'sha256-" + b64 + "';").c_str())) << csp;
  EXPECT_TRUE(Has(csp, "style-src 'self'"));
  EXPECT_TRUE(Has(csp, "frame-ancestors 'none'"));
  EXPECT_FALSE(Has(csp, "unsafe"));
  EXPECT_EQ(csp, HeaderValue(Get("/", std::string()), "Content-Security-Policy"));
  /* An unknown host is the fixed 404, the same as for any path. */
  Reply fixed = Ask(Req("GET", "/no-such", "unknown.example", NULL, NULL), 5000);
  Reply other_js = Ask(Req("GET", "/defiant.js", "unknown.example", NULL, NULL), 5000);
  Reply other_css = Ask(Req("GET", "/defiant.css", "unknown.example", NULL, NULL), 5000);
  EXPECT_EQ(404, other_js.status);
  EXPECT_EQ(404, other_css.status);
  EXPECT_EQ(fixed.body, other_js.body);
  EXPECT_EQ(fixed.body, other_css.body);
}

TEST_F(Edge, AdminWritePost) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  Reply form = Get("/admin/post", cookie);
  std::string token = InputValue(form.body, "token", 0);
  ASSERT_FALSE(token.empty());
  std::string fields = "&title=Fresh+Words&summary=A+summary&body=The+body";
  /* A foreign Origin, a damaged token, and another tenant's session write nothing. */
  Reply foreign = PostForm("/admin/post", cookie, Form(token, fields.c_str()),
      "http://evil.example");
  EXPECT_EQ(403, foreign.status);
  std::string damaged = token;
  damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
  PostForm("/admin/post", cookie, Form(damaged, fields.c_str()));
  EXPECT_FALSE(Has(Visit("/"), "Fresh Words"));
  /* The real submit publishes it. */
  Reply done = PostForm("/admin/post", cookie, Form(token, fields.c_str()));
  EXPECT_EQ(200, done.status);
  EXPECT_TRUE(Has(done.body, "Saved"));
  std::string front = Visit("/");
  EXPECT_TRUE(Has(front, "Fresh Words"));
  EXPECT_TRUE(Has(front, "Hello Title"));
  /* A replay of the same token writes nothing more. */
  Reply again = PostForm("/admin/post", cookie, Form(token, "&title=Replayed&summary=s&body=b"));
  EXPECT_FALSE(Has(again.body, "Saved"));
  EXPECT_FALSE(Has(Visit("/"), "Replayed"));
  /* A missing required field writes nothing. */
  Reply fresh = Get("/admin/post", cookie);
  std::string token2 = InputValue(fresh.body, "token", 0);
  Reply missing = PostForm("/admin/post", cookie, Form(token2, "&summary=only"));
  EXPECT_FALSE(Has(missing.body, "Saved"));
  /* The Lax cookie alone cannot post. */
  std::string lax_only = "Cookie: " + CookiePair(Login("ada", "secret"), "__Host-session") + "\r\n";
  Reply lax = PostForm("/admin/post", lax_only, Form(token2, "&title=Lax+Post&summary=s&body=b"));
  EXPECT_FALSE(Has(lax.body, "Saved"));
  EXPECT_FALSE(Has(Visit("/"), "Lax Post"));
}

TEST_F(Edge, AdminFront) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  Reply page = Get("/admin/front", cookie);
  auto in_region = [&](const std::string & html, const char * region, const char * word) {
    size_t at = html.find(std::string("class=\"") + region + "\"");
    if (at == std::string::npos) {
      return false;
    }
    size_t end = html.find("</div>", at);
    std::string part = html.substr(at, end == std::string::npos ? end : end - at);
    return Has(part, word);
  };
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  /* Remove, with no script. */
  std::string remove = FormToken(page.body, "Remove recent posts");
  ASSERT_FALSE(remove.empty());
  PostForm("/admin/front", cookie, Form(remove + "x", ""));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  Reply foreign = PostForm("/admin/front", cookie, Form(remove, ""), "http://evil.example");
  EXPECT_EQ(403, foreign.status);
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  Reply removed = PostForm("/admin/front", cookie, Form(remove, ""));
  EXPECT_EQ(200, removed.status);
  EXPECT_FALSE(Has(Visit("/"), "Hello Title"));
  EXPECT_FALSE(Has(removed.body, "data-pane"));
  /* The old token is stale now: replaying it changes nothing. */
  PostForm("/admin/front", cookie, Form(remove, ""));
  EXPECT_FALSE(Has(Visit("/"), "Hello Title"));
  /* Place, with no script. */
  std::string place = FormToken(removed.body, "Place recent posts");
  ASSERT_FALSE(place.empty());
  Reply placed = PostForm("/admin/front", cookie, Form(place, ""));
  EXPECT_TRUE(Has(placed.body, "side view recent"));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  /* Placing twice does not double it. */
  std::string place2 = FormToken(placed.body, "Place recent posts");
  Reply twice = PostForm("/admin/front", cookie, Form(place2, ""));
  size_t first = twice.body.find("data-pane");
  ASSERT_NE(std::string::npos, first);
  EXPECT_EQ(std::string::npos, twice.body.find("data-pane", first + 1));
  /* Drag: the script posts the whole pane list as the signed form. */
  std::string order = InputValue(twice.body, "token", 2);
  ASSERT_FALSE(order.empty());
  PostForm("/admin/front", cookie,
      Form(order, "&panes=main+view+recent%0Aside+view+other"));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  PostForm("/admin/front", cookie,
      Form(order, "&panes=main+entity+recent"));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  PostForm("/admin/front", cookie, Form(order, "&panes="));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  Reply dragged = PostForm("/admin/front", cookie,
      Form(order, "&panes=main+view+recent"));
  EXPECT_EQ(200, dragged.status);
  EXPECT_TRUE(in_region(Visit("/"), "main", "Hello Title"));
  EXPECT_FALSE(in_region(Visit("/"), "side", "Hello Title"));
  /* The Lax cookie alone drags nothing. */
  std::string lax_only = "Cookie: " + CookiePair(Login("ada", "secret"), "__Host-session") + "\r\n";
  std::string order2 = InputValue(dragged.body, "token", 2);
  PostForm("/admin/front", lax_only, Form(order2, "&panes=side+view+recent"));
  EXPECT_TRUE(in_region(Visit("/"), "main", "Hello Title"));
}

TEST_F(Edge, AdminUndo) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Catalog * catalog = NULL;
  Site(id);
  AddUser(id, "bob", "hunter2");
  std::string admin = AdminPrincipal(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  ASSERT_EQ(GDEFCORE_OK, gdefcore_id_generate(other));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));

  /* A post changed twice, the second time by someone else. */
  char ** posts = NULL;
  size_t npost = 0;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_ids(catalog, "site.post", 9, &posts, &npost));
  ASSERT_EQ(1u, npost);
  {
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Value value;
    const char * slot;
    size_t len;
    OneValue(&value, "title", "Hello Again", &slot, &len);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, other, strlen(other), &cs));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_save(cs, posts[0], GDEFCORE_PUBLIC_ID_LEN, "en", 2, &value, 1));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, NULL, 0));
    gdefcore_changeset_destroy(cs);
  }
  gdefcore_ids_free(catalog, posts, npost);
  EXPECT_TRUE(Has(Visit("/"), "Hello Again"));
  Reply page = Get("/admin/undo", cookie);
  std::string post_lines = UndoLines(page.body, "Post Hello Again");
  EXPECT_TRUE(Has(post_lines, admin.c_str())) << post_lines;
  EXPECT_TRUE(Has(post_lines, other)) << post_lines;
  Reply undone = PostForm("/admin/undo", cookie, Form(UndoToken(page.body, "Post Hello Again"), ""));
  EXPECT_EQ(200, undone.status);
  std::string front = Visit("/");
  EXPECT_TRUE(Has(front, "Hello Title"));
  EXPECT_FALSE(Has(front, "Hello Again"));
  /* The restore is itself a revision, and this admin is its actor. */
  std::string after = UndoLines(undone.body, "Post Hello Title");
  size_t admins = 0;
  for (size_t at = after.find(admin); at != std::string::npos; at = after.find(admin, at + 1)) {
    admins += 1;
  }
  EXPECT_EQ(2u, admins) << after;

  /* The front page layout: remove the pane, then undo the removal. */
  Reply fp = Get("/admin/front", cookie);
  PostForm("/admin/front", cookie, Form(FormToken(fp.body, "Remove recent posts"), ""));
  EXPECT_FALSE(Has(Visit("/"), "Hello Title"));
  page = Get("/admin/undo", cookie);
  EXPECT_TRUE(Has(UndoLines(page.body, "Front page"), admin.c_str()));
  undone = PostForm("/admin/undo", cookie, Form(UndoToken(page.body, "Front page"), ""));
  EXPECT_EQ(200, undone.status);
  EXPECT_TRUE(Has(Visit("/"), "Hello Title"));
  EXPECT_TRUE(Has(Get("/admin/front", cookie).body, "side view recent"));

  /* A role change. */
  {
    GDEFCORE_Permission permission;
    char ** grants = NULL;
    size_t count = 0;
    bool seen = false;
    memset(&permission, 0, sizeof permission);
    permission.size = sizeof permission;
    permission.label = "Read extras";
    permission.label_len = 11;
    permission.module_name = "site";
    permission.module_name_len = 4;
    permission.action = "read";
    permission.action_len = 4;
    permission.condition = GDEFCORE_ACCESS_ANY;
    permission.type_name = "site.post";
    permission.type_name_len = 9;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog, &permission));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "authenticated", 13, "Read extras", 11,
            GDEFCORE_ACCESS_CONFIRM_AUTHENTICATED,
            strlen(GDEFCORE_ACCESS_CONFIRM_AUTHENTICATED)));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_role_grants(catalog, "authenticated", 13, &grants, &count));
    for (size_t i = 0; i < count; i++) {
      seen = seen || strcmp(grants[i], "Read extras") == 0;
    }
    gdefcore_access_free(catalog, grants, count);
    EXPECT_TRUE(seen);
    page = Get("/admin/undo", cookie);
    ASSERT_TRUE(Has(page.body, "Role authenticated"));
    undone = PostForm("/admin/undo", cookie,
        Form(UndoToken(page.body, "Role authenticated"), ""));
    EXPECT_EQ(200, undone.status);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_role_grants(catalog, "authenticated", 13, &grants, &count));
    seen = false;
    for (size_t i = 0; i < count; i++) {
      seen = seen || strcmp(grants[i], "Read extras") == 0;
    }
    gdefcore_access_free(catalog, grants, count);
    EXPECT_FALSE(seen);
  }
  gdefcore_catalog_destroy(catalog);

  /* A bad token, a foreign Origin and the Lax cookie alone undo nothing. */
  page = Get("/admin/undo", cookie);
  std::string token = UndoToken(page.body, "Post Hello Title");
  ASSERT_FALSE(token.empty());
  PostForm("/admin/undo", cookie, Form(token + "x", ""));
  EXPECT_EQ(403, PostForm("/admin/undo", cookie, Form(token, ""), "http://evil.example").status);
  std::string lax_only = "Cookie: " + CookiePair(Login("ada", "secret"), "__Host-session") + "\r\n";
  PostForm("/admin/undo", lax_only, Form(token, ""));
  EXPECT_TRUE(Has(Visit("/"), "Hello Title"));
  /* Erasure is not offered. */
  EXPECT_FALSE(Has(page.body, "rase"));
}

TEST_F(Edge, AdminRedirects) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  /* A write answers with a redirect, so a refresh does not post it again. */
  std::string token = InputValue(Get("/admin/post", cookie).body, "token", 0);
  Reply saved = PostRaw("/admin/post", cookie,
      Form(token, "&title=Once&summary=s&body=b"));
  EXPECT_EQ(303, saved.status);
  EXPECT_EQ("/admin/post/saved", HeaderValue(saved, "Location"));
  EXPECT_TRUE(saved.body.empty());
  Reply notice = Get("/admin/post/saved", cookie);
  EXPECT_TRUE(Has(notice.body, "Saved."));
  EXPECT_TRUE(Has(notice.body, "<form"));
  EXPECT_FALSE(Has(Get("/admin/post", cookie).body, "Saved."));
  Reply front = Get("/admin/front", cookie);
  Reply removed = PostRaw("/admin/front", cookie,
      Form(FormToken(front.body, "Remove recent posts"), ""));
  EXPECT_EQ(303, removed.status);
  EXPECT_EQ("/admin/front", HeaderValue(removed, "Location"));
  Reply refused = PostRaw("/admin/front", cookie, Form("not-a-token", ""));
  EXPECT_EQ(303, refused.status);
  Reply undo = Get("/admin/undo", cookie);
  Reply undone = PostRaw("/admin/undo", cookie,
      Form(UndoToken(undo.body, "Front page"), ""));
  EXPECT_EQ(303, undone.status);
  EXPECT_EQ("/admin/undo", HeaderValue(undone, "Location"));
  /* Hidden from a visitor, and the notice page is not a way around that. */
  Reply unknown = Get("/no-such-path", std::string());
  Reply hidden = Get("/admin/post/saved", std::string());
  EXPECT_EQ(unknown.status, hidden.status);
  EXPECT_EQ(unknown.body, hidden.body);
  EXPECT_EQ(404, PostRaw("/admin/post", std::string(), "x=1").status);
}

TEST_F(Edge, LoginPage) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  AddUser(id, "bob", "hunter2");
  /* The form is public, needs no script, and posts to /login. */
  Reply page = Get("/login", std::string());
  EXPECT_EQ(200, page.status);
  EXPECT_TRUE(Has(page.body, "<form method=\"post\" action=\"/login\">"));
  EXPECT_TRUE(Has(page.body, "name=\"username\""));
  EXPECT_TRUE(Has(page.body, "type=\"password\""));
  EXPECT_FALSE(Has(page.body, "<script"));
  EXPECT_FALSE(Has(page.body, "did not work"));
  ExpectSecurity(page);
  /* An admin goes to /admin, anyone else to /. */
  Reply admin = PostRaw("/login", std::string(), "username=ada&password=secret");
  Reply other = PostRaw("/login", std::string(), "username=bob&password=hunter2");
  EXPECT_EQ(303, admin.status);
  EXPECT_EQ("/admin", HeaderValue(admin, "Location"));
  EXPECT_FALSE(CookiePair(admin, "__Host-session").empty());
  EXPECT_EQ(303, other.status);
  EXPECT_EQ("/", HeaderValue(other, "Location"));
  EXPECT_TRUE(CookiePair(other, "__Host-admin").empty());
  /* A wrong password and an unknown user get the same page, and no cookie. */
  Reply wrong = PostRaw("/login", std::string(), "username=ada&password=nope");
  Reply unknown = PostRaw("/login", std::string(), "username=nobody&password=nope");
  EXPECT_EQ(401, wrong.status);
  EXPECT_EQ(wrong.status, unknown.status);
  EXPECT_EQ(wrong.body, unknown.body);
  EXPECT_TRUE(Has(wrong.body, "did not work"));
  EXPECT_TRUE(Has(wrong.body, "<form method=\"post\" action=\"/login\">"));
  EXPECT_TRUE(HeaderAll(wrong, "Set-Cookie").empty());
  EXPECT_FALSE(Has(wrong.body, "ada"));
  EXPECT_FALSE(Has(wrong.body, "nobody"));
  /* Signing in with the page's own form lands on the admin. */
  std::string cookie = CookieLine(admin);
  EXPECT_TRUE(Has(Get("/admin", cookie).body, "href=\"/admin/post\""));
}
