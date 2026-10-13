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
 * AD-25's response scanner (AD-18 item 8, an internal ID visible outside the
 * server) applied to what the server sends.
 *
 * Scan.EveryRoute drives the real server, two tenants on one process, across
 * every route it serves, as an anonymous visitor and as each tenant's admin:
 * the public pages, the sign-in page and its refusal, the admin pages, a
 * saved post, the front-page edit, the undo page and an undo, a logout, the
 * fixed 404 for an unknown host, a bad request, a refused cross-origin write,
 * and the redirects. The scanner reads every body and every header line.
 *
 * What is not read for digits is named here and in the code, not left out
 * silently:
 *  - the stylesheet and the script are constants compiled into the server.
 *    Their digits are CSS lengths and JavaScript literals, which the scanner
 *    cannot tell from an id. The test requires them byte-identical across
 *    tenants and identities, so that none can carry per-site data.
 *  - Content-Length and Date state a size and a time. A cookie's value is an
 *    opaque token, and so is the value of a form's hidden "token" input; the
 *    base64url alphabet puts digits beside a hyphen at random, which would
 *    make the scan flaky. A Content-Security-Policy hash source is a digest.
 *  - A declared encoding, charset="utf-8", is not an id. Max-Age on a cookie
 *    is a number of seconds.
 * Everything else in a header or a body is read, including a cookie's other
 * attributes, Location and the actor ids on the undo page (which are public
 * ids, not integers).
 *
 * Scan.Control proves the pipeline sees what it looks for: an id planted in a
 * body, in Location, in an ordinary header and in a cookie attribute is found,
 * and the exempt headers are exactly the listed ones. The planted-defect run
 * (tools/check-scanner.sh) edits a scratch copy of the server to leak a
 * revision's row into a page and requires Scan.EveryRoute to fail by name.
 */

#include "tenants_fixture.h"

#include <ghoti.io/defiant-test/defiant-test.h>

#include <set>

namespace {

bool ScanRaw(const std::string & bytes) {
  return gdeftest_response_has_internal_id(bytes.data(), bytes.size(), NULL, 0) != 0;
}

std::string Normalize(const std::string & text);

bool ScanBytes(const std::string & bytes) {
  return ScanRaw(Normalize(bytes));
}

/* The shortest prefix of @p bytes that the scanner flags, as an excerpt. */
std::string Excerpt(const std::string & bytes) {
  size_t lo = 0;
  size_t hi = bytes.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (ScanBytes(bytes.substr(0, mid))) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  size_t from = lo > 30 ? lo - 30 : 0;
  std::string out = bytes.substr(from, lo - from + 10);
  for (char & c : out) {
    if (c == '\r' || c == '\n') {
      c = ' ';
    }
  }
  return out;
}

std::string Lower(const std::string & in) {
  std::string out;
  for (char c : in) {
    out += (char)tolower((unsigned char)c);
  }
  return out;
}

/* Take every "<marker><run of characters not in stop>" out of @p text. */
std::string Strip(std::string text, const std::string & marker, const char * stop) {
  size_t at;
  while ((at = text.find(marker)) != std::string::npos) {
    size_t end = text.find_first_of(stop, at + marker.size());
    text.erase(at, (end == std::string::npos ? text.size() : end) - at);
  }
  return text;
}

/* Declaring the encoding is not naming a row: take out charset="utf-8" and
 * charset=utf-8, in any case, wherever they stand. */
std::string Normalize(const std::string & input) {
  /* A signed form token is opaque base64url, which puts digits beside a
   * hyphen. It is the one hidden input whose value is not read. */
  std::string text = Strip(input, "name=\"token\" value=\"", "\"");
  std::string out;
  std::string lower = Lower(text);
  size_t at = 0;
  while (at < text.size()) {
    size_t hit = lower.find("charset=", at);
    if (hit == std::string::npos) {
      out += text.substr(at);
      break;
    }
    out += text.substr(at, hit - at);
    size_t after = hit + 8;
    bool quoted = after < lower.size() && lower[after] == '"';
    size_t value = after + (quoted ? 1 : 0);
    if (lower.compare(value, 5, "utf-8") == 0) {
      at = value + 5 + (quoted && value + 5 < lower.size() && lower[value + 5] == '"' ? 1 : 0);
    } else {
      out += text.substr(hit, 8);
      at = after;
    }
  }
  return out;
}

/* The part of a header value that can carry data. What is taken out is the
 * part whose grammar is numeric, and nothing else:
 *  - Content-Length and Date state a size and a time;
 *  - Max-Age on a cookie is a number of seconds, and the cookie's value is an
 *    opaque token, whose base64 alphabet can put digits beside a hyphen;
 *  - a Content-Security-Policy hash source is a base64 digest.
 * Every other header, and every other part of these, is read. */
bool Readable(const std::string & name, std::string * value) {
  std::string lower = Lower(name);
  if (lower == "content-length" || lower == "date") {
    return false;
  }
  if (lower == "set-cookie") {
    size_t semi = value->find(';');
    *value = semi == std::string::npos ? std::string() : value->substr(semi);
    *value = Strip(*value, "Max-Age=", "; ");
  } else if (lower == "content-security-policy") {
    *value = Strip(*value, "'sha256-", "'");
  }
  return true;
}

bool IsText(const Reply & reply) {
  std::string type = HeaderValue(reply, "Content-Type");
  return type.find("css") == std::string::npos &&
         type.find("javascript") == std::string::npos;
}

/* Every place a response leaks an id, with the text that was flagged. */
std::vector<std::string> Leaks(const Reply & reply) {
  std::vector<std::string> out;
  size_t at = 0;
  if (IsText(reply) && ScanBytes(reply.body)) {
    out.push_back("body: ..." + Excerpt(reply.body) + "...");
  }
  while (at < reply.headers.size()) {
    size_t end = reply.headers.find("\r\n", at);
    std::string line = reply.headers.substr(at,
        end == std::string::npos ? std::string::npos : end - at);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = line.substr(0, colon);
      std::string value = line.substr(colon + 1);
      if (Readable(name, &value) && ScanBytes(value)) {
        out.push_back("header " + name + ": ..." + Excerpt(value) + "...");
      }
    }
    if (end == std::string::npos) {
      break;
    }
    at = end + 2;
  }
  return out;
}

