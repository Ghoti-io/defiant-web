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
#include "portable.h"

#include <ghoti.io/defiant-storage/model.h>
#include <ghoti.io/defiant-storage-sqlite/model.h>

#include "size_structs.h"

#include <filesystem>
#include <system_error>
#include <thread>


TEST_F(Edge, EmptyListenBindsNothing) {
  GDEFCORE_Inst_Config * config = NULL;
  SetEnv("DEFIANT_LISTEN", "");
  UnsetEnv("DEFIANT_TRUSTED_PROXIES");
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, NULL, NULL, &server_));
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
  ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, NULL, NULL, &server_));
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
  EXPECT_NE(std::string::npos, cookie.find("SameSite=Strict"));
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
  Reply unknown;
  Reply inactive;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "quiet.example", 13, "", 0, quiet));
  gdefcore_inst_destroy(inst_);
  inst_ = NULL;
  /* Close the tenant behind the installation's back, as an operator's tool
   * would: its registry record (a system record since story S6b, one
   * `name hex` line per fact) gets the status `closed`. */
  {
    auto Hex = [](const std::string & t) {
      static const char d[] = "0123456789abcdef";
      std::string out;
      for (unsigned char c : t) {
        out += d[c >> 4];
        out += d[c & 15];
      }
      return out;
    };
    const char * tenant = reinterpret_cast<const char *>(kRegistry);
    GDEFSTOR_RecordList * list = NULL;
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_record_list(registry_, tenant, sizeof kRegistry, "core.tenant",
            11, "", 0, 0, GDEFSTOR_RECORD_MAX_PAGE, &list));
    std::string key;
    std::string body;
    for (size_t k = 0; k < gdefstor_record_list_count(list); k++) {
      const char * rk = NULL;
      size_t rkn = 0;
      const uint8_t * rb = NULL;
      size_t rbn = 0;
      ASSERT_EQ(GDEFSTOR_OK, gdefstor_record_list_item(list, k, &rk, &rkn, &rb, &rbn, NULL, NULL));
      std::string text((const char *)rb, rbn);
      if (text.find("\nresolution " + Hex("quiet.example") + "\n") !=
          std::string::npos) {
        key.assign(rk, rkn);
        body = text;
      }
    }
    gdefstor_record_list_free(list);
    ASSERT_FALSE(key.empty());
    size_t at = body.find("\nstatus ");
    ASSERT_NE(std::string::npos, at);
    size_t end = body.find('\n', at + 1);
    body.replace(at, end - at, "\nstatus " + Hex("closed"));
    GDEFSTOR_Write * write = NULL;
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_write_begin(registry_, tenant, sizeof kRegistry, NULL, &write));
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_record_put(write, "core.tenant", 11, key.data(), key.size(),
            "core/1", 6,
            (const uint8_t *)body.data(), body.size()));
    ASSERT_EQ(GDEFSTOR_OK, gdefstor_write_commit(write));
  }
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
  EXPECT_NE(std::string::npos, cookie.find("SameSite=Strict"));
  EXPECT_NE(std::string::npos, cookie.find("Path=/"));
}

/* The cookie policies the server can be given: the secure default, Lax, and the
 * plain-HTTP one for a loopback listener, each with the cookie it must set. */
namespace {

struct Policy {
  const char * label;
  bool use_options;
  GDEFWEB_Cookie_SameSite same_site;
  GDEFWEB_Cookie_Transport transport;
  const char * name;
  bool secure;
  const char * same_site_text;
};

const Policy kPolicies[] = {
    {"no options", false, GDEFWEB_COOKIE_SAME_SITE_STRICT,
        GDEFWEB_COOKIE_TRANSPORT_SECURE, "__Host-session", true, "Strict"},
    {"default options", true, GDEFWEB_COOKIE_SAME_SITE_STRICT,
        GDEFWEB_COOKIE_TRANSPORT_SECURE, "__Host-session", true, "Strict"},
    {"lax", true, GDEFWEB_COOKIE_SAME_SITE_LAX, GDEFWEB_COOKIE_TRANSPORT_SECURE,
        "__Host-session", true, "Lax"},
    {"plain loopback", true, GDEFWEB_COOKIE_SAME_SITE_STRICT,
        GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK, "session", false, "Strict"},
    {"plain loopback and lax", true, GDEFWEB_COOKIE_SAME_SITE_LAX,
        GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK, "session", false, "Lax"},
};

/* The attributes after the value, as the policy must give them. */
std::string Attributes(const Policy & p) {
  return std::string(p.secure ? "; Secure" : "") + "; HttpOnly; SameSite=" +
      p.same_site_text + "; Path=/";
}

}  // namespace

