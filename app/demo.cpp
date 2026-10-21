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
 * The success-signal demo's helper. tools/demo.sh starts the real `defiant`
 * program and calls this one for each stage:
 *
 *   defiant-demo web1 PORT SCRATCH       write a post, drag the pane
 *   defiant-demo operator1 DATA SCRATCH  a person, a snapshot, a second tenant
 *   defiant-demo web2 PORT SCRATCH       a day later: undo the drag
 *   defiant-demo operator2 DATA SCRATCH  erase the person, restore the snapshot
 *   defiant-demo web3 PORT SCRATCH       a second tenant on the same process
 *
 * The web stages speak HTTP to the program, with the cutil loop as the
 * client. The operator stages open the data directory directly, so they run
 * while the program is stopped. The first failing step prints one line and
 * exits 1. GHOTI_DEMO_PLANT=ledger makes the restore trust the snapshot's ledger alone.
 */

#include <ghoti.io/cutil/fiber.h>
#include <ghoti.io/cutil/loop.h>
#include <ghoti.io/cutil/socket.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/defiant-core.h>
#include <ghoti.io/defiant-core/inst.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/subject.h>
#include <ghoti.io/defiant-storage-sqlite/model.h>
#include <ghoti.io/defiant-storage/defiant-storage.h>
#include <ghoti.io/defiant-storage/snapshot.h>

#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char kAdminUser[] = "ada";
const char kAdminPass[] = "demo-pass-1";
const char kPostTitle[] = "Demo Post";

int g_step = 0;

[[noreturn]] void Fail(const std::string & what) {
  printf("FAIL %d %s\n", g_step, what.c_str());
  fflush(stdout);
  exit(1);
}

void Ok(int step, const std::string & line) {
  g_step = step;
  printf("%d %s\n", step, line.c_str());
  fflush(stdout);
}

void Need(bool cond, const std::string & what) {
  if (!cond) {
    Fail(what);
  }
}

bool Has(const std::string & hay, const std::string & needle) {
  return hay.find(needle) != std::string::npos;
}

/* ---- the HTTP client: a fiber on the cutil loop ---- */

struct Exchange {
  GCU_Loop * loop;
  uint16_t port;
  std::string request;
  std::string response;
  int done;
  int fail;
};

void ClientFiber(void * arg) {
  Exchange * ex = static_cast<Exchange *>(arg);
  GCU_Socket * sock = NULL;
  GCU_Socket_Address addr;
  GCU_Loop_Op op;
  uint8_t ip[4] = {127, 0, 0, 1};
  char buf[4096];
  ex->fail = 1;
  ex->done = 0;
  if (gcu_socket_create(&sock, GCU_SOCKET_IPV4, GCU_SOCKET_STREAM, NULL) !=
      GCU_SOCKET_OK) {
    ex->done = 1;
    return;
  }
  gcu_socket_address_ipv4(&addr, ip, ex->port);
  gcu_loop_op_init(&op, NULL, NULL);
  if (gcu_loop_connect(ex->loop, &op, sock, &addr) != GCU_LOOP_OK ||
      gcu_loop_fiber_wait(&op) != GCU_LOOP_OK || op.result.status != GCU_LOOP_OK ||
      gcu_loop_write(ex->loop, &op, sock, ex->request.data(), ex->request.size()) !=
          GCU_LOOP_OK ||
      gcu_loop_fiber_wait(&op) != GCU_LOOP_OK || op.result.status != GCU_LOOP_OK) {
    gcu_socket_close(sock);
    ex->done = 1;
    return;
  }
  ex->fail = 0;
  for (;;) {
    if (gcu_loop_read(ex->loop, &op, sock, buf, sizeof buf) != GCU_LOOP_OK ||
        gcu_loop_fiber_wait(&op) != GCU_LOOP_OK || op.result.status != GCU_LOOP_OK) {
      ex->fail = 1;
      break;
    }
    if (op.result.bytes == 0) {
      break;
    }
    ex->response.append(buf, op.result.bytes);
  }
  gcu_socket_close(sock);
  ex->done = 1;
}

struct Reply {
  int status = 0;
  std::string headers;
  std::string body;
};

