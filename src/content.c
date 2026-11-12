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
 *
 * The form of an entity is made from what its type declares at every depth: a
 * control for each field type, a row to each value of a many field, a fieldset
 * to each item of a group (kept by id), a chooser of the entities the person
 * may see for a reference, and what is wrong told beside the control it is
 * about. A type with no personal field is saved as a tree; one with a personal
 * field is saved as values, which leaves what the form cannot carry alone.
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
  /* The core's own words for the last refusal of a save, shown with the notice
   * of it: a nested field is named by its path ("lines.sku: ..."). */
  char reason[256];
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

static int page_notice_text(struct Page * page, int notice, const char * reason) {
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
         page_str(page, text) != 0 ||
         (notice == NOTICE_INVALID && reason != NULL && reason[0] != '\0' &&
             (page_str(page, " ") != 0 || page_esc(page, reason, strlen(reason)) != 0)) ||
         page_str(page, "</p>");
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

/*
 * What a type declares, at every depth (gdefcore_type_tree_field), so that the
 * form is made from the declaration and nothing here names a type or a field.
 * Control names are the field's own name at the root; inside a group they are
 * `<group>-<item>-<field>`, because the names a browser sends back are compared
 * as sent and a field name is letters, digits and underscore only, so a dash
 * is never in one. An item also sends `<group>-<item>--i` (its id, 0 for a new
 * item: its presence is what says the item was on the form) and may send
 * `<group>-<item>--rm` (take it away).
 */
#define SCHEMA_MAX 128
#define NAME_MAX_CTL 192
#define ERRS_MAX 24
#define REFS_MAX 150
#define VALUE_SLOTS 32

struct Schema {
  GDEFCORE_FieldInfo * f;
  size_t n;
  int has_group;
  int has_personal;
};

static void schema_free(struct Schema * s) {
  free(s->f);
  memset(s, 0, sizeof *s);
}

static int schema_load(struct Ctx * c, const char * type, struct Schema * s) {
  size_t n = 0;
  size_t i;
  memset(s, 0, sizeof *s);
  if (gdefcore_type_tree_field_count(c->catalog, type, strlen(type), &n) != GDEFCORE_OK ||
      n > SCHEMA_MAX) {
    return -1;
  }
  s->f = (GDEFCORE_FieldInfo *)calloc(n == 0 ? 1 : n, sizeof *s->f);
  if (s->f == NULL) {
    return -1;
  }
  for (i = 0; i < n; i++) {
    s->f[i].size = sizeof s->f[i];
    if (gdefcore_type_tree_field(c->catalog, type, strlen(type), i, &s->f[i]) !=
        GDEFCORE_OK) {
      schema_free(s);
      return -1;
    }
    if (s->f[i].type == GDEFSTOR_FIELD_GROUP) {
      s->has_group = 1;
    }
    if (s->f[i].personal) {
      s->has_personal = 1;
    }
  }
  s->n = n;
  return 0;
}

static const char * fname(const GDEFCORE_FieldInfo * f) {
  return f->path + f->name_at;
}

static size_t fname_len(const GDEFCORE_FieldInfo * f) {
  return f->path_len - f->name_at;
}

/* A tree save takes the whole entity, and what it leaves out is taken away: it
 * is how an optional field of any type, and a group's items, are emptied (a save
 * of values cannot empty a single field that is not text). A value the form
 * cannot carry (a personal one is sealed for a subject this page does not hold)
 * would be taken away by it. So a type with a personal field is saved as values,
 * which leaves what the form does not carry alone, and shows its groups without
 * a form. */
static int tree_path(const struct Schema * s) {
  return !s->has_personal;
}

/* ---- what a refusal says, beside the field it is about ----------------- */

struct Errs {
  size_t n;
  char name[ERRS_MAX][NAME_MAX_CTL];
  const char * msg[ERRS_MAX];
};

static void err_add(struct Errs * e, const char * name, const char * msg) {
  if (e->n < ERRS_MAX && strlen(name) < NAME_MAX_CTL) {
    size_t i;
    for (i = 0; i < e->n; i++) {
      if (strcmp(e->name[i], name) == 0) {
        return;
      }
    }
    strcpy(e->name[e->n], name);
    e->msg[e->n] = msg;
    e->n++;
  }
}

static const char * err_for(const struct Errs * e, const char * name) {
  size_t i;
  if (e == NULL) {
    return NULL;
  }
  for (i = 0; i < e->n; i++) {
    if (strcmp(e->name[i], name) == 0) {
      return e->msg[i];
    }
  }
  return NULL;
}

/* What a form is drawn again from, when a write is refused or more rows were
 * asked for: the body as it was sent, what is wrong with it, and whether a
 * row is to be added. */
struct Redraw {
  const char * body;
  size_t body_len;
  struct Errs errs;
  int more;
};

/* ---- the entities a reference may name -------------------------------- */

struct Refs {
  int loaded;
  size_t n;
  char id[REFS_MAX][GDEFCORE_PUBLIC_ID_LEN + 1];
  char label[REFS_MAX][CONTENT_CELL_MAX + 1];
};

static void label_of(struct Ctx * c, const char * type, const char * id, char * out,
    size_t cap) {
  char names[CONTENT_COLUMNS][64];
  size_t ncols = columns_of(c, type, names);
  size_t k;
  out[0] = '\0';
  for (k = 0; k < ncols && out[0] == '\0'; k++) {
    char ** texts = NULL;
    size_t * lens = NULL;
    size_t count = 0;
    int locked = 0;
    if (gdefcore_access_read(c->catalog, c->request, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2,
            names[k], strlen(names[k]), &texts, &lens, &count, &locked) == GDEFCORE_OK) {
      if (count > 0 && texts[0] != NULL && lens[0] > 0) {
        size_t n = lens[0] < cap - 1 ? lens[0] : cap - 1;
        while (n > 0 && n < lens[0] && ((unsigned char)texts[0][n] & 0xC0) == 0x80) {
          n--;
        }
        memcpy(out, texts[0], n);
        out[n] = '\0';
      }
      gdefcore_values_free(c->catalog, texts, lens, count);
    }
  }
}

/* The entities the person may see, of every content type, with a name to know
 * each by: what a reference field offers. A reference may name any entity, so
 * the list is of all the types the person can read. It is bounded; an id typed
 * in is checked when the form is saved. */
static void refs_load(struct Ctx * c, struct Refs * r) {
  char ** names = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  size_t t;
  if (r->loaded) {
    return;
  }
  r->loaded = 1;
  if (gdefcore_type_names(c->catalog, GDEFCORE_TYPE_CONTENT, &names, &lens, &count) !=
      GDEFCORE_OK) {
    return;
  }
  for (t = 0; t < count && r->n < REFS_MAX; t++) {
    GDEFCORE_Access_List query;
    char ** ids = NULL;
    size_t nids = 0;
    uint64_t total = 0;
    size_t i;
    char type[CONTENT_TYPE_MAX + 1];
    if (lens[t] > CONTENT_TYPE_MAX) {
      continue;
    }
    memcpy(type, names[t], lens[t]);
    type[lens[t]] = '\0';
    memset(&query, 0, sizeof query);
    query.size = sizeof query;
    query.type_name = type;
    query.type_name_len = lens[t];
    query.lang = "en";
    query.lang_len = 2;
    query.limit = 40;
    if (gdefcore_access_list_page(c->catalog, c->request, &query, &ids, &nids, &total) !=
        GDEFCORE_OK) {
      continue;
    }
    for (i = 0; i < nids && r->n < REFS_MAX; i++) {
      char text[CONTENT_CELL_MAX + 1];
      if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
        continue;
      }
      label_of(c, type, ids[i], text, sizeof text);
      memcpy(r->id[r->n], ids[i], GDEFCORE_PUBLIC_ID_LEN + 1);
      snprintf(r->label[r->n], sizeof r->label[r->n], "%s: %s", type,
          text[0] != '\0' ? text : ids[i]);
      r->n++;
    }
    gdefcore_ids_free(c->catalog, ids, nids);
  }
  gdefcore_values_free(c->catalog, names, lens, count);
}

