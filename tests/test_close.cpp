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
 * The success signal of the web content administration epic, end to end, through
 * the real server and only its forms. A developer declares two content types and
 * the permissions on them, and nothing else: no page, no route, no template. An
 * editor then
 *
 *  1. finds the type in the content index (and a person with no right to it does
 *     not),
 *  2. creates, edits and deletes entities of it, a type of five kinds of field
 *     (a text, a reference, a many-valued field, a group of items, a file),
 *  3. builds a view of its entities and reads it as a public page,
 *  4. places that view on a layout as a widget, and
 *  5. attaches an uploaded file to an entity, so that a week later the sweep keeps
 *     it and removes the file nobody named.
 *
 * Each step has its own plant in tools/check-close.sh that turns this test red.
 */

#include "edge_fixture.h"
#include "portable.h"

#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/upload.h>
#include <ghoti.io/defiant-core/view.h>
#include <ghoti.io/defiant-web/uploads.h>

#include <filesystem>
#include <time.h>

namespace {

namespace fs = std::filesystem;

/* The value of the first control of this name on a page. */
std::string ValueOf(const std::string & page, const std::string & name) {
  const std::string key = "name=\"" + name + "\"";
  size_t at = page.find(key);
  if (at == std::string::npos) {
    return "<none>";
  }
  size_t end = page.find('>', at);
  size_t v = page.find(" value=\"", at);
  if (v == std::string::npos || v > end) {
    return "<none>";
  }
  v += 8;
  return page.substr(v, page.find('"', v) - v);
}

const char kPlace[] = "site.place";
const char kTrip[] = "site.trip";

class Close : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string ada_;
  fs::path dir_;
  GDEFWEB_Blob_Dir * blobs_ = NULL;
  GDEFCORE_Upload_Policy policy_;
  const char * media_[1] = {"image/png"};

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

  static GDEFCORE_Field Field(const char * name, int required = 0, int many = 0, int type = 0) {
    GDEFCORE_Field f;
    memset(&f, 0, sizeof f);
    f.size = sizeof f;
    f.name = name;
    f.name_len = strlen(name);
    f.required = required;
    f.many = many;
    f.type = type;
    return f;
  }

