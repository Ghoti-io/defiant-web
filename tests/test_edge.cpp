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

#include <ghoti.io/defiant-web/defiant-web.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-storage-memory/memory.h>
#include <ghoti.io/cutil/fiber.h>
#include <ghoti.io/security/sha256.h>

#include <gtest/gtest.h>

#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

const uint64_t kLife = 3600;
const uint8_t kRegistry[] = {0x02};

struct Reply {
  int status;
  std::string headers;
  std::string body;
  std::string raw;
};

struct Exchange {
  GCU_Loop * loop;
  uint16_t port;
  std::string request;
  std::string response;
  int client_done;
  int fail;
};

void ClientFiber(void * arg) {
  Exchange * ex = static_cast<Exchange *>(arg);
  GCU_Socket * sock = NULL;
  GCU_Socket_Address addr;
  GCU_Loop_Op op;
  uint8_t ip[4] = {127, 0, 0, 1};
  char buf[4096];
  ex->fail = 0;
  ex->client_done = 0;
  if (gcu_socket_create(&sock, GCU_SOCKET_IPV4, GCU_SOCKET_STREAM, NULL) !=
      GCU_SOCKET_OK) {
    ex->fail = 1;
    ex->client_done = 1;
    return;
  }
  gcu_socket_address_ipv4(&addr, ip, ex->port);
  gcu_loop_op_init(&op, NULL, NULL);
  if (gcu_loop_connect(ex->loop, &op, sock, &addr) != GCU_LOOP_OK ||
      gcu_loop_fiber_wait(&op) != GCU_LOOP_OK ||
      op.result.status != GCU_LOOP_OK) {
    ex->fail = 1;
    ex->client_done = 1;
    gcu_socket_close(sock);
    return;
  }
  if (gcu_loop_write(ex->loop, &op, sock, ex->request.data(),
          ex->request.size()) != GCU_LOOP_OK ||
      gcu_loop_fiber_wait(&op) != GCU_LOOP_OK ||
      op.result.status != GCU_LOOP_OK) {
    ex->fail = 1;
    ex->client_done = 1;
    gcu_socket_close(sock);
    return;
  }
  for (;;) {
    if (gcu_loop_read(ex->loop, &op, sock, buf, sizeof buf) != GCU_LOOP_OK ||
        gcu_loop_fiber_wait(&op) != GCU_LOOP_OK ||
        op.result.status != GCU_LOOP_OK) {
      ex->fail = 1;
      break;
    }
    if (op.result.bytes == 0) {
      break;
    }
    ex->response.append(buf, op.result.bytes);
  }
  gcu_socket_close(sock);
  ex->client_done = 1;
}

Reply Parse(const std::string & raw) {
  Reply out;
  size_t blank;
  size_t line;
  out.status = 0;
  out.raw = raw;
  blank = raw.find("\r\n\r\n");
  if (raw.compare(0, 9, "HTTP/1.1 ") != 0 || blank == std::string::npos) {
    return out;
  }
  out.status = atoi(raw.c_str() + 9);
  out.headers = raw.substr(0, blank);
  out.body = raw.substr(blank + 4);
  line = out.headers.find("\r\n");
  if (line != std::string::npos) {
    out.headers = out.headers.substr(line + 2);
  }
  return out;
}

int B64Url(const std::string & in, unsigned char * out, size_t cap, size_t * out_n) {
  int val = 0;
  int bits = -8;
  size_t o = 0;
  for (unsigned char c : in) {
    int d;
    if (c == '=') {
      break;
    }
    if (c >= 'A' && c <= 'Z') {
      d = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      d = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      d = c - '0' + 52;
    } else if (c == '-') {
      d = 62;
    } else if (c == '_') {
      d = 63;
    } else {
      return -1;
    }
    val = (val << 6) + d;
    bits += 6;
    if (bits >= 0) {
      if (o >= cap) {
        return -1;
      }
      out[o++] = (unsigned char)((val >> bits) & 0xff);
      bits -= 8;
    }
  }
  *out_n = o;
  return 0;
}