/* ---- where a value is read from ---------------------------------------- */

/* A value being drawn comes from one of three places: the body of a refused or
 * extended post, the entity's tree (a type with groups), or the flat form of
 * its root fields. A text is borrowed from the source, or owned (a body's). */
struct Src {
  const char * body;
  size_t body_len;
  GDEFCORE_Tree * tree;
  const GDEFCORE_Form * form;
};

struct Txt {
  const char * p;
  size_t n;
  char * own;
};

static void txt_free(struct Txt * t) {
  if (t->own != NULL) {
    wipe(t->own, t->n);
    free(t->own);
  }
  memset(t, 0, sizeof *t);
}

/* How many values a control of this name carries in a body. */
static size_t body_count(const char * body, size_t n, const char * name) {
  size_t k = 0;
  char * got = NULL;
  size_t got_n = 0;
  while (k < 4096 && form_field_alloc(body, n, name, k, &got, &got_n) == 0) {
    free(got);
    got = NULL;
    k++;
  }
  return k;
}

static uint64_t parse_id(const char * s, size_t n) {
  uint64_t v = 0;
  size_t i;
  if (n == 0 || n > 19) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return 0;
    }
    v = v * 10 + (uint64_t)(s[i] - '0');
  }
  return v;
}

/* The id an item was sent with; 0 for a new one. -1 when the item is not on
 * the form at all. */
static int body_item(const char * body, size_t n, const char * prefix, const char * group,
    size_t k, uint64_t * id) {
  char name[NAME_MAX_CTL];
  char * got = NULL;
  size_t got_n = 0;
  snprintf(name, sizeof name, "%s%s-%zu--i", prefix, group, k);
  if (form_field_alloc(body, n, name, 0, &got, &got_n) != 0) {
    return -1;
  }
  *id = parse_id(got, got_n);
  free(got);
  return 0;
}

/* The k-th value of a leaf of an item (the root item is 0), or -1. */
static int src_leaf(const struct Src * s, const char * cname, const GDEFCORE_FieldInfo * f,
    size_t item, size_t k, struct Txt * out) {
  memset(out, 0, sizeof *out);
  if (s->body != NULL) {
    return form_field_alloc(s->body, s->body_len, cname, k, &out->own, &out->n) == 0
               ? (out->p = out->own, 0)
               : -1;
  }
  if (s->tree != NULL) {
    const char * text = NULL;
    size_t n = 0;
    if (f->many) {
      size_t child = 0;
      if (gdefcore_tree_child(s->tree, item, fname(f), fname_len(f), k, &child) !=
              GDEFCORE_OK ||
          gdefcore_tree_get(s->tree, child, "", 0, &text, &n) != GDEFCORE_OK) {
        return -1;
      }
    } else if (k != 0 ||
               gdefcore_tree_get(s->tree, item, fname(f), fname_len(f), &text, &n) !=
                   GDEFCORE_OK) {
      return -1;
    }
    if (text == NULL) {
      return -1;
    }
    out->p = text;
    out->n = n;
    return 0;
  }
  if (s->form != NULL) {
    const char * name = NULL;
    size_t name_len = 0;
    int req = 0;
    int many = 0;
    int personal = 0;
    size_t mn = 0;
    size_t mx = 0;
    int hr = 0;
    int64_t lo = 0;
    int64_t hi = 0;
    const char * pat = NULL;
    size_t pat_n = 0;
    const char * const * texts = NULL;
    const size_t * tl = NULL;
    size_t count = 0;
    size_t i;
    size_t nf = gdefcore_form_field_count(s->form);
    for (i = 0; i < nf; i++) {
      if (gdefcore_form_field(s->form, i, &name, &name_len, &req, &many, &personal, &mn, &mx,
              &hr, &lo, &hi, &pat, &pat_n, &texts, &tl, &count) == GDEFCORE_OK &&
          name_len == fname_len(f) && memcmp(name, fname(f), name_len) == 0) {
        if (k < count && texts != NULL && texts[k] != NULL) {
          out->p = texts[k];
          out->n = tl[k];
          return 0;
        }
        return -1;
      }
    }
  }
  return -1;
}

/* How many values a leaf has in the source. */
static size_t src_leaf_count(const struct Src * s, const char * cname,
    const GDEFCORE_FieldInfo * f, size_t item) {
  size_t k = 0;
  struct Txt t;
  if (s->body != NULL) {
    return body_count(s->body, s->body_len, cname);
  }
  while (k < 4096 && src_leaf(s, cname, f, item, k, &t) == 0) {
    txt_free(&t);
    k++;
  }
  return k;
}

/* The constraints the core holds a root field to, for the control's attributes
 * (the core checks them again at the save). */
struct Cons {
  int known;
  size_t min_len;
  size_t max_len;
  int has_range;
  int64_t lo;
  int64_t hi;
};

static struct Cons cons_of(const GDEFCORE_Form * form, const char * name, size_t name_len) {
  struct Cons k;
  size_t i;
  size_t nf;
  memset(&k, 0, sizeof k);
  if (form == NULL) {
    return k;
  }
  nf = gdefcore_form_field_count(form);
  for (i = 0; i < nf; i++) {
    const char * nm = NULL;
    size_t nm_len = 0;
    int req = 0;
    int many = 0;
    int personal = 0;
    const char * pat = NULL;
    size_t pat_n = 0;
    const char * const * texts = NULL;
    const size_t * tl = NULL;
    size_t count = 0;
    if (gdefcore_form_field(form, i, &nm, &nm_len, &req, &many, &personal, &k.min_len,
            &k.max_len, &k.has_range, &k.lo, &k.hi, &pat, &pat_n, &texts, &tl, &count) ==
            GDEFCORE_OK &&
        nm_len == name_len && memcmp(nm, name, name_len) == 0) {
      k.known = 1;
      return k;
    }
  }
  memset(&k, 0, sizeof k);
  return k;
}

/* ---- drawing ---------------------------------------------------------- */

struct Draw {
  struct Ctx * c;
  struct Page * page;
  const struct Redraw * again;
  struct Refs refs;
  const struct Src * src;
  const GDEFCORE_Form * form;  /* constraints of the root fields */
};

static int d_attr(struct Draw * d, const char * name, const char * value) {
  return page_str(d->page, " ") != 0 || page_str(d->page, name) != 0 ||
         page_str(d->page, "=\"") != 0 || page_esc(d->page, value, strlen(value)) != 0 ||
         page_str(d->page, "\"");
}

static int d_num_attr(struct Draw * d, const char * name, long long v) {
  char buf[48];
  snprintf(buf, sizeof buf, " %s=\"%lld\"", name, v);
  return page_str(d->page, buf);
}