class Scan : public Pair {
 protected:
  std::vector<std::string> found_;
  size_t seen_ = 0;
  std::map<std::string, std::string> constants_;

  /* Scan one response and remember what leaked. */
  void Check(const std::string & label, const Reply & reply) {
    seen_ += 1;
    EXPECT_NE(0, reply.status) << label << ": no response";
    for (const std::string & where : Leaks(reply)) {
      found_.push_back(label + " (" + std::to_string(reply.status) + ") " + where);
    }
    if (!IsText(reply)) {
      std::string path = label.substr(label.find(' ') + 1);
      auto seen = constants_.find(path);
      if (seen == constants_.end()) {
        constants_[path] = reply.body;
      } else {
        EXPECT_EQ(seen->second, reply.body)
            << path << " differs between tenants or identities";
      }
    }
  }

  void Both(const std::string & label, const char * host, const char * path,
      const std::string & cookie) {
    Check(label + " " + path, GetAt(host, path, cookie));
  }
};

}  // namespace

TEST_F(Scan, Control) {
  Reply reply;
  reply.status = 200;
  reply.body = "<p>Hello</p>";
  reply.headers = "Content-Type: text/html; charset=utf-8\r\nContent-Length: 12\r\n"
                  "Date: Tue, 13 Oct 2026 12:00:00 GMT\r\n"
                  "Set-Cookie: __Host-session=ab-12-cd; Secure; Max-Age=0";
  /* A clean response reads clean, whatever the exempt headers hold. */
  EXPECT_TRUE(Leaks(reply).empty());
  /* An id in the body. */
  Reply body = reply;
  body.body = "<p>Post 42</p>";
  ASSERT_EQ((size_t)1, Leaks(body).size());
  EXPECT_EQ(0u, Leaks(body)[0].find("body:"));
  /* An id in Location, in an ordinary header and in a cookie attribute. */
  Reply loc = reply;
  loc.headers += "\r\nLocation: /admin/post/17";
  ASSERT_EQ((size_t)1, Leaks(loc).size());
  EXPECT_EQ(0u, Leaks(loc)[0].find("header Location:"));
  Reply custom = reply;
  custom.headers += "\r\nX-Row: 9";
  ASSERT_EQ((size_t)1, Leaks(custom).size());
  EXPECT_EQ(0u, Leaks(custom)[0].find("header X-Row:"));
  Reply attr = reply;
  attr.headers += "; Domain=7";
  ASSERT_EQ((size_t)1, Leaks(attr).size());
  EXPECT_EQ(0u, Leaks(attr)[0].find("header Set-Cookie:"));
  /* A stylesheet is not read; the same bytes as a page would be. */
  Reply css = reply;
  css.headers = "Content-Type: text/css";
  css.body = "a{margin:0}";
  EXPECT_TRUE(Leaks(css).empty());
  css.headers = "Content-Type: text/html";
  EXPECT_FALSE(Leaks(css).empty());
  /* The scanner itself: a bare integer, and not a numeric reference or a
   * number inside a word. */
  EXPECT_TRUE(ScanBytes("id 42."));
  EXPECT_FALSE(ScanBytes("&#42;"));
  EXPECT_FALSE(ScanBytes("a42b"));
}

