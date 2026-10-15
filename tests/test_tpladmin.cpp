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
 * The template editor (site-admin story 3), through the real server: the list,
 * the edit page, validate, preview, save with a typed confirmation, history and
 * revert, and what a principal who may not edit a kind is told.
 *
 * Every refusal has a control in the same test: the same request that is
 * refused for one principal or one input is shown to succeed for the other, so
 * a test that cannot see the thing it looks for is not passing by accident.
 * Every action is a plain form: nothing here runs a script.
 */

#include "edge_fixture.h"

#include <ghoti.io/defiant-test/defiant-test.h>

#define TA_STR2(x) #x
#define TA_STR(x) TA_STR2(x)

namespace {

const char kCoreToken[] = TA_STR(GHOTIIO_DEFCORE_NAME);

/* A module that refines template edits, as the story's example does. */
GDEFCORE_Module_Manifest g_man;
GDEFCORE_Module_Sub g_sub;
extern "C" const GDEFCORE_Module_Manifest * JudgeEntry(void) { return &g_man; }

struct Judge {
  int calls = 0;
  std::string allowed;
  std::string kind;
  std::string classification;
};

/* Deny edits of core.page templates unless the classification is the one
 * granted; no opinion about anything else. */
extern "C" GDEFCORE_Result JudgeFn(GDEFCORE_Request * request, const void * args,
    GDEFCORE_Alter * alter, GDEFCORE_Hook_Out * out, void * user) {
  Judge * j = (Judge *)user;
  const GDEFCORE_Template_Write_Hook * a =
      (const GDEFCORE_Template_Write_Hook *)args;
  (void)request;
  (void)alter;
  j->calls += 1;
  j->kind.assign(a->kind, a->kind_len);
  j->classification.assign(a->classification, a->classification_len);
  out->decision = GDEFCORE_DECISION_NONE;
  if (j->kind == "core.page") {
    out->decision = j->classification == j->allowed ? GDEFCORE_DECISION_ALLOW
                                                    : GDEFCORE_DECISION_DENY;
  }
  return GDEFCORE_OK;
}

/* A value whose single text is already in @p slot. */
void Val(GDEFCORE_Value * value, const char * field, const char ** slot, size_t * len) {
  memset(value, 0, sizeof *value);
  value->size = sizeof *value;
  value->field = field;
  value->field_len = strlen(field);
  value->texts = slot;
  value->text_lens = len;
  value->count = 1;
}

/* application/x-www-form-urlencoded. */
std::string Enc(const std::string & in) {
  std::string out;
  for (unsigned char c : in) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof buf, "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

/* The page the way the response scanner reads it: tokens are opaque. */
bool ScansClean(const std::string & page) {
  std::string text = page;
  size_t at;
  while ((at = text.find("name=\"token\" value=\"")) != std::string::npos) {
    size_t end = text.find('"', at + 20);
    text.erase(at, (end == std::string::npos ? text.size() : end) - at);
  }
  /* Declaring the encoding is not naming a row (as the scanner reads it). */
  while ((at = text.find("charset=\"utf-8\"")) != std::string::npos) {
    text.erase(at, 15);
  }
  return gdeftest_response_has_internal_id(text.data(), text.size(), NULL, 0) == 0;
}

const std::string kPaneBody = "<% use title; %><b class=\"edited\"><%= title %></b>";

class TplAdmin : public Edge {
 protected:
  char id_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string cookie_;
  Judge judge_;

  /* A site with the kit, the host given to the server, the admin role granted
   * the permissions of @p kinds, and ada signed in. */
  void Open(const std::vector<std::string> & kinds) {
    Site(id_);
    UseHost();
    if (!kinds.empty()) {
      GrantTemplateKinds(id_, kinds);
    }
    cookie_ = CookieLine(Login("ada", "secret"));
  }

  GDEFCORE_Catalog * Catalog() {
    GDEFCORE_Catalog * catalog = NULL;
    EXPECT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, id_, strlen(id_), &catalog));
    gdefcore_catalog_bind_host(catalog, host_);
    return catalog;
  }