TEST_F(Edge, CookiePolicyOptionsAreHeldToTheirSize) {
  GDEFWEB_Server_Options o;
  gdefweb_server_options_default(&o);
  EXPECT_EQ(sizeof o, o.size);
  EXPECT_EQ(GDEFWEB_COOKIE_SAME_SITE_STRICT, o.same_site);
  EXPECT_EQ(GDEFWEB_COOKIE_TRANSPORT_SECURE, o.transport);
  /* A value outside either enumeration is not a policy. */
  o.same_site = (GDEFWEB_Cookie_SameSite)7;
  EXPECT_EQ(GDEFWEB_ERR_INVALID, TryCreate(&o, "127.0.0.1:0"));
  gdefweb_server_options_default(&o);
  o.transport = (GDEFWEB_Cookie_Transport)9;
  EXPECT_EQ(GDEFWEB_ERR_INVALID, TryCreate(&o, "127.0.0.1:0"));
  size_structs::Run("tests/test_edge.cpp",
      {{"GDEFWEB_Server_Options", [&](size_t size) -> bool {
         GDEFWEB_Server_Options probe;
         gdefweb_server_options_default(&probe);
         probe.size = size;
         return TryCreate(&probe, "127.0.0.1:0") == GDEFWEB_OK;
       }}});
}

TEST_F(Edge, PlainHttpCookieIsRefusedOffLoopback) {
  GDEFWEB_Server_Options plain;
  gdefweb_server_options_default(&plain);
  plain.transport = GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK;
  /* Everything that is not a loopback address, the neighbours of one, and no
   * listener at all (nothing says where it would be reached from). */
  for (const char * listen : {"0.0.0.0:0", "192.0.2.10:8080", "10.0.0.1:80",
           "128.0.0.1:80", "126.255.255.255:80", "[::]:0", "[2001:db8::1]:80",
           "[::2]:80", "[::ffff:127.0.0.1]:80", "[::1:0:0:0:1]:80", ""}) {
    EXPECT_EQ(GDEFWEB_ERR_INSECURE, TryCreate(&plain, listen)) << "LOOPBACK: " << listen;
  }
  plain.same_site = GDEFWEB_COOKIE_SAME_SITE_LAX;
  EXPECT_EQ(GDEFWEB_ERR_INSECURE, TryCreate(&plain, "0.0.0.0:0")) << "LOOPBACK: lax";
  /* The loopback addresses are accepted. */
  for (const char * listen : {"127.0.0.1:0", "127.255.255.254:80", "[::1]:0"}) {
    EXPECT_EQ(GDEFWEB_OK, TryCreate(&plain, listen)) << "LOOPBACK: " << listen;
  }
  /* The secure policy has no such condition: a public listener is what it is for. */
  EXPECT_EQ(GDEFWEB_OK, TryCreate(NULL, "0.0.0.0:0"));
  gdefweb_server_options_default(&plain);
  EXPECT_EQ(GDEFWEB_OK, TryCreate(&plain, "192.0.2.10:8080"));
  EXPECT_EQ(GDEFWEB_OK, TryCreate(&plain, ""));
  EXPECT_NE(std::string(gdefweb_result_string(GDEFWEB_ERR_INSECURE)),
      std::string(gdefweb_result_string(GDEFWEB_ERR_INVALID)));
}

TEST_F(Edge, CookiePolicyShapesTheCookieAndItsClearing) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  bool first = true;
  for (const Policy & p : kPolicies) {
    SCOPED_TRACE(p.label);
    GDEFWEB_Server_Options options;
    gdefweb_server_options_default(&options);
    options.same_site = p.same_site;
    options.transport = p.transport;
    if (!first) {
      gdefweb_server_destroy(server_);
      server_ = NULL;
    }
    ListenWith(NULL, p.use_options ? &options : NULL);
    if (first) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
      InstallKit(id);
      Publish(id, "hello-title", "Hello Title");
      first = false;
    }
    Reply login = Login("ada", "secret");
    ASSERT_EQ(303, login.status);
    ASSERT_EQ((size_t)1, HeaderAll(login, "Set-Cookie").size());
    std::string line = HeaderValue(login, "Set-Cookie");
    std::string pair = CookiePair(login, p.name);
    ASSERT_FALSE(pair.empty()) << "COOKIE: no session cookie in: " << line;
    /* Exactly the documented cookie, and no other attribute. */
    EXPECT_EQ(pair + Attributes(p), line) << "COOKIE: " << p.label;
    EXPECT_EQ(p.secure, Has(line, "Secure")) << "COOKIE: " << p.label;
    EXPECT_EQ(p.secure, line.compare(0, 7, "__Host-") == 0) << "COOKIE: " << p.label;
    std::string cookie = "Cookie: " + pair + "\r\n";
    std::string page = Get("/admin", cookie).body;
    ASSERT_TRUE(Has(page, "Sign out")) << "COOKIE: the cookie does not sign in";
    std::string token = LogoutToken(page);
    ASSERT_FALSE(token.empty()) << "LOGOUT: the page carries no sign-out token";
    /* The session is the cookie of the one name the policy gives: the other
     * policy's name, with the same value, is nothing. */
    std::string other = p.secure ? "session" : "__Host-session";
    std::string stranger = "Cookie: " + other + "=" + pair.substr(pair.find('=') + 1) + "\r\n";
    EXPECT_FALSE(Has(Get("/admin", stranger).body, "Sign out"))
        << "COOKIE: a cookie of the other name signed in";
    /* Signing out clears the cookie with the attributes it was set with. */
    Reply out = PostRaw("/logout", cookie, "token=" + token);
    EXPECT_EQ(303, out.status);
    EXPECT_EQ("/login", HeaderValue(out, "Location"));
    ASSERT_EQ((size_t)1, HeaderAll(out, "Set-Cookie").size());
    EXPECT_EQ(std::string(p.name) + "=" + Attributes(p) + "; Max-Age=0",
        HeaderValue(out, "Set-Cookie"))
        << "COOKIE: the clearing cookie differs from the one set";
    /* The same attributes, in the same order, that the cookie was set with. */
    std::string clearing = HeaderValue(out, "Set-Cookie");
    EXPECT_EQ(line.substr(line.find(';')) + "; Max-Age=0",
        clearing.substr(clearing.find(';')))
        << "COOKIE: the clearing cookie differs from the one set";
  }
}