std::vector<std::string> HeaderAll(const Reply & r, const std::string & name) {
  std::vector<std::string> out;
  std::string needle = name + ":";
  size_t at = 0;
  while (at < r.headers.size()) {
    size_t end = r.headers.find("\r\n", at);
    std::string line = r.headers.substr(at, end == std::string::npos ? end : end - at);
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

std::string Header(const Reply & r, const std::string & name) {
  auto all = HeaderAll(r, name);
  return all.empty() ? std::string() : all[0];
}

class Client {
 public:
  Client(uint16_t port) : port_(port) {
    loop_ = NULL;
    if (gcu_loop_create(&loop_, NULL) != GCU_LOOP_OK) {
      Fail("the client could not create an event loop");
    }
  }
  ~Client() { gcu_loop_destroy(loop_); }

  Reply Send(const std::string & method, const std::string & host,
      const std::string & path, const std::string & cookie, const std::string & body) {
    Exchange ex;
    GCU_Fiber * fiber = NULL;
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
    if (method == "POST") {
      req += "Origin: http://" + host + "\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    if (!cookie.empty()) {
      req += "Cookie: " + cookie + "\r\n";
    }
    req += "\r\n" + body;
    ex.loop = loop_;
    ex.port = port_;
    ex.request = req;
    ex.done = 0;
    ex.fail = 1;
    if (gcu_fiber_create(&fiber, ClientFiber, &ex, GCU_FIBER_DEFAULT_STACK_SIZE, NULL) !=
            GCU_FIBER_OK ||
        gcu_fiber_switch_to(fiber) != GCU_FIBER_OK) {
      Fail("the client could not start a request");
    }
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!ex.done) {
      if (std::chrono::steady_clock::now() > end ||
          gcu_loop_run_once(loop_, 20) != GCU_LOOP_OK) {
        Fail("a request to " + path + " did not finish");
      }
    }
    if (gcu_fiber_is_finished(fiber)) {
      gcu_fiber_destroy(fiber);
    }
    Need(!ex.fail, "a request to " + path + " failed");
    Reply r;
    size_t blank = ex.response.find("\r\n\r\n");
    Need(ex.response.compare(0, 9, "HTTP/1.1 ") == 0 && blank != std::string::npos,
        "the reply to " + path + " was not HTTP");
    r.status = atoi(ex.response.c_str() + 9);
    r.headers = ex.response.substr(0, blank);
    size_t line = r.headers.find("\r\n");
    if (line != std::string::npos) {
      r.headers = r.headers.substr(line + 2);
    }
    r.body = ex.response.substr(blank + 4);
    return r;
  }

  /* A browser follows a 303 with a GET. */
  Reply Post(const std::string & host, const std::string & path,
      const std::string & cookie, const std::string & body) {
    Reply r = Send("POST", host, path, cookie, body);
    if (r.status == 303) {
      return Send("GET", host, Header(r, "Location"), cookie, std::string());
    }
    return r;
  }

 private:
  uint16_t port_;
  GCU_Loop * loop_;
};

/* ---- reading a page the way the demo needs ---- */

std::string InputValue(const std::string & page, const std::string & name, size_t nth) {
  std::string needle = "name=\"" + name + "\" value=\"";
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

/* The token of the form whose button says @p label. */
std::string FormToken(const std::string & page, const std::string & label) {
  size_t at = page.find(label);
  if (at == std::string::npos) {
    return std::string();
  }
  size_t form = page.rfind("<form", at);
  return form == std::string::npos ? std::string()
                                   : InputValue(page.substr(form, at - form), "token", 0);
}

/* The token of the undo form that carries @p label. */
std::string UndoToken(const std::string & page, const std::string & label) {
  size_t at = page.find(label);
  return at == std::string::npos ? std::string()
                                 : InputValue(page.substr(at), "token", 0);
}

std::string Encode(const std::string & in) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : in) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string CookiePair(const Reply & r, const std::string & name) {
  for (const std::string & line : HeaderAll(r, "Set-Cookie")) {
    if (line.compare(0, name.size() + 1, name + "=") == 0) {
      return line.substr(0, line.find(';'));
    }
  }
  return std::string();
}

/* Where the post sits on the public page: "main", "side" or "". */
std::string PostRegion(const std::string & page) {
  size_t at = page.find(kPostTitle);
  if (at == std::string::npos) {
    return std::string();
  }
  size_t main_at = page.find("class=\"main\"");
  size_t side_at = page.find("class=\"side\"");
  if (main_at == std::string::npos || side_at == std::string::npos) {
    return std::string();
  }
  return (at > main_at && at < side_at) ? "main" : "side";
}

std::string ReadFile(const std::string & path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFile(const std::string & path, const std::string & bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), (std::streamsize)bytes.size());
  Need(out.good(), "could not write " + path);
}

