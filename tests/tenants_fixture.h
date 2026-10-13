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
 * Two tenants on one server, one process and one thread. The tests that use
 * it send requests to both in turn and compare what comes back.
 */

#ifndef GHOTI_IO_GDEFWEB_TEST_TENANTS_FIXTURE_H
#define GHOTI_IO_GDEFWEB_TEST_TENANTS_FIXTURE_H

#include "edge_fixture.h"

namespace {

const char kHostA[] = "alpha.example";
const char kHostB[] = "beta.example";

/* Tenant alpha and tenant beta, each with the kit, one published post and an
 * admin called ada with the same password. A second account, bob, holds no
 * role. The two share nothing but the process. */
class Pair : public Edge {
 protected:
  char a_[GDEFCORE_PUBLIC_ID_LEN + 1];
  char b_[GDEFCORE_PUBLIC_ID_LEN + 1];

  void Open(const char * proxies = NULL) {
    Listen(proxies);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_tenant_create(inst_, kHostA, strlen(kHostA), "", 0, a_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_tenant_create(inst_, kHostB, strlen(kHostB), "", 0, b_));
    InstallKit(a_);
    InstallKit(b_);
    Publish(a_, "alpha-post", "Alpha Title");
    Publish(b_, "beta-post", "Beta Title");
    AddUser(a_, "bob", "hunter2");
    AddUser(b_, "bob", "hunter2");
  }

  static std::string Origin(const char * host) {
    return std::string("http://") + host;
  }

  /* One request to @p host. @p origin defaults to the host's own. */
  Reply To(const char * host, const char * method, const char * path,
      const std::string & cookie, const char * body = NULL,
      const char * origin = NULL) {
    std::string extra;
    if (body != NULL) {
      extra += "Origin: " + (origin != NULL ? std::string(origin) : Origin(host)) +
          "\r\nContent-Type: application/x-www-form-urlencoded\r\n";
    }
    extra += cookie;
    return Ask(Req(method, path, host, extra.c_str(), body), 20000);
  }

  Reply GetAt(const char * host, const char * path, const std::string & cookie) {
    return To(host, "GET", path, cookie);
  }

  Reply PostAt(const char * host, const char * path, const std::string & cookie,
      const std::string & body, const char * origin = NULL) {
    return To(host, "POST", path, cookie, body.c_str(), origin);
  }

  /* A browser follows a 303 with a GET of its Location, to the same host. */
  Reply PostFollow(const char * host, const char * path, const std::string & cookie,
      const std::string & body, const char * origin = NULL) {
    Reply sent = PostAt(host, path, cookie, body, origin);
    if (sent.status != 303) {
      return sent;
    }
    return GetAt(host, HeaderValue(sent, "Location").c_str(), cookie);
  }

  Reply LoginAt(const char * host, const char * user, const char * pass) {
    return PostAt(host, "/login", std::string(),
        (std::string("username=") + user + "&password=" + pass).c_str());
  }

  /* The Cookie header for a login at @p host, or "" when it did not succeed. */
  std::string CookiesFor(const char * host, const char * user, const char * pass) {
    Reply login = LoginAt(host, user, pass);
    if (login.status != 303) {
      return std::string();
    }
    return CookieLine(login);
  }
};

}  // namespace

#endif /* GHOTI_IO_GDEFWEB_TEST_TENANTS_FIXTURE_H */