bool HasHeader(const Reply & reply, const char * name, const char * piece) {
  std::string needle = std::string(name) + ":";
  size_t at = 0;
  while (at < reply.headers.size()) {
    size_t end = reply.headers.find("\r\n", at);
    std::string line = reply.headers.substr(at,
        end == std::string::npos ? std::string::npos : end - at);
    if (line.size() >= needle.size() &&
        strncasecmp(line.c_str(), needle.c_str(), needle.size()) == 0) {
      if (piece == NULL || line.find(piece) != std::string::npos) {
        return true;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    at = end + 2;
  }
  return false;
}

std::string HeaderValue(const Reply & reply, const char * name) {
  std::string needle = std::string(name) + ":";
  size_t at = 0;
  while (at < reply.headers.size()) {
    size_t end = reply.headers.find("\r\n", at);
    std::string line = reply.headers.substr(at,
        end == std::string::npos ? std::string::npos : end - at);
    if (line.size() >= needle.size() &&
        strncasecmp(line.c_str(), needle.c_str(), needle.size()) == 0) {
      size_t v = needle.size();
      while (v < line.size() && line[v] == ' ') {
        v += 1;
      }
      return line.substr(v);
    }
    if (end == std::string::npos) {
      break;
    }
    at = end + 2;
  }
  return std::string();
}


std::vector<std::string> HeaderAll(const Reply & reply, const char * name) {
  std::string needle = std::string(name) + ":";
  std::vector<std::string> out;
  size_t at = 0;
  while (at < reply.headers.size()) {
    size_t end = reply.headers.find("\r\n", at);
    std::string line = reply.headers.substr(at,
        end == std::string::npos ? std::string::npos : end - at);
    if (line.size() >= needle.size() &&
        strncasecmp(line.c_str(), needle.c_str(), needle.size()) == 0) {
      size_t v = needle.size();
      while (v < line.size() && line[v] == ' ') {
        v += 1;
      }
      out.push_back(line.substr(v));
    }
    if (end == std::string::npos) {
      break;
    }
    at = end + 2;
  }
  return out;
}

/* The name=value pair of the Set-Cookie line that sets @p name, or "". */
std::string CookiePair(const Reply & reply, const char * name) {
  std::string prefix = std::string(name) + "=";
  for (const std::string & line : HeaderAll(reply, "Set-Cookie")) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      return line.substr(0, line.find(';'));
    }
  }
  return std::string();
}

std::string SetCookieLine(const Reply & reply, const char * name) {
  std::string prefix = std::string(name) + "=";
  for (const std::string & line : HeaderAll(reply, "Set-Cookie")) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      return line;
    }
  }
  return std::string();
}

/* The value of the @p nth hidden input called @p name on a page. */
std::string InputValue(const std::string & page, const char * name, size_t nth) {
  std::string needle = std::string("name=\"") + name + "\" value=\"";
  size_t at = 0;
  size_t seen = 0;
  while ((at = page.find(needle, at)) != std::string::npos) {
    size_t from = at + needle.size();
    if (seen == nth) {
      return page.substr(from, page.find('"', from) - from);
    }
    seen += 1;
    at = from;
  }
  return std::string();
}

/* The token in the form whose action is @p action and whose button says @p label. */
std::string FormToken(const std::string & page, const char * label) {
  size_t at = page.find(label);
  size_t form;
  if (at == std::string::npos) {
    return std::string();
  }
  form = page.rfind("<form", at);
  if (form == std::string::npos) {
    return std::string();
  }
  return InputValue(page.substr(form, at - form), "token", 0);
}

/* The token of the undo form that carries @p label. */
std::string UndoToken(const std::string & page, const char * label) {
  size_t at = page.find(label);
  if (at == std::string::npos) {
    return std::string();
  }
  return InputValue(page.substr(at), "token", 0);
}

/* The history lines of the undo form that carries @p label. */
std::string UndoLines(const std::string & page, const char * label) {
  size_t at = page.find(label);
  size_t end;
  if (at == std::string::npos) {
    return std::string();
  }
  end = page.find("</form>", at);
  return page.substr(at, end == std::string::npos ? end : end - at);
}

void OneValue(GDEFCORE_Value * value, const char * field, const char * text,
    const char ** slot, size_t * len_slot) {
  memset(value, 0, sizeof *value);
  *slot = text;
  *len_slot = strlen(text);
  value->size = sizeof *value;
  value->field = field;
  value->field_len = strlen(field);
  value->texts = slot;
  value->text_lens = len_slot;
  value->count = 1;
}

class Edge : public ::testing::Test {
 protected:
  GDEFSTOR_Store * registry_;
  GDEFSTOR_Store * site_;
  GDEFCORE_Inst * inst_;
  GDEFWEB_Server * server_;
  uint8_t root_[GDEFCORE_ACCOUNT_ROOT_LEN];
  std::string saved_listen_;
  std::string saved_proxies_;
  std::string saved_secret_;
  std::string saved_secret_file_;
  std::string saved_config_;
  int had_listen_;
  int had_proxies_;
  int had_secret_;
  int had_secret_file_;
  int had_config_;