std::string Login(Client & c, const std::string & host) {
  std::string body = std::string("username=") + kAdminUser + "&password=" + kAdminPass;
  Reply r = c.Send("POST", host, "/login", std::string(), body);
  Need(r.status == 303, "the admin could not sign in at " + host);
  std::string session = CookiePair(r, "__Host-session");
  std::string admin = CookiePair(r, "__Host-admin");
  Need(!session.empty() && !admin.empty(), "an admin login set no cookie pair");
  return session + "; " + admin;
}

/* ---- the web stages ---- */

int Web1(uint16_t port, const std::string & scratch) {
  Client c(port);
  std::string host = "127.0.0.1:" + std::to_string(port);
  Reply form_page = c.Send("GET", host, "/login", std::string(), std::string());
  Need(form_page.status == 200 && Has(form_page.body, "<form"), "no sign-in page");
  std::string cookie = Login(c, host);
  WriteFile(scratch + "/cookie", cookie);
  /* 1. write a post. */
  Reply post = c.Send("GET", host, "/admin/post", cookie, std::string());
  std::string token = InputValue(post.body, "token", 0);
  Need(!token.empty(), "the post page has no form");
  Reply saved = c.Post(host, "/admin/post", cookie,
      "token=" + token + "&title=" + Encode(kPostTitle) +
          "&summary=" + Encode("Written by the demo") + "&body=" + Encode("Hello."));
  Need(Has(saved.body, "Saved."), "the post was not saved");
  Reply visitor = c.Send("GET", host, "/", std::string(), std::string());
  Need(Has(visitor.body, kPostTitle), "a visitor does not see the post");
  Ok(1, std::string("write a post: a visitor sees \"") + kPostTitle + "\"");
  /* 2. drag the recent-posts pane to the other region. */
  Need(PostRegion(visitor.body) == "side", "the pane did not start in the side region");
  Reply front = c.Send("GET", host, "/admin/front", cookie, std::string());
  Need(Has(front.body, "id=\"panes\"") && Has(front.body, "defiant.js"),
      "the front page has no drag area or script");
  std::string order = InputValue(front.body, "token", 2);
  Need(!order.empty(), "the front page has no order form");
  c.Post(host, "/admin/front", cookie, "token=" + order + "&panes=main+view+recent");
  Reply moved = c.Send("GET", host, "/", std::string(), std::string());
  Need(PostRegion(moved.body) == "main", "a visitor does not see the new order");
  Ok(2, "drag a widget: recent posts moved from the side to the main region");
  return 0;
}

int Web2(uint16_t port, const std::string & scratch) {
  Client c(port);
  std::string host = "127.0.0.1:" + std::to_string(port);
  /* A day has passed: yesterday's session no longer opens the admin. */
  std::string old_cookie = ReadFile(scratch + "/cookie");
  Reply gone = c.Send("GET", host, "/admin/undo", old_cookie, std::string());
  Need(!Has(gone.body, "Undo") && gone.body.empty(),
      "yesterday's session still opens the admin a day later");
  std::string cookie = Login(c, host);
  Reply page = c.Send("GET", host, "/admin/undo", cookie, std::string());
  std::string token = UndoToken(page.body, "Front page");
  Need(!token.empty(), "the history has no front page entry");
  Need(PostRegion(c.Send("GET", host, "/", std::string(), std::string()).body) == "main",
      "yesterday's drag is not in force");
  c.Post(host, "/admin/undo", cookie, "token=" + token);
  Reply back = c.Send("GET", host, "/", std::string(), std::string());
  Need(PostRegion(back.body) == "side", "the drag was not undone");
  Need(Has(back.body, kPostTitle), "undoing the drag lost the post");
  Ok(3, "undo a change from the day before: the pane is back in the side region");
  return 0;
}