  bool FindId(GDEFCORE_Catalog * catalog, const char * name,
      char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
    char ** ids = NULL;
    size_t n = 0;
    bool have = false;
    if (gdefcore_entity_ids(catalog, "core.template", 13, &ids, &n) != GDEFCORE_OK) {
      return false;
    }
    for (size_t i = 0; i < n && !have; i++) {
      char ** vals = NULL;
      size_t * lens = NULL;
      size_t c = 0;
      if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
              strlen(ids[i]), "en", 2, "name", 4, &vals, &lens, &c) == GDEFCORE_OK) {
        if (c > 0 && lens[0] == strlen(name) && memcmp(vals[0], name, lens[0]) == 0) {
          memcpy(id, ids[i], strlen(ids[i]) + 1);
          have = true;
        }
        gdefcore_values_free(catalog, vals, lens, c);
      }
    }
    gdefcore_ids_free(catalog, ids, n);
    return have;
  }

  std::string Stored(const char * name, const char * field) {
    GDEFCORE_Catalog * catalog = Catalog();
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    std::string out = "!";
    if (FindId(catalog, name, tid)) {
      char ** vals = NULL;
      size_t * lens = NULL;
      size_t c = 0;
      if (gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED, tid,
              GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &vals, &lens,
              &c) == GDEFCORE_OK) {
        out = c > 0 ? std::string(vals[0], lens[0]) : "";
        gdefcore_values_free(catalog, vals, lens, c);
      }
    }
    gdefcore_catalog_destroy(catalog);
    return out;
  }

  size_t Revisions(const char * name) {
    GDEFCORE_Catalog * catalog = Catalog();
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    size_t count = 0;
    if (FindId(catalog, name, tid)) {
      gdefcore_entity_revision_count(catalog, tid, GDEFCORE_PUBLIC_ID_LEN, &count);
    }
    gdefcore_catalog_destroy(catalog);
    return count;
  }

  /* The actor of the latest revision of a template. */
  std::string LatestActor(const char * name) {
    GDEFCORE_Catalog * catalog = Catalog();
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    std::string out = "!";
    size_t count = 0;
    if (FindId(catalog, name, tid) &&
        gdefcore_entity_revision_count(catalog, tid, GDEFCORE_PUBLIC_ID_LEN, &count) ==
            GDEFCORE_OK &&
        count > 0) {
      uint64_t seq = 0;
      char * actor = NULL;
      size_t n = 0;
      if (gdefcore_entity_revision_at(catalog, tid, GDEFCORE_PUBLIC_ID_LEN, count - 1,
              &seq, &actor, &n) == GDEFCORE_OK) {
        out = actor == NULL ? "" : std::string(actor, n);
        gdefcore_text_free(catalog, actor);
      }
    }
    gdefcore_catalog_destroy(catalog);
    return out;
  }

  /* The operator's write: a changeset with no request, so no one is asked. */
  void Operator(const char * name, const std::string & body, const char * kind,
      const char * classification = NULL) {
    GDEFCORE_Catalog * catalog = Catalog();
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Value values[4];
    const char * slots[4] = {name, body.c_str(), kind, classification};
    size_t lens[4] = {strlen(name), body.size(), strlen(kind),
        classification == NULL ? 0 : strlen(classification)};
    size_t n = 3;
    ASSERT_TRUE(FindId(catalog, name, tid));
    Val(&values[0], "name", &slots[0], &lens[0]);
    Val(&values[1], "body", &slots[1], &lens[1]);
    Val(&values[2], "kind", &slots[2], &lens[2]);
    if (classification != NULL) {
      Val(&values[n++], "classification", &slots[3], &lens[3]);
    }
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_save(cs, tid, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, n));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "core.template", 13));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  /* Install and enable the judge for this tenant. */
  void EnableJudge(const std::string & allowed) {
    GDEFCORE_Catalog * catalog = Catalog();
    GDEFCORE_Changeset * cs = NULL;
    judge_.allowed = allowed;
    memset(&g_man, 0, sizeof g_man);
    memset(&g_sub, 0, sizeof g_sub);
    g_sub.size = sizeof g_sub;
    g_sub.hook = GDEFCORE_HOOK_TEMPLATE_WRITE;
    g_sub.hook_len = strlen(GDEFCORE_HOOK_TEMPLATE_WRITE);
    g_sub.kind = GDEFCORE_HOOK_DECISION;
    g_sub.fn = JudgeFn;
    g_sub.user = &judge_;
    g_man.size = sizeof g_man;
    g_man.core_token = kCoreToken;
    g_man.core_token_len = strlen(kCoreToken);
    g_man.name = "judge";
    g_man.name_len = 5;
    g_man.label = "judge";
    g_man.label_len = 5;
    g_man.subs = &g_sub;
    g_man.nsubs = 1;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_install_guest(host_, JudgeEntry));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_module_enable(host_, cs, "judge", 5));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "judge", 5));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }

  /* Pages ------------------------------------------------------------------ */

  Reply EditPage(const char * name) {
    return Get((std::string("/admin/templates/") + name).c_str(), cookie_);
  }

  /*
   * What a browser posts from the edit page whose HTML is @p page (the tokens
   * are the page's own, so an old page gives stale ones).
   */
  std::string FormBody(const std::string & page, const std::string & action,
      const std::string & body, const std::string & kind, const std::string & confirm) {
    return "token=" + InputValue(page, "token", 0) + "&token=" + InputValue(page, "token", 1) +
           "&kind=" + Enc(kind) + "&body=" + Enc(body) + "&confirm=" + Enc(confirm) +
           "&do=" + action;
  }

  /* Post the form of @p page and follow the redirect, as a browser does. */
  Reply Act(const char * name, const std::string & page, const std::string & action,
      const std::string & body, const std::string & kind, const std::string & confirm,
      Reply * sent = NULL) {
    std::string path = std::string("/admin/templates/") + name;
    Reply first = PostRaw(path.c_str(), cookie_,
        FormBody(page, action, body, kind, confirm));
    if (sent != NULL) {
      *sent = first;
    }
    if (first.status != 303) {
      return first;
    }
    return Get(HeaderValue(first, "Location").c_str(), cookie_);
  }

  std::string Fresh(const char * name) { return EditPage(name).body; }
};

}  // namespace

/* -------------------------------------------------------------------- */
/* Who sees what                                                          */
/* -------------------------------------------------------------------- */

