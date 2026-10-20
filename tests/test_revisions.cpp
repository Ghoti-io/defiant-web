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
 * The entity record (access-conditions story A5b, on the storage port since
 * story S6b) on both backends. The core suite runs it on the memory store;
 * this one links the SQLite store too, so the same sequence of writes (two
 * drafts, publish, unpublish, save, revert to a named revision, clear, revert
 * a cleared entity) is run on each and the two transcripts of what the
 * accessors answer must be identical, and the SQLite one must survive closing
 * the database and opening it again. (`activate` went with access SPEC
 * decision 13; a revert is how an older revision becomes the content.)
 */

#include <ghoti.io/defiant-core/defiant-core.h>
#include <ghoti.io/defiant-storage-memory/memory.h>
#include <ghoti.io/defiant-storage-sqlite/model.h>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace {

const uint8_t kTenant[] = {'t'};

std::string PrincipalId(uint8_t seed) {
  uint8_t raw[16];
  char out[GDEFCORE_PUBLIC_ID_LEN + 1];
  memset(raw, seed, sizeof raw);
  raw[0] = 0;
  EXPECT_EQ(GDEFCORE_OK, gdefcore_id_encode(raw, out));
  return std::string(out);
}

/* One entity's answers, as text: what each accessor says now. */
std::string Describe(GDEFCORE_Catalog * cat, const std::string & id) {
  std::string out;
  int state = 0;
  int has = 0;
  uint64_t seq = 0;
  uint64_t version = 0;
  uint64_t head = 0;
  EXPECT_EQ(GDEFCORE_OK, gdefcore_entity_state(cat, id.c_str(), id.size(), &state));
  EXPECT_EQ(GDEFCORE_OK,
      gdefcore_entity_current_revision(cat, id.c_str(), id.size(), &has, &seq));
  EXPECT_EQ(GDEFCORE_OK, gdefcore_entity_version(cat, id.c_str(), id.size(), &version));
  EXPECT_EQ(GDEFCORE_OK, gdefcore_entity_revision(cat, id.c_str(), id.size(), &head));
  out += state == GDEFCORE_STATE_PUBLISHED ? "pub" : "unpub";
  out += " act=" + std::to_string(has ? seq : 0) + " head=" + std::to_string(head) +
         " ver=" + std::to_string(version);
  for (int kind = 0; kind < 2; kind++) {
    char * text = NULL;
    size_t n = 0;
    EXPECT_EQ(GDEFCORE_OK,
        (kind == 0 ? gdefcore_entity_creator : gdefcore_entity_actor)(
            cat, id.c_str(), id.size(), &text, &n));
    out += kind == 0 ? " creator=" : " actor=";
    out += text == NULL ? "-" : std::string(text, n);
    gdefcore_text_free(cat, text);
  }
  for (int view : {GDEFCORE_VIEW_PUBLISHED, GDEFCORE_VIEW_CURRENT,
           GDEFCORE_VIEW_DRAFT}) {
    char ** texts = NULL;
    size_t * lens = NULL;
    size_t count = 0;
    GDEFCORE_Result rc = gdefcore_entity_values(cat, NULL, view, id.c_str(),
        id.size(), "en", 2, "title", 5, &texts, &lens, &count);
    out += " v" + std::to_string(view) + "=";
    if (rc == GDEFCORE_OK) {
      out += std::string(texts[0], lens[0]);
      gdefcore_values_free(cat, texts, lens, count);
    } else {
      out += rc == GDEFCORE_ERR_INVALID ? "<none>" : "<error>";
    }
  }
  return out;
}

struct Scenario {
  GDEFCORE_Catalog * cat;
  std::string transcript;
  std::string e1;
  std::string e2;