TEST_F(Edge, LogoutNeedsTheSignedTokenOfTheSessionsOwnPage) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  AddUser(id, "bob", "hunter2");
  Reply ada = Login("ada", "secret");
  Reply bob = Login("bob", "hunter2");
  std::string cookie = CookieLine(ada);
  std::string page = Get("/admin", cookie).body;
  std::string token = LogoutToken(page);
  ASSERT_FALSE(token.empty()) << "LOGOUT: the page that offers signing out carries no token";
  EXPECT_TRUE(Has(page, "<form method=\"post\" action=\"/logout\">"));
  /* Another form's token, signed for this very session. */
  std::string other_form = FormToken(Get("/admin/front", cookie).body, "Remove recent posts");
  ASSERT_FALSE(other_form.empty());
  std::string damaged = token;
  damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
  std::string truncated = token.substr(0, token.size() - 3);
  struct Refused {
    const char * label;
    std::string cookie;
    std::string body;
    const char * origin;
  };
  const Refused cases[] = {
      {"no token", cookie, "", "http://site.example"},
      {"an empty token", cookie, "token=", "http://site.example"},
      {"a token of nothing", cookie, "token=AAAA", "http://site.example"},
      {"a damaged token", cookie, "token=" + damaged, "http://site.example"},
      {"a truncated token", cookie, "token=" + truncated, "http://site.example"},
      {"another form's token", cookie, "token=" + other_form, "http://site.example"},
      {"the token under another name", cookie, "csrf=" + token, "http://site.example"},
      {"a token signed for a different sign-in", CookieLine(bob), "token=" + token, "http://site.example"},
      {"a foreign Origin with a good token", cookie, "token=" + token, "http://evil.example"},
  };
  for (const Refused & c : cases) {
    SCOPED_TRACE(c.label);
    Reply r = PostRaw("/logout", c.cookie, c.body, c.origin);
    EXPECT_EQ(403, r.status) << "LOGOUT: " << c.label;
    EXPECT_TRUE(HeaderAll(r, "Set-Cookie").empty()) << "LOGOUT: " << c.label;
    /* Nothing was ended: the same cookie still signs in, and the token still works.
     * Core is asked too: a session the server only seemed to keep is not alive. */
    EXPECT_TRUE(Has(Get("/admin", cookie).body, "Sign out")) << "LOGOUT: " << c.label;
    EXPECT_TRUE(SessionLive(id, cookie)) << "LOGOUT: " << c.label;
    EXPECT_TRUE(SessionLive(id, CookieLine(bob))) << "LOGOUT: " << c.label;
  }
  /* No Origin header at all is refused as a foreign one is. */
  Reply bare = Ask(Req("POST", "/logout", "site.example",
                       (std::string("Content-Type: application/x-www-form-urlencoded\r\n") +
                           cookie).c_str(),
                       ("token=" + token).c_str()),
      20000);
  EXPECT_EQ(403, bare.status) << "LOGOUT: no Origin";
  EXPECT_TRUE(HeaderAll(bare, "Set-Cookie").empty());
  EXPECT_TRUE(Has(Get("/admin", cookie).body, "Sign out"));
  /* A GET does not sign anyone out. */
  Reply get = Get("/logout", cookie);
  EXPECT_TRUE(HeaderAll(get, "Set-Cookie").empty());
  /* The token the page gave does. */
  Reply out = PostRaw("/logout", cookie, "token=" + token);
  EXPECT_EQ(303, out.status);
  EXPECT_TRUE(Has(HeaderValue(out, "Set-Cookie"), "Max-Age=0"));
  EXPECT_FALSE(SessionLive(id, cookie)) << "SIGNOUT: the token signed out and the session lives";
}