TEST_F(TplAdmin, ListShowsOnlyKindsThePrincipalMayEdit) {
  Open({"core.pane"});
  Reply list = Get("/admin/templates", cookie_);
  EXPECT_EQ(200, list.status);
  /* The control: the pane templates are there, as links. */
  for (const char * name : {"entity", "recent", "field"}) {
    EXPECT_TRUE(Has(list.body, (std::string("href=\"/admin/templates/") + name + "\"").c_str()))
        << name;
  }
  EXPECT_TRUE(Has(list.body, "Pane"));
  /* The page layout's kind is not named, and its template is not listed. */
  EXPECT_FALSE(Has(list.body, "layout"));
  EXPECT_FALSE(Has(list.body, "Page layout"));
  EXPECT_FALSE(Has(list.body, "core.page"));
  EXPECT_TRUE(ScansClean(list.body));
  ExpectSecurity(list);
  /* Granted the page kind too, both appear: the filter is the permission. */
  GrantTemplateKinds(id_, {"core.page"});
  Reply both = Get("/admin/templates", cookie_);
  EXPECT_TRUE(Has(both.body, "href=\"/admin/templates/layout\""));
  EXPECT_TRUE(Has(both.body, "Page layout"));
}

TEST_F(TplAdmin, NotGrantedIsLikeNotFound) {
  Open({});
  /* Admin, signed in, and granted nothing: the permission is held by no one. */
  Reply list = Get("/admin/templates", cookie_);
  EXPECT_EQ(200, list.status);
  EXPECT_TRUE(Has(list.body, "There are no templates you may edit."));
  for (const char * name : {"layout", "recent", "entity", "field", "Pane", "Page"}) {
    EXPECT_FALSE(Has(list.body, name)) << name;
  }
  Reply missing = EditPage("no-such-template");
  for (const char * name : {"recent", "layout", "entity"}) {
    Reply got = EditPage(name);
    EXPECT_EQ(missing.status, got.status) << name;
    EXPECT_EQ(missing.body, got.body) << name;
    EXPECT_EQ(HeaderValue(missing, "Content-Type"), HeaderValue(got, "Content-Type")) << name;
    Reply hist = Get((std::string("/admin/templates/") + name + "/history").c_str(), cookie_);
    Reply hist_missing = Get("/admin/templates/no-such-template/history", cookie_);
    EXPECT_EQ(hist_missing.status, hist.status) << name;
    EXPECT_EQ(hist_missing.body, hist.body) << name;
    Reply posted = PostRaw((std::string("/admin/templates/") + name).c_str(), cookie_,
        "do=save&body=x&kind=core.pane&confirm=x");
    Reply posted_missing = PostRaw("/admin/templates/no-such-template", cookie_,
        "do=save&body=x&kind=core.pane&confirm=x");
    EXPECT_EQ(posted_missing.status, posted.status) << name;
    EXPECT_EQ(posted_missing.body, posted.body) << name;
  }
  /* The control: the same URL, once the kind is granted, is a real page. */
  GrantTemplateKinds(id_, {"core.pane"});
  Reply real = EditPage("recent");
  EXPECT_NE(missing.body, real.body);
  EXPECT_TRUE(Has(real.body, "<textarea"));
  EXPECT_TRUE(Has(real.body, "&lt;article class=&quot;recent&quot;&gt;"));
}

TEST_F(TplAdmin, AnotherKindsTemplateIsLikeAMissingOne) {
  Open({"core.pane"});
  Reply missing = EditPage("no-such-template");
  /* A page template, to a principal who may edit panes only. */
  Reply layout = EditPage("layout");
  EXPECT_EQ(missing.status, layout.status);
  EXPECT_EQ(missing.body, layout.body);
  EXPECT_FALSE(Has(layout.body, "region"));
  Reply layout_history = Get("/admin/templates/layout/history", cookie_);
  Reply missing_history = Get("/admin/templates/no-such-template/history", cookie_);
  EXPECT_EQ(missing_history.body, layout_history.body);
  /* The control: a pane template is shown, with its body. */
  Reply recent = EditPage("recent");
  EXPECT_TRUE(Has(recent.body, "<textarea"));
  EXPECT_TRUE(Has(recent.body, "&lt;article class=&quot;recent&quot;&gt;"));
  /* Nor can the tail of a path be used to reach it. */
  for (const char * path : {"/admin/templates/layout/r/abc", "/admin/templates/layout/history/r/abc",
           "/admin/templates/layout/extra"}) {
    Reply got = Get(path, cookie_);
    EXPECT_EQ(missing.status, got.status) << path;
    EXPECT_EQ(missing.body, got.body) << path;
  }
}

