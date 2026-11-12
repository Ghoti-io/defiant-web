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
 * The content administration (web content admin story 1), through the real
 * server. The types here are defined by the test, at run time, and are named
 * nowhere in the library: the pages are made from what a type declares, and
 * what a person may do on them is what the core's access answers say.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/upload.h>
#include <ghoti.io/defiant-web/uploads.h>

#include <gtest/gtest-spi.h>

#include <filesystem>

namespace {

namespace fs = std::filesystem;

const char kPng[] = "\x89PNG-not-really-an-image";

class Uploads : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;
  fs::path dir_;
  GDEFWEB_Blob_Dir * blobs_ = NULL;
  GDEFCORE_Upload_Policy policy_;
  const char * media_[3] = {"image/png", "text/html", "image/jpeg"};

  void TearDown() override {
    gdefweb_uploads_bind(NULL, NULL);
    gdefweb_blob_dir_close(blobs_);
    std::error_code ec;
    if (!dir_.empty()) {
      fs::remove_all(dir_, ec);
    }
    Edge::TearDown();
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    return catalog;
  }

  /* The site, the file type, the directory store and the policy, and the
   * administrator's grants of the two file permissions. */
  void Open(bool bind = true) {
    Site(id_);
    GDEFCORE_Catalog * catalog = Catalog();
    EXPECT_EQ(GDEFCORE_OK, gdefcore_upload_declare(catalog));
    for (const char * label : {"Upload files", "See the files you uploaded"}) {
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, "admin", 5, label, strlen(label), "core.role", 9));
    }
    /* A type the library has never heard of, with one field a file is named in. */
    GDEFCORE_Field album[2];
    memset(album, 0, sizeof album);
    album[0].size = sizeof album[0];
    album[0].name = "title";
    album[0].name_len = 5;
    album[0].required = 1;
    album[1].size = sizeof album[1];
    album[1].name = "cover";
    album[1].name_len = 5;
    album[1].type = GDEFSTOR_FIELD_REFERENCE;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_type_define(catalog, "site.album", 10, GDEFCORE_TYPE_CONTENT, album, 2));
    for (const char * action : {"create", "read", "update", "delete"}) {
      const std::string label = std::string(action) + " site.album";
      GDEFCORE_Permission perm;
      memset(&perm, 0, sizeof perm);
      perm.size = sizeof perm;
      perm.label = label.c_str();
      perm.label_len = label.size();
      perm.module_name = "site";
      perm.module_name_len = 4;
      perm.action = action;
      perm.action_len = strlen(action);
      perm.condition = GDEFCORE_ACCESS_ANY;
      perm.type_name = "site.album";
      perm.type_name_len = 10;
      EXPECT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog, &perm));
      EXPECT_EQ(GDEFCORE_OK, gdefcore_access_grant(catalog, "admin", 5, label.c_str(), label.size(), "core.role", 9));
    }
    gdefcore_catalog_destroy(catalog);
    char tmpl[] = "/tmp/gdefweb-uploads-XXXXXX";
    ASSERT_NE((char *)NULL, mkdtemp(tmpl));
    dir_ = tmpl;
    ASSERT_EQ(GDEFWEB_OK, gdefweb_blob_dir_open(tmpl, &blobs_));
    memset(&policy_, 0, sizeof policy_);
    policy_.size = sizeof policy_;
    policy_.max_bytes = 2000;
    policy_.media = media_;
    policy_.media_count = 3;
    policy_.ttl_ms = 3600 * 1000;
    if (bind) {
      ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &policy_));
    }
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* A signed-in person who may use the admin area and upload, or not upload. */
  std::string Person(const char * name, bool may_upload, bool area = true) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Keyring * keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    const std::string role = std::string("role-") + name;
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, role.c_str(), role.size(), NULL));
    std::vector<std::string> labels;
    if (area) {
      labels.push_back(GDEFCORE_ADMIN_USE_LABEL);
    }
    if (may_upload) {
      labels.push_back("Upload files");
      labels.push_back("See the files you uploaded");
    }
    for (const std::string & label : labels) {
      EXPECT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, role.c_str(), role.size(), label.c_str(), label.size(),
              "core.role", 9));
    }
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_principal_add_role(catalog, principal, strlen(principal), role.c_str(),
            role.size()));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal, strlen(principal),
            name, strlen(name), "pw-pw-pw", 8, account));
    gdefcore_catalog_destroy(catalog);
    return CookieLine(Login(name, "pw-pw-pw"));
  }

  size_t StoredFiles() {
    size_t n = 0;
    std::error_code ec;
    for (auto & e : fs::recursive_directory_iterator(dir_, ec)) {
      n += e.is_regular_file() ? 1 : 0;
    }
    return n;
  }

  std::string Token(const std::string & cookie) {
    Reply page = Get("/admin/files", cookie);
    return FormToken(page.body, "type=\"file\"");
  }

  static std::string Part(const std::string & name, const std::string & filename,
      const std::string & type, const std::string & data, const char * boundary = "BOUNDARY") {
    std::string out = std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"" +
                      name + "\"";
    if (!filename.empty()) {
      out += "; filename=\"" + filename + "\"";
    }
    out += "\r\n";
    if (!type.empty()) {
      out += "Content-Type: " + type + "\r\n";
    }
    return out + "\r\n" + data + "\r\n";
  }

  Reply PostMultipart(const std::string & body, const std::string & cookie,
      const char * boundary = "BOUNDARY", const char * path = "/admin/files") {
    return Ask(Req("POST", path, "site.example",
                   (std::string("Origin: http://site.example\r\n"
                                "Content-Type: multipart/form-data; boundary=") +
                       boundary + "\r\n" + cookie)
                       .c_str(),
                   body.c_str()),
        20000);
  }

  Reply Upload(const std::string & cookie, const std::string & filename, const std::string & type,
      const std::string & data) {
    const std::string token = Token(cookie);
    EXPECT_FALSE(token.empty());
    return PostMultipart(Part("token", "", "", token) + Part("file", filename, type, data) +
                             "--BOUNDARY--\r\n",
        cookie);
  }

  /* The id of the first file the page lists. */
  std::string FirstId(const std::string & cookie) {
    Reply page = Get("/admin/files", cookie);
    size_t at = page.body.find("<code>");
    if (at == std::string::npos) {
      return std::string();
    }
    return page.body.substr(at + 6, GDEFCORE_PUBLIC_ID_LEN);
  }
};

}  // namespace