/* Signing out ends the session on the server, not only in the browser: core
 * refuses the token afterwards, wherever it was copied, and no other sign-in of
 * the person or anyone else is touched. */
TEST_F(Edge, SigningOutEndsThisSessionAndNoOther) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  AddUser(id, "bob", "hunter2");
  std::string first = CookieLine(Login("ada", "secret"));
  std::string second = CookieLine(Login("ada", "secret"));
  std::string other = CookieLine(Login("bob", "hunter2"));
  std::string anonymous_admin = Get("/admin", std::string()).body;
  std::string anonymous_front = Get("/hello-title", std::string()).body;
  std::string token = LogoutToken(Get("/admin", first).body);
  ASSERT_FALSE(token.empty());
  ASSERT_TRUE(SessionLive(id, first));
  ASSERT_TRUE(SessionLive(id, second));
  ASSERT_TRUE(SessionLive(id, other));
  size_t before = Revoked(id);
  Reply out = PostRaw("/logout", first, "token=" + token);
  EXPECT_EQ(303, out.status);
  EXPECT_EQ("/login", HeaderValue(out, "Location"));
  ASSERT_EQ((size_t)1, HeaderAll(out, "Set-Cookie").size());
  EXPECT_TRUE(Has(HeaderValue(out, "Set-Cookie"), "Max-Age=0"));
  /* The revocation is in core: the token, as it was copied, is refused there. */
  GDEFCORE_Result rc = GDEFCORE_ERR_INTERNAL;
  EXPECT_FALSE(SessionLive(id, first, &rc)) << "SIGNOUT: core still lets the session in";
  EXPECT_EQ(GDEFCORE_OK, rc) << "SIGNOUT: a revoked session is anonymous, not an error";
  EXPECT_EQ(before + 1, Revoked(id)) << "SIGNOUT: the revocation is not on core's list";
  /* The old cookie, replayed, gets the signed-out answer on an admin page and on
   * a public one. */
  Reply admin = Get("/admin", first);
  EXPECT_FALSE(Has(admin.body, "Sign out")) << "SIGNOUT: the old cookie still signs in";
  EXPECT_EQ(Get("/admin", std::string()).status, admin.status);
  EXPECT_EQ(anonymous_admin, admin.body);
  EXPECT_EQ(anonymous_front, Get("/hello-title", first).body);
  EXPECT_FALSE(Has(Get("/admin/post", first).body, "Sign out"));
  /* The token the old session's page carried signs nothing else out. */
  EXPECT_EQ(403, PostRaw("/logout", second, "token=" + token).status);
  /* No other sign-in was ended: the same person's other sign-in, and another
   * person's. */
  EXPECT_TRUE(SessionLive(id, second)) << "SIGNOUT: the person's other sign-in ended";
  EXPECT_TRUE(SessionLive(id, other)) << "SIGNOUT: another person's session ended";
  EXPECT_TRUE(Has(Get("/admin", second).body, "Sign out"));
  /* A second sign-out with the old cookie is the dead-cookie answer: the cookie
   * is cleared and nothing is written. */
  before = Revoked(id);
  Reply again = PostRaw("/logout", first, "token=" + token);
  EXPECT_EQ(303, again.status);
  EXPECT_EQ("/login", HeaderValue(again, "Location"));
  EXPECT_EQ(HeaderValue(out, "Set-Cookie"), HeaderValue(again, "Set-Cookie"));
  EXPECT_EQ(before, Revoked(id)) << "SIGNOUT: a second sign-out wrote";
  /* The other sign-in can end itself, and then both are gone. */
  Reply second_out = PostRaw("/logout", second,
      "token=" + LogoutToken(Get("/admin", second).body));
  EXPECT_EQ(303, second_out.status);
  EXPECT_FALSE(SessionLive(id, second));
  EXPECT_TRUE(SessionLive(id, other));
  /* A new sign-in after signing out is a new session. */
  std::string later = CookieLine(Login("ada", "secret"));
  EXPECT_TRUE(SessionLive(id, later));
  EXPECT_NE(first, later);
}

/* A cookie that names no live session cannot be forged against, so signing out
 * with it needs no token and changes nothing; but it must not leave a dead cookie
 * in the browser, so it is cleared all the same. */
