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
 * while the program is stopped.
 *
 *   defiant-demo selftest                 the half-state check is shown to see
 *   defiant-demo operator3 DATA SCRATCH   the site name placed on the front page
 *   defiant-demo flow PORT PID LOG  change the site while requests flow
 *
 * The flow stage keeps four readers' requests arriving while the title, a menu
 * entry, a template and a widget are each changed through the admin forms, counts
 * every request, and checks that each response was a whole of one state of the
 * site. It reads the program's process and its counters (SIGUSR1) because the
 * browser cannot show those. The first failing step prints one line and
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

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
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
  Need(!session.empty(), "an admin login set no session cookie");
  return session;
}

/* ---- the flow: requests that keep arriving while the site is changed ---- */

double Now() {
  return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* One response of a request that flows, and what it showed. */
struct Obs {
  int kind;      /* 0: the public front page, 1: the admin home */
  double t0;     /* when the request was made */
  double t1;     /* when its response was taken */
  std::map<std::string, std::string> shown;
};

/* One change, as the browser flow made it: from the moment its post was sent to
 * the moment its answer was taken. */
struct Window {
  double issued = 0;
  double done = 0;
};

typedef std::map<std::string, std::string> Shown;

std::string Between(const std::string & page, const std::string & open,
    const std::string & close) {
  size_t at = page.find(open);
  if (at == std::string::npos) {
    return "<none>";
  }
  at += open.size();
  size_t end = page.find(close, at);
  return end == std::string::npos ? std::string("<none>") : page.substr(at, end - at);
}

/* What the public front page shows of the parts the changes touch: the site
 * name and its tagline (one setting form), the two markers an edited template
 * prints (one template), and where the recent posts are (one layout). */
Shown PublicShown(const std::string & page) {
  Shown s;
  s["title"] = Between(page, "<span class=\"site-name\">", "</span>");
  s["tagline"] = Between(page, "<span class=\"tagline\">", "</span>");
  s["template.top"] = Between(page, "<span class=\"edit-a\">", "</span>");
  s["template.bottom"] = Between(page, "<span class=\"edit-b\">", "</span>");
  std::string region = PostRegion(page);
  s["widget"] = region.empty() ? std::string("<none>") : region;
  return s;
}

/* What the admin home shows of the menu: the whole of it, in order. */
Shown AdminShown(const std::string & page) {
  Shown s;
  s["menu"] = Between(page, "<nav class=\"admin-menu\">", "</nav>");
  return s;
}

/*
 * Whether each response was a whole: the states of the site are the numbered
 * points between its changes (0 is before the first, N after the last), and
 * @p states[k] is what every part showed at point k. A response is a whole when
 * one point explains everything it showed, and that point is neither older than
 * a change that had already been answered when the request was made nor newer
 * than a change that had not yet been sent when the response was taken. Returns
 * one sentence for the first response that is not, or an empty string.
 */
std::string FirstHalfState(const std::vector<Obs> & obs, const std::vector<Window> & changes,
    const std::vector<Shown> & states) {
  const size_t n = changes.size();
  for (const Obs & o : obs) {
    size_t lo = 0;
    size_t hi = 0;
    for (size_t k = 0; k < n; k++) {
      if (changes[k].done < o.t0) {
        lo = k + 1;
      }
      if (changes[k].issued < o.t1) {
        hi = k + 1;
      }
    }
    std::set<size_t> common;
    for (size_t k = 0; k <= n; k++) {
      common.insert(k);
    }
    std::string odd;
    for (const auto & part : o.shown) {
      std::set<size_t> own;
      for (size_t k = 0; k <= n; k++) {
        auto at = states[k].find(part.first);
        if (at != states[k].end() && at->second == part.second) {
          own.insert(k);
        }
      }
      if (own.empty() && odd.empty()) {
        odd = part.first + " showed \"" + part.second + "\", which no state of the site had";
      }
      std::set<size_t> keep;
      for (size_t k : common) {
        if (own.count(k) != 0) {
          keep.insert(k);
        }
      }
      common.swap(keep);
    }
    if (!odd.empty()) {
      return odd;
    }
    if (common.empty()) {
      std::string parts;
      for (const auto & part : o.shown) {
        parts += (parts.empty() ? "" : ", ") + part.first;
      }
      return "a page showed parts of different states (" + parts + ") in one response";
    }
    bool inside = false;
    for (size_t k : common) {
      inside = inside || (k >= lo && k <= hi);
    }
    if (!inside) {
      return *common.rbegin() < lo
                 ? "a request made after change " + std::to_string(lo) +
                       " was answered still showed the state before it"
                 : "a response showed the state after change " +
                       std::to_string(*common.begin()) + ", which had not been sent";
    }
  }
  return std::string();
}

/* The request text, for the client and for the flow. */
std::string BuildRequest(const std::string & method, const std::string & host,
    const std::string & path, const std::string & cookie, const std::string & body) {
  std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
  if (method == "POST") {
    req += "Origin: http://" + host + "\r\n"
           "Content-Type: application/x-www-form-urlencoded\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  if (!cookie.empty()) {
    req += "Cookie: " + cookie + "\r\n";
  }
  return req + "\r\n" + body;
}

bool ParseReply(const std::string & raw, Reply * r) {
  size_t blank = raw.find("\r\n\r\n");
  if (raw.compare(0, 9, "HTTP/1.1 ") != 0 || blank == std::string::npos) {
    return false;
  }
  r->status = atoi(raw.c_str() + 9);
  r->headers = raw.substr(0, blank);
  size_t line = r->headers.find("\r\n");
  if (line != std::string::npos) {
    r->headers = r->headers.substr(line + 2);
  }
  r->body = raw.substr(blank + 4);
  return true;
}

/*
 * Requests that keep arriving while the site is changed. Several readers, each
 * with a request always in flight, share one loop with the editor's requests;
 * every request is counted, and a reader's response is kept with the times it
 * was made and taken and what it showed. The program serves one connection at a
 * time, so the readers wait behind the editor's request in its queue and the
 * editor's waits behind theirs; the check does not rely on that.
 */
class Flow {
 public:
  Flow(uint16_t port, const std::string & host) : port_(port), host_(host) {
    if (gcu_loop_create(&loop_, NULL) != GCU_LOOP_OK) {
      Fail("the flow could not create an event loop");
    }
  }
  ~Flow() {
    for (Job & j : jobs_) {
      if (j.fiber != NULL && gcu_fiber_is_finished(j.fiber)) {
        gcu_fiber_destroy(j.fiber);
      }
    }
    gcu_loop_destroy(loop_);
  }

  /* An editor's request, made while the readers go on. @p expect holds the
   * statuses that are an answer (200, or the redirect a form is answered with);
   * anything else is a failed request and stops the run. */
  Reply Do(const std::string & method, const std::string & path,
      const std::string & cookie, const std::string & body,
      const std::set<int> & expect, Window * window = NULL) {
    Job * j = Submit(method, path, cookie, body, -1);
    if (window != NULL) {
      window->issued = j->t0;
    }
    while (!j->ex.done) {
      Pump();
    }
    double taken = Now();
    Reply r;
    bool parsed = !j->ex.fail && ParseReply(j->ex.response, &r);
    Retire(j);
    sent_ += 1;
    if (window != NULL) {
      window->done = taken;
    }
    if (!parsed || expect.count(r.status) == 0) {
      failed_ += 1;
      Fail(method + " " + path + " was not answered as it should be (" +
           (parsed ? "status " + std::to_string(r.status) : std::string("no reply")) + ")");
    }
    return r;
  }

  void StartReaders(const std::string & admin_cookie) {
    admin_cookie_ = admin_cookie;
    running_ = true;
    for (int i = 0; i < kReaders; i++) {
      Submit("GET", i % 2 == 0 ? "/" : "/admin", i % 2 == 0 ? std::string() : admin_cookie_,
          std::string(), i % 2);
    }
  }

  /* Let the readers go on until @p n more responses have been taken. */
  void Soak(size_t n) {
    size_t target = taken_ + n;
    double end = Now() + 30;
    while (taken_ < target) {
      Pump();
      Need(Now() < end, "the readers' requests were not answered");
    }
  }

  /* No new reader request; wait for the ones in flight. */
  void StopReaders() {
    running_ = false;
    double end = Now() + 30;
    for (;;) {
      bool any = false;
      for (const Job & j : jobs_) {
        any = any || j.reader >= 0;
      }
      if (!any) {
        return;
      }
      Pump();
      Need(Now() < end, "the readers' last requests were not answered");
    }
  }

  const std::vector<Obs> & Observed() const { return obs_; }
  size_t Sent() const { return sent_; }
  size_t ReaderSent() const { return reader_sent_; }
  size_t Failed() const { return failed_; }

 private:
  static const int kReaders = 4;

  struct Job {
    Exchange ex;
    GCU_Fiber * fiber = NULL;
    double t0 = 0;
    int reader = -1; /* the kind of page a reader asks for, or -1 for the editor */
    std::string path;
  };

  Job * Submit(const std::string & method, const std::string & path,
      const std::string & cookie, const std::string & body, int reader) {
    jobs_.emplace_back();
    Job & j = jobs_.back();
    j.reader = reader;
    j.path = path;
    j.ex.loop = loop_;
    j.ex.port = port_;
    j.ex.request = BuildRequest(method, host_, path, cookie, body);
    j.ex.done = 0;
    j.ex.fail = 1;
    j.t0 = Now();
    if (gcu_fiber_create(&j.fiber, ClientFiber, &j.ex, GCU_FIBER_DEFAULT_STACK_SIZE, NULL) !=
            GCU_FIBER_OK ||
        gcu_fiber_switch_to(j.fiber) != GCU_FIBER_OK) {
      Fail("the flow could not start a request");
    }
    return &j;
  }

  void Retire(Job * job) {
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
      if (&*it == job) {
        if (it->fiber != NULL && gcu_fiber_is_finished(it->fiber)) {
          gcu_fiber_destroy(it->fiber);
        }
        jobs_.erase(it);
        return;
      }
    }
  }

  /* A reader's response: counted, and kept with what it showed. */
  void Take(Job & j, double taken) {
    Reply r;
    bool parsed = !j.ex.fail && ParseReply(j.ex.response, &r);
    sent_ += 1;
    reader_sent_ += 1;
    taken_ += 1;
    const char * mark = j.reader == 0 ? "class=\"main\"" : "admin-menu";
    if (!parsed || r.status != 200 || !Has(r.body, mark)) {
      failed_ += 1;
      Fail("a request to " + j.path + " failed while the site was being changed (" +
           (parsed ? "status " + std::to_string(r.status) : std::string("no reply")) + ")");
    }
    Obs o;
    o.kind = j.reader;
    o.t0 = j.t0;
    o.t1 = taken;
    o.shown = j.reader == 0 ? PublicShown(r.body) : AdminShown(r.body);
    obs_.push_back(o);
  }

  void Pump() {
    if (gcu_loop_run_once(loop_, 5) != GCU_LOOP_OK) {
      Fail("the flow's event loop failed");
    }
    double now = Now();
    for (auto it = jobs_.begin(); it != jobs_.end();) {
      Job & j = *it;
      if (!j.ex.done) {
        if (now - j.t0 > 30) {
          Fail("a request to " + j.path + " did not finish");
        }
        ++it;
        continue;
      }
      if (j.reader < 0) {
        ++it;
        continue;
      }
      int kind = j.reader;
      Take(j, now);
      if (j.fiber != NULL && gcu_fiber_is_finished(j.fiber)) {
        gcu_fiber_destroy(j.fiber);
      }
      it = jobs_.erase(it);
      if (running_) {
        Submit("GET", kind == 0 ? "/" : "/admin", kind == 0 ? std::string() : admin_cookie_,
            std::string(), kind);
      }
    }
  }

  uint16_t port_;
  std::string host_;
  GCU_Loop * loop_ = NULL;
  std::list<Job> jobs_;
  std::vector<Obs> obs_;
  std::string admin_cookie_;
  bool running_ = false;
  size_t sent_ = 0;
  size_t reader_sent_ = 0;
  size_t failed_ = 0;
  size_t taken_ = 0;
};