TEST_F(Uploads, AFileIsPostedByAFormListedAndGivenBackToItsUploaderAlone) {
  Open();
  Reply posted = Upload(ada_, "photo<1>.png", "image/png", kPng);
  EXPECT_EQ(303, posted.status) << posted.body;
  EXPECT_EQ(1u, StoredFiles()) << Get("/admin/files", ada_).body;
  Reply list = Get("/admin/files", ada_);
  EXPECT_NE(std::string::npos, list.body.find("photo&lt;1&gt;.png")) << list.body;
  EXPECT_EQ(std::string::npos, list.body.find("photo<1>.png"));
  const std::string id = FirstId(ada_);
  ASSERT_EQ((size_t)GDEFCORE_PUBLIC_ID_LEN, id.size());
  /* The uploader is given the bytes, as data nothing draws or runs as the site. */
  Reply got = Get(("/files/" + id + "/photo.png").c_str(), ada_);
  EXPECT_EQ(200, got.status);
  EXPECT_EQ(kPng, got.body);
  EXPECT_EQ("image/png", HeaderValue(got, "Content-Type"));
  EXPECT_EQ("nosniff", HeaderValue(got, "X-Content-Type-Options"));
  EXPECT_EQ("default-src 'none'; sandbox", HeaderValue(got, "Content-Security-Policy"));
  EXPECT_NE(std::string::npos, HeaderValue(got, "Content-Disposition").find("inline"));
  /* The file name in the header is made of safe characters, and its UTF-8 form
   * is percent-encoded. */
  EXPECT_NE(std::string::npos, HeaderValue(got, "Content-Disposition").find("filename=\"photo_1_.png\""));
  EXPECT_NE(std::string::npos, HeaderValue(got, "Content-Disposition").find("filename*=UTF-8''photo%3C1%3E.png"));
  /* Nobody else, signed in or not, is given a staged file, and is told what a
   * file that is not there is told. */
  std::string other = Person("grace", true);
  EXPECT_EQ(404, Get(("/files/" + id).c_str(), other).status);
  EXPECT_EQ(404, Get(("/files/" + id).c_str(), "").status);
  EXPECT_EQ(404, Get("/files/00000000000000000000", ada_).status);
  EXPECT_EQ(404, Get("/files/short", ada_).status);
}