TEST_F(TplAdmin, EditPageShowsKindContractAndForms) {
  Open({"core.pane"});
  Reply page = EditPage("recent");
  EXPECT_EQ(200, page.status);
  const std::string & b = page.body;
  /* The kind, and its effective contract with types and descriptions. */
  EXPECT_TRUE(Has(b, "Pane"));
  EXPECT_TRUE(Has(b, "<code>title</code> (text)"));
  EXPECT_TRUE(Has(b, "<code>summary</code> (text)"));
  EXPECT_TRUE(Has(b, "The title of the post being shown."));
  EXPECT_TRUE(Has(b, "<code>url</code> takes one argument"));
  /* The body in a textarea, three buttons, signed forms that post. */
  EXPECT_TRUE(Has(b, "<form method=\"post\" action=\"/admin/templates/recent\">"));
  EXPECT_TRUE(Has(b, "<textarea name=\"body\""));
  for (const char * label : {">Validate</button>", ">Preview</button>", ">Save</button>"}) {
    EXPECT_TRUE(Has(b, label)) << label;
  }
  EXPECT_FALSE(InputValue(b, "token", 0).empty());
  EXPECT_FALSE(InputValue(b, "token", 1).empty());
  EXPECT_NE(InputValue(b, "token", 0), InputValue(b, "token", 1));
  EXPECT_TRUE(Has(b, "name=\"confirm\""));
  /* A link to the contract's documentation. */
  EXPECT_TRUE(Has(b, "documentation/templates.md"));
  /* No script of its own, no inline style, no handler: it works as a form. */
  EXPECT_EQ(1u, [&] {
    size_t n = 0;
    for (size_t at = b.find("<script"); at != std::string::npos; at = b.find("<script", at + 1)) {
      n += 1;
    }
    return n;
  }());
  EXPECT_TRUE(Has(b, "<script defer src=\"/defiant.js\""));
  EXPECT_FALSE(Has(b, " onclick="));
  EXPECT_FALSE(Has(b, " style="));
  EXPECT_FALSE(Has(b, "<style"));
  EXPECT_TRUE(ScansClean(b));
  ExpectSecurity(page);
  /* The history and the list link to each other. */
  EXPECT_TRUE(Has(b, "href=\"/admin/templates/recent/history\""));
  EXPECT_TRUE(Has(b, "href=\"/admin/templates\""));
}

/* -------------------------------------------------------------------- */
/* Validate and preview                                                   */
/* -------------------------------------------------------------------- */

TEST_F(TplAdmin, ValidateShowsTheErrorAndTheLine) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  const std::string bad = "<p>one</p>\n<p>two</p>\n<% use title; %><%= titl %>\n";
  std::string page = Fresh("recent");
  Reply sent;
  Reply shown = Act("recent", page, "validate", bad, "core.pane", "", &sent);
  /* A post answers with a redirect to a page, as every write does. */
  EXPECT_EQ(303, sent.status);
  EXPECT_TRUE(sent.body.empty());
  EXPECT_NE(std::string::npos, HeaderValue(sent, "Location").find("/admin/templates/recent/r/"));
  EXPECT_EQ(200, shown.status);
  /* The line of the mistake, and the name, in words. The line number is a
   * character reference: the scanner reads a bare integer as a row id. */
  EXPECT_TRUE(Has(shown.body, "Line &#51;:"));
  EXPECT_TRUE(Has(shown.body, "titl"));
  EXPECT_TRUE(ScansClean(shown.body));
  /* What the editor typed is still in the textarea, and nothing is stored. */
  EXPECT_TRUE(Has(shown.body, "&lt;p&gt;two&lt;/p&gt;"));
  EXPECT_EQ(before, Stored("recent", "body"));
  /* The control: a body that validates is told so, and is not an error list. */
  Reply fine = Act("recent", Fresh("recent"), "validate", kPaneBody, "core.pane", "");
  EXPECT_TRUE(Has(fine.body, "No problems found."));
  EXPECT_FALSE(Has(fine.body, "class=\"issues\""));
  EXPECT_EQ(before, Stored("recent", "body"));
  /* A parse error is reported with its line too. */
  Reply parse = Act("recent", Fresh("recent"), "validate", "<p>a</p>\n<% if %>", "core.pane", "");
  EXPECT_TRUE(Has(parse.body, "class=\"issues\""));
  EXPECT_TRUE(Has(parse.body, "Line &#50;:"));
}

TEST_F(TplAdmin, PreviewRendersSampleDataAndNothingIsWrittenOrShared) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  size_t revisions = Revisions("recent");
  uint64_t shared = gdefcore_render_shareable_count();
  Reply shown = Act("recent", Fresh("recent"), "preview", kPaneBody, "core.pane", "");
  EXPECT_EQ(200, shown.status);
  /* The sample data the contract's variable was bound to, printed escaped
   * inside the template's own markup, shown as text. */
  EXPECT_TRUE(Has(shown.body, "<pre class=\"preview\">"));
  EXPECT_TRUE(Has(shown.body, "&lt;b class=&quot;edited&quot;&gt;Sample title&lt;/b&gt;"));
  EXPECT_FALSE(Has(shown.body, "<b class=\"edited\">"));
  EXPECT_TRUE(Has(shown.body, "Nothing was saved."));
  EXPECT_TRUE(ScansClean(shown.body));
  /* Nothing was written, and nothing was offered to a shared cache. */
  EXPECT_EQ(before, Stored("recent", "body"));
  EXPECT_EQ(revisions, Revisions("recent"));
  EXPECT_EQ(shared, gdefcore_render_shareable_count())
      << "a preview was offered to the shared cache";
  /* The control: the counter does move for a render everyone would be served. */
  EXPECT_NE(std::string::npos, Visit("/").size());
  EXPECT_GT(gdefcore_render_shareable_count(), shared);
  /* And a visitor is not shown the preview. */
  EXPECT_FALSE(Has(Visit("/"), "edited"));
  /* An invalid body still gets an answer: the errors, and no write. */
  Reply bad = Act("recent", Fresh("recent"), "preview", "<% use nope; %>", "core.pane", "");
  EXPECT_EQ(200, bad.status);
  EXPECT_TRUE(Has(bad.body, "class=\"issues\""));
  EXPECT_EQ(before, Stored("recent", "body"));
}

/* -------------------------------------------------------------------- */
/* Save                                                                   */
/* -------------------------------------------------------------------- */