/* One control, with the value it holds. A many field draws one of these per row. */
static int draw_control(struct Draw * d, const GDEFCORE_FieldInfo * f, const char * cname,
    const struct Cons * k, const char * text, size_t n, int bad) {
  struct Page * p = d->page;
  int type = f->type == 0 ? GDEFSTOR_FIELD_TEXT : f->type;
  int area = type == GDEFSTOR_FIELD_TEXT && !f->many && k->max_len > 120;
  const char * req = f->required && !f->many ? " required" : "";
  if (type == GDEFSTOR_FIELD_BOOLEAN) {
    int is_true = n == 4 && memcmp(text, "true", 4) == 0;
    int is_false = n == 5 && memcmp(text, "false", 5) == 0;
    return page_str(p, "<select") != 0 || d_attr(d, "name", cname) != 0 ||
           page_str(p, req) != 0 || (bad && page_str(p, " aria-invalid=\"true\"") != 0) ||
           page_str(p, "><option value=\"\">(not set)</option><option value=\"true\"") != 0 ||
           (is_true && page_str(p, " selected") != 0) ||
           page_str(p, ">Yes</option><option value=\"false\"") != 0 ||
           (is_false && page_str(p, " selected") != 0) ||
           page_str(p, ">No</option></select>");
  }
  if (area) {
    char lim[64];
    snprintf(lim, sizeof lim, " maxlength=\"%zu\"", k->max_len);
    return page_str(p, "<textarea") != 0 || d_attr(d, "name", cname) != 0 ||
           page_str(p, req) != 0 || (bad && page_str(p, " aria-invalid=\"true\"") != 0) ||
           (k->max_len > 0 && page_str(p, lim) != 0) || page_str(p, ">") != 0 ||
           page_esc(p, text, n) != 0 || page_str(p, "</textarea>");
  }
  if (page_str(p, "<input") != 0 || d_attr(d, "name", cname) != 0 || page_str(p, req) != 0 ||
      (bad && page_str(p, " aria-invalid=\"true\"") != 0)) {
    return -1;
  }
  switch (type) {
  case GDEFSTOR_FIELD_INTEGER:
    if (page_str(p, " type=\"number\" step=\"1\"") != 0 ||
        (k->has_range &&
            (d_num_attr(d, "min", k->lo) != 0 || d_num_attr(d, "max", k->hi) != 0))) {
      return -1;
    }
    break;
  case GDEFSTOR_FIELD_DECIMAL: {
    char hint[160];
    if (f->scale > 0) {
      snprintf(hint, sizeof hint, "a number with at most %d digits after the point", f->scale);
    } else {
      snprintf(hint, sizeof hint, "a whole number");
    }
    if (page_str(p, " type=\"text\" inputmode=\"decimal\"") != 0 || d_attr(d, "title", hint) != 0) {
      return -1;
    }
    break;
  }
  case GDEFSTOR_FIELD_FLOAT:
    if (page_str(p, " type=\"text\" inputmode=\"decimal\"") != 0) {
      return -1;
    }
    break;
  case GDEFSTOR_FIELD_DATE:
    if (page_str(p, " type=\"date\"") != 0) {
      return -1;
    }
    break;
  case GDEFSTOR_FIELD_DATETIME:
    if (page_str(p, " type=\"text\" placeholder=\"2026-11-12T09:30:00Z\"") != 0) {
      return -1;
    }
    break;
  case GDEFSTOR_FIELD_REFERENCE:
    refs_load(d->c, &d->refs);
    if (page_str(p, " type=\"text\" list=\"content-refs\" autocomplete=\"off\"") != 0) {
      return -1;
    }
    break;
  default:
    if (k->max_len > 0 && d_num_attr(d, "maxlength", (long long)k->max_len) != 0) {
      return -1;
    }
    break;
  }
  return page_str(p, " value=\"") != 0 || page_esc(p, text, n) != 0 || page_str(p, "\">");
}

static int draw_error(struct Draw * d, const char * cname) {
  const char * msg = d->again != NULL ? err_for(&d->again->errs, cname) : NULL;
  if (msg == NULL) {
    return 0;
  }
  return page_str(d->page, " <span class=\"field-error\" role=\"alert\">") != 0 ||
         page_str(d->page, msg) != 0 || page_str(d->page, "</span>");
}

static int draw_leaf(struct Draw * d, const GDEFCORE_FieldInfo * f, const char * prefix,
    size_t item, int root_flat) {
  char cname[NAME_MAX_CTL];
  struct Cons k;
  size_t have;
  size_t rows;
  size_t r;
  const char * bad;
  struct Page * p = d->page;
  if (strlen(prefix) + fname_len(f) >= sizeof cname) {
    return -1;
  }
  snprintf(cname, sizeof cname, "%s%.*s", prefix, (int)fname_len(f), fname(f));
  memset(&k, 0, sizeof k);
  if (root_flat) {
    k = cons_of(d->form, fname(f), fname_len(f));
  }
  have = src_leaf_count(d->src, cname, f, item);
  bad = d->again != NULL ? err_for(&d->again->errs, cname) : NULL;
  if (!f->many) {
    struct Txt t;
    int got = src_leaf(d->src, cname, f, item, 0, &t) == 0;
    int rc = page_str(p, "<p><label>") != 0 || page_esc(p, fname(f), fname_len(f)) != 0 ||
             (f->required && page_str(p, " (required)") != 0) || page_str(p, " ") != 0 ||
             draw_control(d, f, cname, &k, got ? t.p : "", got ? t.n : 0, bad != NULL) != 0 ||
             page_str(p, "</label>") != 0 || draw_error(d, cname) != 0 || page_str(p, "</p>");
    txt_free(&t);
    return rc ? -1 : 0;
  }
  /* A many field is a row to each value, and rows to add: what comes back is
   * the rows as they were, blanks dropped when it is saved. */
  rows = d->src->body != NULL ? have + (d->again != NULL && d->again->more ? 2 : 0) : have + 2;
  if (rows < 2) {
    rows = 2;
  }
  if (rows > VALUE_SLOTS) {
    rows = VALUE_SLOTS;
  }
  if (page_str(p, "<fieldset class=\"many\"><legend>") != 0 ||
      page_esc(p, fname(f), fname_len(f)) != 0 ||
      (f->required && page_str(p, " (required)") != 0) || page_str(p, "</legend>") != 0) {
    return -1;
  }
  for (r = 0; r < rows; r++) {
    struct Txt t;
    int got = src_leaf(d->src, cname, f, item, r, &t) == 0;
    int rc = page_str(p, "<p>") != 0 ||
             draw_control(d, f, cname, &k, got ? t.p : "", got ? t.n : 0, bad != NULL) != 0 ||
             page_str(p, "</p>");
    txt_free(&t);
    if (rc) {
      return -1;
    }
  }
  return (draw_error(d, cname) != 0 || page_str(p, "</fieldset>") != 0) ? -1 : 0;
}

static int draw_group(struct Draw * d, const struct Schema * s, size_t gi, const char * prefix,
    size_t titem);

/* The fields of a group item, or of the root, in the order the type declares. */
static int draw_children(struct Draw * d, const struct Schema * s, size_t parent,
    const char * prefix, size_t item, int root) {
  size_t j;
  for (j = 0; j < s->n; j++) {
    if (s->f[j].parent != parent) {
      continue;
    }
    if (s->f[j].type == GDEFSTOR_FIELD_GROUP) {
      if (draw_group(d, s, j, prefix, item) != 0) {
        return -1;
      }
    } else if (draw_leaf(d, &s->f[j], prefix, item, root) != 0) {
      return -1;
    }
  }
  return 0;
}

