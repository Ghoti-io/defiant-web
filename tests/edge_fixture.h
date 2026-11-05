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
 * The server, the client and the fixture that the edge tests share: one
 * server on a loopback port, a fiber for the client, and tenants made
 * through the installation.
 */

#ifndef GHOTI_IO_GDEFWEB_TEST_EDGE_FIXTURE_H
#define GHOTI_IO_GDEFWEB_TEST_EDGE_FIXTURE_H

#include <ghoti.io/defiant-web/defiant-web.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/adminarea.h>
#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/module.h>
#include <ghoti.io/defiant-core/template.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-storage-memory/memory.h>
#include <ghoti.io/defiant-storage/model.h>
#include <ghoti.io/cutil/fiber.h>
#include <ghoti.io/security/sha256.h>

#include <gtest/gtest.h>

#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <string>
#include <vector>

namespace {

/* The C runtime on Windows has _putenv_s where POSIX has setenv and unsetenv. */
void SetEnv(const char * name, const char * value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

void UnsetEnv(const char * name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

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

/* The page without its sign-out form, which every signed-in admin page carries
 * and which is not what a test of the page's own forms is about. */
std::string WithoutSignout(std::string page) {
  size_t at = page.find("<form method=\"post\" action=\"/logout\">");
  if (at != std::string::npos) {
    size_t end = page.find("</form>", at);
    if (end != std::string::npos) {
      page.erase(at, end + 7 - at);
    }
  }
  return page;
}

/* The token of the sign-out form a signed-in page carries, or "". */
std::string LogoutToken(const std::string & page) {
  size_t at = page.find("action=\"/logout\"");
  if (at == std::string::npos) {
    return std::string();
  }
  return InputValue(page.substr(at), "token", 0);
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
  GDEFCORE_Host * host_ = NULL;
  GDEFWEB_Server * server_;
  std::string cookie_name_ = "__Host-session";
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
      SetEnv(name, saved.c_str());
    } else {
      UnsetEnv(name);
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
    UnsetEnv("DEFIANT_SECRET");
    UnsetEnv("DEFIANT_SECRET_FILE");
    UnsetEnv("DEFIANT_CONFIG");
    UnsetEnv("DEFIANT_TRUSTED_PROXIES");
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &registry_));
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &site_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_open(registry_, site_, root_, sizeof root_, "", 0, NULL,
            &inst_));
  }

  void TearDown() override {
    gdefweb_server_destroy(server_);
    gdefcore_host_destroy(host_);
    host_ = NULL;
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
    ListenWith(proxies, NULL);
  }

  /* The result of creating a server with @p options at @p listen, which is
   * destroyed again. Nothing about the fixture's own server changes. */
  GDEFWEB_Result TryCreate(const GDEFWEB_Server_Options * options, const char * listen) {
    GDEFCORE_Inst_Config * config = NULL;
    GDEFWEB_Server * made = NULL;
    GDEFWEB_Result rc;
    SetEnv("DEFIANT_LISTEN", listen);
    UnsetEnv("DEFIANT_TRUSTED_PROXIES");
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
    rc = gdefweb_server_create(config, options, NULL, &made);
    gdefcore_inst_config_destroy(config);
    if (rc == GDEFWEB_OK) {
      gdefweb_server_destroy(made);
    } else {
      EXPECT_EQ(nullptr, made);
    }
    return rc;
  }

  /* As Listen(), with the cookie policy @p options (NULL: the defaults). The
   * session cookie then has the name that policy gives. */
  void ListenWith(const char * proxies, const GDEFWEB_Server_Options * options) {
    GDEFCORE_Inst_Config * config = NULL;
    SetEnv("DEFIANT_LISTEN", "127.0.0.1:0");
    if (proxies == NULL) {
      UnsetEnv("DEFIANT_TRUSTED_PROXIES");
    } else {
      SetEnv("DEFIANT_TRUSTED_PROXIES", proxies);
    }
    cookie_name_ = options != NULL &&
            options->transport == GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK
        ? "session"
        : "__Host-session";
    ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_config_load(NULL, &config));
    ASSERT_EQ(GDEFWEB_OK, gdefweb_server_create(config, options, NULL, &server_));
    gdefcore_inst_config_destroy(config);
    ASSERT_EQ(GDEFWEB_OK,
        gdefweb_server_set_inst(server_, inst_, root_, sizeof root_));
    ASSERT_EQ(GDEFWEB_OK, gdefweb_server_listen(server_));
    ASSERT_NE(gdefweb_server_accepting(server_), 0);
  }

  /* The module host of the process, given to the server. After Listen. */
  void UseHost() {
    ASSERT_EQ(GDEFCORE_OK, gdefcore_host_open(site_, NULL, &host_));
    ASSERT_EQ(GDEFWEB_OK, gdefweb_server_set_host(server_, host_));
  }

  /*
   * Grant the admin role the permission to edit templates of each kind (the
   * permission is one, and the grant's condition names the kind), by
   * changeset, as an operator would. Nothing grants it before this.
   */
  void GrantTemplateKinds(const char * id, const std::vector<std::string> & kinds) {
    GDEFCORE_Catalog * catalog = NULL;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    for (const std::string & kind : kinds) {
      const std::string condition = "(= field.kind \"" + kind + "\")";
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant_when(catalog, "admin", 5,
              GDEFCORE_TEMPLATE_EDIT_LABEL, strlen(GDEFCORE_TEMPLATE_EDIT_LABEL),
              condition.data(), condition.size(), "core.role", 9));
    }
    gdefcore_catalog_destroy(catalog);
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

  /* The kit, and (unless @p pages is false) the administrator's grants of the
   * admin pages that need no permission a test is about: the area, the post,
   * the front page, undo, the roles' undo and the matrix. The permissions of the
   * templates, the menus and the settings stay granted to nobody, so the tests
   * of those pages keep their control. A program's first start grants all of
   * them (gdefcore_admin_seed()); a test of that passes false. */
  void InstallKit(const char * id, bool pages = true) {
    GDEFCORE_Catalog * catalog = NULL;
    uint8_t * bytes = NULL;
    size_t n = 0;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_kit(catalog, &bytes, &n));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_install(catalog, bytes, n));
    gdefcore_text_free(catalog, (char *)bytes);
    if (pages) {
      ASSERT_EQ(GDEFCORE_OK, gdefcore_admin_declare(catalog));
      ASSERT_EQ(GDEFCORE_OK, gdefcore_layout_declare(catalog));
      for (const char * label : {GDEFCORE_ADMIN_USE_LABEL, GDEFCORE_ADMIN_UNDO_LABEL,
               GDEFCORE_POST_WRITE_LABEL, GDEFCORE_ROLE_REVERT_LABEL,
               GDEFCORE_ACCESS_READ_LABEL, GDEFCORE_LAYOUT_EDIT_DISPLAY_LABEL,
               GDEFCORE_LAYOUT_EDIT_RULE_LABEL}) {
        ASSERT_EQ(GDEFCORE_OK,
            gdefcore_access_grant(catalog, "admin", 5, label, strlen(label),
                "core.role", 9));
      }
    }
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

  /* The Cookie header line for a login: the one session cookie. */
  std::string CookieLine(const Reply & login) {
    return "Cookie: " + CookiePair(login, cookie_name_.c_str()) + "\r\n";
  }

  Reply Get(const char * path, const std::string & cookie) {
    return Ask(Req("GET", path, "site.example", cookie.c_str(), NULL), 20000);
  }

  /* The reply as sent, with no redirect followed. */
  Reply PostRaw(const char * path, const std::string & cookie, const std::string & body,
      const char * origin = "http://site.example") {
    return Ask(Req("POST", path, "site.example",
                   (std::string("Origin: ") + origin + "\r\n" +
                       "Content-Type: application/x-www-form-urlencoded\r\n" + cookie)
                       .c_str(),
                   body.c_str()),
        20000);
  }

  /* A browser follows a 303 with a GET of its Location. */
  Reply PostForm(const char * path, const std::string & cookie, const std::string & body,
      const char * origin = "http://site.example") {
    Reply sent = PostRaw(path, cookie, body, origin);
    if (sent.status != 303) {
      return sent;
    }
    std::string where = HeaderValue(sent, "Location");
    /* A browser keeps the fragment to itself and does not send it. */
    where = where.substr(0, where.find('#'));
    return Get(where.c_str(), cookie);
  }

  /* A tenant with the kit, one published post, and an admin called ada. */
  void Site(char * id) {
    Listen(NULL);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_tenant_create(inst_, "site.example", 12, "", 0, id));
    InstallKit(id);
    Publish(id, "hello-title", "Hello Title");
  }


  /* The bytes of the session token in a Cookie header line ("Cookie: name=value"),
   * as the server reads them (URL-safe base64, no padding). Empty when the value
   * is not that. */
  static std::string TokenOfCookie(const std::string & cookie_line) {
    static const char kTab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t eq = cookie_line.find('=');
    size_t end = cookie_line.find("\r\n");
    std::string value;
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    if (eq == std::string::npos) {
      return out;
    }
    value = cookie_line.substr(eq + 1, end == std::string::npos ? std::string::npos
                                                                : end - eq - 1);
    for (char c : value) {
      const char * at = strchr(kTab, c);
      if (c == '\0' || at == NULL) {
        return std::string();
      }
      acc = (acc << 6) | (uint32_t)(at - kTab);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        out.push_back((char)((acc >> bits) & 0xffu));
      }
    }
    return out;
  }

  /* The Cookie header line that carries @p token. */
  std::string CookieOfToken(const char * token, size_t n) {
    static const char kTab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string value;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
      acc = (acc << 8) | (uint32_t)(unsigned char)token[i];
      bits += 8;
      while (bits >= 6) {
        bits -= 6;
        value.push_back(kTab[(acc >> bits) & 63u]);
      }
    }
    if (bits > 0) {
      value.push_back(kTab[(acc << (6 - bits)) & 63u]);
    }
    return "Cookie: " + cookie_name_ + "=" + value + "\r\n";
  }

  /* What core says of the session a Cookie line carries: whether the check binds
   * a principal (the session is live). @p rc, if given, receives the check's
   * result. This asks core and not the server, so a cleared cookie cannot stand
   * in for an ended session. */
  bool SessionLive(const char * id, const std::string & cookie_line,
      GDEFCORE_Result * rc = NULL) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFCORE_Request * request = NULL;
    GDEFCORE_Keyring * keyring;
    uint8_t storage[16];
    std::string token = TokenOfCookie(cookie_line);
    size_t who_n = 0;
    GDEFCORE_Result got;
    bool live;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_storage_key(inst_, id, strlen(id), storage));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    keyring = gdefcore_inst_tenant_keyring(inst_, id, strlen(id));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_request_create(storage, sizeof storage, 0, 0, NULL, &request));
    got = gdefcore_account_check(catalog, request, keyring, root_, sizeof root_,
        token.data(), token.size());
    live = got == GDEFCORE_OK && gdefcore_request_principal(request, &who_n) != NULL;
    if (rc != NULL) {
      *rc = got;
    }
    gdefcore_request_destroy(request);
    gdefcore_catalog_destroy(catalog);
    return live;
  }

  /* How many sessions the tenant has revoked: the records of its revocation list.
   * A sign-out that changed nothing leaves it; one that ended a session adds one. */
  size_t Revoked(const char * id) {
    GDEFCORE_Catalog * catalog = NULL;
    GDEFSTOR_RecordList * list = NULL;
    static const char kNs[] = "core.session.revoked";
    const char * tenant;
    size_t tenant_n = 0;
    size_t n;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id, strlen(id), &catalog));
    tenant = (const char *)gdefcore_catalog_tenant(catalog, &tenant_n);
    EXPECT_EQ(GDEFSTOR_OK,
        gdefstor_record_list(site_, tenant, tenant_n, kNs, sizeof kNs - 1, "", 0, 0,
            GDEFSTOR_RECORD_MAX_PAGE, &list));
    n = gdefstor_record_list_count(list);
    gdefstor_record_list_free(list);
    gdefcore_catalog_destroy(catalog);
    return n;
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

#endif /* GHOTI_IO_GDEFWEB_TEST_EDGE_FIXTURE_H */