TEST_F(TplAdmin, SaveWritesOneRevisionWithTheActorAndRendersNext) {
  Open({"core.pane"});
  const std::string marked = "<% use title; %><div id=\"x\">EDITED-RECENT <%= title %></div>";
  size_t revisions = Revisions("recent");
  /* Control: before the save, the front page does not show it. */
  EXPECT_FALSE(Has(Visit("/"), "EDITED-RECENT"));
  Reply sent;
  Reply shown = Act("recent", Fresh("recent"), "save", marked, "core.pane", "recent", &sent);
  EXPECT_EQ(303, sent.status);
  EXPECT_TRUE(Has(shown.body, "Saved."));
  EXPECT_EQ(marked, Stored("recent", "body"));
  EXPECT_EQ(revisions + 1, Revisions("recent"));
  EXPECT_EQ(AdminPrincipal(id_), LatestActor("recent"));
  /* The next request renders it: no restart. */
  EXPECT_TRUE(Has(Visit("/"), "EDITED-RECENT"));
  /* The saved page shows the stored text and a fresh form. */
  EXPECT_TRUE(Has(shown.body, "EDITED-RECENT"));
  EXPECT_FALSE(InputValue(shown.body, "token", 1).empty());
  EXPECT_TRUE(ScansClean(shown.body));
}

TEST_F(TplAdmin, SaveNeedsTheTypedName) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  size_t revisions = Revisions("recent");
  const std::string page = Fresh("recent");
  /* Nothing typed, the wrong name, and the right name in the wrong case. */
  for (const char * typed : {"", "entity", "Recent", "recent "}) {
    Reply shown = Act("recent", page, "save", kPaneBody, "core.pane", typed);
    EXPECT_EQ(200, shown.status);
    EXPECT_TRUE(Has(shown.body, "name typed does not match")) << "[" << typed << "]";
    EXPECT_EQ(before, Stored("recent", "body")) << "[" << typed << "]";
    EXPECT_EQ(revisions, Revisions("recent"));
    /* The editor's text is given back. */
    EXPECT_TRUE(Has(shown.body, "class=&quot;edited&quot;"));
  }
  /* The control: the same form with the name typed writes. */
  Reply written = Act("recent", Fresh("recent"), "save", kPaneBody, "core.pane", "recent");
  EXPECT_TRUE(Has(written.body, "Saved."));
  EXPECT_EQ(kPaneBody, Stored("recent", "body"));
}

TEST_F(TplAdmin, SaveOfAnInvalidBodyIsRefusedWithTheLine) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  size_t revisions = Revisions("recent");
  const std::string bad = "<p>a</p>\n<p>b</p>\n<%= titl %>";
  Reply shown = Act("recent", Fresh("recent"), "save", bad, "core.pane", "recent");
  EXPECT_TRUE(Has(shown.body, "Not saved: the template does not validate."));
  EXPECT_TRUE(Has(shown.body, "Line &#51;:"));
  EXPECT_TRUE(Has(shown.body, "titl"));
  EXPECT_EQ(before, Stored("recent", "body"));
  EXPECT_EQ(revisions, Revisions("recent"));
  EXPECT_TRUE(ScansClean(shown.body));
  /* The control: the corrected body is saved. */
  Reply fixed = Act("recent", Fresh("recent"), "save", "<p>a</p>\n<p>b</p>\n<% use title; %><%= title %>",
      "core.pane", "recent");
  EXPECT_TRUE(Has(fixed.body, "Saved."));
}

TEST_F(TplAdmin, AModuleDenyRefusesTheSaveWithAFixedSentence) {
  Open({"core.pane", "core.page"});
  Operator("layout", Stored("layout", "body"), "core.page", "internal");
  EnableJudge("public");
  const std::string before = Stored("layout", "body");
  const std::string edited = before + "<!-- edit -->";
  /* The classification does not match the grant: refused, nothing stored. */
  Reply shown = Act("layout", Fresh("layout"), "save", edited, "core.page", "layout");
  EXPECT_TRUE(Has(shown.body, "That edit was refused. Nothing was saved."));
  EXPECT_FALSE(Has(shown.body, "judge"));
  EXPECT_FALSE(Has(shown.body, "classification"));
  EXPECT_EQ(before, Stored("layout", "body"));
  EXPECT_GE(judge_.calls, 1) << "the hook was never asked";
  EXPECT_EQ("core.page", judge_.kind);
  EXPECT_EQ("internal", judge_.classification);
  /* Control: the same save, once the classification matches the grant. */
  judge_.allowed = "internal";
  Reply allowed = Act("layout", Fresh("layout"), "save", edited, "core.page", "layout");
  EXPECT_TRUE(Has(allowed.body, "Saved."));
  EXPECT_EQ(edited, Stored("layout", "body"));
  /* Control: the module has no opinion about a pane. */
  judge_.allowed = "public";
  Reply pane = Act("recent", Fresh("recent"), "save", kPaneBody, "core.pane", "recent");
  EXPECT_TRUE(Has(pane.body, "Saved."));
  EXPECT_EQ(kPaneBody, Stored("recent", "body"));
}