TEST_F(Uploads, AnActiveTypeIsNeverServedAsItselfFromTheSitesOwnAddress) {
  Open();
  EXPECT_EQ(303, Upload(ada_, "page.html", "text/html", "<script>alert(1)</script>").status);
  const std::string id = FirstId(ada_);
  ASSERT_FALSE(id.empty());
  Reply got = Get(("/files/" + id + "/page.html").c_str(), ada_);
  EXPECT_EQ(200, got.status);
  EXPECT_EQ("application/octet-stream", HeaderValue(got, "Content-Type"));
  EXPECT_NE(std::string::npos, HeaderValue(got, "Content-Disposition").find("attachment"));
  EXPECT_EQ("nosniff", HeaderValue(got, "X-Content-Type-Options"));
}

TEST_F(Uploads, AFileThePolicyRefusesIsNeverStored) {
  Open();
  /* Over the size, a type the policy does not list, and names that are not names. */
  EXPECT_EQ(303, Upload(ada_, "big.png", "image/png", std::string(3000, 'x')).status);
  EXPECT_EQ(303, Upload(ada_, "a.exe", "application/x-msdownload", "MZ").status);
  EXPECT_EQ(303, Upload(ada_, "../../etc/passwd", "image/png", "x").status);
  EXPECT_EQ(303, Upload(ada_, "a.png", "", "x").status);
  EXPECT_EQ(303, Upload(ada_, "a.png", "image/png", "").status);
  EXPECT_EQ(0u, StoredFiles());
  EXPECT_EQ(std::string::npos, Get("/admin/files", ada_).body.find("<code>"));
  /* A control: a file that fits is kept. */
  EXPECT_EQ(303, Upload(ada_, "ok.png", "image/png", "x").status);
  EXPECT_EQ(1u, StoredFiles());
}

TEST_F(Uploads, ABodyThatIsNotAWellFormedMultipartIsReadAsNothing) {
  Open();
  const std::string token = Token(ada_);
  const std::string tok = Part("token", "", "", token);
  const std::string file = Part("file", "a.png", "image/png", "x");
  const std::string close = "--BOUNDARY--\r\n";
  std::vector<std::pair<std::string, std::string>> bad = {
      {"no closing boundary", tok + file},
      {"text before the first boundary", "preamble\r\n" + tok + file + close},
      {"a part with no name", tok + file + "--BOUNDARY\r\nContent-Disposition: form-data; filename=\"a\"\r\n\r\nx\r\n" + close},
      {"a backslash in a name", tok + file + "--BOUNDARY\r\nContent-Disposition: form-data; name=\"fi\\le\"\r\n\r\nx\r\n" + close},
      {"two files", tok + file + file + close},
      {"two tokens", tok + tok + file + close},
      {"a part head that never ends", tok + file + "--BOUNDARY\r\nContent-Disposition: form-data; name=\"x\"\r\n"},
      {"a head over its cap", tok + file + "--BOUNDARY\r\nContent-Disposition: form-data; name=\"x\"\r\nX-Pad: " + std::string(2000, 'p') + "\r\n\r\nx\r\n" + close},
      {"too many parts", tok + file + Part("a", "", "", "1") + Part("b", "", "", "1") + Part("c", "", "", "1") + Part("d", "", "", "1") + Part("e", "", "", "1") + Part("f", "", "", "1") + Part("g", "", "", "1") + close},
      {"a file name over its cap", tok + file + Part("x", std::string(300, 'n'), "image/png", "x") + close},
      {"data after the closing boundary", tok + file + close + "trailing"},
      {"a part that is not form-data", tok + file + "--BOUNDARY\r\nContent-Disposition: attachment; name=\"x\"\r\n\r\nx\r\n" + close},
  };
  for (const auto & b : bad) {
    Reply r = PostMultipart(b.second, ada_);
    EXPECT_EQ(303, r.status) << b.first;
    EXPECT_EQ(0u, StoredFiles()) << b.first;
  }
  /* Boundaries that are not RFC 2046 boundaries are refused whatever follows,
   * even when the body uses them consistently. */
  for (const std::string & boundary : std::vector<std::string>{"has space", "", std::string(71, 'b')}) {
    const std::string body = Part("token", "", "", token, boundary.c_str()) +
                             Part("file", "a.png", "image/png", "x", boundary.c_str()) +
                             "--" + boundary + "--\r\n";
    Reply r = PostMultipart(body, ada_, boundary.c_str());
    (void)r;
    EXPECT_EQ(0u, StoredFiles()) << "[" << boundary << "]";
  }
  /* The control: the same parts, well formed, are taken. */
  EXPECT_EQ(303, PostMultipart(tok + file + close, ada_).status);
  EXPECT_EQ(1u, StoredFiles());
}