static int draw_group(struct Draw * d, const struct Schema * s, size_t gi, const char * prefix,
    size_t titem) {
  const GDEFCORE_FieldInfo * g = &s->f[gi];
  struct Page * p = d->page;
  size_t have = 0;
  size_t rows;
  size_t k;
  char nprefix[NAME_MAX_CTL];
  if (d->src->body != NULL) {
    uint64_t id;
    while (have < 256 && body_item(d->src->body, d->src->body_len, prefix, fname(g), have, &id) == 0) {
      have++;
    }
  } else if (d->src->tree != NULL) {
    have = gdefcore_tree_child_count(d->src->tree, titem, fname(g), fname_len(g));
  }
  if (g->many) {
    rows = d->src->body != NULL ? have + (d->again != NULL && d->again->more ? 1 : 0) : have + 1;
    if (rows < 1) {
      rows = 1;
    }
    if (rows > 64) {
      rows = 64;
    }
  } else {
    rows = 1;
  }
  for (k = 0; k < rows; k++) {
    uint64_t id = 0;
    size_t child = 0;
    char idname[NAME_MAX_CTL];
    char idtext[32];
    if (strlen(prefix) + fname_len(g) + 24 >= sizeof nprefix) {
      return -1;
    }
    if (d->src->body != NULL) {
      (void)body_item(d->src->body, d->src->body_len, prefix, fname(g), k, &id);
    } else if (d->src->tree != NULL && k < have) {
      GDEFCORE_TreeItem ti;
      memset(&ti, 0, sizeof ti);
      ti.size = sizeof ti;
      if (gdefcore_tree_child(d->src->tree, titem, fname(g), fname_len(g), k, &child) !=
              GDEFCORE_OK ||
          gdefcore_tree_item(d->src->tree, child, &ti) != GDEFCORE_OK) {
        return -1;
      }
      id = ti.id;
    }
    snprintf(nprefix, sizeof nprefix, "%s%.*s-%zu-", prefix, (int)fname_len(g), fname(g), k);
    snprintf(idname, sizeof idname, "%s%.*s-%zu--i", prefix, (int)fname_len(g), fname(g), k);
    snprintf(idtext, sizeof idtext, "%llu", (unsigned long long)id);
    if (page_str(p, "<fieldset class=\"group\"><legend>") != 0 ||
        page_esc(p, fname(g), fname_len(g)) != 0 ||
        (g->many && (snprintf(idtext, sizeof idtext, " %zu", k + 1), page_str(p, idtext) != 0)) ||
        (g->required && !g->many && page_str(p, " (required)") != 0) ||
        page_str(p, "</legend><input type=\"hidden\"") != 0 || d_attr(d, "name", idname) != 0) {
      return -1;
    }
    snprintf(idtext, sizeof idtext, "%llu", (unsigned long long)id);
    if (d_attr(d, "value", idtext) != 0 || page_str(p, ">") != 0 ||
        draw_children(d, s, gi, nprefix, d->src->tree != NULL && k < have ? child : 0, 0) != 0) {
      return -1;
    }
    if (g->many) {
      char rm[NAME_MAX_CTL];
      snprintf(rm, sizeof rm, "%s%.*s-%zu--rm", prefix, (int)fname_len(g), fname(g), k);
      if (page_str(p, "<p><label><input type=\"checkbox\"") != 0 || d_attr(d, "name", rm) != 0 ||
          page_str(p, " value=\"1\"> Remove this one.</label></p>") != 0) {
        return -1;
      }
    }
    {
      char gname[NAME_MAX_CTL];
      const char * msg;
      snprintf(gname, sizeof gname, "%s%.*s-%zu", prefix, (int)fname_len(g), fname(g), k);
      msg = d->again != NULL ? err_for(&d->again->errs, gname) : NULL;
      if (msg != NULL && (page_str(p, "<p class=\"field-error\" role=\"alert\">") != 0 ||
                             page_str(p, msg) != 0 || page_str(p, "</p>") != 0)) {
        return -1;
      }
    }
    if (page_str(p, "</fieldset>") != 0) {
      return -1;
    }
  }
  return 0;
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

/* Whether the person may read this field of the stored entity: what is drawn
 * from an unfiltered tree is only what they may read. */
static int field_readable(struct Ctx * c, const char * type, const char * id, int stored,
    const char * name, size_t name_len) {
  int ok = 0;
  if (!stored) {
    return 1;
  }
  if (gdefcore_access_can_field(c->catalog, c->request, GDEFCORE_ACTION_READ,
          strlen(GDEFCORE_ACTION_READ), type, strlen(type), id, name, name_len, &ok) !=
      GDEFCORE_OK) {
    return 0;
  }
  return ok;
}

static int shown(struct Ctx * c, const char * type, const char * id, int stored,
    const GDEFCORE_FieldInfo * f, int by_tree);

static int draw_form(struct Ctx * c, struct Page * page, const char * type, int kind,
    const char * id, int stored, const struct Schema * s, const struct Src * src,
    const GDEFCORE_Form * form, const struct Redraw * again, int notice) {
  struct Draw d;
  size_t j;
  int rc = -1;
  memset(&d, 0, sizeof d);
  d.c = c;
  d.page = page;
  d.again = again;
  d.src = src;
  d.form = form;
  if (page_notice_text(page, notice, c->reason) != 0 ||
      page_str(page, "<form method=\"post\" action=\"" CONTENT_BASE "/") != 0 ||
      page_esc(page, type, strlen(type)) != 0 || page_str(page, "/") != 0 ||
      page_esc(page, id, strlen(id)) != 0 || page_str(page, "\">") != 0 ||
      hidden_token(c, page, "content-save", id) != 0) {
    return -1;
  }
  for (j = 0; j < s->n; j++) {
    const GDEFCORE_FieldInfo * f = &s->f[j];
    if (f->parent != GDEFCORE_TREE_ROOT) {
      continue;
    }
    if (f->type == GDEFSTOR_FIELD_GROUP) {
      if (!tree_path(s)) {
        if (page_str(page, "<p class=\"note\">") != 0 || page_esc(page, fname(f), fname_len(f)) != 0 ||
            page_str(page, " is kept as it is: this type holds personal values, which this "
                           "form does not carry.</p>") != 0) {
          goto out;
        }
        continue;
      }
      if (!shown(c, type, id, stored, f, 1)) {
        continue;
      }
      if (draw_group(&d, s, j, "", 0) != 0) {
        goto out;
      }
      continue;
    }
    if (!shown(c, type, id, stored, f, src->tree != NULL)) {
      continue;
    }
    if (draw_leaf(&d, f, "", 0, 1) != 0) {
      goto out;
    }
  }
  if (d.refs.loaded) {
    size_t i;
    if (page_str(page, "<datalist id=\"content-refs\">") != 0) {
      goto out;
    }
    for (i = 0; i < d.refs.n; i++) {
      if (page_str(page, "<option value=\"") != 0 ||
          page_esc(page, d.refs.id[i], strlen(d.refs.id[i])) != 0 ||
          page_str(page, "\" label=\"") != 0 ||
          page_esc(page, d.refs.label[i], strlen(d.refs.label[i])) != 0 ||
          page_str(page, "\"></option>") != 0) {
        goto out;
      }
    }
    if (page_str(page, "</datalist>") != 0) {
      goto out;
    }
  }
  if (kind == GDEFCORE_TYPE_CONFIG &&
      page_str(page, "<p><label>This is configuration. To change it, type the type's "
                     "name to confirm: <input name=\"confirm\" autocomplete=\"off\" "
                     "value=\"\"></label></p>") != 0) {
    goto out;
  }
  rc = page_str(page, "<p><button type=\"submit\" name=\"do\" value=\"save\">Save"
                      "</button> <button type=\"submit\" name=\"do\" value=\"more\">Add rows</button></p></form>");
out:
  return rc;
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
    const struct Redraw * again, int notice, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  struct Schema schema;
  struct Src src;
  GDEFCORE_Form * form = NULL;
  GDEFCORE_Tree * tree = NULL;
  int stored = has_revision(c, id);
  int may_update = 1;
  GDEFCORE_Result rc;
  int ret = -1;
  memset(&page, 0, sizeof page);
  memset(&src, 0, sizeof src);
  memset(&schema, 0, sizeof schema);
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
  if (schema_load(c, type, &schema) != 0) {
    gdefcore_form_destroy(form);
    return -1;
  }
  if (form != NULL) {
    if (again != NULL) {
      src.body = again->body;
      src.body_len = again->body_len;
    } else if (tree_path(&schema)) {
      if (stored) {
        if (gdefcore_entity_tree(c->catalog, NULL, GDEFCORE_VIEW_DRAFT, id,
                GDEFCORE_PUBLIC_ID_LEN, "en", 2, &tree) != GDEFCORE_OK) {
          tree = NULL;
        }
      }
      if (tree == NULL && gdefcore_tree_create(c->catalog, &tree) != GDEFCORE_OK) {
        goto done;
      }
      src.tree = tree;
    } else {
      src.form = form;
    }
  }
  if (page_begin(&page, type) != 0 || page_str(&page, "<h1>") != 0 ||
      page_str(&page, stored ? "Edit " : "New ") != 0 ||
      page_esc(&page, type, strlen(type)) != 0 || page_str(&page, "</h1>") != 0 ||
      (form != NULL &&
          draw_form(c, &page, type, kind, id, stored, &schema, &src, form, again, notice) != 0) ||
      (form == NULL && page_notice_text(&page, notice, c->reason) != 0) ||
      (stored && allowed(c, GDEFCORE_ACTION_DELETE, type, id) &&
          draw_delete(c, &page, type, kind, id) != 0) ||
      page_str(&page, "<p><a href=\"" CONTENT_BASE "/") != 0 ||
      page_esc(&page, type, strlen(type)) != 0 ||
      page_str(&page, "\">Back to the list</a></p>") != 0) {
    page_free(&page);
    goto done;
  }
  ret = take_page(&page, out);
done:
  gdefcore_tree_free(tree);
  gdefcore_form_destroy(form);
  schema_free(&schema);
  return ret;
}

/* ---- writes ----------------------------------------------------------- */

static int redirect_list(struct GDEFWEB_Admin_Reply * out, const char * type) {
  char loc[CONTENT_TYPE_MAX + sizeof CONTENT_BASE + 16];
  snprintf(loc, sizeof loc, CONTENT_BASE "/%s", type);
  return redirect_to(out, loc);
}

/* ---- is what was typed a value of the field's type? -------------------- */

static int all_digits(const char * s, size_t n) {
  size_t i;
  if (n == 0) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return 0;
    }
  }
  return 1;
}

