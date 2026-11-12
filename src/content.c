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
 * The content administration: /admin/content lists the types a person may
 * manage, /admin/content/<type> lists the entities of one, and
 * /admin/content/<type>/<id> edits or deletes one. Nothing here names a type.
 * The pages are made from the type's own declaration (its fields, its kind),
 * so a type defined at any time has them, and what a person may do is what the
 * core's access answers say for that type and that entity.
 *
 * A type is there for whoever may create, update or delete some entity of it,
 * and for no one else: a person who may not is given the answer a type that
 * does not exist is given. Every write is a signed form (the token binds the
 * session, the purpose and the entity), a changeset the core rates, and a 303.
 * A refused form is drawn again with what was typed, and nothing is written.
 */

#include "content.h"

#include "adminkit.h"
#include "nav.h"

#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/message.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTENT_BASE "/admin/content"
#define CONTENT_PAGE_SIZE 25
#define CONTENT_COLUMNS 3
#define CONTENT_CELL_MAX 80
#define CONTENT_TYPE_MAX 63
#define CONTENT_FIELDS_MAX 64
#define CONTENT_TEXT_MAX 8192

struct Ctx {
  const GDEFWEB_Allocator * alloc;
  GDEFCORE_Catalog * catalog;
  GDEFCORE_Request * request;
  GDEFCORE_Keyring * keyring;
  const uint8_t * root;
  size_t root_len;
  const char * session;
  size_t session_len;
  GDEFCORE_Routes * routes;
};

/* What a page says about the last write, when it is drawn again. */
enum Notice {
  NOTICE_NONE = 0,
  NOTICE_INVALID,
  NOTICE_STALE,
  NOTICE_CONFIRM
};

static int valid_segment(const char * s, size_t n, size_t max) {
  size_t i;
  if (n == 0 || n > max) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')) {
      return 0;
    }
  }
  return 1;
}

/* The kind of the type if this tenant defines it (and it is not a built-in), else 0. */
static int type_kind_of(struct Ctx * c, const char * name, size_t name_len) {
  int kinds[2] = {GDEFCORE_TYPE_CONTENT, GDEFCORE_TYPE_CONFIG};
  int k;
  for (k = 0; k < 2; k++) {
    char ** names = NULL;
    size_t * lens = NULL;
    size_t count = 0;
    size_t i;
    int found = 0;
    if (gdefcore_type_names(c->catalog, kinds[k], &names, &lens, &count) != GDEFCORE_OK) {
      continue;
    }
    for (i = 0; i < count && !found; i++) {
      found = lens[i] == name_len && memcmp(names[i], name, name_len) == 0;
    }
    gdefcore_values_free(c->catalog, names, lens, count);
    if (found) {
      return kinds[k];
    }
  }
  return 0;
}

static int allowed(struct Ctx * c, const char * action, const char * type,
    const char * id) {
  int ok = 0;
  if (gdefcore_access_can(c->catalog, c->request, action, strlen(action), type,
          strlen(type), id, &ok) != GDEFCORE_OK) {
    return 0;
  }
  return ok;
}

static int may_create(struct Ctx * c, const char * type) {
  int ok = 0;
  if (gdefcore_access_can_create(c->catalog, c->request, NULL, 0, type, strlen(type),
          NULL, NULL, 0, &ok) != GDEFCORE_OK) {
    return 0;
  }
  return ok;
}

/* Whether the person may do something to some entity of the type: the type is
 * there for them. A decision about one entity is asked of that entity. */
static int may_touch(struct Ctx * c, const char * type) {
  return may_create(c, type) || allowed(c, GDEFCORE_ACTION_UPDATE, type, NULL) ||
         allowed(c, GDEFCORE_ACTION_DELETE, type, NULL);
}

static int has_revision(struct Ctx * c, const char * id) {
  size_t count = 0;
  return gdefcore_entity_revision_count(c->catalog, id, GDEFCORE_PUBLIC_ID_LEN, &count) ==
             GDEFCORE_OK &&
         count > 0;
}