TEST_F(Uploads, OnlyAFilePostedToItsOwnPageMayCarryMoreThanAFormDoes) {
  Open();
  policy_.max_bytes = 200000;
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &policy_));
  /* A file well over the 64 KiB every other body is held to is taken... */
  const std::string big(150000, 'x');
  EXPECT_EQ(303, Upload(ada_, "big.png", "image/png", big).status);
  EXPECT_EQ(1u, StoredFiles());
  const std::string id = FirstId(ada_);
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(big, Get(("/files/" + id).c_str(), ada_).body);
  /* ...and a body of that size anywhere else is not held: the server closes on it. */
  Reply form;
  EXPECT_NONFATAL_FAILURE(form = PostRaw("/admin/content", ada_, "token=x&pad=" + big), "ex.fail");
  EXPECT_NE(303, form.status);
  /* The file's own page does not take more than the policy and a form's room. */
  const std::string token = Token(ada_);
  Reply huge;
  EXPECT_NONFATAL_FAILURE(
      huge = PostMultipart(Part("token", "", "", token) +
                               Part("file", "huge.png", "image/png", std::string(230000, 'x')) +
                               "--BOUNDARY--\r\n",
          ada_),
      "ex.fail");
  EXPECT_NE(303, huge.status);
  EXPECT_EQ(1u, StoredFiles());
}

TEST_F(Uploads, AFormWithNoTokenOrAnotherPersonsTokenStoresNothing) {
  Open();
  std::string other = Person("grace", true);
  const std::string mine = Token(ada_);
  const std::string hers = Token(other);
  ASSERT_FALSE(mine.empty());
  ASSERT_FALSE(hers.empty());
  const std::string file = Part("file", "a.png", "image/png", "x");
  EXPECT_EQ(303, PostMultipart(file + "--BOUNDARY--\r\n", ada_).status);
  EXPECT_EQ(303, PostMultipart(Part("token", "", "", "not-a-token") + file + "--BOUNDARY--\r\n", ada_).status);
  /* Her token is for her session. */
  EXPECT_EQ(303, PostMultipart(Part("token", "", "", hers) + file + "--BOUNDARY--\r\n", ada_).status);
  /* A token of another form (the content form's) is not an upload's. */
  Reply content = Get("/admin/content", ada_);
  EXPECT_EQ(0u, StoredFiles());
  (void)content;
  /* A cross-origin post is refused before any of this. */
  Reply cross = Ask(Req("POST", "/admin/files", "site.example",
                        (std::string("Origin: http://evil.example\r\n"
                                     "Content-Type: multipart/form-data; boundary=BOUNDARY\r\n") + ada_).c_str(),
                        (Part("token", "", "", mine) + file + "--BOUNDARY--\r\n").c_str()),
      20000);
  EXPECT_NE(303, cross.status);
  EXPECT_EQ(0u, StoredFiles());
  EXPECT_EQ(303, PostMultipart(Part("token", "", "", mine) + file + "--BOUNDARY--\r\n", ada_).status);
  EXPECT_EQ(1u, StoredFiles());
}