static int digits_at(const char * s, size_t n, size_t at, size_t count) {
  return at + count <= n && all_digits(s + at, count);
}

/* The message for a value that is not of its field's type, or NULL when it is
 * (the core asks again when it is saved, and is the authority). A reference is
 * also checked to be one the person may read: an id typed in that they could
 * not have chosen is refused, whatever it names. */
static const char * typed_msg(struct Ctx * c, const GDEFCORE_FieldInfo * f, const char * t,
    size_t n) {
  size_t i = 0;
  switch (f->type) {
  case GDEFSTOR_FIELD_INTEGER:
    if (n > 0 && t[0] == '-') {
      i = 1;
    }
    return all_digits(t + i, n - i) && n - i <= 18 ? NULL : "must be a whole number";
  case GDEFSTOR_FIELD_DECIMAL: {
    size_t dot = n;
    size_t k;
    if (n > 0 && t[0] == '-') {
      i = 1;
    }
    for (k = i; k < n; k++) {
      if (t[k] == '.') {
        dot = k;
        break;
      }
    }
    if (!all_digits(t + i, dot - i) || dot - i > 18) {
      return f->scale > 0 ? "must be a number" : "must be a whole number";
    }
    if (dot < n) {
      if (f->scale == 0 || !all_digits(t + dot + 1, n - dot - 1) ||
          n - dot - 1 > (size_t)f->scale) {
        return f->scale == 0 ? "must be a whole number"
                             : "has more digits after the point than the field holds";
      }
    }
    return NULL;
  }
  case GDEFSTOR_FIELD_FLOAT: {
    char buf[64];
    char * end = NULL;
    if (n == 0 || n >= sizeof buf) {
      return "must be a number";
    }
    memcpy(buf, t, n);
    buf[n] = '\0';
    (void)strtod(buf, &end);
    return end != NULL && *end == '\0' && buf[0] != ' ' && strcmp(buf, "nan") != 0 &&
                   strstr(buf, "inf") == NULL && strstr(buf, "INF") == NULL &&
                   strstr(buf, "x") == NULL
               ? NULL
               : "must be a number";
  }
  case GDEFSTOR_FIELD_BOOLEAN:
    return (n == 4 && memcmp(t, "true", 4) == 0) || (n == 5 && memcmp(t, "false", 5) == 0)
               ? NULL
               : "must be yes or no";
  case GDEFSTOR_FIELD_DATE:
    return n == 10 && digits_at(t, n, 0, 4) && t[4] == '-' && digits_at(t, n, 5, 2) &&
                   t[7] == '-' && digits_at(t, n, 8, 2) && t[5] <= '1' &&
                   (t[5] == '0' ? t[6] != '0' : t[6] <= '2') && t[8] <= '3'
               ? NULL
               : "must be a date, as 2026-11-12";
  case GDEFSTOR_FIELD_DATETIME:
    return n >= 20 && digits_at(t, n, 0, 4) && t[4] == '-' && digits_at(t, n, 5, 2) &&
                   t[7] == '-' && digits_at(t, n, 8, 2) && t[10] == 'T' &&
                   digits_at(t, n, 11, 2) && t[13] == ':' && digits_at(t, n, 14, 2) &&
                   t[16] == ':' && digits_at(t, n, 17, 2) && t[n - 1] == 'Z'
               ? NULL
               : "must be a date and time, as 2026-11-12T09:30:00Z";
  case GDEFSTOR_FIELD_REFERENCE: {
    char canon[GDEFCORE_PUBLIC_ID_LEN + 1];
    char * got = NULL;
    size_t got_n = 0;
    int ok = 0;
    if (n != GDEFCORE_PUBLIC_ID_LEN || gdefcore_id_canonical(t, n, canon) != GDEFCORE_OK ||
        memcmp(canon, t, n) != 0) {
      return "must be the id of one of the entities offered";
    }
    if (gdefcore_entity_type(c->catalog, canon, GDEFCORE_PUBLIC_ID_LEN, &got, &got_n) ==
        GDEFCORE_OK) {
      char type[CONTENT_TYPE_MAX + 1];
      if (got_n <= CONTENT_TYPE_MAX) {
        memcpy(type, got, got_n);
        type[got_n] = '\0';
        ok = allowed(c, GDEFCORE_ACTION_READ, type, canon);
      }
      gdefcore_text_free(c->catalog, got);
    }
    return ok ? NULL : "must be the id of one of the entities offered";
  }
  default:
    return NULL;
  }
}