int Web3(uint16_t port, const std::string &) {
  Client c(port);
  std::string host = "127.0.0.1:" + std::to_string(port);
  std::string cookie = Login(c, host);
  /* 6. a second tenant on the same process. */
  std::string other = "other.example";
  Reply theirs = c.Send("GET", other, "/", std::string(), std::string());
  Need(theirs.status == 200 && Has(theirs.body, "class=\"main\""),
      "the second tenant does not serve its own front page");
  Need(!Has(theirs.body, kPostTitle), "the second tenant sees the first one's post");
  Reply admin_there = c.Send("GET", other, "/admin", cookie, std::string());
  Need(admin_there.body.empty(), "an admin cookie from tenant A opens tenant B");
  Reply login_there = c.Send("POST", other, "/login", std::string(),
      std::string("username=") + kAdminUser + "&password=" + kAdminPass);
  Need(login_there.status == 401 && HeaderAll(login_there, "Set-Cookie").empty(),
      "tenant A's account signs in at tenant B");
  Ok(6, "a second tenant on the same process sees none of it");
  return 0;
}

/* ---- the operator stages ---- */

struct Site {
  GDEFSTOR_Store * registry = NULL;
  GDEFSTOR_Store * site = NULL; /* the tenant's data store, its blind table too */
  GDEFSTOR_KeyStore * keys = NULL;
  GDEFCORE_Inst * inst = NULL;
  GDEFCORE_Subjects * subjects = NULL;
  GDEFCORE_Catalog * catalog = NULL;
  uint8_t root[GDEFCORE_ACCOUNT_ROOT_LEN];
  uint8_t storage[16];
  char tenant[GDEFCORE_PUBLIC_ID_LEN + 1];
};

int HexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

GDEFSTOR_Store * OpenSqlite(const std::string & dir) {
  GDEFSTOR_Store * store = NULL;
  mkdir(dir.c_str(), 0700);
  /* The entity port's store (story S6b). */
  Need(gdefssqlite_model_store_create(dir.c_str(), NULL, NULL, NULL, &store) ==
          GDEFSSQLITE_OK,
      "could not open the store in " + dir);
  return store;
}

/* The installation's key store (AD-19): keyrings, subject keys and the
 * ledger, in a directory of key files apart from every data store. */
GDEFSTOR_KeyStore * OpenKeys(const std::string & dir) {
  static GDEFSTOR_KeyCipher cipher;
  GDEFSTOR_KeyStore * keys = NULL;
  gdefcore_subject_cipher(&cipher);
  mkdir(dir.c_str(), 0700);
  Need(gdefssqlite_keystore_create(dir.c_str(), &cipher, NULL, NULL, &keys) ==
           GDEFSSQLITE_OK,
      "could not open the key store in " + dir);
  return keys;
}

void OpenSite(Site * s, const std::string & data) {
  const char * secret = getenv("DEFIANT_SECRET");
  Need(secret != NULL && strlen(secret) == 64, "DEFIANT_SECRET is not set");
  for (size_t i = 0; i < sizeof s->root; i++) {
    s->root[i] = (uint8_t)((HexNibble(secret[2 * i]) << 4) | HexNibble(secret[2 * i + 1]));
  }
  s->registry = OpenSqlite(data + "/registry");
  s->site = OpenSqlite(data + "/site");
  s->keys = OpenKeys(data + "/keys");
  Need(gdefcore_inst_open_keys(s->registry, s->site, s->keys, s->root, sizeof s->root, "", 0,
           NULL, &s->inst) == GDEFCORE_OK &&
           gdefcore_inst_default(s->inst, s->tenant) == GDEFCORE_OK &&
           gdefcore_inst_storage_key(s->inst, s->tenant, strlen(s->tenant), s->storage) ==
               GDEFCORE_OK &&
           gdefcore_inst_catalog_open(s->inst, s->tenant, strlen(s->tenant), &s->catalog) ==
               GDEFCORE_OK &&
           gdefcore_subject_open(s->keys, s->site, s->storage, sizeof s->storage, s->root,
               sizeof s->root, gdefcore_inst_tenant_keyring(s->inst, s->tenant, strlen(s->tenant)),
               NULL, &s->subjects) == GDEFCORE_OK,
      "could not open the installation");
  /* The keys that index a personal value the site's changesets write (story
   * S6c: the blind rows are in the site store's blind table). */
  gdefcore_catalog_bind_subjects(s->catalog, s->subjects);
}