TEST_F(Edge, SigningOutWithoutALiveSessionStillClearsTheCookie) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  Site(id);
  std::string live = CookieLine(Login("ada", "secret"));
  std::string kept = CookieLine(Login("ada", "secret"));
  std::string token = LogoutToken(Get("/admin", live).body);
  ASSERT_FALSE(token.empty());
  /* The answer to a signed-in sign-out is the one every dead cookie must match:
   * the clearing line with the attributes the cookie was set with. */
  Reply ended = PostRaw("/logout", live, "token=" + token);
  ASSERT_EQ(303, ended.status);
  std::string clearing = HeaderValue(ended, "Set-Cookie");
  ASSERT_TRUE(Has(clearing, "Max-Age=0"));
  /* An expired session: a real token that lived one second. */
  std::string expired;
  {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Keyring * keyring;
    char * made = NULL;
    size_t made_n = 0;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_login(catalog, keyring, root_, sizeof root_, "ada", 3,
            "secret", 6, 1, &made, &made_n));
    expired = CookieOfToken(made, made_n);
    gdefcore_text_free(catalog, made);
    gdefcore_catalog_destroy(catalog);
  }
  std::string damaged = kept;
  {
    size_t at = damaged.find('=') + 1 + 40;
    damaged[at] = damaged[at] == 'A' ? 'B' : 'A';
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  ASSERT_FALSE(SessionLive(id, expired)) << "the one-second session is still alive";
  struct Dead {
    const char * label;
    std::string cookie;
    std::string body;
  };
  const Dead cases[] = {
      {"an absent cookie", std::string(), ""},
      {"a revoked session", live, ""},
      {"a revoked session with its old token", live, "token=" + token},
      {"an expired session", expired, ""},
      {"a damaged session", damaged, ""},
      {"a cookie that is not base64", "Cookie: " + cookie_name_ + "=!!not*base64!!\r\n", ""},
      {"a cookie of nothing", "Cookie: " + cookie_name_ + "=\r\n", ""},
      {"a cookie that is too short", "Cookie: " + cookie_name_ + "=AAAA\r\n", ""},
      {"a session cookie of the other policy's name", "Cookie: session=" +
           kept.substr(kept.find('=') + 1), ""},
      {"no token with a bad body", std::string(), "token=garbage"},
  };
  for (const Dead & c : cases) {
    SCOPED_TRACE(c.label);
    size_t before = Revoked(id);
    Reply r = PostRaw("/logout", c.cookie, c.body);
    EXPECT_EQ(303, r.status) << "DEAD: " << c.label;
    EXPECT_EQ("/login", HeaderValue(r, "Location")) << "DEAD: " << c.label;
    ASSERT_EQ((size_t)1, HeaderAll(r, "Set-Cookie").size()) << "DEAD: " << c.label;
    EXPECT_EQ(clearing, HeaderValue(r, "Set-Cookie")) << "DEAD: " << c.label;
    EXPECT_EQ(before, Revoked(id)) << "DEAD: " << c.label << " wrote";
    /* Nothing of anyone else was touched. */
    EXPECT_TRUE(SessionLive(id, kept)) << "DEAD: " << c.label << " ended another sign-in";
  }
  /* The Origin still decides first: a foreign one, or none, is refused and sets
   * nothing, for a dead cookie as for a live one. */
  for (const Dead & c : cases) {
    SCOPED_TRACE(c.label);
    Reply foreign = PostRaw("/logout", c.cookie, c.body, "http://evil.example");
    EXPECT_EQ(403, foreign.status) << "ORIGIN: " << c.label;
    EXPECT_TRUE(HeaderAll(foreign, "Set-Cookie").empty()) << "ORIGIN: " << c.label;
    Reply bare = Ask(Req("POST", "/logout", "site.example",
                         (std::string("Content-Type: application/x-www-form-urlencoded\r\n") +
                             c.cookie).c_str(),
                         c.body.c_str()),
        20000);
    EXPECT_EQ(403, bare.status) << "ORIGIN: no Origin, " << c.label;
    EXPECT_TRUE(HeaderAll(bare, "Set-Cookie").empty()) << "ORIGIN: no Origin, " << c.label;
  }
  /* A foreign Origin with a live session and its good token: refused, and the
   * session is kept (the same case the refusal test has, asked of core). */
  Reply foreign_live = PostRaw("/logout", kept, "token=" + LogoutToken(Get("/admin", kept).body),
      "http://evil.example");
  EXPECT_EQ(403, foreign_live.status);
  EXPECT_TRUE(HeaderAll(foreign_live, "Set-Cookie").empty());
  EXPECT_TRUE(SessionLive(id, kept)) << "ORIGIN: a foreign request ended a session";
  /* A GET never signs anyone out, live or dead. */
  EXPECT_TRUE(HeaderAll(Get("/logout", live), "Set-Cookie").empty());
}

/* When the session cannot be ended, the person is told so and the cookie is not
 * cleared; and when the store cannot say whether the session is live, the cookie
 * is not taken for a dead one. Either would otherwise look like a sign-out. */