TEST_F(TplAdmin, AStaleFormIsAConflictAndNothingIsOverwritten) {
  Open({"core.pane"});
  const std::string theirs = "<% use title; %><i>theirs</i>";
  const std::string mine = "<% use title; %><i>MINE-UNSAVED</i>";
  /* Two tabs open on the same template. */
  const std::string old_page = Fresh("recent");
  Reply first = Act("recent", Fresh("recent"), "save", theirs, "core.pane", "recent");
  EXPECT_TRUE(Has(first.body, "Saved."));
  EXPECT_EQ(theirs, Stored("recent", "body"));
  size_t revisions = Revisions("recent");
  /* The older tab saves: refused as a conflict. */
  Reply shown = Act("recent", old_page, "save", mine, "core.pane", "recent");
  EXPECT_TRUE(Has(shown.body, "this template changed while you were editing it"));
  EXPECT_EQ(theirs, Stored("recent", "body"));
  EXPECT_EQ(revisions, Revisions("recent"));
  /* The editor's own text is given back, and the stored version is shown. */
  EXPECT_TRUE(Has(shown.body, "MINE-UNSAVED"));
  EXPECT_TRUE(Has(shown.body, "The stored version"));
  EXPECT_TRUE(Has(shown.body, "theirs"));
  /* The control: the same text from the page the conflict gave back saves. */
  Reply again = Act("recent", shown.body, "save", mine, "core.pane", "recent");
  EXPECT_TRUE(Has(again.body, "Saved."));
  EXPECT_EQ(mine, Stored("recent", "body"));
  EXPECT_EQ(revisions + 1, Revisions("recent"));
}

TEST_F(TplAdmin, MovingToAnotherKindNeedsBothKindsAndTheNewContract) {
  Open({"core.pane"});
  const std::string plain = "<p>fixed</p>";
  /* The kind the principal may not edit is not offered, and not named. */
  Reply page = EditPage("recent");
  EXPECT_TRUE(Has(page.body, "<option value=\"core.pane\" selected>Pane</option>"));
  EXPECT_FALSE(Has(page.body, "core.page"));
  EXPECT_FALSE(Has(page.body, "Page layout"));
  /* Posting it anyway is refused as a kind that is not there. */
  Reply shown = Act("recent", page.body, "save", plain, "core.page", "recent");
  EXPECT_TRUE(Has(shown.body, "choose one of the kinds listed"));
  EXPECT_EQ("core.pane", Stored("recent", "kind"));
  EXPECT_NE(plain, Stored("recent", "body"));
  Reply nowhere = Act("recent", Fresh("recent"), "save", plain, "no.such.kind", "recent");
  EXPECT_EQ(shown.body.find("choose one of the kinds listed") != std::string::npos,
            nowhere.body.find("choose one of the kinds listed") != std::string::npos);
  EXPECT_EQ("core.pane", Stored("recent", "kind"));
  /* With both permissions the move is offered, and is checked against the new
   * contract: `title` is not on a page's. */
  GrantTemplateKinds(id_, {"core.page"});
  Reply both = EditPage("recent");
  EXPECT_TRUE(Has(both.body, "<option value=\"core.page\">Page layout</option>"));
  Reply invalid = Act("recent", both.body, "save", kPaneBody, "core.page", "recent");
  EXPECT_TRUE(Has(invalid.body, "does not validate"));
  EXPECT_EQ("core.pane", Stored("recent", "kind"));
  /* The control: a body both contracts accept moves. */
  Reply moved = Act("recent", Fresh("recent"), "save", plain, "core.page", "recent");
  EXPECT_TRUE(Has(moved.body, "Saved."));
  EXPECT_EQ("core.page", Stored("recent", "kind"));
  EXPECT_EQ(plain, Stored("recent", "body"));
}

/* -------------------------------------------------------------------- */
/* History and revert                                                     */
/* -------------------------------------------------------------------- */

TEST_F(TplAdmin, HistoryListsRevisionsAndRevertRestoresTheOldBody) {
  Open({"core.pane"});
  const std::string original = Stored("recent", "body");
  Reply history = Get("/admin/templates/recent/history", cookie_);
  EXPECT_EQ(200, history.status);
  /* Before any edit there is nothing earlier to restore. */
  EXPECT_TRUE(Has(history.body, "There is no earlier version to restore."));
  EXPECT_FALSE(Has(history.body, "Revert the latest change"));
  ASSERT_TRUE(Has(Act("recent", Fresh("recent"), "save", kPaneBody, "core.pane", "recent").body, "Saved."));
  history = Get("/admin/templates/recent/history", cookie_);
  EXPECT_TRUE(Has(history.body, "The current version, by "));
  EXPECT_TRUE(Has(history.body, AdminPrincipal(id_).c_str()));
  EXPECT_TRUE(Has(history.body, "the installation"));
  EXPECT_TRUE(Has(history.body, "Revert the latest change"));
  EXPECT_TRUE(ScansClean(history.body));
  ExpectSecurity(history);
  size_t revisions = Revisions("recent");
  std::string token = InputValue(history.body, "token", 0);
  ASSERT_FALSE(token.empty());
  /* Without the typed name nothing is reverted. */
  Reply wrong = PostForm("/admin/templates/recent/history", cookie_,
      "token=" + token + "&confirm=nope&do=revert");
  EXPECT_TRUE(Has(wrong.body, "name typed does not match"));
  EXPECT_EQ(kPaneBody, Stored("recent", "body"));
  EXPECT_EQ(revisions, Revisions("recent"));
  /* The control: with it, a new revision restores the old body, by the actor. */
  Reply sent = PostRaw("/admin/templates/recent/history", cookie_,
      "token=" + token + "&confirm=recent&do=revert");
  EXPECT_EQ(303, sent.status);
  Reply done = Get(HeaderValue(sent, "Location").c_str(), cookie_);
  EXPECT_TRUE(Has(done.body, "Reverted."));
  EXPECT_EQ(original, Stored("recent", "body"));
  EXPECT_EQ(revisions + 1, Revisions("recent"));
  EXPECT_EQ(AdminPrincipal(id_), LatestActor("recent"));
  /* The old token is stale now: the same post does nothing more. */
  Reply stale = PostForm("/admin/templates/recent/history", cookie_,
      "token=" + token + "&confirm=recent&do=revert");
  EXPECT_TRUE(Has(stale.body, "Not reverted"));
  EXPECT_EQ(revisions + 1, Revisions("recent"));
}

