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
 * The files a module ships, served through the real server: `GET
 * /assets/<owner>/<name>` answers with the type the module declared, the bytes
 * it declared, a hash address that caches for a year and a bare one that does
 * not, only while the module is enabled for the tenant; and a public page
 * carries a policy that lets it load them, where an admin page keeps its own.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/asset.h>

#include <map>

namespace {

#define ASSETS_STR2(x) #x
#define ASSETS_STR(x) ASSETS_STR2(x)
const char kToken[] = ASSETS_STR(GHOTIIO_DEFCORE_NAME);

const char kCss[] = "body { color: #123; }\n";
const char kJs[] = "document.title = 'x';\n";
const char kSvg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect/></svg>";
/* Not text: a NUL, a byte that is not UTF-8, and the PNG signature. */
const char kPng[] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', '\0', '\xff',
    '\xfe', '\x00', 'Z'};
const char kOtherCss[] = "p { margin: 0; }\n";

struct Module {
  std::string name;
  std::vector<GDEFCORE_Module_Asset> assets;
  std::vector<std::string> keep;
  GDEFCORE_Module_Manifest man;

  void Add(const char * file, int kind, const char * type, const void * bytes,
      size_t n) {
    GDEFCORE_Module_Asset a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.name = file;
    a.name_len = strlen(file);
    a.kind = kind;
    a.media_type = type;
    a.media_type_len = type == NULL ? 0 : strlen(type);
    a.bytes = (const uint8_t *)bytes;
    a.bytes_len = n;
    assets.push_back(a);
  }

  const GDEFCORE_Module_Manifest * Build() {
    memset(&man, 0, sizeof man);
    man.size = sizeof man;
    man.core_token = kToken;
    man.core_token_len = strlen(kToken);
    man.name = name.c_str();
    man.name_len = name.size();
    man.schema_version = 1;
    man.assets = assets.data();
    man.nassets = assets.size();
    return &man;
  }
};

Module g_art;
Module g_shop;

extern "C" const GDEFCORE_Module_Manifest * ArtEntry(void) { return g_art.Build(); }
extern "C" const GDEFCORE_Module_Manifest * ShopEntry(void) { return g_shop.Build(); }

class Assets : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];

  void Open() {
    Site(id_);
    UseHost();
    g_art = Module();
    g_art.name = "art";
    g_art.Add("style.css", GDEFCORE_ASSET_CSS, NULL, kCss, sizeof kCss - 1);
    g_art.Add("app.js", GDEFCORE_ASSET_JS, NULL, kJs, sizeof kJs - 1);
    g_art.Add("logo.svg", GDEFCORE_ASSET_IMAGE, "image/svg+xml", kSvg,
        sizeof kSvg - 1);
    g_art.Add("pixel.png", GDEFCORE_ASSET_IMAGE, "image/png", kPng, sizeof kPng);
    /* A stylesheet whose name says it is a picture. */
    g_art.Add("worker.png.css", GDEFCORE_ASSET_CSS, NULL, kCss, sizeof kCss - 1);
    g_shop = Module();
    g_shop.name = "shop";
    g_shop.Add("style.css", GDEFCORE_ASSET_CSS, NULL, kOtherCss,
        sizeof kOtherCss - 1);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, ArtEntry));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, ShopEntry));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  void Toggle(const char * name, bool on) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    if (on) {
      ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, name, strlen(name)));
    } else {
      ASSERT_EQ(GDEFCORE_OK, gdefcore_module_disable(host_, cs, name, strlen(name)));
    }
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, name, strlen(name)));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  /* The hash the library gives a file, so a test never computes one itself. */
  std::string HashOf(const char * ref) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Asset found;
    memset(&found, 0, sizeof found);
    found.size = sizeof found;
    std::string hash;
    if (gdefcore_asset_find(catalog, NULL, ref, strlen(ref), &found) ==
        GDEFCORE_OK) {
      hash = found.hash;
    }
    gdefcore_catalog_destroy(catalog);
    return hash;
  }

  Reply GetWith(const char * path, const char * extra) {
    return Ask(Req("GET", path, "site.example", extra, NULL), 20000);
  }
};

}  // namespace