namespace {
struct Faults {
  bool fail_get = false;
  bool fail_put = false;
  GDEFSTOR_Result (*get)(void *, void *, const char *, size_t, const char *, size_t,
      const char *, size_t, const GDEFSTOR_ModelVisitor *) = NULL;
  GDEFSTOR_Result (*put)(void *, void *, const GDEFSTOR_ModelRecord *) = NULL;
};
Faults g_faults;

bool IsRevocation(const char * ns, size_t n) {
  static const char kPrefix[] = "core.session.revoked";
  return n >= sizeof kPrefix - 1 && memcmp(ns, kPrefix, sizeof kPrefix - 1) == 0;
}

GDEFSTOR_Result FaultGet(void * ctx, void * txn, const char * tenant, size_t tenant_len,
    const char * ns, size_t ns_len, const char * key, size_t key_len,
    const GDEFSTOR_ModelVisitor * visitor) {
  if (g_faults.fail_get && IsRevocation(ns, ns_len)) {
    return GDEFSTOR_ERR_IO;
  }
  return g_faults.get(ctx, txn, tenant, tenant_len, ns, ns_len, key, key_len, visitor);
}

GDEFSTOR_Result FaultPut(void * ctx, void * txn, const GDEFSTOR_ModelRecord * r) {
  if (g_faults.fail_put && IsRevocation(r->ns, r->ns_len)) {
    return GDEFSTOR_ERR_IO;
  }
  return g_faults.put(ctx, txn, r);
}
}  // namespace