/* Whether @p id is an entity (or a reservation) of @p type. */
static int is_entity_of(struct Ctx * c, const char * type, const char * id) {
  char canon[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * got = NULL;
  size_t got_n = 0;
  int same;
  if (strlen(id) != GDEFCORE_PUBLIC_ID_LEN ||
      gdefcore_id_canonical(id, strlen(id), canon) != GDEFCORE_OK ||
      strcmp(canon, id) != 0 ||
      gdefcore_entity_type(c->catalog, id, GDEFCORE_PUBLIC_ID_LEN, &got, &got_n) !=
          GDEFCORE_OK) {
    return 0;
  }
  same = got_n == strlen(type) && memcmp(got, type, got_n) == 0;
  gdefcore_text_free(c->catalog, got);
  return same;
}

/* Whether the id a token was signed for is @p id, which may be spelled as the
 * principal's is: the core signs the canonical form. */
static int same_target(const char * target, const char * id) {
  char canon[GDEFCORE_PUBLIC_ID_LEN + 1];
  return gdefcore_id_canonical(id, strlen(id), canon) == GDEFCORE_OK &&
         memcmp(target, canon, GDEFCORE_PUBLIC_ID_LEN) == 0;
}

/* Whether the core accepts these values for this form: each against the
 * constraints of its field (a length, a range, a pattern, a required field).
 * The token has been checked; this opens it again for the values it carries. */
static int values_fit(struct Ctx * c, const char * body, size_t body_len,
    const GDEFCORE_Value * values, size_t nvalues) {
  char * token = NULL;
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char * resigned = NULL;
  size_t resigned_n = 0;
  int ok = 0;
  GDEFCORE_Value given[CONTENT_FIELDS_MAX];
  size_t ngiven = 0;
  size_t i;
  /* A field with no value is a field the form does not carry. */
  for (i = 0; i < nvalues && ngiven < CONTENT_FIELDS_MAX; i++) {
    if (values[i].count > 0) {
      given[ngiven++] = values[i];
    }
  }
  values = given;
  nvalues = ngiven;
  if (form_field_alloc(body, body_len, "token", 0, &token, &token_n) == 0 &&
      b64_decode(token, raw, sizeof raw, &raw_n) == 0 &&
      gdefcore_form_resign(c->catalog, c->request, c->keyring, c->root, c->root_len,
          c->session, c->session_len, (const char *)raw, raw_n, values, nvalues,
          &resigned, &resigned_n) == GDEFCORE_OK) {
    ok = 1;
  }
  gdefcore_text_free(c->catalog, resigned);
  if (token != NULL) {
    wipe(token, token_n);
  }
  free(token);
  wipe(raw, sizeof raw);
  return ok;
}

/* A purpose's token, checked: genuine for this session, made for this purpose
 * and this entity. Its base version is given back. */
static int token_ok(struct Ctx * c, const char * body, size_t body_len,
    const char * form_id, const char * id, uint64_t * base) {
  char * token = NULL;
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char got_form[32];
  size_t got_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t b = 0;
  int ok = 0;
  if (form_field_alloc(body, body_len, "token", 0, &token, &token_n) == 0 &&
      b64_decode(token, raw, sizeof raw, &raw_n) == 0 &&
      gdefcore_form_check(c->catalog, c->request, c->keyring, c->root, c->root_len,
          c->session, c->session_len, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&
      token_meta(raw, raw_n, got_form, sizeof got_form, &got_len, target, &b) == 0 &&
      same_target(target, id) &&
      got_len == strlen(form_id) && memcmp(got_form, form_id, got_len) == 0) {
    ok = 1;
    if (base != NULL) {
      *base = b;
    }
  }
  if (token != NULL) {
    wipe(token, token_n);
  }
  free(token);
  wipe(raw, sizeof raw);
  return ok;
}

/* Sign a form for @p target. The core holds a form to the constraints of the
 * type it is for, and a required field must be among the values a form carries:
 * the token carries each required field empty, as the post form always has. */
static int sign(struct Ctx * c, const char * form_id, const char * target, char ** out) {
  char * type = NULL;
  size_t type_n = 0;
  size_t nfields = 0;
  size_t i;
  size_t n = 0;
  GDEFCORE_Value values[CONTENT_FIELDS_MAX];
  const char * slots[CONTENT_FIELDS_MAX];
  size_t lens[CONTENT_FIELDS_MAX];
  char * names[CONTENT_FIELDS_MAX];
  int rc = -1;
  if (gdefcore_entity_type(c->catalog, target, GDEFCORE_PUBLIC_ID_LEN, &type, &type_n) !=
      GDEFCORE_OK) {
    return -1;
  }
  if (gdefcore_type_field_count(c->catalog, type, type_n, &nfields) == GDEFCORE_OK) {
    for (i = 0; i < nfields && n < CONTENT_FIELDS_MAX; i++) {
      char * name = NULL;
      size_t name_n = 0;
      int required = 0;
      int many = 0;
      if (gdefcore_type_field(c->catalog, type, type_n, i, &name, &name_n, &required,
              &many) != GDEFCORE_OK) {
        continue;
      }
      if (!required) {
        gdefcore_text_free(c->catalog, name);
        continue;
      }
      names[n] = name;
      memset(&values[n], 0, sizeof values[n]);
      values[n].size = sizeof values[n];
      values[n].field = name;
      values[n].field_len = name_n;
      slots[n] = "";
      lens[n] = 0;
      values[n].texts = &slots[n];
      values[n].text_lens = &lens[n];
      values[n].count = 1;
      n++;
    }
  }
  rc = sign_b64(c->alloc, c->catalog, c->request, c->keyring, c->root, c->root_len,
      c->session, c->session_len, form_id, target, values, n, out);
  for (i = 0; i < n; i++) {
    gdefcore_text_free(c->catalog, names[i]);
  }
  gdefcore_text_free(c->catalog, type);
  return rc;
}

static int hidden_token(struct Ctx * c, struct Page * page, const char * form_id,
    const char * target) {
  char * token = NULL;
  int rc;
  if (sign(c, form_id, target, &token) != 0) {
    return -1;
  }
  rc = page_token(page, token);
  gcu_allocator_free(c->alloc, token);
  return rc;
}

static int page_notice_text(struct Page * page, int notice) {
  const char * text = NULL;
  if (notice == NOTICE_INVALID) {
    text = "Not saved: a value does not fit the field it is for. Nothing was changed.";
  } else if (notice == NOTICE_STALE) {
    text = "Not saved: this was changed while you were editing. Nothing was changed.";
  } else if (notice == NOTICE_CONFIRM) {
    text = "Not done: the type name typed does not match. Nothing was changed.";
  }
  if (text == NULL) {
    return 0;
  }
  return page_str(page, "<p class=\"notice\" role=\"alert\">") != 0 ||
         page_str(page, text) != 0 || page_str(page, "</p>");
}

/* Section of a segment of the path, after @p prefix, up to the next slash. */
static const char * segment(const char * p, size_t * len) {
  const char * end = strchr(p, '/');
  *len = end == NULL ? strlen(p) : (size_t)(end - p);
  return p;
}

/* ---- the index -------------------------------------------------------- */

static int index_section(struct Ctx * c, struct Page * page, int kind,
    const char * heading, int * shown) {
  char ** names = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  size_t i;
  int opened = 0;
  if (gdefcore_type_names(c->catalog, kind, &names, &lens, &count) != GDEFCORE_OK) {
    return 0;
  }
  for (i = 0; i < count; i++) {
    char type[CONTENT_TYPE_MAX + 1];
    if (!valid_segment(names[i], lens[i], CONTENT_TYPE_MAX)) {
      continue;
    }
    memcpy(type, names[i], lens[i]);
    type[lens[i]] = '\0';
    if (!may_touch(c, type)) {
      continue;
    }
    if (!opened &&
        (page_str(page, "<h2>") != 0 || page_str(page, heading) != 0 ||
            page_str(page, "</h2><ul>") != 0)) {
      gdefcore_values_free(c->catalog, names, lens, count);
      return -1;
    }
    opened = 1;
    *shown += 1;
    if (page_str(page, "<li><a href=\"" CONTENT_BASE "/") != 0 ||
        page_esc(page, type, lens[i]) != 0 || page_str(page, "\">") != 0 ||
        page_esc(page, type, lens[i]) != 0 || page_str(page, "</a></li>") != 0) {
      gdefcore_values_free(c->catalog, names, lens, count);
      return -1;
    }
  }
  gdefcore_values_free(c->catalog, names, lens, count);
  return opened && page_str(page, "</ul>") != 0 ? -1 : 0;
}

static int index_page(struct Ctx * c, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  int shown = 0;
  memset(&page, 0, sizeof page);
  page.alloc = c->alloc;
  if (page_begin(&page, "Content") != 0 || page_str(&page, "<h1>Content</h1>") != 0 ||
      index_section(c, &page, GDEFCORE_TYPE_CONTENT, "Content", &shown) != 0 ||
      index_section(c, &page, GDEFCORE_TYPE_CONFIG, "Configuration", &shown) != 0 ||
      (shown == 0 && page_str(&page, "<p>There is no type you may manage.</p>") != 0)) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
}

/* ---- the list --------------------------------------------------------- */

/* One cell: the first value of a field the person may read, shortened. */
static int cell(struct Ctx * c, struct Page * page, const char * id, const char * field) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  int locked = 0;
  size_t n;
  int rc = 0;
  if (gdefcore_access_read(c->catalog, c->request, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2,
          field, strlen(field), &texts, &lens, &count, &locked) != GDEFCORE_OK) {
    return 0;
  }
  if (count > 0 && texts[0] != NULL) {
    n = lens[0];
    if (n > CONTENT_CELL_MAX) {
      n = CONTENT_CELL_MAX;
      while (n > 0 && ((unsigned char)texts[0][n] & 0xC0) == 0x80) {
        n--;
      }
    }
    rc = page_esc(page, texts[0], n) != 0 ||
         (n < lens[0] && page_str(page, "...") != 0);
  }
  gdefcore_values_free(c->catalog, texts, lens, count);
  return rc ? -1 : 0;
}

/* The names of the first few fields that hold something to read in a list. */
static size_t columns_of(struct Ctx * c, const char * type, char names[][64]) {
  size_t count = 0;
  size_t i;
  size_t n = 0;
  if (gdefcore_type_field_count(c->catalog, type, strlen(type), &count) != GDEFCORE_OK) {
    return 0;
  }
  for (i = 0; i < count && n < CONTENT_COLUMNS; i++) {
    char * name = NULL;
    size_t name_len = 0;
    int required = 0;
    int many = 0;
    int personal = 0;
    if (gdefcore_type_field(c->catalog, type, strlen(type), i, &name, &name_len,
            &required, &many) != GDEFCORE_OK) {
      continue;
    }
    if (gdefcore_type_field_personal(c->catalog, type, strlen(type), i, &personal) !=
            GDEFCORE_OK ||
        personal || name_len >= 64) {
      gdefcore_text_free(c->catalog, name);
      continue;
    }
    memcpy(names[n], name, name_len);
    names[n][name_len] = '\0';
    n++;
    gdefcore_text_free(c->catalog, name);
  }
  return n;
}

static int list_page(struct Ctx * c, const char * type, unsigned long page_no,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char names[CONTENT_COLUMNS][64];
  size_t ncols = columns_of(c, type, names);
  GDEFCORE_Access_List query;
  char ** ids = NULL;
  size_t count = 0;
  uint64_t total = 0;
  size_t i;
  size_t k;
  char buf[32];
  memset(&page, 0, sizeof page);
  page.alloc = c->alloc;
  memset(&query, 0, sizeof query);
  query.size = sizeof query;
  query.type_name = type;
  query.type_name_len = strlen(type);
  query.lang = "en";
  query.lang_len = 2;
  query.offset = (uint64_t)(page_no - 1) * CONTENT_PAGE_SIZE;
  query.limit = CONTENT_PAGE_SIZE;
  if (gdefcore_access_list_page(c->catalog, c->request, &query, &ids, &count, &total) !=
      GDEFCORE_OK) {
    ids = NULL;
    count = 0;
    total = 0;
  }
  if (page_begin(&page, type) != 0 || page_str(&page, "<h1>") != 0 ||
      page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "</h1>") != 0 ||
      page_str(&page, "<table class=\"content-list\"><thead><tr>") != 0) {
    goto fail;
  }
  for (k = 0; k < ncols; k++) {
    if (page_str(&page, "<th>") != 0 || page_esc(&page, names[k], strlen(names[k])) != 0 ||
        page_str(&page, "</th>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "<th></th></tr></thead><tbody>") != 0) {
    goto fail;
  }
  for (i = 0; i < count; i++) {
    int may_edit;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
      continue;
    }
    may_edit = allowed(c, GDEFCORE_ACTION_UPDATE, type, ids[i]) ||
               allowed(c, GDEFCORE_ACTION_DELETE, type, ids[i]);
    if (page_str(&page, "<tr>") != 0) {
      goto fail;
    }
    for (k = 0; k < ncols; k++) {
      if (page_str(&page, "<td>") != 0 || cell(c, &page, ids[i], names[k]) != 0 ||
          page_str(&page, "</td>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "<td>") != 0 ||
        (may_edit &&
            (page_str(&page, "<a href=\"" CONTENT_BASE "/") != 0 ||
                page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "/") != 0 ||
                page_esc(&page, ids[i], strlen(ids[i])) != 0 ||
                page_str(&page, "\">Edit</a>") != 0)) ||
        page_str(&page, "</td></tr>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</tbody></table>") != 0) {
    goto fail;
  }
  if (count == 0 && page_str(&page, "<p>Nothing here yet.</p>") != 0) {
    goto fail;
  }
  /* Paging: links by path, as a page of this server has no query. */
  if (page_no > 1) {
    snprintf(buf, sizeof buf, "%lu", page_no - 1);
    if (page_str(&page, "<p><a rel=\"prev\" href=\"" CONTENT_BASE "/") != 0 ||
        page_esc(&page, type, strlen(type)) != 0 ||
        (page_no - 1 == 1 ? 0 : page_str(&page, "/page/") != 0 || page_str(&page, buf) != 0) ||
        page_str(&page, "\">Newer</a></p>") != 0) {
      goto fail;
    }
  }
  if ((uint64_t)page_no * CONTENT_PAGE_SIZE < total) {
    snprintf(buf, sizeof buf, "%lu", page_no + 1);
    if (page_str(&page, "<p><a rel=\"next\" href=\"" CONTENT_BASE "/") != 0 ||
        page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "/page/") != 0 ||
        page_str(&page, buf) != 0 || page_str(&page, "\">Older</a></p>") != 0) {
      goto fail;
    }
  }
  if (may_create(c, type)) {
    if (page_str(&page, "<form method=\"post\" action=\"" CONTENT_BASE "/") != 0 ||
        page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "\">") != 0) {
      goto fail;
    }
    /* The token of the button is bound to the one who holds it: the form is
     * signed for an entity that is there, and the principal is. */
    {
      size_t who_n = 0;
      const char * who = gdefcore_request_principal(c->request, &who_n);
      char target[GDEFCORE_PUBLIC_ID_LEN + 1];
      if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
        goto fail;
      }
      memcpy(target, who, who_n);
      target[who_n] = '\0';
      if (hidden_token(c, &page, "content-new", target) != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "<p><button type=\"submit\" name=\"do\" value=\"new\">New "
                        "</button></p></form>") != 0) {
      goto fail;
    }
  }
  gdefcore_ids_free(c->catalog, ids, count);
  return take_page(&page, out);
fail:
  gdefcore_ids_free(c->catalog, ids, count);
  page_free(&page);
  return -1;
}