TEST_F(Assets, AShippedFileIsServedWithItsDeclaredTypeAndExactBytes) {
  Open();
  Toggle("art", true);
  Reply css = Get("/assets/art/style.css", std::string());
  EXPECT_EQ(200, css.status);
  EXPECT_EQ(std::string(kCss), css.body);
  EXPECT_EQ("text/css; charset=utf-8", HeaderValue(css, "Content-Type"));
  EXPECT_EQ("nosniff", HeaderValue(css, "X-Content-Type-Options"));
  Reply js = Get("/assets/art/app.js", std::string());
  EXPECT_EQ(std::string(kJs), js.body);
  EXPECT_EQ("text/javascript; charset=utf-8", HeaderValue(js, "Content-Type"));
  /* Binary, whole: the NUL and the bytes that are not UTF-8 arrive. */
  Reply png = Get("/assets/art/pixel.png", std::string());
  EXPECT_EQ(200, png.status);
  EXPECT_EQ(std::string(kPng, sizeof kPng), png.body);
  EXPECT_EQ("image/png", HeaderValue(png, "Content-Type"));
  EXPECT_EQ(std::to_string(sizeof kPng), HeaderValue(png, "Content-Length"));
  /* The type is the declared kind's, whatever the name says. */
  Reply odd = Get("/assets/art/worker.png.css", std::string());
  EXPECT_EQ("text/css; charset=utf-8", HeaderValue(odd, "Content-Type"));
  /* A picture that can carry script is served with a sandbox. */
  Reply svg = Get("/assets/art/logo.svg", std::string());
  EXPECT_EQ("image/svg+xml", HeaderValue(svg, "Content-Type"));
  EXPECT_TRUE(Has(HeaderValue(svg, "Content-Security-Policy"), "sandbox"));
  EXPECT_FALSE(Has(HeaderValue(css, "Content-Security-Policy"), "unsafe"));
}

TEST_F(Assets, TheHashAddressCachesForAYearAndTheBareOneRevalidates) {
  Open();
  Toggle("art", true);
  std::string hash = HashOf("art/style.css");
  ASSERT_EQ((size_t)GDEFCORE_ASSET_HASH_LEN, hash.size());
  Reply hashed = Get(("/assets/art/style.css?v=" + hash).c_str(), std::string());
  EXPECT_EQ(200, hashed.status);
  EXPECT_EQ("public, max-age=31536000, immutable",
      HeaderValue(hashed, "Cache-Control"));
  EXPECT_EQ("\"" + hash + "\"", HeaderValue(hashed, "ETag"));
  /* CONTROL: no hash, an old one, and a different file's hash are not the
   * address of this text, so none of them is cached for a year. */
  const char * not_it[] = {"/assets/art/style.css",
      "/assets/art/style.css?v=00000000000000000000000000000000",
      "/assets/art/style.css?v=", "/assets/art/style.css?x=1",
      "/assets/art/style.css?v=abc"};
  for (const char * path : not_it) {
    Reply r = Get(path, std::string());
    EXPECT_EQ(200, r.status) << path;
    EXPECT_EQ("no-cache", HeaderValue(r, "Cache-Control")) << path;
    EXPECT_EQ("\"" + hash + "\"", HeaderValue(r, "ETag")) << path;
  }
  /* The hash may be one of several parameters. */
  Reply many = Get(("/assets/art/style.css?a=1&v=" + hash + "&b=2").c_str(),
      std::string());
  EXPECT_EQ("public, max-age=31536000, immutable", HeaderValue(many, "Cache-Control"));
}

TEST_F(Assets, AMatchingEtagIsAnsweredNotModifiedWithNoBody) {
  Open();
  Toggle("art", true);
  std::string hash = HashOf("art/style.css");
  std::string tag = "\"" + hash + "\"";
  const std::string same[] = {"If-None-Match: " + tag, "If-None-Match: W/" + tag,
      "If-None-Match: \"zzz\", " + tag, "If-None-Match: *"};
  for (const std::string & header : same) {
    Reply r = GetWith("/assets/art/style.css", header.c_str());
    EXPECT_EQ(304, r.status) << header;
    EXPECT_EQ(std::string(), r.body) << header;
    EXPECT_EQ(tag, HeaderValue(r, "ETag")) << header;
  }
  /* CONTROL: another tag, or none, is the whole file. */
  Reply other = GetWith("/assets/art/style.css", "If-None-Match: \"nope\"");
  EXPECT_EQ(200, other.status);
  EXPECT_EQ(std::string(kCss), other.body);
  /* The tag of one file does not stand for another. */
  Reply wrong = GetWith("/assets/art/app.js", ("If-None-Match: " + tag).c_str());
  EXPECT_EQ(200, wrong.status);
}