TEST_F(Scan, EveryRoute) {
  Open();
  const struct {
    const char * host;
    const char * alias;
  } tenants[] = {{kHostA, "/alpha-post"}, {kHostB, "/beta-post"}};
  std::string admin[2];
  std::string member[2];
  for (int i = 0; i < 2; i++) {
    admin[i] = CookiesFor(tenants[i].host, "ada", "secret");
    member[i] = CookiesFor(tenants[i].host, "bob", "hunter2");
    ASSERT_FALSE(admin[i].empty());
    ASSERT_FALSE(member[i].empty());
  }
  const char * const get_paths[] = {"/", "/no-such", "/login", "/admin",
      "/admin/post", "/admin/front", "/admin/undo", "/admin/more",
      "/admin/nothing", "/admin/post/saved", "/defiant.css", "/defiant.js",
      "/alive", "/ready", "/logout"};
  /* Interleaved: both tenants, every identity, in turn. */
  for (int round = 0; round < 2; round++) {
    for (int i = 0; i < 2; i++) {
      const char * host = tenants[i].host;
      const std::string identities[3] = {std::string(), member[i], admin[i]};
      const char * names[3] = {"anon", "member", "admin"};
      for (int who = 0; who < 3; who++) {
        std::string label = std::string(host) + " " + names[who];
        Both(label, host, tenants[i].alias, identities[who]);
        for (const char * path : get_paths) {
          Both(label, host, path, identities[who]);
        }
      }
    }
  }
  /* Writes. */
  for (int i = 0; i < 2; i++) {
    const char * host = tenants[i].host;
    std::string label = std::string(host);
    Check(label + " POST /login (refused)", LoginAt(host, "ada", "wrong"));
    Check(label + " POST /login (member)",
        PostAt(host, "/login", std::string(), "username=bob&password=hunter2"));
    Check(label + " POST /login (admin)",
        PostAt(host, "/login", std::string(), "username=ada&password=secret"));
    Check(label + " POST /logout", PostAt(host, "/logout", admin[i], ""));
    Check(label + " POST /admin/post (foreign origin)",
        PostAt(host, "/admin/post", admin[i], "x=1", "http://evil.example"));
    Check(label + " POST /anything (anon)", PostAt(host, "/no-such", std::string(), "x=1"));
    /* Write a post, edit the front page, change a post twice, undo it. */
    std::string form = GetAt(host, "/admin/post", admin[i]).body;
    std::string token = InputValue(form, "token", 0);
    ASSERT_FALSE(token.empty());
    Reply sent = PostAt(host, "/admin/post", admin[i],
        Form(token, "&title=Second+Words&summary=A+summary&body=The+body"));
    Check(label + " POST /admin/post (303)", sent);
    EXPECT_EQ(303, sent.status);
    Check(label + " GET after POST /admin/post",
        GetAt(host, HeaderValue(sent, "Location").c_str(), admin[i]));
    Reply front = GetAt(host, "/admin/front", admin[i]);
    std::string remove = FormToken(front.body, "Remove recent posts");
    ASSERT_FALSE(remove.empty());
    Reply removed = PostAt(host, "/admin/front", admin[i], Form(remove, ""));
    Check(label + " POST /admin/front (303)", removed);
    Check(label + " GET after POST /admin/front",
        GetAt(host, HeaderValue(removed, "Location").c_str(), admin[i]));
    Reply undo = GetAt(host, "/admin/undo", admin[i]);
    Check(label + " GET /admin/undo (after writes)", undo);
    std::string label_undo = "Post Second Words";
    std::string undo_token = UndoToken(undo.body, label_undo.c_str());
    if (!undo_token.empty()) {
      Reply undone = PostAt(host, "/admin/undo", admin[i], Form(undo_token, ""));
      Check(label + " POST /admin/undo", undone);
      Check(label + " GET after POST /admin/undo",
          GetAt(host, HeaderValue(undone, "Location").c_str(), admin[i]));
    }
  }
  /* Responses that name no tenant. */
  Check("unknown host", Ask(Req("GET", "/", "missing.example", NULL, NULL), 5000));
  Check("bad request", Ask("GARBAGE\r\n\r\n", 5000));
  EXPECT_GT(seen_, (size_t)120) << "the route list shrank";
  for (const std::string & leak : found_) {
    ADD_FAILURE() << "internal id in " << leak;
  }
  printf("Scan.EveryRoute: %zu responses read, %zu leaks\n", seen_, found_.size());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