void CloseSite(Site * s) {
  gdefcore_subject_destroy(s->subjects);
  gdefcore_catalog_destroy(s->catalog);
  gdefcore_inst_destroy(s->inst);
  gdefstor_keystore_destroy(s->keys);
  gdefstor_store_destroy(s->site);
  gdefstor_store_destroy(s->registry);
}

const char kField[] = "email";
const char kPlain[] = "grace@example.org";

bool ReadIs(Site * s, GDEFCORE_Catalog * catalog, const std::string & subject,
    const std::string & entity, const char * expect) {
  char * text = NULL;
  size_t n = 0;
  GDEFCORE_Result rc = gdefcore_subject_read(s->subjects, catalog, subject.c_str(),
      subject.size(), entity.c_str(), entity.size(), kField, strlen(kField), "en", 2, &text, &n);
  bool ok = rc == GDEFCORE_OK && text != NULL && n == strlen(expect) &&
            memcmp(text, expect, n) == 0;
  gdefcore_subject_free(s->subjects, text);
  return ok;
}

int Operator1(const std::string & data, const std::string & scratch) {
  Site s;
  OpenSite(&s, data);
  /* A person whose email is personal data, and the snapshot taken that day.
   * A person is a subject: the type says so, and core makes each one's
   * subject with it, under which their email is stored (core story S7). */
  GDEFCORE_Field field;
  memset(&field, 0, sizeof field);
  field.size = sizeof field;
  field.name = kField;
  field.name_len = strlen(kField);
  field.required = 1;
  field.personal = 1;
  char subject[GDEFCORE_PUBLIC_ID_LEN + 1];
  char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
  Need(gdefcore_type_define(s.catalog, "site.person", 11,
           GDEFCORE_TYPE_CONTENT | GDEFCORE_TYPE_SUBJECTS, &field, 1) == GDEFCORE_OK &&
           gdefcore_entity_create(s.catalog, "site.person", 11, entity, NULL) == GDEFCORE_OK &&
           gdefcore_entity_subject(s.catalog, entity, strlen(entity), subject) == GDEFCORE_OK &&
           gdefcore_subject_save(s.subjects, s.catalog, NULL, 0, entity, strlen(entity), kField,
               strlen(kField), "en", 2, kPlain, strlen(kPlain)) == GDEFCORE_OK,
      "could not record the person");
  Need(ReadIs(&s, s.catalog, subject, entity, kPlain), "the person is not readable");
  uint8_t * zip = NULL;
  size_t zip_n = 0;
  Need(gdefcore_subject_snapshot(s.subjects, s.site, NULL, 0, &zip, &zip_n) == GDEFCORE_OK,
      "could not take the snapshot");
  WriteFile(scratch + "/snapshot.zip", std::string((const char *)zip, zip_n));
  gdefstor_bytes_free(NULL, zip);
  /* Its key set, taken after it: the key half of the backup pair. */
  uint8_t * set = NULL;
  size_t set_n = 0;
  Need(gdefcore_subject_keyset(s.subjects, &set, &set_n) == GDEFCORE_OK,
      "could not take the key set");
  WriteFile(scratch + "/keyset", std::string((const char *)set, set_n));
  gdefstor_bytes_free(NULL, set);
  WriteFile(scratch + "/person", std::string(subject) + "\n" + entity + "\n");
  /* A second tenant on the same installation, with the starter kit. */
  char other[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Catalog * theirs = NULL;
  uint8_t * kit = NULL;
  size_t kit_n = 0;
  Need(gdefcore_inst_tenant_create(s.inst, "other.example", 13, "", 0, other) == GDEFCORE_OK &&
           gdefcore_inst_catalog_open(s.inst, other, strlen(other), &theirs) == GDEFCORE_OK &&
           gdefcore_render_kit(theirs, &kit, &kit_n) == GDEFCORE_OK &&
           gdefcore_render_install(theirs, kit, kit_n) == GDEFCORE_OK,
      "could not make the second tenant");
  gdefcore_text_free(theirs, (char *)kit);
  gdefcore_catalog_destroy(theirs);
  CloseSite(&s);
  return 0;
}

int Operator2(const std::string & data, const std::string & scratch) {
  Site s;
  OpenSite(&s, data);
  std::istringstream person(ReadFile(scratch + "/person"));
  std::string subject, entity;
  std::getline(person, subject);
  std::getline(person, entity);
  Need(ReadIs(&s, s.catalog, subject, entity, kPlain), "the person is gone before the erasure");
  /* 4. erase the person who asked. */
  Need(gdefcore_subject_erase(s.subjects, subject.c_str(), subject.size(), subject.c_str(),
           subject.size()) == GDEFCORE_OK,
      "the erasure was refused");
  Need(ReadIs(&s, s.catalog, subject, entity, GDEFCORE_SUBJECT_ERASED),
      "the person still reads after the erasure");
  Ok(4, "erase a person: the record now reads as erased");
  /* 5. restore the earlier snapshot into an empty tenant. */
  std::string zip = ReadFile(scratch + "/snapshot.zip");
  std::string set = ReadFile(scratch + "/keyset");
  Need(!zip.empty() && !set.empty(), "there is no backup pair to restore");
  GDEFSTOR_Store * empty = OpenSqlite(scratch + "/restored");
  const char * plant = getenv("GHOTI_DEMO_PLANT");
  if (plant != NULL && strcmp(plant, "ledger") == 0) {
    /* A restore that trusts the backup's ledger alone: the key set goes into
     * a key store of its own, the live ledger with the newer erasure is never
     * read, and the rows go back through the storage import. */
    GDEFSTOR_KeyStore * fresh = OpenKeys(scratch + "/keys-restored");
    Need(gdefstor_keystore_import(fresh, (const char *)s.storage, sizeof s.storage,
             (const uint8_t *)set.data(), set.size()) == GDEFSTOR_OK,
        "the planted restore failed to import the key set");
    gdefcore_subject_destroy(s.subjects);
    s.subjects = NULL;
    gdefstor_keystore_destroy(s.keys);
    s.keys = fresh;
    Need(gdefcore_subject_open(s.keys, s.site, s.storage, sizeof s.storage, s.root,
             sizeof s.root, NULL, NULL, &s.subjects) == GDEFCORE_OK &&
             gdefstor_snapshot_import(empty, (const char *)s.storage, sizeof s.storage,
                 (const uint8_t *)zip.data(), zip.size(), NULL) == GDEFSTOR_OK,
        "the planted restore failed to import");
    gdefcore_catalog_bind_subjects(s.catalog, s.subjects);
  } else {
    GDEFCORE_Restore * report = NULL;
    GDEFCORE_Result restored_rc = gdefcore_subject_restore(s.subjects, NULL, empty,
        (const uint8_t *)zip.data(), zip.size(), (const uint8_t *)set.data(), set.size(),
        &report);
    if (restored_rc != GDEFCORE_OK && gdefcore_restore_reason(report, NULL) != NULL) {
      fprintf(stderr, "%s\n", gdefcore_restore_reason(report, NULL));
    }
    gdefcore_restore_free(report);
    Need(restored_rc == GDEFCORE_OK, "the restore failed to import");
  }
  GDEFCORE_Catalog * restored = NULL;
  Need(gdefcore_catalog_open(empty, s.storage, sizeof s.storage, NULL, NULL, &restored) ==
           GDEFCORE_OK,
      "could not open the restored tenant");
  bool erased = ReadIs(&s, restored, subject, entity, GDEFCORE_SUBJECT_ERASED);
  bool leaked = ReadIs(&s, restored, subject, entity, kPlain);
  gdefcore_catalog_destroy(restored);
  gdefstor_store_destroy(empty);
  CloseSite(&s);
  g_step = 5;
  Need(!leaked, "the restored snapshot gave the person back");
  Need(erased, "the restored tenant does not read the person as erased");
  Ok(5, "restore the earlier snapshot: the person is still erased");
  return 0;
}

}  // namespace

int main(int argc, char ** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: defiant-demo web1|web2|web3 PORT SCRATCH | operator1|operator2 DATA SCRATCH\n");
    return 2;
  }
  std::string mode = argv[1];
  if (mode == "web1") {
    return Web1((uint16_t)atoi(argv[2]), argv[3]);
  }
  if (mode == "web2") {
    return Web2((uint16_t)atoi(argv[2]), argv[3]);
  }
  if (mode == "web3") {
    return Web3((uint16_t)atoi(argv[2]), argv[3]);
  }
  if (mode == "operator1") {
    return Operator1(argv[2], argv[3]);
  }
  if (mode == "operator2") {
    return Operator2(argv[2], argv[3]);
  }
  fprintf(stderr, "defiant-demo: unknown stage %s\n", argv[1]);
  return 2;
}