/* Whether a new item has nothing typed in it: a blank row, not an item. */
static int item_blank(const struct Schema * s, size_t gi, const char * nprefix,
    const char * body, size_t body_len) {
  size_t j;
  for (j = 0; j < s->n; j++) {
    const GDEFCORE_FieldInfo * f = &s->f[j];
    if (f->parent != gi) {
      continue;
    }
    if (f->type != GDEFSTOR_FIELD_GROUP) {
      char cname[NAME_MAX_CTL];
      size_t q;
      snprintf(cname, sizeof cname, "%s%.*s", nprefix, (int)fname_len(f), fname(f));
      for (q = 0; q < 64; q++) {
        char * v = NULL;
        size_t vn = 0;
        if (form_field_alloc(body, body_len, cname, q, &v, &vn) != 0) {
          break;
        }
        free(v);
        if (vn > 0) {
          return 0;
        }
      }
    } else {
      uint64_t sub;
      size_t q;
      for (q = 0; q < 64; q++) {
        if (body_item(body, body_len, nprefix, fname(f), q, &sub) != 0) {
          break;
        }
        char np2[NAME_MAX_CTL];
        snprintf(np2, sizeof np2, "%s%.*s-%zu-", nprefix, (int)fname_len(f), fname(f), q);
        if (sub != 0 || !item_blank(s, (size_t)(f - s->f), np2, body, body_len)) {
          return 0;
        }
      }
    }
  }
  return 1;
}

/* What a validation walks with. */
struct Vctx {
  struct Ctx * c;
  const struct Schema * s;
  const char * type;
  const char * id;
  int stored;
  int by_tree;
  const char * body;
  size_t body_len;
  struct Errs * errs;
};

/* A root field is on the form when the person may update it and, for what is
 * drawn from a tree, read it. */
static int shown(struct Ctx * c, const char * type, const char * id, int stored,
    const GDEFCORE_FieldInfo * f, int by_tree) {
  /* The core's field grants are of the fields it counts as flat: a group is
   * the entity's to change or read as a whole, which the page has asked
   * already (it is drawn for whoever may update the entity). */
  if (f->type == GDEFSTOR_FIELD_GROUP) {
    return 1;
  }
  return field_editable(c, type, id, stored, fname(f), fname_len(f), f->personal) &&
         (!by_tree || field_readable(c, type, id, stored, fname(f), fname_len(f)));
}

/* Every value in the body against its field's type, and every required field
 * and group against being there: what is wrong, beside the control it is
 * about. Walks the body as the form was drawn. */
static void validate_fields(const struct Vctx * v, size_t parent, const char * prefix);

static void validate_group(const struct Vctx * v, size_t gi, const char * prefix) {
  const GDEFCORE_FieldInfo * g = &v->s->f[gi];
  size_t k;
  size_t kept = 0;
  for (k = 0; k < 256; k++) {
    uint64_t id;
    char rm[NAME_MAX_CTL];
    char nprefix[NAME_MAX_CTL];
    char * got = NULL;
    size_t got_n = 0;
    if (body_item(v->body, v->body_len, prefix, fname(g), k, &id) != 0) {
      break;
    }
    snprintf(rm, sizeof rm, "%s%.*s-%zu--rm", prefix, (int)fname_len(g), fname(g), k);
    if (form_field_alloc(v->body, v->body_len, rm, 0, &got, &got_n) == 0) {
      free(got);
      continue;
    }
    snprintf(nprefix, sizeof nprefix, "%s%.*s-%zu-", prefix, (int)fname_len(g), fname(g), k);
    if (id == 0 && item_blank(v->s, gi, nprefix, v->body, v->body_len)) {
      continue;
    }
    validate_fields(v, gi, nprefix);
    kept++;
  }
  if (g->required && kept == 0) {
    char gname[NAME_MAX_CTL];
    snprintf(gname, sizeof gname, "%s%.*s-0", prefix, (int)fname_len(g), fname(g));
    err_add(v->errs, gname, "at least one is required");
  }
}

static void validate_fields(const struct Vctx * v, size_t parent, const char * prefix) {
  size_t j;
  for (j = 0; j < v->s->n; j++) {
    const GDEFCORE_FieldInfo * f = &v->s->f[j];
    char cname[NAME_MAX_CTL];
    size_t k;
    size_t given = 0;
    if (f->parent != parent) {
      continue;
    }
    if (parent == GDEFCORE_TREE_ROOT &&
        (!shown(v->c, v->type, v->id, v->stored, f, v->by_tree) ||
            (f->type == GDEFSTOR_FIELD_GROUP && !v->by_tree))) {
      continue;
    }
    if (f->type == GDEFSTOR_FIELD_GROUP) {
      validate_group(v, j, prefix);
      continue;
    }
    if (f->personal || strlen(prefix) + fname_len(f) >= sizeof cname) {
      continue;
    }
    snprintf(cname, sizeof cname, "%s%.*s", prefix, (int)fname_len(f), fname(f));
    for (k = 0; k < (f->many ? 4096u : 1u); k++) {
      char * got = NULL;
      size_t got_n = 0;
      if (form_field_alloc(v->body, v->body_len, cname, k, &got, &got_n) != 0) {
        break;
      }
      if (got_n > 0) {
        const char * msg = typed_msg(v->c, f, got, got_n);
        given++;
        if (msg != NULL) {
          err_add(v->errs, cname, msg);
        }
      }
      free(got);
    }
    if (f->required && given == 0) {
      err_add(v->errs, cname, "is required");
    }
  }
}

/* ---- the save ---------------------------------------------------------- */

/* The values of the flat save: each non-personal root field the person may
 * update, as typed. */
struct Flat {
  GDEFCORE_Value values[SCHEMA_MAX];
  const char * slots[SCHEMA_MAX][VALUE_SLOTS];
  size_t lens[SCHEMA_MAX][VALUE_SLOTS];
  char names[SCHEMA_MAX][64];
  char * texts[SCHEMA_MAX][VALUE_SLOTS];
  int required[SCHEMA_MAX];
  size_t n;
};

static void flat_free(struct Flat * fl) {
  size_t i;
  size_t k;
  for (i = 0; i < SCHEMA_MAX; i++) {
    for (k = 0; k < VALUE_SLOTS; k++) {
      if (fl->texts[i][k] != NULL) {
        wipe(fl->texts[i][k], fl->lens[i][k]);
        free(fl->texts[i][k]);
      }
    }
  }
}

/* Offer the values to the core with the form's token. When it does not take
 * them, offer each field's value with the required ones (a form must carry
 * every required field): the one it does not take is the one the person is told
 * about. */
static void attribute_flat(struct Ctx * c, const struct Flat * fl, const char * body,
    size_t body_len, struct Errs * errs) {
  GDEFCORE_Value given[SCHEMA_MAX];
  size_t ngiven = 0;
  size_t i;
  for (i = 0; i < fl->n; i++) {
    if (fl->values[i].count > 0) {
      given[ngiven++] = fl->values[i];
    }
  }
  if (values_fit(c, body, body_len, given, ngiven)) {
    return;
  }
  for (i = 0; i < fl->n; i++) {
    GDEFCORE_Value trial[SCHEMA_MAX];
    size_t nt = 0;
    size_t q;
    if (fl->values[i].count == 0) {
      continue;
    }
    for (q = 0; q < fl->n; q++) {
      if (fl->values[q].count > 0 && (q == i || fl->required[q])) {
        trial[nt++] = fl->values[q];
      }
    }
    if (!values_fit(c, body, body_len, trial, nt)) {
      err_add(errs, fl->names[i], "does not fit the field: check its length, range or form");
    }
  }
}