/* ---- the web stages ---- */

int Web1(uint16_t port, const std::string & scratch) {
  Client c(port);
  std::string host = "127.0.0.1:" + std::to_string(port);
  Reply form_page = c.Send("GET", host, "/login", std::string(), std::string());
  Need(form_page.status == 200 && Has(form_page.body, "<form"), "no sign-in page");
  std::string cookie = Login(c, host);
  WriteFile(scratch + "/cookie", cookie);
  /* The first administrator reaches every admin page with no grant made by
   * hand: the program's first start gave the role what each page asks. */
  for (const char * path : {"/admin", "/admin/more", "/admin/post", "/admin/front",
           "/admin/undo", "/admin/access", "/admin/templates", "/admin/menus",
           "/admin/settings"}) {
    Reply page = c.Send("GET", host, path, cookie, std::string());
    Need(page.status == 200 && !page.body.empty(),
        std::string("the first administrator cannot reach ") + path);
  }
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
  c.Post(host, "/admin/front", cookie, "token=" + order + "&panes=main+recent");
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
  Need(admin_there.body.empty(), "a session from tenant A opens tenant B");
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
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(),
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

/* ---- reading the program, which is the one thing the browser cannot show ---- */

/* When the process started, in the kernel's ticks: the same process has the
 * same number for as long as it lives, and a restart cannot repeat it. */
unsigned long long ProcessStart(long pid) {
  std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(in, line);
  size_t close = line.rfind(')');
  Need(close != std::string::npos, "the program's process cannot be read");
  std::istringstream rest(line.substr(close + 2));
  std::string field;
  for (int i = 3; i < 22; i++) {
    rest >> field;
  }
  unsigned long long start = 0;
  rest >> start;
  Need(start != 0, "the program's start time cannot be read");
  return start;
}

struct Counts {
  unsigned long long compiled = 0;
  unsigned long long run = 0;
};

size_t CountOf(const std::string & hay, const std::string & needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
    n += 1;
  }
  return n;
}