  static void Allow(GDEFCORE_Catalog * catalog, const char * type, const char * action) {
    const std::string label = std::string(action) + " " + type;
    GDEFCORE_Permission p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.label = label.c_str();
    p.label_len = label.size();
    p.module_name = "site";
    p.module_name_len = 4;
    p.action = action;
    p.action_len = strlen(action);
    p.condition = GDEFCORE_ACCESS_ANY;
    p.type_name = type;
    p.type_name_len = strlen(type);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog, &p));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "admin", 5, label.c_str(), label.size(), "core.role", 9));
  }

  /* What a developer writes: two types and the permissions on them. The site, the file type, the
   * blob store and the permissions to edit views are what a program's first start does. */
  void Open() {
    Site(id_);
    GDEFCORE_Catalog * catalog = Catalog();
    ASSERT_EQ(GDEFCORE_OK, gdefcore_upload_declare(catalog));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_view_declare(catalog));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_view_declare_edit(catalog));
    for (const char * label : {"Upload files", "See the files you uploaded", GDEFCORE_VIEW_EDIT_LABEL}) {
      ASSERT_EQ(GDEFCORE_OK,
          gdefcore_access_grant(catalog, "admin", 5, label, strlen(label), "core.role", 9));
    }
    GDEFCORE_Field place[1] = {Field("title", 1)};
    GDEFCORE_Field stop[2] = {Field("name", 1), Field("note")};
    GDEFCORE_Field trip[5] = {Field("title", 1), Field("place", 0, 0, GDEFSTOR_FIELD_REFERENCE),
        Field("tags", 0, 1), Field("stops", 0, 1, GDEFSTOR_FIELD_GROUP),
        Field("photo", 0, 0, GDEFSTOR_FIELD_REFERENCE)};
    trip[3].children = stop;
    trip[3].child_count = 2;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog, kPlace, strlen(kPlace), GDEFCORE_TYPE_CONTENT, place, 1));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog, kTrip, strlen(kTrip), GDEFCORE_TYPE_CONTENT, trip, 5));
    for (const char * type : {kPlace, kTrip}) {
      for (const char * action : {"create", "read", "update", "delete"}) {
        Allow(catalog, type, action);
      }
    }
    gdefcore_catalog_destroy(catalog);
    std::string tmpl_str = PortableTempDir() + "/gdefweb-close-XXXXXX";
    char * tmpl = &tmpl_str[0];
    ASSERT_NE((char *)NULL, mkdtemp(tmpl));
    dir_ = tmpl;
    ASSERT_EQ(GDEFWEB_OK, gdefweb_blob_dir_open(tmpl, &blobs_));
    memset(&policy_, 0, sizeof policy_);
    policy_.size = sizeof policy_;
    policy_.max_bytes = 2000;
    policy_.media = media_;
    policy_.media_count = 1;
    policy_.ttl_ms = 3600 * 1000;
    ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_bind(gdefweb_blob_dir_store(blobs_), &policy_));
    ada_ = CookieLine(Login("ada", "secret"));
  }

  /* A person who may use the admin area and holds none of the rights on the types. */
  std::string Outsider() {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Keyring * keyring = gdefcore_inst_tenant_keyring(inst_, id_, strlen(id_));
    char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog, principal));
    EXPECT_EQ(GDEFCORE_OK, gdefcore_access_role_create(catalog, "outsiders", 9, NULL));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_grant(catalog, "outsiders", 9, GDEFCORE_ADMIN_USE_LABEL,
            strlen(GDEFCORE_ADMIN_USE_LABEL), "core.role", 9));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_access_principal_add_role(catalog, principal, strlen(principal), "outsiders", 9));
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog, keyring, root_, sizeof root_, principal, strlen(principal),
            "olga", 4, "pw-pw-pw", 8, account));
    gdefcore_catalog_destroy(catalog);
    return CookieLine(Login("olga", "pw-pw-pw"));
  }

  static std::string TokenOf(const std::string & page, const char * value) {
    return FormToken(page, (std::string("value=\"") + value + "\"").c_str());
  }

  Reply Post(const std::string & path, const std::string & token, const std::string & rest) {
    return PostRaw(path.c_str(), ada_, "token=" + token + rest);
  }

  /* The "New" button of a type's list and the form it leads to: the path of the entity. */
  std::string StartNew(const char * type) {
    const std::string base = std::string("/admin/content/") + type;
    Reply list = Get(base.c_str(), ada_);
    Reply made = Post(base, TokenOf(list.body, "new"), "&do=new");
    EXPECT_EQ(303, made.status) << list.body;
    return HeaderValue(made, "Location");
  }

  /* One entity made and saved through the forms: the path of its page. */
  std::string Make(const char * type, const std::string & rest) {
    const std::string path = StartNew(type);
    Reply form = Get(path.c_str(), ada_);
    Reply saved = Post(path, TokenOf(form.body, "save"), "&do=save" + rest);
    EXPECT_EQ(303, saved.status) << saved.body;
    return path;
  }

  static std::string Last(const std::string & path) { return path.substr(path.rfind('/') + 1); }

  static std::string Part(const std::string & name, const std::string & filename,
      const std::string & type, const std::string & data) {
    std::string out = "--BOUNDARY\r\nContent-Disposition: form-data; name=\"" + name + "\"";
    if (!filename.empty()) {
      out += "; filename=\"" + filename + "\"";
    }
    out += "\r\n";
    if (!type.empty()) {
      out += "Content-Type: " + type + "\r\n";
    }
    return out + "\r\n" + data + "\r\n";
  }

  Reply Upload(const std::string & filename, const std::string & data) {
    Reply page = Get("/admin/files", ada_);
    const std::string token = FormToken(page.body, "type=\"file\"");
    EXPECT_FALSE(token.empty());
    return Ask(Req("POST", "/admin/files", "site.example",
                   (std::string("Origin: http://site.example\r\n"
                                "Content-Type: multipart/form-data; boundary=BOUNDARY\r\n") +
                       ada_)
                       .c_str(),
                   (Part("token", "", "", token) + Part("file", filename, "image/png", data) +
                       "--BOUNDARY--\r\n")
                       .c_str()),
        20000);
  }

  size_t StoredFiles() {
    size_t n = 0;
    std::error_code ec;
    for (auto & e : fs::recursive_directory_iterator(dir_, ec)) {
      n += e.is_regular_file() ? 1 : 0;
    }
    return n;
  }

  /* The id a control offers for the option that reads @p label. */
  static std::string Offered(const std::string & page, const std::string & label) {
    size_t at = page.find(label);
    if (at == std::string::npos) {
      return std::string();
    }
    size_t value = page.rfind("value=\"", at);
    return value == std::string::npos ? std::string()
                                      : page.substr(value + 7, GDEFCORE_PUBLIC_ID_LEN);
  }

  /* The layout page's own form for @p act on the placement of @p widget. */
  static std::string LayoutBody(const std::string & page, const std::string & widget,
      const std::string & act, const std::string & extra) {
    const std::string strong = "<strong>" + widget + "</strong>";
    size_t at = page.find(strong);
    if (at == std::string::npos) {
      return std::string();
    }
    const std::string needle = "name=\"act\" value=\"" + act + "\"><input type=\"hidden\" name=\"index\" value=\"";
    size_t form_at = page.find(needle, at);
    if (form_at == std::string::npos) {
      return std::string();
    }
    const size_t idx_from = form_at + needle.size();
    const std::string index = page.substr(idx_from, page.find('"', idx_from) - idx_from);
    size_t form = page.rfind("<form", form_at);
    size_t end = page.find("</form>", form_at);
    const std::string part = page.substr(form, end - form);
    return "token=" + InputValue(part, "token", 0) + "&version=" + InputValue(part, "version", 0) +
           "&act=" + act + "&index=" + index + extra;
  }
};

}  // namespace