/* ---- the form of one entity ------------------------------------------- */

struct Posted {
  char * text[CONTENT_FIELDS_MAX];  /* what was typed, by field of the form */
  size_t len[CONTENT_FIELDS_MAX];
};

static void posted_free(struct Posted * p) {
  size_t i;
  for (i = 0; i < CONTENT_FIELDS_MAX; i++) {
    if (p->text[i] != NULL) {
      wipe(p->text[i], p->len[i]);
      free(p->text[i]);
    }
  }
  memset(p, 0, sizeof *p);
}

/* Whether this field is one the form carries: not personal (a personal value is
 * sealed for its subject, which this page does not hold), and, when the entity
 * is stored, one the person may update. */
static int field_editable(struct Ctx * c, const char * type, const char * id, int stored,
    const char * name, size_t name_len, int personal) {
  int ok = 0;
  if (personal) {
    return 0;
  }
  if (!stored) {
    return 1;
  }
  if (gdefcore_access_can_field(c->catalog, c->request, GDEFCORE_ACTION_UPDATE,
          strlen(GDEFCORE_ACTION_UPDATE), type, strlen(type), id, name, name_len, &ok) !=
      GDEFCORE_OK) {
    return 0;
  }
  return ok;
}

static int draw_form(struct Ctx * c, struct Page * page, const char * type, int kind,
    const char * id, int stored, const GDEFCORE_Form * form, const struct Posted * posted,
    int notice) {
  size_t nfields = gdefcore_form_field_count(form);
  size_t i;
  if (page_notice_text(page, notice) != 0 ||
      page_str(page, "<form method=\"post\" action=\"" CONTENT_BASE "/") != 0 ||
      page_esc(page, type, strlen(type)) != 0 || page_str(page, "/") != 0 ||
      page_esc(page, id, strlen(id)) != 0 || page_str(page, "\">") != 0 ||
      hidden_token(c, page, "content-save", id) != 0) {
    return -1;
  }
  for (i = 0; i < nfields && i < CONTENT_FIELDS_MAX; i++) {
    const char * name = NULL;
    size_t name_len = 0;
    int required = 0;
    int many = 0;
    int personal = 0;
    size_t min_len = 0;
    size_t max_len = 0;
    int has_range = 0;
    int64_t range_min = 0;
    int64_t range_max = 0;
    const char * pattern = NULL;
    size_t pattern_len = 0;
    const char * const * texts = NULL;
    const size_t * text_lens = NULL;
    size_t count = 0;
    size_t j;
    int area;
    if (gdefcore_form_field(form, i, &name, &name_len, &required, &many, &personal,
            &min_len, &max_len, &has_range, &range_min, &range_max, &pattern,
            &pattern_len, &texts, &text_lens, &count) != GDEFCORE_OK) {
      return -1;
    }
    if (!field_editable(c, type, id, stored, name, name_len, personal)) {
      continue;
    }
    area = many || max_len > 120;
    if (page_str(page, "<p><label>") != 0 || page_esc(page, name, name_len) != 0 ||
        (required && page_str(page, " (required)") != 0) ||
        page_str(page, area ? " <textarea name=\"" : " <input name=\"") != 0 ||
        page_esc(page, name, name_len) != 0 || page_str(page, "\"") != 0 ||
        (required && page_str(page, " required") != 0)) {
      return -1;
    }
    if (max_len > 0) {
      char lim[64];
      snprintf(lim, sizeof lim, " maxlength=\"%zu\"", max_len);
      if (page_str(page, lim) != 0) {
        return -1;
      }
    }
    if (area) {
      if (page_str(page, ">") != 0) {
        return -1;
      }
    } else if (page_str(page, " value=\"") != 0) {
      return -1;
    }
    if (posted != NULL && posted->text[i] != NULL) {
      if (page_esc(page, posted->text[i], posted->len[i]) != 0) {
        return -1;
      }
    } else {
      for (j = 0; j < count; j++) {
        if (texts == NULL || texts[j] == NULL) {
          continue;
        }
        if ((j > 0 && page_str(page, "\n") != 0) ||
            page_esc(page, texts[j], text_lens[j]) != 0) {
          return -1;
        }
      }
    }
    if (page_str(page, area ? "</textarea></label></p>" : "\"></label></p>") != 0) {
      return -1;
    }
  }
  if (kind == GDEFCORE_TYPE_CONFIG &&
      page_str(page, "<p><label>This is configuration. To change it, type the type's "
                     "name to confirm: <input name=\"confirm\" autocomplete=\"off\" "
                     "value=\"\"></label></p>") != 0) {
    return -1;
  }
  return page_str(page, "<p><button type=\"submit\" name=\"do\" value=\"save\">Save"
                        "</button></p></form>");
}