/* -------------------------------------------------------------------- */
/* A hostile body, and the flash                                          */
/* -------------------------------------------------------------------- */

TEST_F(TplAdmin, AHostileStoredBodyCannotBreakTheEditPage) {
  Open({"core.pane"});
  const std::string hostile = "</textarea><script>alert(1)</script><!--\"'&amp;";
  Operator("recent", hostile, "core.pane");
  Reply page = EditPage("recent");
  EXPECT_EQ(200, page.status);
  /* The text is in the textarea as text: every angle bracket is an entity. */
  EXPECT_TRUE(Has(page.body, "&lt;/textarea&gt;&lt;script&gt;alert(1)&lt;/script&gt;&lt;!--"));
  EXPECT_TRUE(Has(page.body, "&amp;amp;"));
  EXPECT_FALSE(Has(page.body, "alert(1)</script>"));
  /* Exactly one textarea closes, and the only script is the server's own. */
  size_t closes = 0;
  for (size_t at = page.body.find("</textarea>"); at != std::string::npos;
       at = page.body.find("</textarea>", at + 1)) {
    closes += 1;
  }
  EXPECT_EQ(1u, closes);
  EXPECT_FALSE(Has(page.body, "<script>"));
  /* Saving what is shown, unchanged, stores the same bytes. */
  Reply saved = Act("recent", page.body, "save", hostile, "core.pane", "recent");
  EXPECT_TRUE(Has(saved.body, "Saved."));
  EXPECT_EQ(hostile, Stored("recent", "body"));
  /* The preview of it prints it escaped. */
  Reply preview = Act("recent", Fresh("recent"), "preview", hostile, "core.pane", "");
  EXPECT_TRUE(Has(preview.body, "&lt;script&gt;"));
  EXPECT_FALSE(Has(preview.body, "alert(1)</script>"));
  /* A body that begins with a line break keeps it (the parser drops the first). */
  Operator("recent", "\nline\n", "core.pane");
  Reply lead = Act("recent", Fresh("recent"), "save", "\nline\n", "core.pane", "recent");
  EXPECT_TRUE(Has(lead.body, "Saved."));
  EXPECT_EQ("\nline\n", Stored("recent", "body"));
}

TEST_F(TplAdmin, WhatARedirectCarriesBelongsToOneSession) {
  Open({"core.pane"});
  Reply sent;
  Act("recent", Fresh("recent"), "validate", "<p>DRAFT-SECRET</p>", "core.pane", "", &sent);
  std::string where = HeaderValue(sent, "Location");
  ASSERT_FALSE(where.empty());
  /* The session that posted it reads it (the control)... */
  EXPECT_TRUE(Has(Get(where.c_str(), cookie_).body, "DRAFT-SECRET"));
  /* ...another sign-in of the same admin does not. */
  std::string other = CookieLine(Login("ada", "secret"));
  Reply theirs = Get(where.c_str(), other);
  EXPECT_EQ(200, theirs.status);
  EXPECT_FALSE(Has(theirs.body, "DRAFT-SECRET"));
  EXPECT_TRUE(Has(theirs.body, "<textarea"));
  /* Nor does an anonymous visitor see anything. */
  Reply visitor = Get(where.c_str(), std::string());
  EXPECT_FALSE(Has(visitor.body, "DRAFT-SECRET"));
  EXPECT_FALSE(Has(visitor.body, "textarea"));
}

TEST_F(TplAdmin, ASignedFormIsRequired) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  /* No token, a made-up one, and the check token offered for a save. */
  Reply none = PostForm("/admin/templates/recent", cookie_,
      "kind=core.pane&body=" + Enc(kPaneBody) + "&confirm=recent&do=save");
  EXPECT_TRUE(Has(none.body, "out of date"));
  Reply fake = PostForm("/admin/templates/recent", cookie_,
      "token=nope&token=nope&kind=core.pane&body=" + Enc(kPaneBody) + "&confirm=recent&do=save");
  EXPECT_TRUE(Has(fake.body, "out of date"));
  const std::string page = Fresh("recent");
  Reply swapped = PostForm("/admin/templates/recent", cookie_,
      "token=" + InputValue(page, "token", 0) + "&token=" + InputValue(page, "token", 0) +
          "&kind=core.pane&body=" + Enc(kPaneBody) + "&confirm=recent&do=save");
  EXPECT_TRUE(Has(swapped.body, "out of date"));
  EXPECT_EQ(before, Stored("recent", "body"));
  /* A foreign Origin writes nothing at all. */
  Reply foreign = PostRaw("/admin/templates/recent", cookie_, FormBody(page, "save", kPaneBody,
      "core.pane", "recent"), "http://evil.example");
  EXPECT_EQ(403, foreign.status);
  EXPECT_EQ(before, Stored("recent", "body"));
  /* The control: the page's own tokens save. */
  Reply ok = Act("recent", page, "save", kPaneBody, "core.pane", "recent");
  EXPECT_TRUE(Has(ok.body, "Saved."));
}