/* Ask the program for its counters (SIGUSR1) and read the line it writes. */
Counts ProgramCounts(long pid, const std::string & log) {
  static const char kLine[] = "defiant: stats ";
  size_t before = CountOf(ReadFile(log), kLine);
  Need(kill((pid_t)pid, SIGUSR1) == 0, "the program cannot be asked for its counters");
  for (int i = 0; i < 500; i++) {
    std::string text = ReadFile(log);
    if (CountOf(text, kLine) > before) {
      size_t at = text.rfind(kLine);
      Counts c;
      Need(sscanf(text.c_str() + at, "defiant: stats compiled=%llu rendered=%llu", &c.compiled,
               &c.run) == 2,
          "the program's counters were not in the form expected");
      return c;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::string seen = ReadFile(log);
  Fail("the program did not report its counters; its log ends: " +
       seen.substr(seen.size() > 400 ? seen.size() - 400 : 0) +
       (kill((pid_t)pid, 0) == 0 ? " [still running]" : " [gone]"));
}

/* The cost of a page, in template bodies compiled, measured alone. */
unsigned long long CostOf(Flow & f, long pid, const std::string & log, const std::string & path,
    const std::string & cookie) {
  Counts a = ProgramCounts(pid, log);
  f.Do("GET", path, cookie, std::string(), {200});
  Counts b = ProgramCounts(pid, log);
  Need(b.compiled - a.compiled == b.run - a.run,
      "a request to " + path + " compiled templates it did not run");
  return b.compiled - a.compiled;
}

/* The form in @p page whose row for @p entry has the button @p action. */
std::string RowForm(const std::string & page, const std::string & entry,
    const std::string & action) {
  size_t row = page.find("<tr id=\"e-" + entry + "\"");
  if (row == std::string::npos) {
    return std::string();
  }
  size_t button = page.find("name=\"action\" value=\"" + action + "\"", row);
  if (button == std::string::npos || button > page.find("</tr>", row)) {
    return std::string();
  }
  size_t form = page.rfind("<form", button);
  return page.substr(form, page.find("</form>", button) - form);
}

/* The post a browser makes from a menu editor form. */
std::string MenuBody(const std::string & form, const std::string & extra) {
  return "token=" + InputValue(form, "token", 0) + "&version=" +
         InputValue(form, "version", 0) + extra;
}

const char kMoreEntry[] = "core.l-admin-more";

/* The edited template: the recent-posts pane with a marker at its top and at
 * its bottom, so that a half-written edit would show one and not the other. */
const char kEditedPane[] =
    "<% use title; use summary; %><article class=\"recent\">"
    "<span class=\"edit-a\">demo-edit-1</span><h2><%= title %></h2>"
    "<p><%= summary %></p><span class=\"edit-b\">demo-edit-1</span></article>";

int FlowStage(uint16_t port, long pid, const std::string & log) {
  const std::string host = "127.0.0.1:" + std::to_string(port);
  Flow f(port, host);
  const std::set<int> ok = {200};
  const std::set<int> answered = {200, 303};
  std::vector<Shown> states;   /* what every part showed at each point */
  std::vector<Window> changes; /* the changes, in the order they were made */
  Shown now;                   /* what every part shows now */

  auto login = [&]() {
    Reply r = f.Do("POST", "/login", std::string(),
        std::string("username=") + kAdminUser + "&password=" + kAdminPass, {303});
    std::string cookie = CookiePair(r, "__Host-session");
    Need(!cookie.empty(), "a sign-in set no session cookie");
    return cookie;
  };
  /* A browser follows a redirect with a GET, as the same person. */
  auto follow = [&](const Reply & r, const std::string & cookie) {
    if (r.status != 303) {
      return r;
    }
    return f.Do("GET", Header(r, "Location"), cookie, std::string(), ok);
  };
  /* What the site shows now, read by two requests of the editor's own. */
  auto look = [&](const std::string & editor) {
    Shown s = PublicShown(f.Do("GET", "/", std::string(), std::string(), ok).body);
    for (const auto & part : AdminShown(f.Do("GET", "/admin", editor, std::string(), ok).body)) {
      s[part.first] = part.second;
    }
    return s;
  };
  /* A change, made by a form and answered as a browser is answered. */
  auto change = [&](const std::string & path, const std::string & cookie,
                    const std::string & body) {
    Window w;
    Reply sent = f.Do("POST", path, cookie, body, answered, &w);
    changes.push_back(w);
    return follow(sent, cookie);
  };

  g_step = 7;
  /* Two sign-ins: the editor's, and the readers', so that the messages a
   * change leaves for the editor are not drawn by the readers' pages. */
  const std::string editor = login();
  const std::string readers = login();
  const unsigned long long started = ProcessStart(pid);
  Need(CountOf(ReadFile(log), "listening on port") == 1, "the program was started more than once");
  const Counts first = ProgramCounts(pid, log);
  now = look(editor);
  Need(now["title"] != "<none>",
      "the front page does not draw the site name, which the demo's operator stage places");
  Need(now["widget"] == "main", "the recent posts did not start in the main region");
  Need(now["menu"].find("href=\"/admin/more\"") != std::string::npos,
      "the admin menu has no entry for More");
  states.push_back(now);
  const unsigned long long cost_front = CostOf(f, pid, log, "/", std::string());
  const unsigned long long cost_admin = CostOf(f, pid, log, "/admin", editor);
  Need(cost_front > 0 && cost_admin > 0, "a page was drawn with no template compiled");

  f.StartReaders(readers);
  f.Soak(12);

  /* 7. the site title, a setting: the title and the tagline in one save. */
  {
    Reply form = f.Do("GET", "/admin/settings/core/site", editor, std::string(), ok);
    Need(!InputValue(form.body, "version", 0).empty(), "the settings page has no form");
    follow(change("/admin/settings/core/site", editor,
               "token=" + InputValue(form.body, "token", 0) + "&version=" +
                   InputValue(form.body, "version", 0) + "&s.core.site_title=" +
                   Encode("Gadget Works") + "&s.core.tagline=" + Encode("Tools and more")),
        editor);
    now = look(editor);
    Need(now["title"] == "Gadget Works" && now["tagline"] == "Tools and more",
        "the request after the title was changed does not show the new title");
    states.push_back(now);
    f.Soak(12);
    Ok(7, "change the site title: the very next request shows it");
  }

  /* 8. a menu entry, renamed and then moved. */
  {
    g_step = 8;
    Reply page = f.Do("GET", "/admin/menus", editor, std::string(), ok);
    std::string form = RowForm(page.body, kMoreEntry, "rename");
    Need(!form.empty(), "the menu editor has no rename button for More");
    change("/admin/menus", editor,
        MenuBody(form, std::string("&name=") + kMoreEntry + "&action=rename&label=Elsewhere"));
    Shown renamed = look(editor);
    Need(renamed["menu"].find("href=\"/admin/more\">Elsewhere") != std::string::npos,
        "the request after the menu entry was renamed does not show the new name");
    states.push_back(renamed);
    f.Soak(12);
    page = f.Do("GET", "/admin/menus", editor, std::string(), ok);
    form = RowForm(page.body, kMoreEntry, "up");
    Need(!form.empty(), "the menu editor has no up button for More");
    change("/admin/menus", editor,
        MenuBody(form, std::string("&name=") + kMoreEntry + "&action=up"));
    Shown moved = look(editor);
    Need(moved["menu"] != renamed["menu"] &&
             moved["menu"].find("href=\"/admin/more\">Elsewhere") != std::string::npos,
        "the request after the menu entry was moved does not show it in its new place");
    states.push_back(moved);
    now = moved;
    f.Soak(12);
    Ok(8, "rename and move a menu entry: the very next request shows each");
  }

  /* 9. a page template, edited. */
  {
    g_step = 9;
    Reply page = f.Do("GET", "/admin/templates/recent", editor, std::string(), ok);
    Need(!InputValue(page.body, "token", 1).empty(), "the template page has no save form");
    Reply saved = change("/admin/templates/recent", editor,
        "token=" + InputValue(page.body, "token", 0) + "&token=" +
            InputValue(page.body, "token", 1) + "&kind=core.pane&body=" + Encode(kEditedPane) +
            "&confirm=recent&do=save");
    Need(Has(saved.body, "Saved."), "the template was not saved");
    now = look(editor);
    Need(now["template.top"] == "demo-edit-1" && now["template.bottom"] == "demo-edit-1",
        "the request after the template was edited does not show the edit");
    states.push_back(now);
    f.Soak(12);
    Ok(9, "edit a template: the very next request shows it");
  }

  /* 10. a widget, moved: taken off the page and put back, in the other region. */
  {
    g_step = 10;
    Reply page = f.Do("GET", "/admin/front", editor, std::string(), ok);
    std::string place = FormToken(page.body, "Place recent posts");
    std::string remove = FormToken(page.body, "Remove recent posts");
    Need(!place.empty() && !remove.empty(), "the front page editor has no place or remove form");
    change("/admin/front", editor, "token=" + remove);
    now = look(editor);
    Need(now["widget"] == "<none>",
        "the request after the widget was taken off does not show it gone");
    states.push_back(now);
    f.Soak(8);
    page = f.Do("GET", "/admin/front", editor, std::string(), ok);
    place = FormToken(page.body, "Place recent posts");
    change("/admin/front", editor, "token=" + place);
    now = look(editor);
    Need(now["widget"] == "side",
        "the request after the widget was put back does not show it in the side region");
    states.push_back(now);
    f.Soak(12);
    Ok(10, "move a widget: the very next request shows it gone, then in its new region");
  }

  f.StopReaders();

  /* 11. what the requests that flowed showed. */
  g_step = 11;
  Need(f.Failed() == 0, "a request failed");
  for (size_t k = 0; k < changes.size(); k++) {
    size_t during = 0;
    for (const Obs & o : f.Observed()) {
      during += (o.t0 < changes[k].done && o.t1 > changes[k].issued) ? 1 : 0;
    }
    Need(during > 0, "no request was in flight during change " + std::to_string(k + 1) +
                         ", so the check proved nothing about it");
  }
  Need(states.size() == changes.size() + 1, "the flow lost count of the states of the site");
  std::string torn = FirstHalfState(f.Observed(), changes, states);
  Need(torn.empty(), "a half state: " + torn);
  size_t overlapped = 0;
  for (const Window & w : changes) {
    for (const Obs & o : f.Observed()) {
      overlapped += (o.t0 < w.done && o.t1 > w.issued) ? 1 : 0;
    }
  }
  Ok(11, std::to_string(f.Sent()) + " requests (" + std::to_string(f.ReaderSent()) +
             " from readers that never stopped), 0 failed, " + std::to_string(overlapped) +
             " in flight during a change, every response a whole of one state");

  /* 12. the same process, and nothing built ahead of a request. */
  g_step = 12;
  Need(kill((pid_t)pid, 0) == 0 && ProcessStart(pid) == started,
      "the program's process is not the one the flow began with");
  Need(CountOf(ReadFile(log), "listening on port") == 1, "the program was started again");
  Counts last = ProgramCounts(pid, log);
  Need(last.compiled - first.compiled == last.run - first.run,
      "templates were compiled that no request ran");
  Need(last.compiled > first.compiled, "the flow compiled nothing");
  Counts again = ProgramCounts(pid, log);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  Counts idle = ProgramCounts(pid, log);
  Need(idle.compiled == again.compiled && idle.run == again.run,
      "the program compiled templates while no request was arriving");
  Need(CostOf(f, pid, log, "/", std::string()) == cost_front &&
           CostOf(f, pid, log, "/admin", editor) == cost_admin,
      "a page costs a different number of compiles after the changes");
  Ok(12, "the same process throughout, " + std::to_string(last.compiled - first.compiled) +
             " templates compiled for " + std::to_string(last.run - first.run) +
             " run, none while idle, a page costs what it did");

  /* 13. sign out. */
  g_step = 13;
  for (const std::string & cookie : {editor, readers}) {
    Reply page = f.Do("GET", "/admin", cookie, std::string(), ok);
    std::string token = FormToken(page.body, "Sign out");
    Need(!token.empty(), "the admin home has no sign-out form");
    Reply out = f.Do("POST", "/logout", cookie, "token=" + token, {303, 500});
    Need(out.status == 303 && Header(out, "Location").find("/login") != std::string::npos,
        "signing out was not answered with the sign-in page");
    Need(!Has(out.body, "still signed in") && !Has(out.body, "Still signed in"),
        "signing out reached the page that says the session is still active");
    Reply gone = f.Do("GET", "/admin", cookie, std::string(), {200, 404});
    Need(gone.body.empty(), "a signed-out session still opens the admin");
  }
  Ok(13, "sign out: the sign-in page, never the failed sign-out page, and the session is over");
  return 0;
}

/* The check that decides whether a response was a whole is itself shown to
 * see: pages made up to be whole pass, and a torn one, a stale one, one from
 * the future and one of no state each fail with their own sentence. */
int SelfTest() {
  std::vector<Shown> states = {
      {{"title", "old"}, {"tagline", "old"}},
      {{"title", "new"}, {"tagline", "new"}},
      {{"title", "new"}, {"tagline", "newer"}},
  };
  std::vector<Window> changes(2);
  changes[0].issued = 10;
  changes[0].done = 11;
  changes[1].issued = 20;
  changes[1].done = 21;
  auto page = [](double t0, double t1, const char * title, const char * tagline) {
    Obs o;
    o.kind = 0;
    o.t0 = t0;
    o.t1 = t1;
    o.shown["title"] = title;
    o.shown["tagline"] = tagline;
    return o;
  };
  auto verdict = [&](const Obs & o) {
    return FirstHalfState(std::vector<Obs>{o}, changes, states);
  };
  Need(verdict(page(1, 2, "old", "old")).empty(), "a whole page before every change was refused");
  Need(verdict(page(10.5, 10.9, "old", "old")).empty(), "a whole old page during a change was refused");
  Need(verdict(page(10.5, 10.9, "new", "new")).empty(), "a whole new page during a change was refused");
  Need(verdict(page(30, 31, "new", "newer")).empty(), "a whole page after every change was refused");
  Need(Has(verdict(page(10.5, 10.9, "new", "old")), "parts of different states"),
      "a page with the new title and the old tagline was not called torn");
  Need(Has(verdict(page(12, 13, "old", "old")), "still showed the state before"),
      "a page that is older than an answered change was not called stale");
  Need(Has(verdict(page(5, 6, "new", "new")), "had not been sent"),
      "a page that is newer than every sent change was not called early");
  Need(Has(verdict(page(5, 6, "other", "old")), "no state of the site had"),
      "a page of no state was not called so");
  Ok(0, "the half-state check passes whole pages and fails a torn, a stale, an early and an unknown one");
  return 0;
}

/* The site's name on the front page, as a site that draws it has it: the
 * layout editor that would place it is not built, so the operator stage does,
 * and the recent posts are in the main region beside it. */
int Operator3(const std::string & data) {
  Site s;
  OpenSite(&s, data);
  char ** ids = NULL;
  size_t n = 0;
  std::string front;
  Need(gdefcore_entity_ids(s.catalog, "core.display", 12, &ids, &n) == GDEFCORE_OK,
      "could not list the displays");
  for (size_t i = 0; i < n && front.empty(); i++) {
    char ** t = NULL;
    size_t * l = NULL;
    size_t c = 0;
    if (gdefcore_entity_values(s.catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i], strlen(ids[i]),
            "en", 2, "name", 4, &t, &l, &c) == GDEFCORE_OK) {
      if (c > 0 && std::string(t[0], l[0]) == "front") {
        front = ids[i];
      }
      gdefcore_values_free(s.catalog, t, l, c);
    }
  }
  gdefcore_ids_free(s.catalog, ids, n);
  Need(!front.empty(), "the site has no front display");
  const char * panes[2] = {"main sitename", "main recent"};
  size_t lens[2] = {strlen(panes[0]), strlen(panes[1])};
  GDEFCORE_Value v;
  memset(&v, 0, sizeof v);
  v.size = sizeof v;
  v.field = "panes";
  v.field_len = 5;
  v.texts = panes;
  v.text_lens = lens;
  v.count = 2;
  GDEFCORE_Changeset * cs = NULL;
  Need(gdefcore_changeset_prepare(s.catalog, NULL, 0, &cs) == GDEFCORE_OK &&
           gdefcore_entity_save(cs, front.data(), front.size(), "en", 2, &v, 1) == GDEFCORE_OK &&
           gdefcore_changeset_apply(cs, "core.display", 12) == GDEFCORE_OK,
      "could not place the site name on the front page");
  gdefcore_changeset_destroy(cs);
  CloseSite(&s);
  return 0;
}

}  // namespace

int main(int argc, char ** argv) {
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "selftest" && argc == 2) {
    return SelfTest();
  }
  if (mode == "flow" && argc == 5) {
    return FlowStage((uint16_t)atoi(argv[2]), atol(argv[3]), argv[4]);
  }
  if (mode == "operator3" && argc == 4) {
    return Operator3(argv[2]);
  }
  if (argc != 4) {
    fprintf(stderr,
        "usage: defiant-demo selftest | web1|web2|web3 PORT SCRATCH | operator1|operator2|operator3 DATA SCRATCH |"
        " flow PORT PID LOG\n");
    return 2;
  }
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