TEST_F(Edge, SigningOutThatCannotEndTheSessionIsNotASignOut) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string tmpl_str = PortableTempDir() + "/gdefweb-signout-XXXXXX";
  char * tmpl = &tmpl_str[0];
  GDEFSSQLITE_ModelOptions options;
  GDEFSTOR_ModelBackend backend;
  ASSERT_NE(nullptr, mkdtemp(tmpl));
  std::string dir = tmpl;
  /* The tenant's site store is a SQLite one whose revocation list can be made
   * unreadable or unwritable; every other read and write is the real one. */
  gdefcore_inst_destroy(inst_);
  inst_ = NULL;
  gdefstor_store_destroy(site_);
  site_ = NULL;
  g_faults = Faults();
  gdefssqlite_model_options_default(&options);
  options.synchronous = GDEFSSQLITE_SYNC_OFF;
  ASSERT_EQ(GDEFSSQLITE_OK, gdefssqlite_model_backend(dir.c_str(), &options, NULL, &backend));
  g_faults.get = backend.record_get;
  g_faults.put = backend.record_put;
  backend.record_get = FaultGet;
  backend.record_put = FaultPut;
  ASSERT_EQ(GDEFSTOR_OK, gdefstor_store_open(&backend, NULL, NULL, &site_));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_open(registry_, site_, root_, sizeof root_, "", 0, NULL, &inst_));
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  std::string token = LogoutToken(Get("/admin", cookie).body);
  ASSERT_FALSE(token.empty());
  ASSERT_TRUE(SessionLive(id, cookie));
  struct Failing {
    const char * label;
    bool get;
    bool put;
  };
  const Failing cases[] = {
      {"the revocation cannot be written", false, true},
      {"the revocation list cannot be read", true, false},
  };
  for (const Failing & c : cases) {
    SCOPED_TRACE(c.label);
    g_faults.fail_get = c.get;
    g_faults.fail_put = c.put;
    Reply r = PostRaw("/logout", cookie, "token=" + token);
    EXPECT_EQ(500, r.status) << "FAILED: " << c.label;
    EXPECT_TRUE(HeaderAll(r, "Set-Cookie").empty())
        << "FAILED: " << c.label << ": the cookie was cleared though the session was not ended";
    EXPECT_TRUE(Has(r.body, "You are still signed in")) << "FAILED: " << c.label;
    EXPECT_TRUE(Has(r.body, "<!DOCTYPE html>"))
        << "FAILED: " << c.label << ": the failed sign-out answered with something other than the page";
    EXPECT_TRUE(Has(r.body, "href=\"/admin\""))
        << "FAILED: " << c.label << ": the page offers no way back";
    EXPECT_FALSE(Has(r.body, "<form")) << "FAILED: " << c.label << ": a retry form needs a token the store may not mint";
    EXPECT_TRUE(HasHeader(r, "Content-Type", "text/html")) << "FAILED: " << c.label;
    EXPECT_TRUE(HasHeader(r, "Cache-Control", "no-store")) << "FAILED: " << c.label;
    g_faults.fail_get = false;
    g_faults.fail_put = false;
    /* The session was not ended, and the person can try again. */
    EXPECT_TRUE(SessionLive(id, cookie)) << "FAILED: " << c.label;
  }
  /* A dead cookie while the list cannot be read is not told from a live one: the
   * store cannot say, so the cookie is kept and the person is told to try again. */
  g_faults.fail_get = true;
  Reply unknown = PostRaw("/logout", cookie, "");
  EXPECT_EQ(500, unknown.status) << "FAILED: an unreadable list was taken for a dead session";
  EXPECT_TRUE(HeaderAll(unknown, "Set-Cookie").empty());
  g_faults.fail_get = false;
  Reply out = PostRaw("/logout", cookie, "token=" + token);
  EXPECT_EQ(303, out.status) << "FAILED: the retry did not sign out";
  EXPECT_FALSE(SessionLive(id, cookie));
  g_faults = Faults();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
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
  EXPECT_FALSE(Has(WithoutSignout(home.body), "<form"));
  EXPECT_FALSE(Has(WithoutSignout(home.body), "<input"));
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
  EXPECT_TRUE(Has(more.body, "<h2>core</h2>"));
  EXPECT_FALSE(Has(more.body, "enant"));
  EXPECT_FALSE(Has(WithoutSignout(more.body), "<form"));
  Reply post = Get("/admin/post", cookie);
  EXPECT_TRUE(Has(post.body, "<form"));
  EXPECT_FALSE(Has(post.body, "id=\"panes\""));
  Reply front = Get("/admin/front", cookie);
  EXPECT_TRUE(Has(front.body, "side recent"));
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
  std::string bob_cookie = CookieLine(bob);
  std::string bad_cookie = "Cookie: __Host-session=not-a-token\r\n";
  const char * paths[] = {"/admin", "/admin/post", "/admin/front", "/admin/undo",
      "/admin/more", "/admin/nothing"};
  std::string cookies[] = {std::string(), bob_cookie, bad_cookie};
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
  EXPECT_TRUE(Has(session, "SameSite=Strict"));
  EXPECT_TRUE(Has(session, "Secure"));
  EXPECT_TRUE(Has(session, "HttpOnly"));
  EXPECT_TRUE(Has(session, "Path=/"));
  EXPECT_FALSE(Has(session, "Domain="));
  /* One cookie is the whole sign-in, for a person with an admin page and for one
   * without. */
  EXPECT_EQ((size_t)1, HeaderAll(admin, "Set-Cookie").size());
  EXPECT_EQ((size_t)1, HeaderAll(bob, "Set-Cookie").size());
  EXPECT_FALSE(SetCookieLine(bob, "__Host-session").empty());
  Reply out = PostRaw("/logout", CookieLine(admin),
      "token=" + LogoutToken(Get("/admin", CookieLine(admin)).body));
  EXPECT_TRUE(Has(SetCookieLine(out, "__Host-session"), "Max-Age=0"));
  EXPECT_EQ((size_t)1, HeaderAll(out, "Set-Cookie").size());
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
  /* A public page is sent the policy that lets it load the site's own files
   * (test_assets.cpp), and the admin's is not it. */
  EXPECT_NE(csp, HeaderValue(Get("/", std::string()), "Content-Security-Policy"));
  EXPECT_EQ(csp, HeaderValue(Get("/login", std::string()), "Content-Security-Policy"));
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
  /* Another sign-in's cookie cannot post this form's token. */
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
  PostForm("/admin/front", cookie, Form(Corrupt(remove), ""));
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
  EXPECT_TRUE(Has(placed.body, "side recent"));
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
      Form(order, "&panes=main+recent%0Aside+other"));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  PostForm("/admin/front", cookie,
      Form(order, "&panes=main+content"));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  PostForm("/admin/front", cookie, Form(order, "&panes="));
  EXPECT_TRUE(in_region(Visit("/"), "side", "Hello Title"));
  Reply dragged = PostForm("/admin/front", cookie,
      Form(order, "&panes=main+recent"));
  EXPECT_EQ(200, dragged.status);
  EXPECT_TRUE(in_region(Visit("/"), "main", "Hello Title"));
  EXPECT_FALSE(in_region(Visit("/"), "side", "Hello Title"));
  /* Another sign-in's cookie drags nothing with this form's token. */
  std::string lax_only = "Cookie: " + CookiePair(Login("ada", "secret"), "__Host-session") + "\r\n";
  std::string order2 = InputValue(dragged.body, "token", 2);
  PostForm("/admin/front", lax_only, Form(order2, "&panes=side+recent"));
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
  EXPECT_TRUE(Has(Get("/admin/front", cookie).body, "side recent"));

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

  /* A bad token, a foreign Origin and another sign-in's cookie undo nothing. */
  page = Get("/admin/undo", cookie);
  std::string token = UndoToken(page.body, "Post Hello Title");
  ASSERT_FALSE(token.empty());
  PostForm("/admin/undo", cookie, Form(Corrupt(token), ""));
  EXPECT_EQ(403, PostForm("/admin/undo", cookie, Form(token, ""), "http://evil.example").status);
  std::string lax_only = "Cookie: " + CookiePair(Login("ada", "secret"), "__Host-session") + "\r\n";
  PostForm("/admin/undo", lax_only, Form(token, ""));
  EXPECT_TRUE(Has(Visit("/"), "Hello Title"));
  /* Erasure is not offered. */
  EXPECT_FALSE(Has(page.body, "rase"));
}

/* The admin pages name a post by the revision it currently is, published or
 * not: a post that was saved and then unpublished has a title in the undo
 * list, and the public front page does not show it. A post that exists only
 * as a draft has no revision to undo (a draft is never one, story S6b), so the
 * undo list does not offer it. */