/* Whether the entity holds a value for the field now. */
static int held(struct Ctx * c, const char * id, const char * field) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  int locked = 0;
  int yes = 0;
  if (gdefcore_access_read(c->catalog, c->request, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field,
          strlen(field), &texts, &lens, &count, &locked) == GDEFCORE_OK) {
    yes = count > 0;
    gdefcore_values_free(c->catalog, texts, lens, count);
  }
  return yes;
}

/* Collect what was typed for the root fields. Returns 0, or -1 when a value is
 * longer than a form may carry. */
static int collect_flat(struct Ctx * c, const struct Schema * s, const char * type,
    const char * id, int stored, int as_tree, const char * body, size_t body_len,
    struct Flat * fl, struct Errs * errs) {
  size_t j;
  int rc = 0;
  for (j = 0; j < s->n; j++) {
    const GDEFCORE_FieldInfo * f = &s->f[j];
    size_t k;
    size_t count = 0;
    size_t slot = fl->n;
    if (f->parent != GDEFCORE_TREE_ROOT || f->type == GDEFSTOR_FIELD_GROUP ||
        fname_len(f) >= sizeof fl->names[0] ||
        !field_editable(c, type, id, stored, fname(f), fname_len(f), f->personal)) {
      continue;
    }
    fl->required[slot] = f->required;
    memcpy(fl->names[slot], fname(f), fname_len(f));
    fl->names[slot][fname_len(f)] = '\0';
    for (k = 0; k < (f->many ? (size_t)VALUE_SLOTS : 1u); k++) {
      char * got = NULL;
      size_t got_n = 0;
      if (form_field_alloc(body, body_len, fl->names[slot], k, &got, &got_n) != 0) {
        break;
      }
      if (got_n > CONTENT_TEXT_MAX) {
        free(got);
        err_add(errs, fl->names[slot], "is too long");
        rc = -1;
        continue;
      }
      if (got_n == 0) {
        free(got);
        continue;
      }
      fl->texts[slot][count] = got;
      fl->lens[slot][count] = got_n;
      fl->slots[slot][count] = got;
      count++;
    }
    memset(&fl->values[slot], 0, sizeof fl->values[slot]);
    fl->values[slot].size = sizeof fl->values[slot];
    fl->values[slot].field = fl->names[slot];
    fl->values[slot].field_len = fname_len(f);
    fl->values[slot].texts = fl->slots[slot];
    fl->values[slot].text_lens = fl->lens[slot];
    fl->values[slot].count = count;
    if (count == 0) {
      /* Nothing typed. A field that holds nothing stays as it is. One that
       * held a value is cleared: a many field by no values, a single one by an
       * empty text (the core's rule). A required field is refused before this. */
      if (as_tree || !stored || !held(c, id, fl->names[slot])) {
        continue;
      }
      if (!f->many && (f->type == GDEFSTOR_FIELD_TEXT || f->type == 0)) {
        fl->slots[slot][0] = "";
        fl->lens[slot][0] = 0;
        fl->values[slot].count = 1;
      } else if (!f->many) {
        /* A save of values cannot empty a single field that is not text. */
        err_add(errs, fl->names[slot], "cannot be emptied once it has a value");
        rc = -1;
        continue;
      }
    }
    fl->n++;
  }
  return rc;
}

/* The tree of a save: the fields the person may update from what was typed, the
 * others as the entity holds them now. */
static int build_fields(const struct Vctx * v, size_t parent, const char * prefix,
    GDEFCORE_Tree * nt, size_t nitem, const GDEFCORE_Tree * ot);

/* One field of an item as the entity holds it, into the new tree. */
static int copy_field(const struct Schema * s, size_t j, const GDEFCORE_Tree * ot,
    size_t oitem, GDEFCORE_Tree * nt, size_t nitem);

static int copy_fields(const struct Schema * s, size_t parent, const GDEFCORE_Tree * ot,
    size_t oitem, GDEFCORE_Tree * nt, size_t nitem) {
  size_t j;
  for (j = 0; j < s->n; j++) {
    if (s->f[j].parent == parent && copy_field(s, j, ot, oitem, nt, nitem) != 0) {
      return -1;
    }
  }
  return 0;
}

static int copy_field(const struct Schema * s, size_t j, const GDEFCORE_Tree * ot,
    size_t oitem, GDEFCORE_Tree * nt, size_t nitem) {
  const GDEFCORE_FieldInfo * f = &s->f[j];
  if (f->type == GDEFSTOR_FIELD_GROUP || f->many) {
    size_t cn = gdefcore_tree_child_count(ot, oitem, fname(f), fname_len(f));
    size_t k;
    for (k = 0; k < cn; k++) {
      size_t oc = 0;
      size_t nc = 0;
      GDEFCORE_TreeItem ti;
      memset(&ti, 0, sizeof ti);
      ti.size = sizeof ti;
      if (gdefcore_tree_child(ot, oitem, fname(f), fname_len(f), k, &oc) != GDEFCORE_OK ||
          gdefcore_tree_item(ot, oc, &ti) != GDEFCORE_OK ||
          gdefcore_tree_add(nt, nitem, fname(f), fname_len(f), ti.id, &nc) != GDEFCORE_OK) {
        return -1;
      }
      if (f->type == GDEFSTOR_FIELD_GROUP) {
        if (copy_fields(s, j, ot, oc, nt, nc) != 0) {
          return -1;
        }
      } else {
        const char * text = NULL;
        size_t n = 0;
        if (gdefcore_tree_get(ot, oc, "", 0, &text, &n) == GDEFCORE_OK && text != NULL &&
            gdefcore_tree_set(nt, nc, "", 0, text, n) != GDEFCORE_OK) {
          return -1;
        }
      }
    }
    return 0;
  }
  {
    const char * text = NULL;
    size_t n = 0;
    if (gdefcore_tree_get(ot, oitem, fname(f), fname_len(f), &text, &n) == GDEFCORE_OK &&
        text != NULL && gdefcore_tree_set(nt, nitem, fname(f), fname_len(f), text, n) != GDEFCORE_OK) {
      return -1;
    }
  }
  return 0;
}

static int build_group(const struct Vctx * v, size_t gi, const char * prefix,
    GDEFCORE_Tree * nt, size_t nitem) {
  const GDEFCORE_FieldInfo * g = &v->s->f[gi];
  size_t k;
  for (k = 0; k < 256; k++) {
    uint64_t gid;
    char rm[NAME_MAX_CTL];
    char nprefix[NAME_MAX_CTL];
    char * got = NULL;
    size_t got_n = 0;
    size_t nc = 0;
    if (body_item(v->body, v->body_len, prefix, fname(g), k, &gid) != 0) {
      break;
    }
    snprintf(rm, sizeof rm, "%s%.*s-%zu--rm", prefix, (int)fname_len(g), fname(g), k);
    if (form_field_alloc(v->body, v->body_len, rm, 0, &got, &got_n) == 0) {
      free(got);
      continue;
    }
    snprintf(nprefix, sizeof nprefix, "%s%.*s-%zu-", prefix, (int)fname_len(g), fname(g), k);
    if (gid == 0 && item_blank(v->s, gi, nprefix, v->body, v->body_len)) {
      continue;
    }
    if (gdefcore_tree_add(nt, nitem, fname(g), fname_len(g), gid, &nc) != GDEFCORE_OK ||
        build_fields(v, gi, nprefix, nt, nc, NULL) != 0) {
      return -1;
    }
  }
  return 0;
}