  void Apply(GDEFCORE_Changeset * cs) {
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, NULL, 0));
    gdefcore_changeset_destroy(cs);
  }

  GDEFCORE_Changeset * Prepare(const std::string & actor) {
    GDEFCORE_Changeset * cs = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_changeset_prepare(cat, actor.empty() ? NULL : actor.c_str(),
            actor.size(), &cs));
    return cs;
  }

  void Write(const std::string & id, const std::string & actor,
      const char * title, bool draft) {
    GDEFCORE_Changeset * cs = Prepare(actor);
    const char * texts[1] = {title};
    size_t lens[1] = {strlen(title)};
    GDEFCORE_Value value;
    memset(&value, 0, sizeof value);
    value.size = sizeof value;
    value.field = "title";
    value.field_len = 5;
    value.texts = texts;
    value.text_lens = lens;
    value.count = 1;
    ASSERT_EQ(GDEFCORE_OK,
        (draft ? gdefcore_entity_save_draft : gdefcore_entity_save)(
            cs, id.c_str(), id.size(), "en", 2, &value, 1));
    Apply(cs);
  }

  void Step(const char * what) {
    transcript += std::string(what) + " e1: " + Describe(cat, e1) + "\n";
    transcript += std::string(what) + " e2: " + Describe(cat, e2) + "\n";
  }

  void Go() {
    std::string a = PrincipalId(1);
    std::string b = PrincipalId(2);
    GDEFCORE_Field field;
    char id[GDEFCORE_PUBLIC_ID_LEN + 1];
    GDEFCORE_Changeset * cs = NULL;
    memset(&field, 0, sizeof field);
    field.size = sizeof field;
    field.name = "title";
    field.name_len = 5;
    field.required = 1;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(cat, "site.note", 9, GDEFCORE_TYPE_CONTENT, &field, 1));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(cat, "site.note", 9, id, NULL));
    e1 = id;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(cat, "site.note", 9, id, NULL));
    e2 = id;
    Step("created");
    Write(e1, a, "one", true);
    Write(e1, b, "two", true);
    Step("two drafts");
    cs = Prepare(b);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_publish(cs, e1.c_str(), e1.size()));
    Apply(cs);
    Step("published");
    cs = Prepare(a);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_unpublish(cs, e1.c_str(), e1.size()));
    Apply(cs);
    Step("unpublished");
    Write(e1, a, "three", false);
    Step("saved");
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_changeset_revert_to(cat, e1.c_str(), e1.size(), 1, b.c_str(),
            b.size(), &cs));
    Apply(cs);
    Step("reverted to 1");
    Write(e1, a, "draft over one", true);
    Step("drafted");
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_changeset_revert_to(cat, e1.c_str(), e1.size(), 2, b.c_str(),
            b.size(), &cs));
    Apply(cs);
    Step("reverted to 2");
    Write(e2, a, "only", false);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_changeset_revert(cat, e2.c_str(), e2.size(), NULL, 0, &cs));
    Apply(cs);
    Step("cleared e2");
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_changeset_revert_to(cat, e2.c_str(), e2.size(), 1, NULL, 0, &cs));
    Apply(cs);
    Step("restored e2");
  }
};

}  // namespace

TEST(RevisionsBackends, MemoryAndSqliteAgreeAndSqliteSurvivesReopening) {
  std::string memory_transcript;
  std::string sqlite_transcript;
  std::string e1;
  std::string e2;
  {
    GDEFSTOR_Store * store = NULL;
    GDEFCORE_Catalog * cat = NULL;
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &store));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_catalog_open(store, kTenant, 1, NULL, NULL, &cat));
    Scenario run;
    run.cat = cat;
    run.Go();
    ASSERT_FALSE(HasFatalFailure());
    memory_transcript = run.transcript;
    gdefcore_catalog_destroy(cat);
    gdefstor_store_destroy(store);
  }
  const char * tmp = getenv("TMPDIR");
  std::string pattern = std::string(tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp") +
                        "/defiant-web-revisions-XXXXXX";
  std::vector<char> path(pattern.begin(), pattern.end());
  path.push_back('\0');
  ASSERT_NE(nullptr, mkdtemp(path.data()));
  {
    GDEFSTOR_Store * store = NULL;
    GDEFCORE_Catalog * cat = NULL;
    ASSERT_EQ(GDEFSSQLITE_OK,
        gdefssqlite_model_store_create(path.data(), NULL, NULL, NULL, &store));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_catalog_open(store, kTenant, 1, NULL, NULL, &cat));
    Scenario run;
    run.cat = cat;
    run.Go();
    ASSERT_FALSE(HasFatalFailure());
    sqlite_transcript = run.transcript;
    e1 = run.e1;
    e2 = run.e2;
    gdefcore_catalog_destroy(cat);
    gdefstor_store_destroy(store);
  }
  /* the answers are the backend's business no more than the storage is */
  EXPECT_FALSE(memory_transcript.empty());
  EXPECT_EQ(memory_transcript, sqlite_transcript);
  /* and what was written is what is read back after a close and a reopen */
  {
    GDEFSTOR_Store * store = NULL;
    GDEFCORE_Catalog * cat = NULL;
    ASSERT_EQ(GDEFSSQLITE_OK,
        gdefssqlite_model_store_create(path.data(), NULL, NULL, NULL, &store));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_catalog_open(store, kTenant, 1, NULL, NULL, &cat));
    std::string last1 = Describe(cat, e1);
    std::string last2 = Describe(cat, e2);
    EXPECT_NE(std::string::npos, sqlite_transcript.rfind("reverted to 2 e1: " + last1));
    EXPECT_NE(std::string::npos, sqlite_transcript.rfind("restored e2 e2: " + last2));
    gdefcore_catalog_destroy(cat);
    gdefstor_store_destroy(store);
  }
  std::string cleanup = std::string("rm -rf '") + path.data() + "'";
  EXPECT_EQ(0, system(cleanup.c_str()));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