static int draw_delete(struct Ctx * c, struct Page * page, const char * type, int kind,
    const char * id) {
  if (page_str(page, "<h2>Delete</h2><form method=\"post\" action=\"" CONTENT_BASE "/") != 0 ||
      page_esc(page, type, strlen(type)) != 0 || page_str(page, "/") != 0 ||
      page_esc(page, id, strlen(id)) != 0 || page_str(page, "\">") != 0 ||
      hidden_token(c, page, "content-delete", id) != 0) {
    return -1;
  }
  if (kind == GDEFCORE_TYPE_CONFIG) {
    if (page_str(page, "<p><label>To delete this, type the type's name: <input "
                       "name=\"confirm\" autocomplete=\"off\" value=\"\"></label></p>") != 0) {
      return -1;
    }
  } else if (page_str(page, "<p><label><input type=\"checkbox\" name=\"confirm\" "
                            "value=\"yes\"> Yes, delete this.</label></p>") != 0) {
    return -1;
  }
  return page_str(page, "<p><button type=\"submit\" name=\"do\" value=\"delete\">Delete"
                        "</button></p></form>");
}

/* The entity's page: GET, or a refused post drawn again. */
static int edit_page(struct Ctx * c, const char * type, int kind, const char * id,
    const struct Posted * posted, int notice, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  GDEFCORE_Form * form = NULL;
  int stored = has_revision(c, id);
  int may_update = 1;
  GDEFCORE_Result rc;
  memset(&page, 0, sizeof page);
  page.alloc = c->alloc;
  if (stored) {
    may_update = allowed(c, GDEFCORE_ACTION_UPDATE, type, id);
    if (!may_update && !allowed(c, GDEFCORE_ACTION_DELETE, type, id)) {
      return unknown_admin("GET", out);
    }
    rc = may_update ? gdefcore_form_tree_for(c->catalog, c->request, id,
                          GDEFCORE_PUBLIC_ID_LEN, "en", 2, &form)
                    : GDEFCORE_OK;
  } else {
    if (!may_create(c, type)) {
      return unknown_admin("GET", out);
    }
    rc = gdefcore_form_tree(c->catalog, type, strlen(type), id, GDEFCORE_PUBLIC_ID_LEN,
        "en", 2, &form);
  }
  if (rc == GDEFCORE_ERR_INVALID) {
    return unknown_admin("GET", out);
  }
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  if (page_begin(&page, type) != 0 || page_str(&page, "<h1>") != 0 ||
      page_str(&page, stored ? "Edit " : "New ") != 0 ||
      page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "</h1>") != 0 ||
      (form != NULL && draw_form(c, &page, type, kind, id, stored, form, posted, notice) != 0) ||
      (form == NULL && page_notice_text(&page, notice) != 0) ||
      (stored && allowed(c, GDEFCORE_ACTION_DELETE, type, id) &&
          draw_delete(c, &page, type, kind, id) != 0) ||
      page_str(&page, "<p><a href=\"" CONTENT_BASE "/") != 0 ||
      page_esc(&page, type, strlen(type)) != 0 ||
      page_str(&page, "\">Back to the list</a></p>") != 0) {
    gdefcore_form_destroy(form);
    page_free(&page);
    return -1;
  }
  gdefcore_form_destroy(form);
  return take_page(&page, out);
}