TEST_F(TplAdmin, TextThatCannotBeKeptIsRefused) {
  Open({"core.pane"});
  const std::string before = Stored("recent", "body");
  const std::string page = Fresh("recent");
  /* A NUL would end the text for the validator and not for the store. */
  Reply nul = PostForm("/admin/templates/recent", cookie_,
      "token=" + InputValue(page, "token", 0) + "&token=" + InputValue(page, "token", 1) +
          "&kind=core.pane&body=%3Cp%3Ea%00%3C%25+nope+%25%3E&confirm=recent&do=save");
  EXPECT_TRUE(Has(nul.body, "cannot be saved"));
  EXPECT_EQ(before, Stored("recent", "body"));
  /* Too large (more than the editor keeps, less than the server reads). */
  std::string big(61 * 1024 + 100, 'x');
  Reply large = PostForm("/admin/templates/recent", cookie_,
      FormBody(page, "save", big, "core.pane", "recent"));
  EXPECT_TRUE(Has(large.body, "the template is too large"));
  EXPECT_EQ(before, Stored("recent", "body"));
  /* The control: ordinary text saves, and a CRLF form is kept with LF. */
  Reply ok = PostForm("/admin/templates/recent", cookie_,
      "token=" + InputValue(page, "token", 0) + "&token=" + InputValue(page, "token", 1) +
          "&kind=core.pane&body=%3Cp%3Ea%0D%0Ab%3C%2Fp%3E&confirm=recent&do=save");
  EXPECT_TRUE(Has(ok.body, "Saved."));
  EXPECT_EQ("<p>a\nb</p>", Stored("recent", "body"));
}

TEST_F(TplAdmin, ANameWithSpacesAndMarkupIsAddressable) {
  Open({"core.pane"});
  /* A template an operator named with a space and an ampersand. */
  {
    GDEFCORE_Catalog * catalog = Catalog();
    char tid[GDEFCORE_PUBLIC_ID_LEN + 1];
    GDEFCORE_Changeset * cs = NULL;
    GDEFCORE_Value values[3];
    const char * slots[3] = {"my page & <b>", "<p>odd</p>", "core.pane"};
    size_t lens[3] = {strlen(slots[0]), strlen(slots[1]), strlen(slots[2])};
    ASSERT_EQ(GDEFCORE_OK, gdefcore_entity_create(catalog, "core.template", 13, tid, NULL));
    Val(&values[0], "name", &slots[0], &lens[0]);
    Val(&values[1], "body", &slots[1], &lens[1]);
    Val(&values[2], "kind", &slots[2], &lens[2]);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_prepare(catalog, NULL, 0, &cs));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_save(cs, tid, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, 3));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_changeset_apply(cs, "core.template", 13));
    gdefcore_changeset_destroy(cs);
    gdefcore_catalog_destroy(catalog);
  }
  Reply list = Get("/admin/templates", cookie_);
  /* The link is percent-encoded, the text is escaped. */
  EXPECT_TRUE(Has(list.body, "href=\"/admin/templates/my%20page%20%26%20%3Cb%3E\""));
  EXPECT_TRUE(Has(list.body, ">my page &amp; &lt;b&gt;</a>"));
  EXPECT_FALSE(Has(list.body, "<b>"));
  Reply page = Get("/admin/templates/my%20page%20%26%20%3Cb%3E", cookie_);
  EXPECT_TRUE(Has(page.body, "<h1>Template my page &amp; &lt;b&gt;</h1>"));
  EXPECT_TRUE(Has(page.body, "&lt;p&gt;odd&lt;/p&gt;"));
  /* Saved by its name typed as it is. */
  Reply sent = PostRaw("/admin/templates/my%20page%20%26%20%3Cb%3E", cookie_,
      FormBody(page.body, "save", "<p>changed</p>", "core.pane", "my page & <b>"));
  EXPECT_EQ(303, sent.status);
  EXPECT_EQ(0u, HeaderValue(sent, "Location")
                    .find("/admin/templates/my%20page%20%26%20%3Cb%3E/r/"));
  EXPECT_TRUE(Has(Get(HeaderValue(sent, "Location").c_str(), cookie_).body, "Saved."));
  EXPECT_EQ("<p>changed</p>", Stored("my page & <b>", "body"));
}

TEST_F(TplAdmin, AVisitorAndAnOrdinaryAccountSeeNothing) {
  Open({"core.pane"});
  AddUser(id_, "bob", "hunter2");
  Reply unknown = Get("/no-such-path", std::string());
  Reply bob = Login("bob", "hunter2");
  for (const char * path : {"/admin/templates", "/admin/templates/recent",
           "/admin/templates/recent/history"}) {
    Reply visitor = Get(path, std::string());
    EXPECT_EQ(unknown.status, visitor.status) << path;
    EXPECT_EQ(unknown.body, visitor.body) << path;
    Reply member = Get(path, CookieLine(bob));
    EXPECT_EQ(unknown.status, member.status) << path;
    EXPECT_EQ(unknown.body, member.body) << path;
  }
  EXPECT_EQ(404, PostRaw("/admin/templates/recent", std::string(), "do=save").status);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