TEST_F(Uploads, APersonWhoMayNotUploadIsToldWhatAPageThatIsNotThereIsTold) {
  Open();
  std::string reader = Person("reader", false);
  const std::string token = Token(reader);
  /* Someone with no right to the admin area is told what a page that is not
   * there is told, and their post is nothing either. */
  std::string outsider = Person("outsider", true, false);
  EXPECT_EQ(std::string::npos, Get("/admin/files", outsider).body.find("type=\"file\""));
  EXPECT_EQ(404, PostMultipart(Part("token", "", "", "x") + Part("file", "a.png", "image/png", "x") + "--BOUNDARY--\r\n", outsider).status);
  EXPECT_EQ(0u, StoredFiles());
  /* The page is there for the area; the post is refused by the core. */
  Reply r = PostMultipart(Part("token", "", "", token) + Part("file", "a.png", "image/png", "x") + "--BOUNDARY--\r\n", reader);
  EXPECT_EQ(303, r.status);
  EXPECT_EQ(0u, StoredFiles());
}

TEST_F(Uploads, AnEditorAttachesAFileFromTheContentFormAndTheReferenceKeepsItPastItsExpiry) {
  Open();
  EXPECT_EQ(303, Upload(ada_, "cover.png", "image/png", "cover bytes").status);
  EXPECT_EQ(303, Upload(ada_, "stray.png", "image/png", "stray bytes").status);
  EXPECT_EQ(2u, StoredFiles());
  /* The content form of a type that never heard of files offers them by name. */
  Reply list = Get("/admin/content/site.album", ada_);
  Reply made = PostRaw("/admin/content/site.album", ada_,
      "token=" + FormToken(list.body, "value=\"new\"") + "&do=new");
  ASSERT_EQ(303, made.status) << list.body;
  const std::string path = HeaderValue(made, "Location");
  Reply form = Get(path.c_str(), ada_);
  EXPECT_NE(std::string::npos, form.body.find("cover.png")) << form.body;
  EXPECT_NE(std::string::npos, form.body.find("stray.png"));
  /* The id of the cover, as the form offers it. */
  size_t at = form.body.find("cover.png");
  size_t value = form.body.rfind("value=\"", at);
  ASSERT_NE(std::string::npos, value);
  const std::string cover = form.body.substr(value + 7, GDEFCORE_PUBLIC_ID_LEN);
  Reply saved = PostRaw(path.c_str(), ada_,
      "token=" + FormToken(form.body, "value=\"save\"") + "&do=save&title=Holiday&cover=" + cover);
  EXPECT_EQ(303, saved.status) << saved.body;
  /* A week later, the sweep keeps the file the entity names and removes the other. */
  GDEFCORE_Catalog * catalog = Catalog();
  size_t promoted = 0, demoted = 0, removed = 0;
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_sweep(catalog,
      (uint64_t)time(NULL) * 1000 + 7ull * 24 * 3600 * 1000, &promoted, &demoted, &removed));
  /* The sweep is the host's one call, and it needs uploads to be bound. */
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(NULL, NULL));
  EXPECT_EQ(GDEFWEB_ERR_INVALID, gdefweb_uploads_sweep(catalog, 0, NULL, NULL, NULL));
  EXPECT_EQ(GDEFWEB_ERR_INVALID, gdefweb_uploads_sweep(NULL, 0, NULL, NULL, NULL));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &policy_));
  gdefcore_catalog_destroy(catalog);
  EXPECT_EQ(1u, promoted);
  EXPECT_EQ(1u, removed);
  EXPECT_EQ(1u, StoredFiles());
  EXPECT_EQ("cover bytes", Get(("/files/" + cover).c_str(), ada_).body);
  EXPECT_EQ(std::string::npos, Get("/admin/files", ada_).body.find("stray.png"));
}