  void Remember(const char * name, std::string * saved, int * had) {
    const char * cur = getenv(name);
    *had = cur != NULL;
    if (cur != NULL) {
      *saved = cur;
    }
  }

  void Restore(const char * name, const std::string & saved, int had) {
    if (had) {
      setenv(name, saved.c_str(), 1);
    } else {
      unsetenv(name);
    }
  }

  void SetUp() override {
    registry_ = NULL;
    site_ = NULL;
    inst_ = NULL;
    server_ = NULL;
    memset(root_, 0x5a, sizeof root_);
    Remember("DEFIANT_LISTEN", &saved_listen_, &had_listen_);
    Remember("DEFIANT_TRUSTED_PROXIES", &saved_proxies_, &had_proxies_);
    Remember("DEFIANT_SECRET", &saved_secret_, &had_secret_);
    Remember("DEFIANT_SECRET_FILE", &saved_secret_file_, &had_secret_file_);
    Remember("DEFIANT_CONFIG", &saved_config_, &had_config_);
    unsetenv("DEFIANT_SECRET");
    unsetenv("DEFIANT_SECRET_FILE");
    unsetenv("DEFIANT_CONFIG");
    unsetenv("DEFIANT_TRUSTED_PROXIES");
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &registry_));
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &site_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_open(registry_, site_, root_, sizeof root_, "", 0, NULL,
            &inst_));
  }

  void TearDown() override {
    gdefweb_server_destroy(server_);
    gdefcore_inst_destroy(inst_);
    gdefstor_store_destroy(site_);
    gdefstor_store_destroy(registry_);
    Restore("DEFIANT_LISTEN", saved_listen_, had_listen_);
    Restore("DEFIANT_TRUSTED_PROXIES", saved_proxies_, had_proxies_);
    Restore("DEFIANT_SECRET", saved_secret_, had_secret_);
    Restore("DEFIANT_SECRET_FILE", saved_secret_file_, had_secret_file_);
    Restore("DEFIANT_CONFIG", saved_config_, had_config_);
  }

  void Listen(const char * proxies) {
    GDEFCORE_Inst_Config * config = NULL;
    setenv("DEFIANT_LISTEN", "127.0.0.1:0", 1);
    if (proxies == NULL) {
      unsetenv("DEFIANT_TRUSTED_PROXIES");
    } else {
      setenv("DEFIANT_TRUSTED_PROXIES", proxies, 1);
    }
    ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
    ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, NULL, &server_));
    gdefcore_inst_config_destroy(config);
    ASSERT_EQ(GDEFWEB_OK,
        gdefweb_server_set_inst(server_, inst_, root_, sizeof root_));
    ASSERT_EQ(GDEFWEB_OK, gdefweb_server_listen(server_));
    ASSERT_NE(gdefweb_server_accepting(server_), 0);
  }

  bool Pump(Exchange * ex, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!gdefweb_server_done(server_) || !ex->client_done) {
      if (std::chrono::steady_clock::now() > end) {
        return false;
      }
      if (gcu_loop_run_once(gdefweb_server_loop(server_), 20) != GCU_LOOP_OK) {
        return false;
      }
    }
    return true;
  }

  Reply Ask(const std::string & request, int ms) {
    Exchange ex;
    GCU_Socket_Address local;
    GCU_Fiber * fiber = NULL;
    Reply reply;
    ex.loop = gdefweb_server_loop(server_);
    ex.port = 0;
    ex.request = request;
    ex.client_done = 0;
    ex.fail = 0;
    EXPECT_EQ(GDEFWEB_OK, gdefweb_server_local(server_, &local));
    ex.port = local.port;
    EXPECT_EQ(GDEFWEB_OK, gdefweb_server_accept_one(server_));
    EXPECT_EQ(GCU_FIBER_OK,
        gcu_fiber_create(&fiber, ClientFiber, &ex, GCU_FIBER_DEFAULT_STACK_SIZE,
            NULL));
    EXPECT_EQ(GCU_FIBER_OK, gcu_fiber_switch_to(fiber));
    EXPECT_TRUE(Pump(&ex, ms));
    EXPECT_EQ(0, ex.fail);
    if (gcu_fiber_is_finished(fiber)) {
      gcu_fiber_destroy(fiber);
    }
    reply = Parse(ex.response);
    return reply;
  }

  static std::string Req(const char * method, const char * path, const char * host,
      const char * extra, const char * body) {
    std::string out = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " +
        host + "\r\n";
    if (extra != NULL && extra[0] != '\0') {
      out += extra;
      if (out.size() < 2 || out.substr(out.size() - 2) != "\r\n") {
        out += "\r\n";
      }
    }
    if (body != NULL) {
      out += "Content-Length: " + std::to_string(strlen(body)) + "\r\n";
    }
    out += "\r\n";
    if (body != NULL) {
      out += body;
    }
    return out;
  }

  void InstallKit(const char * id) {
    GDEFCORE_Catalog * catalog = NULL;
    uint8_t * bytes = NULL;
    size_t n = 0;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_kit(catalog, &bytes, &n));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_install(catalog, bytes, n));
    gdefcore_text_free(catalog, (char *)bytes);
    gdefcore_catalog_destroy(catalog);
  }

  void Publish(const char * id, const char * alias, const char * title) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Request * request = NULL;
    GDEFCORE_Keyring * keyring = NULL;
    uint8_t storage[16];
    char admin[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
    char * token = NULL;
    char * form = NULL;
    size_t token_n = 0;
    size_t form_n = 0;
    GDEFCORE_Value values[2];
    const char * slots[2];
    size_t lens[2];
    int conflict = 1;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_storage_key(inst_, id, strlen(id), storage));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    ASSERT_NE(keyring, nullptr);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_request_create(storage, sizeof storage, 0, 0, NULL, &request));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_admin_principal(catalog, admin));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, admin,
            strlen(admin), "ada", 3, "secret", 6, account));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_login(catalog, keyring, root_, sizeof root_, "ada", 3,
            "secret", 6, kLife, &token, &token_n));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_check(catalog, request, keyring, root_, sizeof root_,
            token, token_n));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_create(catalog, "site.post", 9, entity, NULL));
    OneValue(&values[0], "title", title, &slots[0], &lens[0]);
    OneValue(&values[1], "body", "Body", &slots[1], &lens[1]);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_form_sign(catalog, request, keyring, root_, sizeof root_, token,
            token_n, "edit", 4, entity, strlen(entity), "en", 2, values, 2, alias,
            strlen(alias), &form, &form_n));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_form_submit(catalog, request, keyring, root_, sizeof root_,
            token, token_n, form, form_n, &conflict));
    EXPECT_EQ(0, conflict);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_form_publish(catalog, entity, strlen(entity)));
    gdefcore_text_free(catalog, form);
    gdefcore_text_free(catalog, token);
    gdefcore_request_destroy(request);
    gdefcore_catalog_destroy(catalog);
  }


  Reply Login(const char * user, const char * pass) {
    std::string body = std::string("username=") + user + "&password=" + pass;
    return Ask(Req("POST", "/login", "site.example",
                   "Origin: http://site.example\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\n",
                   body.c_str()),
        20000);
  }

  /* The Cookie header line for a login: both cookies when both were set. */
  std::string CookieLine(const Reply & login) {
    std::string line = "Cookie: " + CookiePair(login, "__Host-session");
    std::string admin = CookiePair(login, "__Host-admin");
    if (!admin.empty()) {
      line += "; " + admin;
    }
    return line + "\r\n";
  }

  Reply Get(const char * path, const std::string & cookie) {
    return Ask(Req("GET", path, "site.example", cookie.c_str(), NULL), 20000);
  }

  Reply PostForm(const char * path, const std::string & cookie, const std::string & body,
      const char * origin = "http://site.example") {
    return Ask(Req("POST", path, "site.example",
                   (std::string("Origin: ") + origin + "\r\n" +
                       "Content-Type: application/x-www-form-urlencoded\r\n" + cookie)
                       .c_str(),
                   body.c_str()),
        20000);
  }

  /* A tenant with the kit, one published post, and an admin called ada. */
  void Site(char * id) {
    Listen(NULL);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
    InstallKit(id);
    Publish(id, "hello-title", "Hello Title");
  }


  /* A second account that holds no role. */
  void AddUser(const char * id, const char * name, const char * pass) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Keyring * keyring;
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    ASSERT_NE(keyring, nullptr);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal,
            strlen(principal), name, strlen(name), pass, strlen(pass), account));
    gdefcore_catalog_destroy(catalog);
  }

  std::string AdminPrincipal(const char * id) {
    GDEFCORE_Catalog * catalog = NULL;
    char admin[GDEFCORE_PUBLIC_ID_LEN + 1];
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_admin_principal(catalog, admin));
    gdefcore_catalog_destroy(catalog);
    return std::string(admin);
  }

  /* What a visitor with no cookie is shown. */
  std::string Visit(const char * path) {
    return Get(path, std::string()).body;
  }

  static bool Has(const std::string & hay, const char * needle) {
    return hay.find(needle) != std::string::npos;
  }

  static std::string Form(const std::string & token, const char * rest) {
    return "token=" + token + rest;
  }

  void ExpectSecurity(const Reply & reply) {
    EXPECT_TRUE(HasHeader(reply, "Content-Security-Policy", "frame-ancestors 'none'"));
    EXPECT_TRUE(HasHeader(reply, "Content-Security-Policy", "script-src"));
    EXPECT_EQ(std::string::npos,
        HeaderValue(reply, "Content-Security-Policy").find("unsafe-inline"));
  }
};

}  // namespace