TEST_F(Edge, AdminNamesAnUnpublishedPostAndOffersNoUndoOfADraft) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  char draft[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Catalog * catalog = NULL;
  Site(id);
  std::string cookie = CookieLine(Login("ada", "secret"));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
  {
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Value values[2];
    const char * slots[2];
    size_t lens[2];
    OneValue(&values[0], "title", "Held Back", &slots[0], &lens[0]);
    OneValue(&values[1], "body", "not public", &slots[1], &lens[1]);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(catalog, "site.post", 9, post, NULL));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_save(cs, post, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, 2));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, NULL, 0));
    gdefcore_changeset_destroy(cs);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_unpublish(cs, post, GDEFCORE_PUBLIC_ID_LEN));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, NULL, 0));
    gdefcore_changeset_destroy(cs);
    OneValue(&values[0], "title", "Only A Draft", &slots[0], &lens[0]);
    OneValue(&values[1], "body", "never published", &slots[1], &lens[1]);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(catalog, "site.post", 9, draft, NULL));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_save_draft(cs, draft, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, 2));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, NULL, 0));
    gdefcore_changeset_destroy(cs);
  }
  gdefcore_catalog_destroy(catalog);
  std::string front = Visit("/");
  EXPECT_FALSE(Has(front, "Held Back"));
  EXPECT_FALSE(Has(front, "Only A Draft"));
  std::string undo = Get("/admin/undo", cookie).body;
  EXPECT_TRUE(Has(undo, "Post Held Back"));
  EXPECT_FALSE(Has(undo, "Only A Draft"));
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
  EXPECT_EQ("/admin/post", HeaderValue(saved, "Location"));
  EXPECT_TRUE(saved.body.empty());
  EXPECT_FALSE(Has(saved.body, "Saved.")) << "the message is not in the redirect";
  /* The page the redirect lands on says so once: a refresh does not. */
  Reply notice = Get("/admin/post", cookie);
  EXPECT_TRUE(Has(notice.body, "<div class=\"message status\">Saved.</div>")) << notice.body;
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
  /* Hidden from a visitor, and the old notice page is not a way around that. */
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

namespace {

#define EDGE_STR2(x) #x
#define EDGE_STR(x) EDGE_STR2(x)
const char kEdgeCoreToken[] = EDGE_STR(GHOTIIO_DEFCORE_NAME);

GDEFCORE_Result NothingToBringForward(GDEFCORE_Migration *, void *) {
  return GDEFCORE_OK;
}

/* The module `news` at version 2, with its step from 1 (core story S11). */
const GDEFCORE_Module_Manifest * NewsAtTwo(void) {
  static GDEFCORE_Module_Manifest m;
  static GDEFCORE_Module_Step step;
  memset(&m, 0, sizeof m);
  memset(&step, 0, sizeof step);
  step.size = sizeof step;
  step.from = 1;
  step.fn = NothingToBringForward;
  m.size = sizeof m;
  m.core_token = kEdgeCoreToken;
  m.core_token_len = strlen(kEdgeCoreToken);
  m.name = "news";
  m.name_len = 4;
  m.schema_version = 2;
  m.steps = &step;
  m.nsteps = 1;
  return &m;
}

}  // namespace

/* A tenant whose stored data waits for its forward migration (a module the
 * process runs at a version above the one the tenant's data is at) gets a
 * maintenance response and nothing else; once migrated it is served again
 * (AD-15). */
TEST_F(Edge, ATenantAwaitingItsMigrationAnswersMaintenance) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  Listen(NULL);
  UseHost();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, NewsAtTwo));
  Reply before = Get("/hello-title", std::string());
  ASSERT_EQ(200, before.status);
  /* The tenant holds news's data at version 1. */
  uint8_t key[16];
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_storage_key(inst_, id, strlen(id), key));
  GDEFSTOR_Write * w = NULL;
  ASSERT_EQ(GDEFSTOR_OK, gdefstor_write_begin(site_, (const char *)key,
      sizeof key, NULL, &w));
  ASSERT_EQ(GDEFSTOR_OK, gdefstor_record_put(w, GDEFCORE_SCHEMA_TYPE,
      strlen(GDEFCORE_SCHEMA_TYPE), "news", 4, GDEFCORE_SCHEMA_CORE_STAMP,
      strlen(GDEFCORE_SCHEMA_CORE_STAMP), (const uint8_t *)"1", 1));
  ASSERT_EQ(GDEFSTOR_OK, gdefstor_write_commit(w));
  Reply waiting = Get("/hello-title", std::string());
  EXPECT_EQ(503, waiting.status);
  EXPECT_TRUE(Has(waiting.body, "under maintenance"));
  EXPECT_FALSE(Has(waiting.body, "Hello Title"));
  ExpectSecurity(waiting);
  size_t moved = 0;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_schema_migrate(host_, site_, key,
      sizeof key, 1000, &moved, NULL, NULL));
  EXPECT_EQ(1u, moved);
  Reply after = Get("/hello-title", std::string());
  EXPECT_EQ(200, after.status);
  EXPECT_TRUE(Has(after.body, "Hello Title"));
}