TEST_F(Close, ADeveloperDeclaresTwoTypesAndAnEditorRunsThemFromTheBrowser) {
  Open();
  const std::string outsider = Outsider();

  /* ---- 1. the type is in the content index, for whoever may manage it --------------------- */
  {
    SCOPED_TRACE("the content index");
    Reply index = Get("/admin/content", ada_);
    EXPECT_TRUE(Has(index.body, "href=\"/admin/content/site.trip\">site.trip</a>")) << index.body;
    EXPECT_TRUE(Has(index.body, "href=\"/admin/content/site.place\">site.place</a>"));
    Reply none = Get("/admin/content", outsider);
    EXPECT_FALSE(Has(none.body, "site.trip")) << "a person with no right to a type is not offered it";
    EXPECT_TRUE(Has(none.body, "There is no type you may manage."));
  }

  /* ---- 5a. two files are uploaded; one will be attached ------------------------------------ */
  ASSERT_EQ(303, Upload("cover.png", "cover bytes").status);
  ASSERT_EQ(303, Upload("stray.png", "stray bytes").status);
  ASSERT_EQ(2u, StoredFiles());

  /* ---- 2. create, edit and delete: a text, a reference, a many field, a group, a file --------- */
  std::string trip_path;
  std::string trip_id;
  {
    SCOPED_TRACE("create, edit, delete");
    const std::string place_path = Make(kPlace, "&title=Oslo");
    const std::string place_id = Last(place_path);
    const std::string form_path = StartNew(kTrip);
    Reply form = Get(form_path.c_str(), ada_);
    ASSERT_EQ(200, form.status);
    const std::string cover = Offered(form.body, "cover.png");
    ASSERT_EQ(GDEFCORE_PUBLIC_ID_LEN, cover.size()) << form.body;
    Reply saved = Post(form_path, TokenOf(form.body, "save"),
        "&do=save&title=Fjord+week&place=" + place_id + "&tags=cold&tags=blue" +
            "&stops-0--i=0&stops-0-name=Ferry&stops-0-note=early" +
            "&stops-1--i=0&stops-1-name=Hut&photo=" + cover);
    ASSERT_EQ(303, saved.status) << saved.body;
    trip_path = form_path;
    trip_id = Last(form_path);
    /* What was typed is what the form shows again. */
    Reply again = Get(trip_path.c_str(), ada_);
    EXPECT_EQ("Fjord week", ValueOf(again.body, "title")) << again.body;
    EXPECT_TRUE(Has(again.body, "name=\"tags\" value=\"cold\""));
    EXPECT_TRUE(Has(again.body, "name=\"tags\" value=\"blue\""));
    EXPECT_EQ("Ferry", ValueOf(again.body, "stops-0-name"));
    EXPECT_EQ("Hut", ValueOf(again.body, "stops-1-name"));
    EXPECT_EQ(place_id, ValueOf(again.body, "place")) << again.body;
    EXPECT_EQ(cover, ValueOf(again.body, "photo")) << again.body;
    /* Edit: a new title, nothing else lost. */
    Reply changed = Post(trip_path, TokenOf(again.body, "save"),
        "&do=save&title=Fjord+fortnight&place=" + place_id + "&tags=cold&tags=blue" +
            "&stops-0--i=" + InputValue(again.body, "stops-0--i", 0) + "&stops-0-name=Ferry&stops-0-note=early" +
            "&stops-1--i=" + InputValue(again.body, "stops-1--i", 0) + "&stops-1-name=Hut&photo=" + cover);
    ASSERT_EQ(303, changed.status) << changed.body;
    Reply list = Get("/admin/content/site.trip", ada_);
    EXPECT_TRUE(Has(list.body, ">Fjord fortnight</td>")) << list.body;
    EXPECT_FALSE(Has(list.body, ">Fjord week</td>"));
    /* A second trip, made and then deleted: the box ticked, and then it is gone. */
    const std::string doomed = Make(kTrip, "&title=Doomed");
    Reply doomed_page = Get(doomed.c_str(), ada_);
    Reply gone = Post(doomed, TokenOf(doomed_page.body, "delete"), "&do=delete&confirm=yes");
    EXPECT_EQ(303, gone.status);
    Reply after = Get("/admin/content/site.trip", ada_);
    EXPECT_FALSE(Has(after.body, ">Doomed</td>")) << "a deleted entity is not listed";
    EXPECT_TRUE(Has(after.body, ">Fjord fortnight</td>"));
  }

  /* ---- 3. a view of the trips, read as a public page --------------------------------------- */
  {
    SCOPED_TRACE("the view as a page");
    Reply form = Get("/admin/views/new", ada_);
    Reply made = PostRaw("/admin/views/new", ada_,
        "token=" + TokenOf(form.body, "save") +
            "&do=save&name=trips&type=site.trip&col0_field=title&col0_label=Trip&col1_field=tags" +
            "&col1_fmt=join&format=table&limit=25");
    ASSERT_EQ(303, made.status) << made.body;
    Reply page = Get("/v/trips", ada_);
    EXPECT_EQ(200, page.status);
    EXPECT_TRUE(Has(page.body, "<th>Trip</th>")) << page.body;
    EXPECT_TRUE(Has(page.body, "<td>Fjord fortnight</td>")) << page.body;
    EXPECT_FALSE(Has(page.body, "Doomed")) << "the deleted trip is not a row";
    Reply visitor = Get("/v/trips", std::string());
    EXPECT_FALSE(Has(visitor.body, "Fjord")) << "a visitor sees the rows the access filter lets them";
  }

  /* ---- 4. the same view, placed on the front layout as a widget ------------------------------ */
  {
    SCOPED_TRACE("the view as a widget");
    Reply layout = Get("/admin/layouts/front", ada_);
    ASSERT_EQ(200, layout.status);
    EXPECT_TRUE(Has(layout.body, "<option value=\"view\">view</option>")) << "the editor offers the widget";
    /* The form that places a widget is the page's last, with act=place at item 0. */
    const std::string needle = "name=\"act\" value=\"place\"><input type=\"hidden\" name=\"index\" value=\"0\">";
    size_t at = layout.body.find(needle);
    ASSERT_NE(std::string::npos, at);
    size_t form = layout.body.rfind("<form", at);
    size_t end = layout.body.find("</form>", at);
    const std::string part = layout.body.substr(form, end - form);
    Reply placed = PostRaw("/admin/layouts/front", ada_,
        "token=" + InputValue(part, "token", 0) + "&version=" + InputValue(part, "version", 0) +
            "&act=place&index=0&widget=view&region=side&position=0");
    ASSERT_EQ(303, placed.status) << placed.body;
    Reply now = Get("/admin/layouts/front", ada_);
    const std::string cfg = LayoutBody(now.body, "view", "configure",
        "&title=Trips&target=trips&path=&type=&roles=");
    ASSERT_FALSE(cfg.empty()) << now.body;
    ASSERT_EQ(303, PostRaw("/admin/layouts/front", ada_, cfg).status);
    Reply front = Get("/", ada_);
    EXPECT_TRUE(Has(front.body, "<table class=\"view\">")) << front.body;
    EXPECT_TRUE(Has(front.body, "<td>Fjord fortnight</td>")) << front.body;
    Reply blind = Get("/", std::string());
    EXPECT_FALSE(Has(blind.body, "class=\"view\"")) << "a visitor's page has no trace of it";
    EXPECT_FALSE(Has(blind.body, "Fjord"));
  }

  /* ---- 5b. a week later the sweep keeps what an entity names and removes the rest ------------- */
  {
    SCOPED_TRACE("the sweep");
    ASSERT_EQ(2u, StoredFiles()) << "both files are there until the sweep runs";
    GDEFCORE_Catalog * catalog = Catalog();
    size_t promoted = 0, demoted = 0, removed = 0;
    ASSERT_EQ(GDEFWEB_OK, gdefweb_uploads_sweep(catalog,
        (uint64_t)time(NULL) * 1000 + 7ull * 24 * 3600 * 1000, &promoted, &demoted, &removed));
    gdefcore_catalog_destroy(catalog);
    EXPECT_EQ(1u, promoted);
    EXPECT_EQ(1u, removed);
    EXPECT_EQ(1u, StoredFiles());
    Reply files = Get("/admin/files", ada_);
    EXPECT_FALSE(Has(files.body, "stray.png")) << "the file nobody named is gone";
    Reply edit = Get(trip_path.c_str(), ada_);
    const std::string cover = ValueOf(edit.body, "photo");
    EXPECT_EQ("cover bytes", Get(("/files/" + cover).c_str(), ada_).body) << "the attached file survived";
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