TEST_F(Edge, EmptyListenBindsNothing) {
  GDEFCORE_Inst_Config * config = NULL;
  setenv("DEFIANT_LISTEN", "", 1);
  unsetenv("DEFIANT_TRUSTED_PROXIES");
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
  int saved = dup(STDOUT_FILENO);
  int pipes[2];
  char log[1024];
  ssize_t n;
  Listen("203.0.113.5");
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "acme.example", 12, "", 0, acme));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "other.example", 13, "", 0, other));
  InstallKit(acme);
  InstallKit(other);
  Publish(acme, "acme-post", "Acme Title");
  Publish(other, "other-post", "Other Title");
  ASSERT_EQ(0, pipe(pipes));
  fflush(stdout);
  ASSERT_EQ(STDOUT_FILENO, dup2(pipes[1], STDOUT_FILENO));
  reply = Ask(Req("GET", "/?password=hunter2", "acme.example",
                  "X-Forwarded-Host: other.example\r\n"
                  "X-Forwarded-For: 203.0.113.9\r\n",
                  NULL),
      20000);
  fflush(stdout);
  ASSERT_EQ(STDOUT_FILENO, dup2(saved, STDOUT_FILENO));
  close(saved);
  close(pipes[1]);
  n = read(pipes[0], log, sizeof log - 1);
  close(pipes[0]);
  ASSERT_GE(n, 0);
  log[n] = '\0';
  EXPECT_EQ(200, reply.status);
  EXPECT_NE(std::string::npos, reply.body.find("Acme Title"));
  EXPECT_EQ(std::string::npos, reply.body.find("Other Title"));
  EXPECT_NE(std::string::npos, std::string(log).find("client=127.0.0.0"));
  EXPECT_EQ(std::string::npos, std::string(log).find("203.0.113.9"));
  EXPECT_EQ(std::string::npos, std::string(log).find("hunter2"));
  EXPECT_EQ(std::string::npos, std::string(log).find("?"));
  EXPECT_EQ(std::string::npos, std::string(log).find("password"));
}