/* ---- writes ----------------------------------------------------------- */

static int redirect_list(struct GDEFWEB_Admin_Reply * out, const char * type) {
  char loc[CONTENT_TYPE_MAX + sizeof CONTENT_BASE + 16];
  snprintf(loc, sizeof loc, CONTENT_BASE "/%s", type);
  return redirect_to(out, loc);
}

/* Split a many-valued text into its lines: pieces point into @p text, which is
 * changed (a CR is dropped, a line break ends a value). */
static size_t split_lines(char * text, size_t n, const char ** slots, size_t * lens,
    size_t cap) {
  size_t count = 0;
  size_t i = 0;
  while (i <= n && count < cap) {
    size_t start = i;
    while (i < n && text[i] != '\n') {
      i++;
    }
    {
      size_t end = i;
      if (end > start && text[end - 1] == '\r') {
        end--;
      }
      if (end > start) {
        slots[count] = text + start;
        lens[count] = end - start;
        count++;
      }
    }
    i++;
  }
  return count;
}

#define VALUE_SLOTS 32

/*
 * Save the form of one entity. Returns 0 with @p out set (a redirect, a refusal
 * drawn again, or the page that does not exist), -1 for an error that is not
 * the person's.
 */
static int save_post(struct Ctx * c, const char * type, int kind, const char * id,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  GDEFCORE_Form * form = NULL;
  struct Posted posted;
  GDEFCORE_Value values[CONTENT_FIELDS_MAX];
  const char * slots[CONTENT_FIELDS_MAX][VALUE_SLOTS];
  size_t lens[CONTENT_FIELDS_MAX][VALUE_SLOTS];
  char names[CONTENT_FIELDS_MAX][64];
  size_t nvalues = 0;
  size_t nfields;
  size_t i;
  uint64_t base = 0;
  int stored = has_revision(c, id);
  int notice = NOTICE_INVALID;
  int rc = -1;
  GDEFCORE_Changeset * cs = NULL;
  int risk = 0;
  GDEFCORE_Result r;
  memset(&posted, 0, sizeof posted);
  if (stored ? !allowed(c, GDEFCORE_ACTION_UPDATE, type, id) : !may_create(c, type)) {
    return unknown_admin("POST", out);
  }
  r = stored ? gdefcore_form_tree_for(c->catalog, c->request, id, GDEFCORE_PUBLIC_ID_LEN,
                   "en", 2, &form)
             : gdefcore_form_tree(c->catalog, type, strlen(type), id,
                   GDEFCORE_PUBLIC_ID_LEN, "en", 2, &form);
  if (r != GDEFCORE_OK) {
    return r == GDEFCORE_ERR_INVALID ? unknown_admin("POST", out) : -1;
  }
  nfields = gdefcore_form_field_count(form);
  /* What was typed, kept first, so that a refusal of any kind gives it back. */
  for (i = 0; i < nfields && i < CONTENT_FIELDS_MAX; i++) {
    const char * name = NULL;
    size_t name_len = 0;
    int required = 0;
    int many = 0;
    int personal = 0;
    size_t min_len = 0;
    size_t max_len = 0;
    int has_range = 0;
    int64_t range_min = 0;
    int64_t range_max = 0;
    const char * pattern = NULL;
    size_t pattern_len = 0;
    const char * const * texts = NULL;
    const size_t * text_lens = NULL;
    size_t count = 0;
    char field[64];
    if (gdefcore_form_field(form, i, &name, &name_len, &required, &many, &personal,
            &min_len, &max_len, &has_range, &range_min, &range_max, &pattern,
            &pattern_len, &texts, &text_lens, &count) != GDEFCORE_OK ||
        name_len == 0 || name_len >= sizeof field) {
      goto done;
    }
    memcpy(field, name, name_len);
    field[name_len] = '\0';
    if (!field_editable(c, type, id, stored, name, name_len, personal) ||
        form_field_alloc(body, body_len, field, 0, &posted.text[i], &posted.len[i]) != 0) {
      posted.text[i] = NULL;
    } else if (posted.len[i] > CONTENT_TEXT_MAX) {
      goto again_invalid;
    }
  }
  if (!token_ok(c, body, body_len, "content-save", id, &base) ||
      !base_matches(c->catalog, id, base)) {
    notice = NOTICE_STALE;
    goto again;
  }
  for (i = 0; i < nfields && i < CONTENT_FIELDS_MAX; i++) {
    const char * name = NULL;
    size_t name_len = 0;
    int required = 0;
    int many = 0;
    int personal = 0;
    size_t min_len = 0;
    size_t max_len = 0;
    int has_range = 0;
    int64_t range_min = 0;
    int64_t range_max = 0;
    const char * pattern = NULL;
    size_t pattern_len = 0;
    const char * const * texts = NULL;
    const size_t * text_lens = NULL;
    size_t count = 0;
    if (gdefcore_form_field(form, i, &name, &name_len, &required, &many, &personal,
            &min_len, &max_len, &has_range, &range_min, &range_max, &pattern,
            &pattern_len, &texts, &text_lens, &count) != GDEFCORE_OK ||
        name_len == 0 || name_len >= sizeof names[0]) {
      goto done;
    }
    if (!field_editable(c, type, id, stored, name, name_len, personal)) {
      continue;
    }
    memcpy(names[nvalues], name, name_len);
    names[nvalues][name_len] = '\0';
    if (posted.text[i] == NULL) {
      continue;
    }
    memset(&values[nvalues], 0, sizeof values[nvalues]);
    values[nvalues].size = sizeof values[nvalues];
    values[nvalues].field = names[nvalues];
    values[nvalues].field_len = name_len;
    values[nvalues].texts = slots[nvalues];
    values[nvalues].text_lens = lens[nvalues];
    if (many) {
      values[nvalues].count = split_lines(posted.text[i], posted.len[i], slots[nvalues],
          lens[nvalues], VALUE_SLOTS);
    } else {
      slots[nvalues][0] = posted.text[i];
      lens[nvalues][0] = posted.len[i];
      values[nvalues].count = posted.len[i] == 0 ? 0 : 1;
    }
    if (values[nvalues].count == 0) {
      /* Nothing typed. A required field is then refused. A field that holds
       * nothing stays as it is. One that held a value is cleared: a many field
       * by no values, a single one by an empty text (the core's rule). */
      if (required) {
        goto again_invalid;
      }
      if (count == 0) {
        continue;
      }
      if (!many) {
        slots[nvalues][0] = "";
        lens[nvalues][0] = 0;
        values[nvalues].count = 1;
      }
    }
    nvalues++;
  }
  if (!values_fit(c, body, body_len, values, nvalues)) {
    goto again_invalid;
  }
  {
    const char * who;
    size_t who_n = 0;
    who = gdefcore_request_principal(c->request, &who_n);
    if (gdefcore_changeset_prepare(c->catalog, who, who_n, &cs) != GDEFCORE_OK) {
      goto done;
    }
    gdefcore_changeset_set_request(cs, c->request);
    r = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, nvalues);
    if (r != GDEFCORE_OK) {
      if (r == GDEFCORE_ERR_OOM) {
        goto done;
      }
      goto again_invalid;
    }
    if (gdefcore_changeset_rate(cs, &risk) != GDEFCORE_OK) {
      goto done;
    }
    if (risk == GDEFCORE_RISK_HIGH) {
      char confirm[256];
      size_t confirm_n = sizeof confirm;
      char * typed = NULL;
      size_t typed_n = 0;
      confirm_n = touched_confirm(cs, confirm, sizeof confirm);
      if (form_field_alloc(body, body_len, "confirm", 0, &typed, &typed_n) != 0 ||
          typed_n != strlen(type) || memcmp(typed, type, typed_n) != 0 || confirm_n == 0) {
        free(typed);
        notice = NOTICE_CONFIRM;
        goto again;
      }
      free(typed);
      r = gdefcore_changeset_apply(cs, confirm, confirm_n);
    } else {
      r = gdefcore_changeset_apply(cs, NULL, 0);
    }
    if (r == GDEFCORE_ERR_OOM) {
      goto done;
    }
    if (r != GDEFCORE_OK) {
      notice = gdefcore_changeset_conflict(cs, NULL) != NULL ? NOTICE_STALE : NOTICE_INVALID;
      goto again;
    }
  }
  say(c->catalog, c->request, GDEFCORE_MESSAGE_STATUS, "Saved.");
  rc = redirect_list(out, type);
  (void)kind;
  goto done;