static int build_fields(const struct Vctx * v, size_t parent, const char * prefix,
    GDEFCORE_Tree * nt, size_t nitem, const GDEFCORE_Tree * ot) {
  size_t j;
  for (j = 0; j < v->s->n; j++) {
    const GDEFCORE_FieldInfo * f = &v->s->f[j];
    char cname[NAME_MAX_CTL];
    size_t k;
    if (f->parent != parent) {
      continue;
    }
    if (parent == GDEFCORE_TREE_ROOT &&
        !shown(v->c, v->type, v->id, v->stored, f, v->by_tree)) {
      /* Not the person's to change: as it is now. */
      if (ot != NULL && copy_field(v->s, j, ot, 0, nt, nitem) != 0) {
        return -1;
      }
      continue;
    }
    if (f->type == GDEFSTOR_FIELD_GROUP) {
      if (build_group(v, j, prefix, nt, nitem) != 0) {
        return -1;
      }
      continue;
    }
    if (strlen(prefix) + fname_len(f) >= sizeof cname) {
      return -1;
    }
    snprintf(cname, sizeof cname, "%s%.*s", prefix, (int)fname_len(f), fname(f));
    for (k = 0; k < (f->many ? 4096u : 1u); k++) {
      char * got = NULL;
      size_t got_n = 0;
      size_t child = 0;
      int bad = 0;
      if (form_field_alloc(v->body, v->body_len, cname, k, &got, &got_n) != 0) {
        break;
      }
      if (got_n > CONTENT_TEXT_MAX) {
        bad = 1;
      } else if (got_n > 0) {
        if (f->many) {
          bad = gdefcore_tree_add(nt, nitem, fname(f), fname_len(f), 0, &child) != GDEFCORE_OK ||
                gdefcore_tree_set(nt, child, "", 0, got, got_n) != GDEFCORE_OK;
        } else {
          bad = gdefcore_tree_set(nt, nitem, fname(f), fname_len(f), got, got_n) != GDEFCORE_OK;
        }
      }
      free(got);
      if (bad) {
        return -1;
      }
    }
  }
  return 0;
}

/*
 * Save the form of one entity. Returns 0 with @p out set (a redirect, a refusal
 * drawn again, or the page that does not exist), -1 for an error that is not
 * the person's. A type with groups and no personal field is saved as a tree
 * (the groups' items kept by id, the fields the person may not update as they
 * are); any other type's root fields are saved as values, which leaves what the
 * form does not carry alone.
 */
static int save_post(struct Ctx * c, const char * type, int kind, const char * id,
    const char * body, size_t body_len, int more, struct GDEFWEB_Admin_Reply * out) {
  struct Schema schema;
  struct Redraw again;
  struct Vctx vc;
  struct Flat * fl = NULL;
  GDEFCORE_Tree * nt = NULL;
  GDEFCORE_Tree * ot = NULL;
  uint64_t base = 0;
  int stored = has_revision(c, id);
  int notice = NOTICE_INVALID;
  int rc = -1;
  GDEFCORE_Changeset * cs = NULL;
  int risk = 0;
  GDEFCORE_Result r;
  int as_tree;
  memset(&schema, 0, sizeof schema);
  memset(&again, 0, sizeof again);
  memset(&vc, 0, sizeof vc);
  again.body = body;
  again.body_len = body_len;
  again.more = more;
  if (stored ? !allowed(c, GDEFCORE_ACTION_UPDATE, type, id) : !may_create(c, type)) {
    return unknown_admin("POST", out);
  }
  if (schema_load(c, type, &schema) != 0) {
    return -1;
  }
  as_tree = tree_path(&schema);
  if (!token_ok(c, body, body_len, "content-save", id, &base) ||
      (!more && !base_matches(c->catalog, id, base))) {
    notice = NOTICE_STALE;
    goto again;
  }
  if (more) {
    /* Rows were asked for, and nothing is written. */
    notice = NOTICE_NONE;
    goto again;
  }
  vc.c = c;
  vc.s = &schema;
  vc.type = type;
  vc.id = id;
  vc.stored = stored;
  vc.by_tree = as_tree;
  vc.body = body;
  vc.body_len = body_len;
  vc.errs = &again.errs;
  validate_fields(&vc, GDEFCORE_TREE_ROOT, "");
  fl = (struct Flat *)calloc(1, sizeof *fl);
  if (fl == NULL) {
    goto done;
  }
  if (collect_flat(c, &schema, type, id, stored, as_tree, body, body_len, fl, &again.errs) != 0) {
    goto again_invalid;
  }
  if (again.errs.n == 0) {
    attribute_flat(c, fl, body, body_len, &again.errs);
  }
  if (again.errs.n > 0) {
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
    if (as_tree) {
      if (stored &&
          gdefcore_entity_tree(c->catalog, NULL, GDEFCORE_VIEW_DRAFT, id, GDEFCORE_PUBLIC_ID_LEN,
              "en", 2, &ot) != GDEFCORE_OK) {
        goto done;
      }
      if (gdefcore_tree_create(c->catalog, &nt) != GDEFCORE_OK) {
        goto done;
      }
      if (build_fields(&vc, GDEFCORE_TREE_ROOT, "", nt, 0, ot) != 0) {
        goto again_invalid;
      }
      r = gdefcore_tree_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, nt);
    } else {
      r = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, fl->values, fl->n);
    }
    if (r != GDEFCORE_OK) {
      if (r == GDEFCORE_ERR_OOM) {
        goto done;
      }
      {
        size_t why_n = 0;
        const char * why = gdefcore_changeset_refusal(cs, &why_n);
        if (why != NULL && why_n > 0) {
          snprintf(c->reason, sizeof c->reason, "%.*s", (int)why_n, why);
        }
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
  goto done;
again_invalid:
  notice = NOTICE_INVALID;
again:
  gdefcore_changeset_destroy(cs);
  cs = NULL;
  rc = edit_page(c, type, kind, id, &again, notice, out);
done:
  gdefcore_changeset_destroy(cs);
  gdefcore_tree_free(nt);
  gdefcore_tree_free(ot);
  if (fl != NULL) {
    flat_free(fl);
    free(fl);
  }
  schema_free(&schema);
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
  {
    GDEFCORE_Result made = gdefcore_entity_create(c->catalog, type, strlen(type), id, NULL);
    if (made == GDEFCORE_ERR_INVALID) {
      /* A type whose entities are subjects is made for a person, which this
       * button does not know: it is refused, not an error of the server's. */
      say(c->catalog, c->request, GDEFCORE_MESSAGE_ERROR,
          "Not started: entities of this type cannot be made here.");
      return redirect_list(out, type);
    }
    if (made != GDEFCORE_OK) {
      return -1;
    }
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
  c.reason[0] = '\0';
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
      rc = save_post(&c, type, kind, rest, body, body_len, 0, out);
    } else if (action_n == 4 && memcmp(action, "more", 4) == 0) {
      rc = save_post(&c, type, kind, rest, body, body_len, 1, out);
    } else if (action_n == 6 && memcmp(action, "delete", 6) == 0) {
      rc = delete_post(&c, type, kind, rest, body, body_len, out);
    } else {
      rc = unknown_admin(method, out);
    }
    free(action);
    return rc;
  }
}
