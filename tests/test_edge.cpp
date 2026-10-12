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

#include <gtest/gtest.h>

#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

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
    gdefcore_request_destroy(request);
    gdefcore_catalog_destroy(catalog);
  }
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
  std::string cookie;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
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
  int i;
  Listen(NULL);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
  InstallKit(id);
  Publish(id, "hello-title", "Hello Title");
  for (i = 0; i < 8; i++) {
    Reply fail = Ask(Req("POST", "/login", "site.example",
                         "Origin: http://site.example\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n",
                         "username=ada&password=wrong"),
        30000);
    EXPECT_EQ(std::string::npos, HeaderValue(fail, "Set-Cookie").find("__Host-session="));
  }
  ninth = Ask(Req("POST", "/login", "site.example",
                  "Origin: http://site.example\r\n"
                  "Content-Type: application/x-www-form-urlencoded\r\n",
                  "username=ada&password=secret"),
      5000);
  EXPECT_EQ(429, ninth.status);
  EXPECT_EQ(std::string::npos, HeaderValue(ninth, "Set-Cookie").find("__Host-session="));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