TEST_F(Assets, TwoOwnersSameNamedFilesAreSeparateAndADisabledModuleServesNothing) {
  Open();
  /* Neither module is enabled for the tenant yet. */
  EXPECT_EQ(404, Get("/assets/art/style.css", std::string()).status);
  EXPECT_EQ(404, Get("/assets/shop/style.css", std::string()).status);
  Toggle("art", true);
  Toggle("shop", true);
  Reply a = Get("/assets/art/style.css", std::string());
  Reply s = Get("/assets/shop/style.css", std::string());
  EXPECT_EQ(std::string(kCss), a.body);
  EXPECT_EQ(std::string(kOtherCss), s.body);
  EXPECT_NE(HeaderValue(a, "ETag"), HeaderValue(s, "ETag"));
  /* Disabling one stops its files and only its files, with no other step. */
  Toggle("art", false);
  EXPECT_EQ(404, Get("/assets/art/style.css", std::string()).status);
  EXPECT_EQ(404, Get("/assets/art/pixel.png", std::string()).status);
  EXPECT_EQ(std::string(kOtherCss), Get("/assets/shop/style.css", std::string()).body);
  Toggle("art", true);
  EXPECT_EQ(200, Get("/assets/art/style.css", std::string()).status);
}

TEST_F(Assets, ANameThatIsNotAnAssetIsTheFixedNotFoundAndNeverReachesALookup) {
  Open();
  Toggle("art", true);
  const char * bad[] = {"/assets/", "/assets/art", "/assets/art/", "/assets//style.css",
      "/assets/art/nope.css", "/assets/nobody/style.css",
      "/assets/art/style.css/", "/assets/art/../art/style.css",
      "/assets/../art/style.css", "/assets/art/..%2fstyle.css",
      "/assets/art/%2e%2e/style.css", "/assets/art%2fstyle.css",
      "/assets/art/style%2ecss", "/assets/art\\style.css",
      "/assets/art/sub/style.css", "/assets/ART/style.css",
      "/assets/art/STYLE.CSS", "/assets/art/style.css%00.png",
      "/assets/art/style.css%00", "/assetsx/art/style.css"};
  for (const char * path : bad) {
    Reply r = Get(path, std::string());
    EXPECT_EQ(404, r.status) << path;
    EXPECT_FALSE(Has(r.body, "color")) << path;
  }
  Reply miss = Get("/assets/art/nope.css", std::string());
  EXPECT_EQ("Not found\n", miss.body);
  /* An unknown host is the same 404, whatever it asks for. */
  Reply other = Ask(Req("GET", "/assets/art/style.css", "unknown.example", NULL, NULL),
      5000);
  EXPECT_EQ(404, other.status);
  EXPECT_EQ("Not found\n", other.body);
  /* A write method is not an asset request. */
  Reply post = PostRaw("/assets/art/style.css", std::string(), "x=1");
  EXPECT_NE(200, post.status);
}

TEST_F(Assets, APublicPageMayLoadTheSitesFilesAndAnAdminPageKeepsItsOwnPolicy) {
  Open();
  Toggle("art", true);
  Reply pub = Get("/", std::string());
  std::string csp = HeaderValue(pub, "Content-Security-Policy");
  EXPECT_TRUE(Has(csp, "style-src 'self'")) << csp;
  EXPECT_TRUE(Has(csp, "script-src 'self'")) << csp;
  EXPECT_TRUE(Has(csp, "img-src 'self'")) << csp;
  EXPECT_TRUE(Has(csp, "font-src 'self'")) << csp;
  EXPECT_TRUE(Has(csp, "default-src 'none'")) << csp;
  EXPECT_TRUE(Has(csp, "frame-ancestors 'none'")) << csp;
  EXPECT_FALSE(Has(csp, "unsafe")) << csp;
  EXPECT_FALSE(Has(csp, "http")) << csp;
  EXPECT_FALSE(Has(csp, "*")) << csp;
  /* The admin's is the one that has no 'self' for a script. */
  Reply login = Get("/login", std::string());
  std::string admin = HeaderValue(login, "Content-Security-Policy");
  EXPECT_FALSE(Has(admin, "script-src 'self'")) << admin;
  EXPECT_TRUE(Has(admin, "script-src 'sha256-")) << admin;
  EXPECT_FALSE(Has(admin, "img-src")) << admin;
  EXPECT_EQ(admin, HeaderValue(Get("/defiant.js", std::string()),
                       "Content-Security-Policy"));
  EXPECT_NE(admin, csp);
  /* A signed-in editor's admin page is the admin's. */
  std::string cookie = CookieLine(Login("ada", "secret"));
  Reply page = Get("/admin", cookie);
  EXPECT_EQ(admin, HeaderValue(page, "Content-Security-Policy"));
  EXPECT_EQ("no-store", HeaderValue(page, "Cache-Control"));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