TEST_F(Uploads, UploadsAreOffUntilTheyAreBoundAndStaySoWhenUnbound) {
  Open(false);
  Reply page = Get("/admin/files", ada_);
  EXPECT_NE(std::string::npos, page.body.find("not turned on"));
  EXPECT_EQ(std::string::npos, page.body.find("type=\"file\""));
  Reply r = PostMultipart(Part("token", "", "", "x") + Part("file", "a.png", "image/png", "x") + "--BOUNDARY--\r\n", ada_);
  EXPECT_EQ(404, r.status);
  EXPECT_EQ(0u, StoredFiles());
  EXPECT_EQ(404, Get("/files/00000000000000000000", ada_).status);
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &policy_));
  EXPECT_NE(std::string::npos, Get("/admin/files", ada_).body.find("type=\"file\""));
  ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(NULL, NULL));
  EXPECT_NE(std::string::npos, Get("/admin/files", ada_).body.find("not turned on"));
  /* A store or a policy of the wrong size is not bound. */
  GDEFCORE_Upload_Policy bad = policy_;
  bad.size = sizeof bad + 1;
  EXPECT_EQ(GDEFWEB_ERR_INVALID, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &bad));
  EXPECT_EQ(GDEFWEB_ERR_INVALID, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), NULL));
}

TEST_F(Uploads, TheDirectoryStoreKeepsATenantsBytesApartAndNeverLeavesItsRoot) {
  Open();
  const GDEFCORE_Blob_Store * s = gdefweb_blob_dir_store(blobs_);
  const std::string good(64, 'a');
  const uint8_t t1[2] = {1, 2};
  const uint8_t t2[2] = {1, 3};
  uint8_t buf[16];
  size_t n = 0;
  EXPECT_EQ(0, s->put(s->ctx, t1, 2, good.c_str(), (const uint8_t *)"one", 3));
  EXPECT_EQ(0, s->put(s->ctx, t2, 2, good.c_str(), (const uint8_t *)"two", 3));
  ASSERT_EQ(0, s->get(s->ctx, t1, 2, good.c_str(), buf, sizeof buf, &n));
  EXPECT_EQ("one", std::string((char *)buf, n));
  ASSERT_EQ(0, s->get(s->ctx, t2, 2, good.c_str(), buf, sizeof buf, &n));
  EXPECT_EQ("two", std::string((char *)buf, n));
  /* A buffer too small is a refusal and never a truncation. */
  EXPECT_NE(0, s->get(s->ctx, t1, 2, good.c_str(), buf, 2, &n));
  /* A digest that is not 64 lowercase hex characters names no path at all. */
  for (const std::string & bad : std::vector<std::string>{std::string("../../../etc/passwd"), std::string(64, 'A'),
           std::string(63, 'a'), std::string(65, 'a'), std::string("aa/") + std::string(61, 'a'),
           std::string()}) {
    EXPECT_NE(0, s->put(s->ctx, t1, 2, bad.c_str(), (const uint8_t *)"x", 1)) << bad;
    EXPECT_NE(0, s->get(s->ctx, t1, 2, bad.c_str(), buf, sizeof buf, &n)) << bad;
    EXPECT_NE(0, s->remove(s->ctx, t1, 2, bad.c_str())) << bad;
  }
  EXPECT_NE(0, s->put(s->ctx, NULL, 0, good.c_str(), (const uint8_t *)"x", 1));
  EXPECT_EQ(2u, StoredFiles());
  EXPECT_EQ(0, s->remove(s->ctx, t1, 2, good.c_str()));
  EXPECT_EQ(0, s->remove(s->ctx, t1, 2, good.c_str())) << "removing what is not there is success";
  EXPECT_EQ(1u, StoredFiles());
  EXPECT_NE(0, s->get(s->ctx, t1, 2, good.c_str(), buf, sizeof buf, &n));
  EXPECT_NE(GDEFWEB_OK, gdefweb_blob_dir_open("/definitely/not/a/directory", &blobs_));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
