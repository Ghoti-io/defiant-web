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
 * Views in the browser (CAP-2). /admin/views lists the stored views and draws
 * one form for a view: its type, condition, sort, columns, arguments, set format
 * and page size, with the fields of the chosen type and the operators and
 * formatters each kind takes written beside it, from the core's own tables.
 * Every action is a signed form that works with scripts off. A save is one
 * changeset that the core validates, so a refusal comes back with the core's
 * words and the form is drawn again with what was typed. A preview runs the saved
 * view as the signed-in editor with example argument values. /v/<name> serves a
 * view to a visitor through the access filter. Who may write a view is the
 * permission GDEFCORE_VIEW_EDIT, which the core asks of the changeset as well as
 * the route.
 */

#include "views.h"

#include "adminkit.h"
#include "nav.h"

#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/message.h>
#include <ghoti.io/defiant-core/view.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWS_BASE "/admin/views"
#define VIEWS_COLS 8
#define VIEWS_ARGS 4
#define VIEWS_NAME_MAX 64
#define VIEWS_COND_MAX 2048
#define VIEWS_SHORT 128
#define VIEWS_LIMIT_DEFAULT 25

struct Ctx {
  const GDEFWEB_Allocator * alloc;
  GDEFCORE_Catalog * catalog;
  GDEFCORE_Request * request;
  GDEFCORE_Keyring * keyring;
  const uint8_t * root;
  size_t root_len;
  const char * session;
  size_t session_len;
};

/* What the form says, as text, so that a refused form is drawn again with it. */
struct Draft {
  char name[VIEWS_NAME_MAX + 1];
  char type[VIEWS_SHORT];
  char condition[VIEWS_COND_MAX + 1];
  char sort[VIEWS_SHORT];
  char sort2[VIEWS_SHORT];
  int desc;
  int desc2;
  int format;
  char limit[16];
  size_t ncol;
  struct {
    char field[VIEWS_SHORT];
    char formatter[VIEWS_SHORT];
    char label[VIEWS_SHORT];
  } col[VIEWS_COLS];
  size_t narg;
  struct {
    char name[VIEWS_SHORT];
    int source;
    char fixed[VIEWS_SHORT];
  } arg[VIEWS_ARGS];
  int too_long; /* a value did not fit what the form allows */
};

static const char * const kSourceName[] = {"", "url", "exposed", "principal", "fixed"};
static const char * const kFormatName[] = {"", "table", "list", "csv", "json"};

/* ---- small helpers ------------------------------------------------------------ */

static int name_segment_ok(const char * s, size_t n) {
  size_t i;
  if (n == 0 || n > VIEWS_NAME_MAX) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return 0;
    }
  }
  return 1;
}

static int copy_in(char * dst, size_t cap, const char * s, size_t n, int * too_long) {
  if (n >= cap) {
    *too_long = 1;
    n = cap - 1;
  }
  memcpy(dst, s, n);
  dst[n] = '\0';
  return 0;
}

/* The posted value of @p name, copied into @p dst; an absent value is empty. */
static void body_text(const char * body, size_t body_len, const char * name, char * dst,
    size_t cap, int * too_long) {
  char * v = NULL;
  size_t n = 0;
  dst[0] = '\0';
  if (form_field_alloc(body, body_len, name, 0, &v, &n) == 0) {
    (void)copy_in(dst, cap, v, n, too_long);
  }
  free(v);
}

static int body_has(const char * body, size_t body_len, const char * name) {
  char * v = NULL;
  size_t n = 0;
  int has = form_field_alloc(body, body_len, name, 0, &v, &n) == 0;
  free(v);
  return has;
}