TEST_F(Edge, TrustedForward) {
  char acme[GDEFCORE_PUBLIC_ID_LEN + 1];
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  Reply reply;
  int saved = dup(STDOUT_FILENO);
  int pipes[2];
  char log[1024];
  ssize_t n;
  Listen("127.0.0.1");
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "acme.example", 12, "", 0, acme));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "other.example", 13, "", 0, other));
  InstallKit(acme);
  InstallKit(other);
  Publish(acme, "acme-post", "Acme Title");
  Publish(other, "other-post", "Other Title");
  ASSERT_EQ(0, pipe(pipes));
  fflush(stdout);
  ASSERT_EQ(STDOUT_FILENO, dup2(pipes[1], STDOUT_FILENO));
  reply = Ask(Req("GET", "/", "acme.example",
                  "X-Forwarded-Host: other.example\r\n"
                  "X-Forwarded-For: 203.0.113.9\r\n",
                  NULL),
      20000);
  fflush(stdout);
  ASSERT_EQ(STDOUT_FILENO, dup2(saved, STDOUT_FILENO));
  close(saved);
  close(pipes[1]);
  n = read(pipes[0], log, sizeof log - 1);
  close(pipes[0]);
  ASSERT_GE(n, 0);
  log[n] = '\0';
  EXPECT_EQ(200, reply.status);
  EXPECT_NE(std::string::npos, reply.body.find("Other Title"));
  EXPECT_EQ(std::string::npos, reply.body.find("Acme Title"));
  EXPECT_NE(std::string::npos, std::string(log).find("client=203.0.0.0"));
  EXPECT_EQ(std::string::npos, std::string(log).find("203.0.113.9"));
}

TEST_F(Edge, ReadyBeforeOpen) {
  GDEFCORE_Inst_Config * config = NULL;
  Reply closed;
  Reply open;
  setenv("DEFIANT_LISTEN", "127.0.0.1:0", 1);
  unsetenv("DEFIANT_TRUSTED_PROXIES");
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