again_invalid:
  notice = NOTICE_INVALID;
again:
  gdefcore_form_destroy(form);
  form = NULL;
  gdefcore_changeset_destroy(cs);
  cs = NULL;
  rc = edit_page(c, type, kind, id, &posted, notice, out);
done:
  gdefcore_form_destroy(form);
  gdefcore_changeset_destroy(cs);
  posted_free(&posted);
  return rc;
}

static int delete_post(struct Ctx * c, const char * type, int kind, const char * id,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  uint64_t base = 0;
  GDEFCORE_Changeset * cs = NULL;
  char * typed = NULL;
  size_t typed_n = 0;
  int risk = 0;
  GDEFCORE_Result r;
  const char * who;
  size_t who_n = 0;
  int rc = -1;
  if (!has_revision(c, id) || !allowed(c, GDEFCORE_ACTION_DELETE, type, id)) {
    return unknown_admin("POST", out);
  }
  if (!token_ok(c, body, body_len, "content-delete", id, &base) ||
      !base_matches(c->catalog, id, base)) {
    return edit_page(c, type, kind, id, NULL, NOTICE_STALE, out);
  }
  if (form_field_alloc(body, body_len, "confirm", 0, &typed, &typed_n) != 0 ||
      (kind == GDEFCORE_TYPE_CONFIG
              ? !(typed_n == strlen(type) && memcmp(typed, type, typed_n) == 0)
              : !(typed_n == 3 && memcmp(typed, "yes", 3) == 0))) {
    free(typed);
    return edit_page(c, type, kind, id, NULL, NOTICE_CONFIRM, out);
  }
  free(typed);
  who = gdefcore_request_principal(c->request, &who_n);
  if (gdefcore_changeset_prepare(c->catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, c->request);
  r = gdefcore_entity_clear(cs, id, GDEFCORE_PUBLIC_ID_LEN);
  if (r == GDEFCORE_OK) {
    r = gdefcore_changeset_rate(cs, &risk);
  }
  if (r == GDEFCORE_OK) {
    if (risk == GDEFCORE_RISK_HIGH) {
      char confirm[256];
      size_t n = touched_confirm(cs, confirm, sizeof confirm);
      r = n > 0 ? gdefcore_changeset_apply(cs, confirm, n) : GDEFCORE_ERR_INVALID;
    } else {
      r = gdefcore_changeset_apply(cs, NULL, 0);
    }
  }
  if (r == GDEFCORE_ERR_OOM) {
    goto done;
  }
  if (r != GDEFCORE_OK) {
    say(c->catalog, c->request, GDEFCORE_MESSAGE_ERROR, "Not deleted.");
    rc = redirect_to(out, CONTENT_BASE);
    rc = redirect_list(out, type);
    goto done;
  }
  say(c->catalog, c->request, GDEFCORE_MESSAGE_STATUS, "Deleted.");
  rc = redirect_list(out, type);
done:
  gdefcore_changeset_destroy(cs);
  return rc;
}

/* The button of a list: a fresh entity is reserved and its form is the next page. */
static int new_post(struct Ctx * c, const char * type, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char loc[CONTENT_TYPE_MAX + GDEFCORE_PUBLIC_ID_LEN + sizeof CONTENT_BASE + 16];
  const char * who;
  size_t who_n = 0;
  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  if (!may_create(c, type)) {
    return unknown_admin("POST", out);
  }
  who = gdefcore_request_principal(c->request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return unknown_admin("POST", out);
  }
  memcpy(principal, who, who_n);
  principal[who_n] = '\0';
  if (!token_ok(c, body, body_len, "content-new", principal, NULL)) {
    say(c->catalog, c->request, GDEFCORE_MESSAGE_ERROR,
        "Not started: the form was out of date.");
    return redirect_list(out, type);
  }
  if (gdefcore_entity_create(c->catalog, type, strlen(type), id, NULL) != GDEFCORE_OK) {
    return -1;
  }
  snprintf(loc, sizeof loc, CONTENT_BASE "/%s/%s", type, id);
  return redirect_to(out, loc);
}

/* ---- dispatch --------------------------------------------------------- */

int gdefweb_content_path(const char * path) {
  size_t n;
  if (path == NULL) {
    return 0;
  }
  n = strlen(path);
  return (n == sizeof CONTENT_BASE - 1 && memcmp(path, CONTENT_BASE, n) == 0) ||
         (n > sizeof CONTENT_BASE && memcmp(path, CONTENT_BASE "/", sizeof CONTENT_BASE) == 0);
}

int gdefweb_content_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out) {
  struct Ctx c;
  int is_get = strcmp(method, "GET") == 0;
  const char * rest;
  const char * seg;
  size_t seg_n;
  char type[CONTENT_TYPE_MAX + 1];
  int kind;
  c.alloc = alloc;
  c.catalog = catalog;
  c.request = request;
  c.keyring = keyring;
  c.root = root;
  c.root_len = root_len;
  c.session = session;
  c.session_len = session_len;
  c.routes = routes;
  if (strcmp(path, CONTENT_BASE) == 0) {
    return is_get && gdefweb_nav_follows(routes, request, path, "core.admin-content")
               ? index_page(&c, out)
               : unknown_admin(method, out);
  }
  rest = path + sizeof CONTENT_BASE;
  seg = segment(rest, &seg_n);
  if (!valid_segment(seg, seg_n, CONTENT_TYPE_MAX)) {
    return unknown_admin(method, out);
  }
  memcpy(type, seg, seg_n);
  type[seg_n] = '\0';
  /* The route's own check, then the type: a type that is not there and a type
   * the person may not touch are one answer. */
  if (!gdefweb_nav_follows(routes, request, path, seg[seg_n] == '\0'
                                                      ? "core.admin-content-type"
                                                      : "core.admin-content-entity") &&
      !gdefweb_nav_follows(routes, request, path, "core.admin-content-page")) {
    return unknown_admin(method, out);
  }
  kind = type_kind_of(&c, type, seg_n);
  if (kind == 0 || !may_touch(&c, type)) {
    return unknown_admin(method, out);
  }
  if (seg[seg_n] == '\0') {
    return is_get ? list_page(&c, type, 1, out) : (strcmp(method, "POST") == 0
                       ? new_post(&c, type, body, body_len, out)
                       : unknown_admin(method, out));
  }
  rest = seg + seg_n + 1;
  if (strncmp(rest, "page/", 5) == 0) {
    char * end = NULL;
    unsigned long n = strtoul(rest + 5, &end, 10);
    if (!is_get || end == rest + 5 || *end != '\0' || n < 2 || n > 1000000UL) {
      return unknown_admin(method, out);
    }
    return list_page(&c, type, n, out);
  }
  if (!is_entity_of(&c, type, rest)) {
    return unknown_admin(method, out);
  }
  if (is_get) {
    return edit_page(&c, type, kind, rest, NULL, NOTICE_NONE, out);
  }
  {
    char * action = NULL;
    size_t action_n = 0;
    int rc;
    if (form_field_alloc(body, body_len, "do", 0, &action, &action_n) != 0) {
      free(action);
      return unknown_admin(method, out);
    }
    if (action_n == 4 && memcmp(action, "save", 4) == 0) {
      rc = save_post(&c, type, kind, rest, body, body_len, out);
    } else if (action_n == 6 && memcmp(action, "delete", 6) == 0) {
      rc = delete_post(&c, type, kind, rest, body, body_len, out);
    } else {
      rc = unknown_admin(method, out);
    }
    free(action);
    return rc;
  }
}