static int token_for(struct Ctx * c, const char * body, size_t body_len, const char * form_id,
    char target[GDEFCORE_PUBLIC_ID_LEN + 1], uint64_t * base) {
  char * token = NULL;
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char got_form[32];
  size_t got_len = 0;
  uint64_t b = 0;
  int ok = 0;
  if (form_field_alloc(body, body_len, "token", 0, &token, &token_n) == 0 &&
      b64_decode(token, raw, sizeof raw, &raw_n) == 0 &&
      gdefcore_form_check(c->catalog, c->request, c->keyring, c->root, c->root_len,
          c->session, c->session_len, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&
      token_meta(raw, raw_n, got_form, sizeof got_form, &got_len, target, &b) == 0 &&
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

/* Sign a form for @p target (an entity id: a view's, or the person's own for a
 * new one). The core holds a form to the required fields of the target's type,
 * so the token carries each of them empty, as the other forms do. */
static int sign_for(struct Ctx * c, const char * form_id, const char * target, char ** out) {
  enum { kMax = 32 };
  char * type = NULL;
  size_t type_n = 0;
  size_t nfields = 0;
  size_t i;
  size_t n = 0;
  GDEFCORE_Value values[kMax];
  const char * slots[kMax];
  size_t lens[kMax];
  char * names[kMax];
  int rc = -1;
  if (gdefcore_entity_type(c->catalog, target, GDEFCORE_PUBLIC_ID_LEN, &type, &type_n) !=
      GDEFCORE_OK) {
    return -1;
  }
  if (gdefcore_type_field_count(c->catalog, type, type_n, &nfields) == GDEFCORE_OK) {
    for (i = 0; i < nfields && n < kMax; i++) {
      char * name = NULL;
      size_t name_n = 0;
      int required = 0;
      int many = 0;
      if (gdefcore_type_field(c->catalog, type, type_n, i, &name, &name_n, &required, &many) !=
          GDEFCORE_OK) {
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
  rc = sign_b64(c->alloc, c->catalog, c->request, c->keyring, c->root, c->root_len, c->session,
      c->session_len, form_id, target, values, n, out);
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
  if (sign_for(c, form_id, target, &token) != 0) {
    return -1;
  }
  rc = page_token(page, token);
  gcu_allocator_free(c->alloc, token);
  return rc;
}

/* The id of the entity of the stored view @p name; 0 when there is none. */
static int view_entity(struct Ctx * c, const char * name, char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  int found = 0;
  if (gdefcore_entity_ids(c->catalog, GDEFCORE_VIEW_TYPE, sizeof GDEFCORE_VIEW_TYPE - 1, &ids,
          &count) != GDEFCORE_OK) {
    return 0;
  }
  for (i = 0; i < count && !found; i++) {
    char ** texts = NULL;
    size_t * lens = NULL;
    size_t n = 0;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
      continue;
    }
    if (gdefcore_entity_values(c->catalog, NULL, GDEFCORE_VIEW_PUBLISHED, ids[i],
            GDEFCORE_PUBLIC_ID_LEN, "en", 2, "name", 4, &texts, &lens, &n) == GDEFCORE_OK) {
      if (n > 0 && lens[0] == strlen(name) && memcmp(texts[0], name, lens[0]) == 0) {
        memcpy(id, ids[i], GDEFCORE_PUBLIC_ID_LEN + 1);
        found = 1;
      }
      gdefcore_values_free(c->catalog, texts, lens, n);
    }
  }
  gdefcore_ids_free(c->catalog, ids, count);
  return found;
}

static int principal_id(struct Ctx * c, char out[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  size_t n = 0;
  const char * who = gdefcore_request_principal(c->request, &n);
  if (who == NULL || n != GDEFCORE_PUBLIC_ID_LEN) {
    return 0;
  }
  memcpy(out, who, n);
  out[n] = '\0';
  return 1;
}

/* ---- the draft ------------------------------------------------------------------ */

static int source_of(const char * s) {
  int i;
  for (i = 1; i <= 4; i++) {
    if (strcmp(s, kSourceName[i]) == 0) {
      return i;
    }
  }
  return 0;
}

static int format_of(const char * s) {
  int i;
  for (i = 1; i <= 4; i++) {
    if (strcmp(s, kFormatName[i]) == 0) {
      return i;
    }
  }
  return 0;
}

static void draft_from_body(struct Draft * d, const char * body, size_t body_len) {
  size_t i;
  char key[32];
  char text[VIEWS_SHORT];
  memset(d, 0, sizeof *d);
  body_text(body, body_len, "name", d->name, sizeof d->name, &d->too_long);
  body_text(body, body_len, "type", d->type, sizeof d->type, &d->too_long);
  body_text(body, body_len, "condition", d->condition, sizeof d->condition, &d->too_long);
  body_text(body, body_len, "sort", d->sort, sizeof d->sort, &d->too_long);
  body_text(body, body_len, "sort2", d->sort2, sizeof d->sort2, &d->too_long);
  d->desc = body_has(body, body_len, "desc");
  d->desc2 = body_has(body, body_len, "desc2");
  body_text(body, body_len, "format", text, sizeof text, &d->too_long);
  d->format = format_of(text);
  body_text(body, body_len, "limit", d->limit, sizeof d->limit, &d->too_long);
  for (i = 0; i < VIEWS_COLS; i++) {
    char field[VIEWS_SHORT];
    snprintf(key, sizeof key, "col%zu_field", i);
    body_text(body, body_len, key, field, sizeof field, &d->too_long);
    if (field[0] == '\0') {
      continue;
    }
    memcpy(d->col[d->ncol].field, field, sizeof field);
    snprintf(key, sizeof key, "col%zu_fmt", i);
    body_text(body, body_len, key, d->col[d->ncol].formatter,
        sizeof d->col[d->ncol].formatter, &d->too_long);
    snprintf(key, sizeof key, "col%zu_label", i);
    body_text(body, body_len, key, d->col[d->ncol].label, sizeof d->col[d->ncol].label,
        &d->too_long);
    d->ncol += 1;
  }
  for (i = 0; i < VIEWS_ARGS; i++) {
    char name[VIEWS_SHORT];
    snprintf(key, sizeof key, "arg%zu_name", i);
    body_text(body, body_len, key, name, sizeof name, &d->too_long);
    if (name[0] == '\0') {
      continue;
    }
    memcpy(d->arg[d->narg].name, name, sizeof name);
    snprintf(key, sizeof key, "arg%zu_source", i);
    body_text(body, body_len, key, text, sizeof text, &d->too_long);
    d->arg[d->narg].source = source_of(text);
    snprintf(key, sizeof key, "arg%zu_fixed", i);
    body_text(body, body_len, key, d->arg[d->narg].fixed, sizeof d->arg[d->narg].fixed,
        &d->too_long);
    d->narg += 1;
  }
}

static void draft_from_def(struct Draft * d, const GDEFCORE_View_Def * def) {
  size_t i;
  int ignore = 0;
  memset(d, 0, sizeof *d);
  (void)copy_in(d->name, sizeof d->name, def->name, def->name_len, &ignore);
  (void)copy_in(d->type, sizeof d->type, def->type, def->type_len, &ignore);
  if (def->condition != NULL) {
    (void)copy_in(d->condition, sizeof d->condition, def->condition, def->condition_len,
        &ignore);
  }
  if (def->sort != NULL) {
    (void)copy_in(d->sort, sizeof d->sort, def->sort, def->sort_len, &ignore);
  }
  if (def->sort2 != NULL) {
    (void)copy_in(d->sort2, sizeof d->sort2, def->sort2, def->sort2_len, &ignore);
  }
  d->desc = def->sort_descending;
  d->desc2 = def->sort2_descending;
  d->format = def->format;
  snprintf(d->limit, sizeof d->limit, "%llu", (unsigned long long)def->limit);
  for (i = 0; i < def->column_count && i < VIEWS_COLS; i++) {
    const GDEFCORE_View_Column * col = &def->columns[i];
    (void)copy_in(d->col[i].field, sizeof d->col[i].field, col->field, col->field_len, &ignore);
    if (col->formatter != NULL) {
      (void)copy_in(d->col[i].formatter, sizeof d->col[i].formatter, col->formatter,
          col->formatter_len, &ignore);
    }
    if (col->label != NULL) {
      (void)copy_in(d->col[i].label, sizeof d->col[i].label, col->label, col->label_len,
          &ignore);
    }
    d->ncol += 1;
  }
  for (i = 0; i < def->arg_count && i < VIEWS_ARGS; i++) {
    const GDEFCORE_View_Arg * a = &def->args[i];
    (void)copy_in(d->arg[i].name, sizeof d->arg[i].name, a->name, a->name_len, &ignore);
    d->arg[i].source = a->source;
    if (a->fixed != NULL) {
      (void)copy_in(d->arg[i].fixed, sizeof d->arg[i].fixed, a->fixed, a->fixed_len, &ignore);
    }
    d->narg += 1;
  }
}

struct Built {
  GDEFCORE_View_Column cols[VIEWS_COLS];
  GDEFCORE_View_Arg args[VIEWS_ARGS];
  GDEFCORE_View_Def def;
};

/* The core's definition of what the draft says. A limit that is not a number is
 * 0, which the core refuses with its reason. */
static void build_def(const struct Draft * d, struct Built * b) {
  size_t i;
  char * end = NULL;
  unsigned long long limit = strtoull(d->limit, &end, 10);
  memset(b, 0, sizeof *b);
  for (i = 0; i < d->ncol; i++) {
    b->cols[i].size = sizeof b->cols[i];
    b->cols[i].field = d->col[i].field;
    b->cols[i].field_len = strlen(d->col[i].field);
    b->cols[i].formatter = d->col[i].formatter;
    b->cols[i].formatter_len = strlen(d->col[i].formatter);
    b->cols[i].label = d->col[i].label;
    b->cols[i].label_len = strlen(d->col[i].label);
  }
  for (i = 0; i < d->narg; i++) {
    b->args[i].size = sizeof b->args[i];
    b->args[i].name = d->arg[i].name;
    b->args[i].name_len = strlen(d->arg[i].name);
    b->args[i].source = d->arg[i].source;
    b->args[i].fixed = d->arg[i].fixed;
    b->args[i].fixed_len = strlen(d->arg[i].fixed);
  }
  b->def.size = sizeof b->def;
  b->def.name = d->name;
  b->def.name_len = strlen(d->name);
  b->def.type = d->type;
  b->def.type_len = strlen(d->type);
  b->def.condition = d->condition;
  b->def.condition_len = strlen(d->condition);
  b->def.sort = d->sort;
  b->def.sort_len = strlen(d->sort);
  b->def.sort_descending = d->desc;
  b->def.sort2 = d->sort2;
  b->def.sort2_len = strlen(d->sort2);
  b->def.sort2_descending = d->desc2;
  b->def.columns = b->cols;
  b->def.column_count = d->ncol;
  b->def.args = d->narg > 0 ? b->args : NULL;
  b->def.arg_count = d->narg;
  b->def.format = d->format;
  b->def.limit = (end != NULL && *end == '\0' && d->limit[0] != '\0') ? limit : 0;
}

/* ---- drawing ----------------------------------------------------------------- */

static int attr(struct Page * page, const char * name, const char * value) {
  return page_str(page, " ") != 0 || page_str(page, name) != 0 ||
         page_str(page, "=\"") != 0 || page_esc(page, value, strlen(value)) != 0 ||
         page_str(page, "\"") != 0;
}

static int text_input(struct Page * page, const char * label, const char * name,
    const char * value, int readonly) {
  return page_str(page, "<p><label>") != 0 || page_esc(page, label, strlen(label)) != 0 ||
         page_str(page, " <input type=\"text\"") != 0 || attr(page, "name", name) != 0 ||
         attr(page, "value", value) != 0 || (readonly && page_str(page, " readonly") != 0) ||
         page_str(page, "></label></p>");
}

static int select_of(struct Page * page, const char * name, const char * const * options,
    size_t first, size_t last, const char * current) {
  size_t i;
  if (page_str(page, "<select") != 0 || attr(page, "name", name) != 0 ||
      page_str(page, ">") != 0) {
    return -1;
  }
  for (i = first; i <= last; i++) {
    if (page_str(page, "<option") != 0 || attr(page, "value", options[i]) != 0 ||
        (strcmp(options[i], current) == 0 && page_str(page, " selected") != 0) ||
        page_str(page, ">") != 0 || page_esc(page, options[i], strlen(options[i])) != 0 ||
        page_str(page, "</option>") != 0) {
      return -1;
    }
  }
  return page_str(page, "</select>");
}

/* What may be written for the type: each field with its kind and the operators
 * and formatters that kind takes, from the core's own tables. */
static int field_table(struct Ctx * c, struct Page * page, const char * type) {
  size_t count = 0;
  size_t i;
  if (type[0] == '\0' ||
      gdefcore_type_tree_field_count(c->catalog, type, strlen(type), &count) != GDEFCORE_OK) {
    return 0;
  }
  if (page_str(page, "<h2>Fields of ") != 0 || page_esc(page, type, strlen(type)) != 0 ||
      page_str(page, "</h2><table><thead><tr><th>Field</th><th>Operators</th>"
                     "<th>Formatters</th></tr></thead><tbody>") != 0) {
    return -1;
  }
  for (i = 0; i < count; i++) {
    GDEFCORE_FieldInfo info;
    size_t n;
    size_t k;
    const char * const * ops;
    const char * const * fmts;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (gdefcore_type_tree_field(c->catalog, type, strlen(type), i, &info) != GDEFCORE_OK) {
      continue;
    }
    if (page_str(page, "<tr><td>") != 0 || page_esc(page, info.path, info.path_len) != 0 ||
        page_str(page, "</td><td>") != 0) {
      return -1;
    }
    if (info.type == GDEFSTOR_FIELD_GROUP || info.depth > 0) {
      if (page_str(page, "not in a condition") != 0) {
        return -1;
      }
    } else if (info.personal) {
      if (page_str(page, "personal: not shown") != 0) {
        return -1;
      }
    } else {
      ops = gdefcore_view_operators(info.type, &n);
      for (k = 0; k < n; k++) {
        if ((k > 0 && page_str(page, " ") != 0) || page_esc(page, ops[k], strlen(ops[k])) != 0) {
          return -1;
        }
      }
    }
    if (page_str(page, "</td><td>") != 0) {
      return -1;
    }
    if (info.personal) {
      if (page_str(page, "none") != 0) {
        return -1;
      }
    } else {
      fmts = gdefcore_view_formatters(info.type, info.many || info.depth > 0, &n);
      for (k = 0; k < n; k++) {
        if ((k > 0 && page_str(page, " ") != 0) ||
            page_esc(page, fmts[k], strlen(fmts[k])) != 0) {
          return -1;
        }
      }
    }
    if (page_str(page, "</td></tr>") != 0) {
      return -1;
    }
  }
  return page_str(page, "</tbody></table>");
}

/* A view's preview: the saved view run as the signed-in editor. */
static int preview_section(struct Ctx * c, struct Page * page, const struct Draft * d,
    const char * body, size_t body_len) {
  GDEFCORE_View_Param params[VIEWS_ARGS];
  char values[VIEWS_ARGS][VIEWS_SHORT];
  GDEFCORE_View_Result * result = NULL;
  size_t i;
  size_t row;
  size_t col;
  size_t n_params = 0;
  GDEFCORE_Result rc;
  int ignore = 0;
  for (i = 0; i < d->narg; i++) {
    char key[32];
    if (d->arg[i].source != GDEFCORE_VIEW_SOURCE_URL &&
        d->arg[i].source != GDEFCORE_VIEW_SOURCE_EXPOSED) {
      continue;
    }
    snprintf(key, sizeof key, "p_%s", d->arg[i].name);
    body_text(body, body_len, key, values[n_params], sizeof values[n_params], &ignore);
    if (values[n_params][0] == '\0') {
      continue;
    }
    memset(&params[n_params], 0, sizeof params[n_params]);
    params[n_params].size = sizeof params[n_params];
    params[n_params].name = d->arg[i].name;
    params[n_params].name_len = strlen(d->arg[i].name);
    params[n_params].value = values[n_params];
    params[n_params].value_len = strlen(values[n_params]);
    n_params += 1;
  }
  rc = gdefcore_view_run(c->catalog, c->request, d->name, strlen(d->name), params, n_params, 0,
      &result);
  if (page_str(page, "<h2>Preview</h2>") != 0) {
    gdefcore_view_result_free(result);
    return -1;
  }
  if (rc != GDEFCORE_OK) {
    gdefcore_view_result_free(result);
    return page_str(page, "<p>The saved view does not run.</p>");
  }
  if (page_str(page, "<p>") != 0) {
    gdefcore_view_result_free(result);
    return -1;
  }
  {
    char count[48];
    snprintf(count, sizeof count, "%llu rows match; the first %zu are shown.",
        (unsigned long long)gdefcore_view_result_total(result),
        gdefcore_view_result_rows(result));
    if (page_str(page, count) != 0 || page_str(page, "</p><table><thead><tr>") != 0) {
      gdefcore_view_result_free(result);
      return -1;
    }
  }
  for (col = 0; col < gdefcore_view_result_columns(result); col++) {
    size_t n = 0;
    const char * l = gdefcore_view_result_label(result, col, &n);
    if (page_str(page, "<th>") != 0 || page_esc(page, l, n) != 0 ||
        page_str(page, "</th>") != 0) {
      gdefcore_view_result_free(result);
      return -1;
    }
  }
  if (page_str(page, "</tr></thead><tbody>") != 0) {
    gdefcore_view_result_free(result);
    return -1;
  }
  for (row = 0; row < gdefcore_view_result_rows(result); row++) {
    if (page_str(page, "<tr>") != 0) {
      gdefcore_view_result_free(result);
      return -1;
    }
    for (col = 0; col < gdefcore_view_result_columns(result); col++) {
      size_t n = 0;
      const char * v = gdefcore_view_result_cell(result, row, col, &n);
      if (page_str(page, "<td>") != 0 || page_esc(page, v, n) != 0 ||
          page_str(page, "</td>") != 0) {
        gdefcore_view_result_free(result);
        return -1;
      }
    }
    if (page_str(page, "</tr>") != 0) {
      gdefcore_view_result_free(result);
      return -1;
    }
  }
  gdefcore_view_result_free(result);
  return page_str(page, "</tbody></table>");
}

/* The form of a view: new when @p entity is empty. @p reason is the core's
 * refusal, drawn above the buttons; @p preview_body the posted preview values. */
static int form_page(struct Ctx * c, const struct Draft * d, const char * entity,
    const char * reason, const char * notice, int with_preview, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  const char * target = entity[0] != '\0' ? entity : principal;
  size_t i;
  memset(&page, 0, sizeof page);
  page.alloc = c->alloc;
  if (!principal_id(c, principal)) {
    return unknown_admin("GET", out);
  }
  if (page_begin(&page, entity[0] != '\0' ? "Edit a view" : "New view") != 0 ||
      page_str(&page, "<h1>") != 0 ||
      page_str(&page, entity[0] != '\0' ? "Edit a view" : "New view") != 0 ||
      page_str(&page, "</h1>") != 0) {
    goto fail;
  }
  if (notice != NULL && (page_str(&page, "<p class=\"notice\" role=\"alert\">") != 0 ||
                            page_esc(&page, notice, strlen(notice)) != 0 ||
                            page_str(&page, "</p>") != 0)) {
    goto fail;
  }
  if (page_str(&page, "<form method=\"post\" action=\"" VIEWS_BASE "/") != 0 ||
      page_esc(&page, entity[0] != '\0' ? d->name : "new",
          entity[0] != '\0' ? strlen(d->name) : 3) != 0 ||
      page_str(&page, "\">") != 0 || hidden_token(c, &page, "view-save", target) != 0 ||
      text_input(&page, "Name", "name", d->name, entity[0] != '\0') != 0 ||
      text_input(&page, "Type", "type", d->type, 0) != 0 ||
      page_str(&page, "<p><label>Condition <textarea name=\"condition\" rows=\"4\" cols=\"60\">") != 0 ||
      page_esc(&page, d->condition, strlen(d->condition)) != 0 ||
      page_str(&page, "</textarea></label> Written as the condition language does: "
                      "<code>(and (= field.kind \"news\") (= field.author $who))</code>. "
                      "An argument is <code>$name</code> and is declared below.</p>") != 0 ||
      text_input(&page, "Sort by", "sort", d->sort, 0) != 0 ||
      page_str(&page, "<p><label><input type=\"checkbox\" name=\"desc\" value=\"1\"") != 0 ||
      (d->desc && page_str(&page, " checked") != 0) ||
      page_str(&page, "> Descending</label></p>") != 0 ||
      text_input(&page, "Then by", "sort2", d->sort2, 0) != 0 ||
      page_str(&page, "<p><label><input type=\"checkbox\" name=\"desc2\" value=\"1\"") != 0 ||
      (d->desc2 && page_str(&page, " checked") != 0) ||
      page_str(&page, "> Descending</label></p><h2>Columns</h2>") != 0) {
    goto fail;
  }
  for (i = 0; i < VIEWS_COLS; i++) {
    char key[32];
    const char * f = i < d->ncol ? d->col[i].field : "";
    const char * fm = i < d->ncol ? d->col[i].formatter : "";
    const char * l = i < d->ncol ? d->col[i].label : "";
    snprintf(key, sizeof key, "col%zu_field", i);
    if (page_str(&page, "<p><label>Field <input type=\"text\"") != 0 ||
        attr(&page, "name", key) != 0 || attr(&page, "value", f) != 0 ||
        page_str(&page, "></label> ") != 0) {
      goto fail;
    }
    snprintf(key, sizeof key, "col%zu_fmt", i);
    if (page_str(&page, "<label>Formatter <input type=\"text\"") != 0 ||
        attr(&page, "name", key) != 0 || attr(&page, "value", fm) != 0 ||
        page_str(&page, "></label> ") != 0) {
      goto fail;
    }
    snprintf(key, sizeof key, "col%zu_label", i);
    if (page_str(&page, "<label>Heading <input type=\"text\"") != 0 ||
        attr(&page, "name", key) != 0 || attr(&page, "value", l) != 0 ||
        page_str(&page, "></label></p>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "<h2>Arguments</h2>") != 0) {
    goto fail;
  }
  for (i = 0; i < VIEWS_ARGS; i++) {
    char key[32];
    const char * nm = i < d->narg ? d->arg[i].name : "";
    const char * fx = i < d->narg ? d->arg[i].fixed : "";
    int src = i < d->narg ? d->arg[i].source : GDEFCORE_VIEW_SOURCE_URL;
    snprintf(key, sizeof key, "arg%zu_name", i);
    if (page_str(&page, "<p><label>Name <input type=\"text\"") != 0 ||
        attr(&page, "name", key) != 0 || attr(&page, "value", nm) != 0 ||
        page_str(&page, "></label> <label>Source ") != 0) {
      goto fail;
    }
    snprintf(key, sizeof key, "arg%zu_source", i);
    if (select_of(&page, key, kSourceName, 1, 4, kSourceName[src < 1 || src > 4 ? 1 : src]) != 0 ||
        page_str(&page, "</label> ") != 0) {
      goto fail;
    }
    snprintf(key, sizeof key, "arg%zu_fixed", i);
    if (page_str(&page, "<label>Value or default <input type=\"text\"") != 0 ||
        attr(&page, "name", key) != 0 || attr(&page, "value", fx) != 0 ||
        page_str(&page, "></label></p>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "<p><label>Format ") != 0 ||
      select_of(&page, "format", kFormatName, 1, 4, kFormatName[d->format < 1 || d->format > 4 ? 1 : d->format]) != 0 ||
      page_str(&page, "</label> ") != 0 ||
      page_str(&page, "<label>Rows in a page <input type=\"text\" size=\"4\"") != 0 ||
      attr(&page, "name", "limit") != 0 ||
      attr(&page, "value", d->limit[0] != '\0' ? d->limit : "25") != 0 ||
      page_str(&page, "></label></p>") != 0) {
    goto fail;
  }
  if (reason != NULL &&
      (page_str(&page, "<p class=\"notice\" role=\"alert\">Not saved: ") != 0 ||
          page_esc(&page, reason, strlen(reason)) != 0 || page_str(&page, "</p>") != 0)) {
    goto fail;
  }
  if (page_str(&page, "<p><button type=\"submit\" name=\"do\" value=\"save\">Save</button></p></form>") != 0) {
    goto fail;
  }
  if (entity[0] != '\0') {
    char loc[96];
    snprintf(loc, sizeof loc, "/v/%s", d->name);
    if (page_str(&page, "<p><a href=\"") != 0 || page_esc(&page, loc, strlen(loc)) != 0 ||
        page_str(&page, "\">The public page of this view</a></p>") != 0) {
      goto fail;
    }
    /* A preview: the saved view, with example values for what it asks. */
    if (page_str(&page, "<form method=\"post\" action=\"" VIEWS_BASE "/") != 0 ||
        page_esc(&page, d->name, strlen(d->name)) != 0 || page_str(&page, "\">") != 0 ||
        hidden_token(c, &page, "view-preview", target) != 0 ||
        attr(&page, "data-view", d->name) != 0) {
      goto fail;
    }
    for (i = 0; i < d->narg; i++) {
      char key[VIEWS_SHORT + 4];
      char sample[VIEWS_SHORT];
      int ignore = 0;
      if (d->arg[i].source != GDEFCORE_VIEW_SOURCE_URL &&
          d->arg[i].source != GDEFCORE_VIEW_SOURCE_EXPOSED) {
        continue;
      }
      snprintf(key, sizeof key, "p_%s", d->arg[i].name);
      body_text(with_preview ? body : "", with_preview ? body_len : 0, key, sample,
          sizeof sample, &ignore);
      if (page_str(&page, "<p><label>Example for ") != 0 ||
          page_esc(&page, d->arg[i].name, strlen(d->arg[i].name)) != 0 ||
          page_str(&page, " <input type=\"text\"") != 0 || attr(&page, "name", key) != 0 ||
          attr(&page, "value", sample) != 0 || page_str(&page, "></label></p>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "<p><button type=\"submit\" name=\"do\" value=\"preview\">Preview</button></p></form>") != 0) {
      goto fail;
    }
    if (with_preview && preview_section(c, &page, d, body, body_len) != 0) {
      goto fail;
    }
    if (page_str(&page, "<form method=\"post\" action=\"" VIEWS_BASE "/") != 0 ||
        page_esc(&page, d->name, strlen(d->name)) != 0 || page_str(&page, "\">") != 0 ||
        hidden_token(c, &page, "view-delete", target) != 0 ||
        page_str(&page, "<p><label>To delete this view type its name: <input type=\"text\" "
                        "name=\"confirm\"></label> <button type=\"submit\" name=\"do\" "
                        "value=\"delete\">Delete</button></p></form>") != 0) {
      goto fail;
    }
  }
  if (field_table(c, &page, d->type) != 0) {
    goto fail;
  }
  return take_page(&page, out);
fail:
  page_free(&page);
  return -1;
}

static int list_page(struct Ctx * c, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char ** names = NULL;
  size_t count = 0;
  size_t i;
  memset(&page, 0, sizeof page);
  page.alloc = c->alloc;
  if (page_begin(&page, "Views") != 0 || page_str(&page, "<h1>Views</h1>") != 0 ||
      page_str(&page, "<p><a href=\"" VIEWS_BASE "/new\">New view</a></p>") != 0) {
    goto fail;
  }
  if (gdefcore_view_names(c->catalog, &names, &count) != GDEFCORE_OK) {
    goto fail;
  }
  if (count == 0 && page_str(&page, "<p>There is no view yet.</p>") != 0) {
    goto fail;
  }
  if (count > 0 && page_str(&page, "<ul>") != 0) {
    goto fail;
  }
  for (i = 0; i < count; i++) {
    if (page_str(&page, "<li><a href=\"" VIEWS_BASE "/") != 0 ||
        page_esc(&page, names[i], strlen(names[i])) != 0 || page_str(&page, "\">") != 0 ||
        page_esc(&page, names[i], strlen(names[i])) != 0 || page_str(&page, "</a></li>") != 0) {
      goto fail;
    }
  }
  if (count > 0 && page_str(&page, "</ul>") != 0) {
    goto fail;
  }
  gdefcore_view_names_free(c->catalog, names, count);
  return take_page(&page, out);
fail:
  gdefcore_view_names_free(c->catalog, names, count);
  page_free(&page);
  return -1;
}

/* ---- writes ------------------------------------------------------------------- */

static int apply_changeset(struct Ctx * c, GDEFCORE_Changeset * cs, char * why, size_t why_cap) {
  int risk = 0;
  GDEFCORE_Result r = gdefcore_changeset_rate(cs, &risk);
  if (r == GDEFCORE_OK) {
    if (risk == GDEFCORE_RISK_HIGH) {
      char confirm[256];
      size_t n = touched_confirm(cs, confirm, sizeof confirm);
      r = n > 0 ? gdefcore_changeset_apply(cs, confirm, n) : GDEFCORE_ERR_INVALID;
    } else {
      r = gdefcore_changeset_apply(cs, NULL, 0);
    }
  }
  if (r != GDEFCORE_OK) {
    size_t n = 0;
    const char * text = gdefcore_changeset_refusal(cs, &n);
    if (text != NULL && n > 0) {
      snprintf(why, why_cap, "%.*s", (int)n, text);
    }
  }
  (void)c;
  return r == GDEFCORE_OK ? 0 : (r == GDEFCORE_ERR_OOM ? -1 : 1);
}

static int save_post(struct Ctx * c, const char * entity, const char * route_name,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct Draft d;
  struct Built b;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  char principal[GDEFCORE_PUBLIC_ID_LEN + 1];
  char own[GDEFCORE_PUBLIC_ID_LEN + 1];
  char why[256];
  uint64_t base = 0;
  GDEFCORE_Changeset * cs = NULL;
  const char * who;
  size_t who_n = 0;
  int rc;
  GDEFCORE_Result r;
  why[0] = '\0';
  if (!principal_id(c, principal)) {
    return unknown_admin("POST", out);
  }
  draft_from_body(&d, body, body_len);
  if (entity[0] != '\0') {
    /* An edit keeps the name the view has; the form shows it and cannot change it. */
    int ignore = 0;
    (void)copy_in(d.name, sizeof d.name, route_name, strlen(route_name), &ignore);
  }
  if (!token_for(c, body, body_len, "view-save", target, &base) ||
      (entity[0] != '\0' ? strncmp(target, entity, GDEFCORE_PUBLIC_ID_LEN) != 0
                         : strncmp(target, principal, GDEFCORE_PUBLIC_ID_LEN) != 0)) {
    return form_page(c, &d, entity, NULL, "Not saved: the form was out of date. Nothing was changed.",
        0, NULL, 0, out);
  }
  if (entity[0] != '\0' && !base_matches(c->catalog, entity, base)) {
    return form_page(c, &d, entity, NULL,
        "Not saved: this was changed while you were editing. Nothing was changed.", 0, NULL, 0,
        out);
  }
  if (entity[0] == '\0' && view_entity(c, d.name, own)) {
    return form_page(c, &d, entity, "a view of that name exists already; open it to change it",
        NULL, 0, NULL, 0, out);
  }
  if (d.too_long) {
    return form_page(c, &d, entity, "a value is longer than this form takes", NULL, 0, NULL, 0, out);
  }
  build_def(&d, &b);
  who = gdefcore_request_principal(c->request, &who_n);
  if (gdefcore_changeset_prepare(c->catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, c->request);
  r = gdefcore_view_stage(cs, &b.def);
  if (r == GDEFCORE_OK) {
    rc = apply_changeset(c, cs, why, sizeof why);
    r = rc == 0 ? GDEFCORE_OK : (rc < 0 ? GDEFCORE_ERR_OOM : GDEFCORE_ERR_INVALID);
  } else if (r == GDEFCORE_ERR_INVALID) {
    size_t n = 0;
    const char * text = gdefcore_changeset_refusal(cs, &n);
    if (text != NULL && n > 0) {
      snprintf(why, sizeof why, "%.*s", (int)n, text);
    }
  }
  gdefcore_changeset_destroy(cs);
  if (r == GDEFCORE_ERR_OOM) {
    return -1;
  }
  if (r != GDEFCORE_OK) {
    return form_page(c, &d, entity, why[0] != '\0' ? why : "the view could not be saved", NULL, 0,
        NULL, 0, out);
  }
  say(c->catalog, c->request, GDEFCORE_MESSAGE_STATUS, "Saved.");
  {
    char loc[VIEWS_NAME_MAX + sizeof VIEWS_BASE + 4];
    snprintf(loc, sizeof loc, VIEWS_BASE "/%s", d.name);
    return redirect_to(out, loc);
  }
}

static int delete_post(struct Ctx * c, const char * entity, const char * name, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct Draft d;
  GDEFCORE_View_Def def;
  void * owner = NULL;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  char typed[VIEWS_SHORT];
  char why[256];
  uint64_t base = 0;
  GDEFCORE_Changeset * cs = NULL;
  const char * who;
  size_t who_n = 0;
  int ignore = 0;
  int rc;
  why[0] = '\0';
  if (gdefcore_view_get(c->catalog, name, strlen(name), &def, &owner) != GDEFCORE_OK) {
    return unknown_admin("POST", out);
  }
  draft_from_def(&d, &def);
  gdefcore_view_def_free(c->catalog, owner);
  if (!token_for(c, body, body_len, "view-delete", target, &base) ||
      strncmp(target, entity, GDEFCORE_PUBLIC_ID_LEN) != 0 ||
      !base_matches(c->catalog, entity, base)) {
    return form_page(c, &d, entity, NULL,
        "Not deleted: the form was out of date. Nothing was changed.", 0, NULL, 0, out);
  }
  body_text(body, body_len, "confirm", typed, sizeof typed, &ignore);
  if (strcmp(typed, name) != 0) {
    return form_page(c, &d, entity, NULL,
        "Not deleted: the name typed does not match. Nothing was changed.", 0, NULL, 0, out);
  }
  who = gdefcore_request_principal(c->request, &who_n);
  if (gdefcore_changeset_prepare(c->catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, c->request);
  rc = gdefcore_entity_clear(cs, entity, GDEFCORE_PUBLIC_ID_LEN) == GDEFCORE_OK
           ? apply_changeset(c, cs, why, sizeof why)
           : 1;
  gdefcore_changeset_destroy(cs);
  if (rc < 0) {
    return -1;
  }
  say(c->catalog, c->request, rc == 0 ? GDEFCORE_MESSAGE_STATUS : GDEFCORE_MESSAGE_ERROR,
      rc == 0 ? "Deleted." : "Not deleted.");
  return redirect_to(out, VIEWS_BASE);
}

static int preview_post(struct Ctx * c, const char * entity, const char * name, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct Draft d;
  GDEFCORE_View_Def def;
  void * owner = NULL;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  if (gdefcore_view_get(c->catalog, name, strlen(name), &def, &owner) != GDEFCORE_OK) {
    return unknown_admin("POST", out);
  }
  draft_from_def(&d, &def);
  gdefcore_view_def_free(c->catalog, owner);
  if (!token_for(c, body, body_len, "view-preview", target, &base) ||
      strncmp(target, entity, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    return form_page(c, &d, entity, NULL, "Not shown: the form was out of date.", 0, NULL, 0, out);
  }
  return form_page(c, &d, entity, NULL, NULL, 1, body, body_len, out);
}

/* ---- dispatch ----------------------------------------------------------------- */

int gdefweb_views_path(const char * path) {
  return strcmp(path, VIEWS_BASE) == 0 || strncmp(path, VIEWS_BASE "/", sizeof VIEWS_BASE) == 0;
}

int gdefweb_views_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out) {
  struct Ctx c;
  int is_get = strcmp(method, "GET") == 0;
  const char * seg;
  char name[VIEWS_NAME_MAX + 1];
  char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
  c.alloc = alloc;
  c.catalog = catalog;
  c.request = request;
  c.keyring = keyring;
  c.root = root;
  c.root_len = root_len;
  c.session = session;
  c.session_len = session_len;
  /* The route's own check is the permission, and the answer to one a person
   * lacks is the answer to a page that is not there. */
  if (!gdefweb_nav_follows(routes, request, VIEWS_BASE, "core.admin-views") ||
      !gdefweb_views_path(path)) {
    return unknown_admin(method, out);
  }
  if (strcmp(path, VIEWS_BASE) == 0) {
    return is_get ? list_page(&c, out) : unknown_admin(method, out);
  }
  seg = path + sizeof VIEWS_BASE;
  if (!name_segment_ok(seg, strlen(seg))) {
    return unknown_admin(method, out);
  }
  memcpy(name, seg, strlen(seg) + 1);
  if (strcmp(name, "new") == 0) {
    struct Draft d;
    memset(&d, 0, sizeof d);
    d.format = GDEFCORE_VIEW_FORMAT_TABLE;
    snprintf(d.limit, sizeof d.limit, "%d", VIEWS_LIMIT_DEFAULT);
    if (is_get) {
      return form_page(&c, &d, "", NULL, NULL, 0, NULL, 0, out);
    }
    if (strcmp(method, "POST") != 0) {
      return unknown_admin(method, out);
    }
    return save_post(&c, "", "", body, body_len, out);
  }
  if (!view_entity(&c, name, entity)) {
    return unknown_admin(method, out);
  }
  if (is_get) {
    struct Draft d;
    GDEFCORE_View_Def def;
    void * owner = NULL;
    if (gdefcore_view_get(catalog, name, strlen(name), &def, &owner) != GDEFCORE_OK) {
      return unknown_admin(method, out);
    }
    draft_from_def(&d, &def);
    gdefcore_view_def_free(catalog, owner);
    return form_page(&c, &d, entity, NULL, NULL, 0, NULL, 0, out);
  }
  {
    char action[16];
    int ignore = 0;
    body_text(body, body_len, "do", action, sizeof action, &ignore);
    if (strcmp(action, "save") == 0) {
      return save_post(&c, entity, name, body, body_len, out);
    }
    if (strcmp(action, "delete") == 0) {
      return delete_post(&c, entity, name, body, body_len, out);
    }
    if (strcmp(action, "preview") == 0) {
      return preview_post(&c, entity, name, body, body_len, out);
    }
  }
  return unknown_admin(method, out);
}

/* ---- the public page --------------------------------------------------------- */

int gdefweb_views_public_path(const char * path) {
  return path != NULL && strncmp(path, "/v/", 3) == 0 && path[3] != '\0';
}

/* The value of @p name in a query string, percent-decoded, or "" when absent. */
static void query_value(const char * query, const char * name, char * out, size_t cap) {
  size_t nl = strlen(name);
  const char * p = query;
  out[0] = '\0';
  while (p != NULL && *p != '\0') {
    const char * amp = strchr(p, '&');
    size_t seg = amp == NULL ? strlen(p) : (size_t)(amp - p);
    if (seg > nl && memcmp(p, name, nl) == 0 && p[nl] == '=') {
      size_t n = 0;
      if (pct_decode(p + nl + 1, seg - nl - 1, out, cap, &n) != 0) {
        out[0] = '\0';
        return;
      }
      out[n < cap ? n : cap - 1] = '\0';
      return;
    }
    p = amp == NULL ? NULL : amp + 1;
  }
}

static int html_head(struct Page * page, const char * title) {
  return page_str(page, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                        "<title>") != 0 ||
         page_esc(page, title, strlen(title)) != 0 || page_str(page, "</title></head><body>") != 0;
}

int gdefweb_views_public(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, const char * path, const char * query,
    struct GDEFWEB_Admin_Reply * out) {
  const char * name = path + 3;
  GDEFCORE_View_Def def;
  void * owner = NULL;
  GDEFCORE_View_Param params[GDEFCORE_VIEW_MAX_ARGS];
  char values[GDEFCORE_VIEW_MAX_ARGS][VIEWS_SHORT];
  char pagetext[16];
  GDEFCORE_View_Result * result = NULL;
  struct Page page;
  size_t n_params = 0;
  size_t i;
  size_t row;
  size_t col;
  unsigned long page_no = 1;
  uint64_t limit;
  uint64_t total;
  GDEFCORE_Result rc;
  int format;
  memset(out, 0, sizeof *out);
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (query == NULL) {
    query = "";
  }
  if (!name_segment_ok(name, strlen(name)) ||
      gdefcore_view_get(catalog, name, strlen(name), &def, &owner) != GDEFCORE_OK) {
    out->status = 404;
    out->reason = "Not Found";
    out->type = "text/plain; charset=utf-8";
    return 0;
  }
  limit = def.limit;
  format = def.format;
  for (i = 0; i < def.arg_count && n_params < GDEFCORE_VIEW_MAX_ARGS; i++) {
    const GDEFCORE_View_Arg * a = &def.args[i];
    char key[VIEWS_SHORT];
    if (a->source != GDEFCORE_VIEW_SOURCE_URL && a->source != GDEFCORE_VIEW_SOURCE_EXPOSED) {
      continue;
    }
    if (a->name_len >= sizeof key) {
      continue;
    }
    memcpy(key, a->name, a->name_len);
    key[a->name_len] = '\0';
    query_value(query, key, values[n_params], sizeof values[n_params]);
    if (values[n_params][0] == '\0') {
      continue;
    }
    memset(&params[n_params], 0, sizeof params[n_params]);
    params[n_params].size = sizeof params[n_params];
    params[n_params].name = a->name;
    params[n_params].name_len = a->name_len;
    params[n_params].value = values[n_params];
    params[n_params].value_len = strlen(values[n_params]);
    n_params += 1;
  }
  query_value(query, "page", pagetext, sizeof pagetext);
  if (pagetext[0] != '\0') {
    char * end = NULL;
    unsigned long v = strtoul(pagetext, &end, 10);
    if (*end == '\0' && v >= 1 && v <= 1000000UL) {
      page_no = v;
    }
  }
  rc = gdefcore_view_run(catalog, request, name, strlen(name), params, n_params,
      (uint64_t)(page_no - 1) * limit, &result);
  if (rc != GDEFCORE_OK) {
    gdefcore_view_def_free(catalog, owner);
    if (rc == GDEFCORE_ERR_OOM) {
      return -1;
    }
    out->status = 404;
    out->reason = "Not Found";
    out->type = "text/plain; charset=utf-8";
    return 0;
  }
  if (format == GDEFCORE_VIEW_FORMAT_CSV || format == GDEFCORE_VIEW_FORMAT_JSON) {
    char * text = NULL;
    size_t text_n = 0;
    char * copy;
    rc = gdefcore_view_result_text(catalog, result, &text, &text_n);
    gdefcore_view_result_free(result);
    gdefcore_view_def_free(catalog, owner);
    if (rc != GDEFCORE_OK) {
      return -1;
    }
    copy = (char *)gcu_allocator_malloc(alloc, text_n + 1);
    if (copy == NULL) {
      gdefcore_text_free(catalog, text);
      return -1;
    }
    memcpy(copy, text, text_n);
    copy[text_n] = '\0';
    gdefcore_text_free(catalog, text);
    out->status = 200;
    out->reason = "OK";
    out->type = format == GDEFCORE_VIEW_FORMAT_CSV ? "text/csv; charset=utf-8"
                                                  : "application/json; charset=utf-8";
    out->body = copy;
    out->len = text_n;
    return 0;
  }
  total = gdefcore_view_result_total(result);
  if (html_head(&page, name) != 0 || page_str(&page, "<h1>") != 0 ||
      page_esc(&page, name, strlen(name)) != 0 || page_str(&page, "</h1>") != 0) {
    goto fail;
  }
  /* The values a visitor may choose, as a form that is a plain GET. */
  {
    int opened = 0;
    for (i = 0; i < def.arg_count; i++) {
      const GDEFCORE_View_Arg * a = &def.args[i];
      char key[VIEWS_SHORT];
      char cur[VIEWS_SHORT];
      if (a->source != GDEFCORE_VIEW_SOURCE_EXPOSED || a->name_len >= sizeof key) {
        continue;
      }
      memcpy(key, a->name, a->name_len);
      key[a->name_len] = '\0';
      query_value(query, key, cur, sizeof cur);
      if (!opened && page_str(&page, "<form method=\"get\">") != 0) {
        goto fail;
      }
      opened = 1;
      if (page_str(&page, "<label>") != 0 || page_esc(&page, key, strlen(key)) != 0 ||
          page_str(&page, " <input type=\"text\"") != 0 || attr(&page, "name", key) != 0 ||
          attr(&page, "value", cur) != 0 || page_str(&page, "></label> ") != 0) {
        goto fail;
      }
    }
    if (opened && page_str(&page, "<button type=\"submit\">Show</button></form>") != 0) {
      goto fail;
    }
  }
  if (format == GDEFCORE_VIEW_FORMAT_LIST) {
    if (page_str(&page, "<ul>") != 0) {
      goto fail;
    }
    for (row = 0; row < gdefcore_view_result_rows(result); row++) {
      if (page_str(&page, "<li>") != 0) {
        goto fail;
      }
      for (col = 0; col < gdefcore_view_result_columns(result); col++) {
        size_t n = 0;
        const char * v = gdefcore_view_result_cell(result, row, col, &n);
        if ((col > 0 && page_str(&page, " &middot; ") != 0) || page_esc(&page, v, n) != 0) {
          goto fail;
        }
      }
      if (page_str(&page, "</li>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</ul>") != 0) {
      goto fail;
    }
  } else {
    if (page_str(&page, "<table><thead><tr>") != 0) {
      goto fail;
    }
    for (col = 0; col < gdefcore_view_result_columns(result); col++) {
      size_t n = 0;
      const char * l = gdefcore_view_result_label(result, col, &n);
      if (page_str(&page, "<th>") != 0 || page_esc(&page, l, n) != 0 ||
          page_str(&page, "</th>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</tr></thead><tbody>") != 0) {
      goto fail;
    }
    for (row = 0; row < gdefcore_view_result_rows(result); row++) {
      if (page_str(&page, "<tr>") != 0) {
        goto fail;
      }
      for (col = 0; col < gdefcore_view_result_columns(result); col++) {
        size_t n = 0;
        const char * v = gdefcore_view_result_cell(result, row, col, &n);
        if (page_str(&page, "<td>") != 0 || page_esc(&page, v, n) != 0 ||
            page_str(&page, "</td>") != 0) {
          goto fail;
        }
      }
      if (page_str(&page, "</tr>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</tbody></table>") != 0) {
      goto fail;
    }
  }
  /* Paging: plain links that keep what the visitor chose. */
  if (total > limit) {
    char link[512];
    char kept[384];
    size_t kn = 0;
    kept[0] = '\0';
    for (i = 0; i < n_params; i++) {
      /* the value is encoded by the page's own escaping of the whole href */
      kn += (size_t)snprintf(kept + kn, sizeof kept - kn, "%s%s=%s", kn > 0 ? "&" : "",
          params[i].name, params[i].value);
      if (kn >= sizeof kept) {
        kn = sizeof kept - 1;
        break;
      }
    }
    if (page_no > 1) {
      snprintf(link, sizeof link, "/v/%s?%s%spage=%lu", name, kept, kn > 0 ? "&" : "", page_no - 1);
      if (page_str(&page, "<p><a href=\"") != 0 || page_esc(&page, link, strlen(link)) != 0 ||
          page_str(&page, "\">Previous</a>") != 0) {
        goto fail;
      }
    } else if (page_str(&page, "<p>") != 0) {
      goto fail;
    }
    if (page_no * limit < total) {
      snprintf(link, sizeof link, "/v/%s?%s%spage=%lu", name, kept, kn > 0 ? "&" : "", page_no + 1);
      if (page_str(&page, " <a href=\"") != 0 || page_esc(&page, link, strlen(link)) != 0 ||
          page_str(&page, "\">Next</a>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</p>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</body></html>") != 0) {
    goto fail;
  }
  gdefcore_view_result_free(result);
  gdefcore_view_def_free(catalog, owner);
  out->status = 200;
  out->reason = "OK";
  out->type = "text/html; charset=utf-8";
  out->body = page.p;
  out->len = page.n;
  return 0;
fail:
  gdefcore_view_result_free(result);
  gdefcore_view_def_free(catalog, owner);
  page_free(&page);
  return -1;
}
