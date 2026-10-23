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
 * Server-rendered pages for the three offers, plus More. The script only
 * posts a signed pane list. Place, remove, posts and undo are forms.
 */

#include "admin.h"

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/template.h>
#include <ghoti.io/security/random.h>
#include <ghoti.io/security/sha256.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char kJs[] =
    "(function () {\n"
    "  var root = document.getElementById(\"panes\");\n"
    "  if (!root) return;\n"
    "  var dragged = null;\n"
    "  root.addEventListener(\"dragstart\", function (event) {\n"
    "    var item = event.target.closest(\"[data-pane]\");\n"
    "    if (!item) return;\n"
    "    dragged = item;\n"
    "    event.dataTransfer.setData(\"text/plain\", \"pane\");\n"
    "  });\n"
    "  root.addEventListener(\"dragover\", function (event) {\n"
    "    if (event.target.closest(\"[data-region]\")) event.preventDefault();\n"
    "  });\n"
    "  root.addEventListener(\"drop\", function (event) {\n"
    "    var region = event.target.closest(\"[data-region]\");\n"
    "    var form = document.getElementById(\"reorder\");\n"
    "    var field, lines = [], regions, i, j, items;\n"
    "    event.preventDefault();\n"
    "    if (!dragged || !region || !form) return;\n"
    "    region.appendChild(dragged);\n"
    "    dragged = null;\n"
    "    regions = root.querySelectorAll(\"[data-region]\");\n"
    "    for (i = 0; i < regions.length; i += 1) {\n"
    "      items = regions[i].querySelectorAll(\"[data-pane]\");\n"
    "      for (j = 0; j < items.length; j += 1) {\n"
    "        lines.push(regions[i].getAttribute(\"data-region\") + \" view recent\");\n"
    "      }\n"
    "    }\n"
    "    field = form.querySelector(\"[name=panes]\");\n"
    "    if (!field) return;\n"
    "    field.value = lines.join(\"\\n\");\n"
    "    form.submit();\n"
    "  });\n"
    "}());\n";

static const char kCss[] =
    "html {\n"
    "  color-scheme: light dark;\n"
    "  background-color: Canvas;\n"
    "  color: CanvasText;\n"
    "}\n"
    "[data-region] {\n"
    "  min-height: 3rem;\n"
    "  margin: 0.5rem 0;\n"
    "  padding: 0.25rem 0.5rem;\n"
    "  border: 1px dashed GrayText;\n"
    "}\n"
    "[data-pane] {\n"
    "  cursor: grab;\n"
    "}\n"
    "textarea {\n"
    "  box-sizing: border-box;\n"
    "  width: 100%;\n"
    "  min-height: 24em;\n"
    "  font-family: monospace;\n"
    "}\n"
    "pre {\n"
    "  white-space: pre-wrap;\n"
    "  overflow-wrap: anywhere;\n"
    "}\n";

static char kHash[80];
static char kPolicy[320];
static int kAssets;

static void wipe(void * p, size_t n) {
  volatile unsigned char * b = (volatile unsigned char *)p;
  size_t i;
  for (i = 0; i < n; i++) {
    b[i] = 0;
  }
}

static void ensure_assets(void) {
  unsigned char dig[GSEC_SHA256_DIGEST_LEN];
  static const char kTab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char b64[48];
  size_t i = 0;
  size_t o = 0;
  if (kAssets) {
    return;
  }
  if (gsec_sha256(kJs, sizeof kJs - 1, dig) != GSEC_OK) {
    snprintf(kPolicy, sizeof kPolicy,
        "default-src 'none'; script-src 'none'; style-src 'self'; "
        "frame-ancestors 'none'");
    kHash[0] = '\0';
    kAssets = 1;
    return;
  }
  while (i + 3 <= sizeof dig) {
    unsigned int v = ((unsigned int)dig[i] << 16) | ((unsigned int)dig[i + 1] << 8) |
                     (unsigned int)dig[i + 2];
    b64[o++] = kTab[(v >> 18) & 63u];
    b64[o++] = kTab[(v >> 12) & 63u];
    b64[o++] = kTab[(v >> 6) & 63u];
    b64[o++] = kTab[v & 63u];
    i += 3;
  }
  if (sizeof dig - i == 1) {
    unsigned int v = (unsigned int)dig[i] << 16;
    b64[o++] = kTab[(v >> 18) & 63u];
    b64[o++] = kTab[(v >> 12) & 63u];
    b64[o++] = '=';
    b64[o++] = '=';
  } else if (sizeof dig - i == 2) {
    unsigned int v = ((unsigned int)dig[i] << 16) | ((unsigned int)dig[i + 1] << 8);
    b64[o++] = kTab[(v >> 18) & 63u];
    b64[o++] = kTab[(v >> 12) & 63u];
    b64[o++] = kTab[(v >> 6) & 63u];
    b64[o++] = '=';
  }
  b64[o] = '\0';
  snprintf(kHash, sizeof kHash, "sha256-%s", b64);
  snprintf(kPolicy, sizeof kPolicy,
      "default-src 'none'; script-src '%s'; style-src 'self'; "
      "frame-ancestors 'none'",
      kHash);
  kAssets = 1;
}

const char * gdefweb_admin_csp(void) {
  ensure_assets();
  return kPolicy;
}

const char * gdefweb_admin_login_page(int failed) {
  static const char kHead[] =
      "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<title>Sign in</title><link rel=\"stylesheet\" href=\"/defiant.css\">"
      "</head><body><h1>Sign in</h1>";
  static const char kForm[] =
      "<form method=\"post\" action=\"/login\">"
      "<p><label>Username <input name=\"username\" autocomplete=\"username\"></label></p>"
      "<p><label>Password <input name=\"password\" type=\"password\" "
      "autocomplete=\"current-password\"></label></p>"
      "<p><button type=\"submit\">Sign in</button></p></form></body></html>";
  static char ok[sizeof kHead + sizeof kForm];
  static char bad[sizeof kHead + sizeof kForm + 64];
  if (ok[0] == '\0') {
    snprintf(ok, sizeof ok, "%s%s", kHead, kForm);
    snprintf(bad, sizeof bad, "%s<p>That username and password did not work.</p>%s",
        kHead, kForm);
  }
  return failed ? bad : ok;
}

int gdefweb_admin_static(const char * path, const char ** body, size_t * len,
    const char ** type) {
  if (path == NULL || body == NULL || len == NULL || type == NULL) {
    return 0;
  }
  if (strcmp(path, "/defiant.js") == 0) {
    *body = kJs;
    *len = sizeof kJs - 1;
    *type = "text/javascript; charset=utf-8";
    return 1;
  }
  if (strcmp(path, "/defiant.css") == 0) {
    *body = kCss;
    *len = sizeof kCss - 1;
    *type = "text/css; charset=utf-8";
    return 1;
  }
  return 0;
}

int gdefweb_admin_path(const char * path) {
  size_t n;
  if (path == NULL) {
    return 0;
  }
  n = strlen(path);
  if (n == 6 && memcmp(path, "/admin", 6) == 0) {
    return 1;
  }
  if (n > 7 && memcmp(path, "/admin/", 7) == 0) {
    return 1;
  }
  return 0;
}

static int b64_val(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '-' || c == '+') {
    return 62;
  }
  if (c == '_' || c == '/') {
    return 63;
  }
  return -1;
}

static int b64_decode(const char * in, unsigned char * out, size_t cap,
    size_t * out_n) {
  size_t n;
  size_t i = 0;
  size_t o = 0;
  if (in == NULL) {
    return -1;
  }
  n = strlen(in);
  while (i + 1 < n) {
    int a = b64_val(in[i]);
    int b = b64_val(in[i + 1]);
    int c = i + 2 < n ? b64_val(in[i + 2]) : -2;
    int d = i + 3 < n ? b64_val(in[i + 3]) : -2;
    unsigned int v;
    if (a < 0 || b < 0) {
      return -1;
    }
    v = ((unsigned int)a << 18) | ((unsigned int)b << 12);
    if (o >= cap) {
      return -1;
    }
    out[o++] = (unsigned char)((v >> 16) & 0xffu);
    if (c >= 0) {
      v |= (unsigned int)c << 6;
      if (o >= cap) {
        return -1;
      }
      out[o++] = (unsigned char)((v >> 8) & 0xffu);
    }
    if (d >= 0) {
      v |= (unsigned int)d;
      if (o >= cap) {
        return -1;
      }
      out[o++] = (unsigned char)(v & 0xffu);
    }
    i += 4;
  }
  *out_n = o;
  return 0;
}

static char * b64_encode(const GDEFWEB_Allocator * alloc, const unsigned char * in,
    size_t n, size_t * out_n) {
  static const char kTab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t cap = (n / 3 + 1) * 4 + 1;
  char * out = (char *)gcu_allocator_malloc(alloc, cap);
  size_t i = 0;
  size_t o = 0;
  if (out == NULL) {
    return NULL;
  }
  while (i < n) {
    unsigned int v = (unsigned int)in[i++] << 16;
    int remain = 1;
    if (i < n) {
      v |= (unsigned int)in[i++] << 8;
      remain = 2;
    }
    if (i < n) {
      v |= (unsigned int)in[i++];
      remain = 3;
    }
    out[o++] = kTab[(v >> 18) & 63u];
    out[o++] = kTab[(v >> 12) & 63u];
    if (remain >= 2) {
      out[o++] = kTab[(v >> 6) & 63u];
    }
    if (remain == 3) {
      out[o++] = kTab[v & 63u];
    }
  }
  out[o] = '\0';
  *out_n = o;
  return out;
}

static int cookie_named(const char * cookie, const char * name, char * out,
    size_t cap) {
  size_t nlen;
  const char * p;
  if (cookie == NULL || name == NULL) {
    return -1;
  }
  nlen = strlen(name);
  p = cookie;
  while (*p != '\0') {
    while (*p == ' ' || *p == ';') {
      p += 1;
    }
    if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
      size_t i = 0;
      p += nlen + 1;
      while (p[i] != '\0' && p[i] != ';') {
        i += 1;
      }
      if (i == 0 || i >= cap) {
        return -1;
      }
      memcpy(out, p, i);
      out[i] = '\0';
      return 0;
    }
    while (*p != '\0' && *p != ';') {
      p += 1;
    }
  }
  return -1;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int pct_decode(const char * in, size_t n, char * out, size_t cap,
    size_t * out_n) {
  size_t i = 0;
  size_t o = 0;
  while (i < n) {
    unsigned char c = (unsigned char)in[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      int hi;
      int lo;
      if (i + 2 >= n) {
        return -1;
      }
      hi = hex_nibble(in[i + 1]);
      lo = hex_nibble(in[i + 2]);
      if (hi < 0 || lo < 0) {
        return -1;
      }
      c = (unsigned char)((hi << 4) | lo);
      i += 2;
    }
    if (o + 1 >= cap) {
      return -1;
    }
    out[o++] = (char)c;
    i += 1;
  }
  out[o] = '\0';
  *out_n = o;
  return 0;
}

static int form_pair(const char * body, size_t n, const char * name, char * out,
    size_t cap, size_t * out_n) {
  size_t nlen = strlen(name);
  size_t i = 0;
  while (i < n) {
    size_t start = i;
    size_t eq;
    size_t end;
    while (i < n && body[i] != '&') {
      i += 1;
    }
    end = i;
    eq = start;
    while (eq < end && body[eq] != '=') {
      eq += 1;
    }
    if (eq < end && eq - start == nlen && memcmp(body + start, name, nlen) == 0) {
      return pct_decode(body + eq + 1, end - (eq + 1), out, cap, out_n);
    }
    if (i < n && body[i] == '&') {
      i += 1;
    }
  }
  return -1;
}

int gdefweb_admin_is_admin(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request) {
  const char * id;
  size_t n = 0;
  char ** roles = NULL;
  size_t count = 0;
  size_t i;
  int yes = 0;
  if (catalog == NULL || request == NULL) {
    return 0;
  }
  id = gdefcore_request_principal(request, &n);
  if (id == NULL || n == 0) {
    return 0;
  }
  if (gdefcore_access_principal_roles(catalog, id, n, &roles, &count) !=
      GDEFCORE_OK) {
    return 0;
  }
  for (i = 0; i < count; i++) {
    if (strcmp(roles[i], "admin") == 0) {
      yes = 1;
    }
  }
  gdefcore_access_free(catalog, roles, count);
  return yes;
}

int gdefweb_admin_allowed(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * cookie) {
  char encoded[1200];
  unsigned char raw[768];
  size_t raw_n = 0;
  int ok = 0;
  if (cookie_named(cookie, "__Host-admin", encoded, sizeof encoded) != 0) {
    return 0;
  }
  if (b64_decode(encoded, raw, sizeof raw, &raw_n) != 0) {
    wipe(encoded, sizeof encoded);
    return 0;
  }
  if (gdefcore_account_check(catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n) == GDEFCORE_OK) {
    ok = gdefweb_admin_is_admin(catalog, request);
  }
  wipe(raw, sizeof raw);
  wipe(encoded, sizeof encoded);
  return ok;
}

struct Page {
  const GDEFWEB_Allocator * alloc;
  char * p;
  size_t n;
  size_t cap;
};

static int page_add(struct Page * page, const char * s, size_t n) {
  if (page->n + n + 1 < page->n) {
    return -1;
  }
  if (page->n + n + 1 > page->cap) {
    size_t cap = page->cap == 0 ? 256 : page->cap;
    char * next;
    while (cap < page->n + n + 1) {
      if (cap > (size_t)-1 / 2) {
        return -1;
      }
      cap *= 2;
    }
    next = (char *)gcu_allocator_realloc(page->alloc, page->p, cap);
    if (next == NULL) {
      return -1;
    }
    page->p = next;
    page->cap = cap;
  }
  if (n > 0) {
    memcpy(page->p + page->n, s, n);
  }
  page->n += n;
  page->p[page->n] = '\0';
  return 0;
}

static int page_str(struct Page * page, const char * s) {
  return page_add(page, s, strlen(s));
}

static int page_esc(struct Page * page, const char * s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    char c = s[i];
    const char * rep = NULL;
    if (c == '&') {
      rep = "&amp;";
    } else if (c == '<') {
      rep = "&lt;";
    } else if (c == '>') {
      rep = "&gt;";
    } else if (c == '"') {
      rep = "&quot;";
    }
    if (rep != NULL) {
      if (page_str(page, rep) != 0) {
        return -1;
      }
    } else if (page_add(page, &c, 1) != 0) {
      return -1;
    }
  }
  return 0;
}

static int page_begin(struct Page * page, const char * title) {
  ensure_assets();
  if (page_str(page, "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>") !=
          0 ||
      page_esc(page, title, strlen(title)) != 0 ||
      page_str(page, "</title><link rel=\"stylesheet\" href=\"/defiant.css\">"
                     "<script defer src=\"/defiant.js\" integrity=\"") != 0 ||
      page_str(page, kHash) != 0 ||
      page_str(page, "\"></script></head><body><p><a href=\"/admin\">Home</a></p>") !=
          0) {
    return -1;
  }
  return 0;
}

static int page_end(struct Page * page) {
  return page_str(page, "</body></html>");
}

static void page_free(struct Page * page) {
  gcu_allocator_free(page->alloc, page->p);
  page->p = NULL;
  page->n = 0;
  page->cap = 0;
}

static int take_page(struct Page * page, struct GDEFWEB_Admin_Reply * out) {
  if (page_end(page) != 0) {
    page_free(page);
    return -1;
  }
  out->status = 200;
  out->reason = "OK";
  out->type = "text/html; charset=utf-8";
  out->body = page->p;
  out->len = page->n;
  page->p = NULL;
  page->n = 0;
  page->cap = 0;
  return 0;
}

static int session_raw(const char * cookie, unsigned char * raw, size_t cap,
    size_t * out_n) {
  char encoded[1200];
  int rc;
  if (cookie_named(cookie, "__Host-admin", encoded, sizeof encoded) != 0) {
    return -1;
  }
  rc = b64_decode(encoded, raw, cap, out_n);
  wipe(encoded, sizeof encoded);
  return rc;
}

static int token_meta(const unsigned char * raw, size_t n, char * form_id,
    size_t form_cap, size_t * form_len, char * target, uint64_t * base) {
  size_t at;
  size_t fid;
  size_t i;
  uint64_t v = 0;
  if (n < 18 || raw[0] != 1 || raw[1] != 1 || raw[2] != 1 || raw[3] != 4) {
    return -1;
  }
  if (memcmp(raw + 4, "form", 4) != 0) {
    return -1;
  }
  at = 16;
  fid = ((size_t)raw[at] << 8) | (size_t)raw[at + 1];
  at += 2;
  if (fid == 0 || fid >= form_cap || n < at + fid + GDEFCORE_PUBLIC_ID_LEN + 8) {
    return -1;
  }
  memcpy(form_id, raw + at, fid);
  form_id[fid] = '\0';
  *form_len = fid;
  at += fid;
  memcpy(target, raw + at, GDEFCORE_PUBLIC_ID_LEN);
  target[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  at += GDEFCORE_PUBLIC_ID_LEN;
  for (i = 0; i < 8; i++) {
    v = (v << 8) | raw[at + i];
  }
  *base = v;
  return 0;
}

static int sign_b64(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * form_id,
    const char * target, const GDEFCORE_Value * values, size_t nvalues,
    char ** out) {
  char * token = NULL;
  size_t token_n = 0;
  size_t b64_n = 0;
  GDEFCORE_Result rc = gdefcore_form_sign(catalog, request, keyring, root, root_len,
      session, session_len, form_id, strlen(form_id), target, GDEFCORE_PUBLIC_ID_LEN,
      "en", 2, values, nvalues, NULL, 0, &token, &token_n);
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  *out = b64_encode(alloc, (const unsigned char *)token, token_n, &b64_n);
  gdefcore_text_free(catalog, token);
  return *out == NULL ? -1 : 0;
}

static int one_value(GDEFCORE_Value * value, const char * field, const char * text,
    const char ** slot, size_t * lens) {
  memset(value, 0, sizeof *value);
  value->size = sizeof *value;
  value->field = field;
  value->field_len = strlen(field);
  *slot = text;
  *lens = strlen(text);
  value->texts = slot;
  value->text_lens = lens;
  value->count = 1;
  return 0;
}

/*
 * A field of the revision the entity currently is (its newest), whether
 * or not it is published. The admin pages name entities for the person who edits
 * them, so a post that is still a draft has a title here and a template that was
 * unpublished can still be found by its name: the published view would show
 * neither (SPEC decision 9).
 */
static int field_copy(GDEFCORE_Catalog * catalog, const char * id, const char * field,
    char * out, size_t cap) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_CURRENT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens,
      &count);
  if (rc != GDEFCORE_OK || count == 0 || texts[0] == NULL || lens[0] + 1 > cap) {
    if (rc == GDEFCORE_OK) {
      gdefcore_values_free(catalog, texts, lens, count);
    }
    return -1;
  }
  memcpy(out, texts[0], lens[0]);
  out[lens[0]] = '\0';
  gdefcore_values_free(catalog, texts, lens, count);
  return 0;
}

static int find_front(GDEFCORE_Catalog * catalog, char * id) {
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  int found = 0;
  if (gdefcore_entity_ids(catalog, "core.display", 12, &ids, &count) != GDEFCORE_OK) {
    return -1;
  }
  for (i = 0; i < count; i++) {
    char name[64];
    if (strlen(ids[i]) == GDEFCORE_PUBLIC_ID_LEN &&
        field_copy(catalog, ids[i], "name", name, sizeof name) == 0 &&
        strcmp(name, "front") == 0) {
      memcpy(id, ids[i], GDEFCORE_PUBLIC_ID_LEN + 1);
      found = 1;
      break;
    }
  }
  gdefcore_ids_free(catalog, ids, count);
  return found ? 0 : -1;
}

struct Held {
  char region[32];
  char kind[32];
  char target[32];
  char line[96];
  int recent;
};

static int load_panes(GDEFCORE_Catalog * catalog, const char * id, struct Held * held,
    size_t cap, size_t * out_n) {
  size_t i;
  (void)id;
  *out_n = 0;
  for (i = 0; i < cap; i++) {
    char * region = NULL;
    char * kind = NULL;
    char * target = NULL;
    size_t region_n = 0;
    size_t kind_n = 0;
    size_t target_n = 0;
    GDEFCORE_Result rc = gdefcore_render_pane_at(catalog, "front", 5, i, &region,
        &region_n, &kind, &kind_n, &target, &target_n);
    if (rc != GDEFCORE_OK) {
      break;
    }
    if (region_n >= sizeof held[i].region || kind_n >= sizeof held[i].kind ||
        target_n >= sizeof held[i].target ||
        region_n + kind_n + target_n + 2 >= sizeof held[i].line) {
      gdefcore_text_free(catalog, region);
      gdefcore_text_free(catalog, kind);
      gdefcore_text_free(catalog, target);
      return -1;
    }
    memcpy(held[i].region, region, region_n);
    held[i].region[region_n] = '\0';
    memcpy(held[i].kind, kind, kind_n);
    held[i].kind[kind_n] = '\0';
    memcpy(held[i].target, target, target_n);
    held[i].target[target_n] = '\0';
    memcpy(held[i].line, region, region_n);
    held[i].line[region_n] = ' ';
    memcpy(held[i].line + region_n + 1, kind, kind_n);
    held[i].line[region_n + 1 + kind_n] = ' ';
    memcpy(held[i].line + region_n + kind_n + 2, target, target_n);
    held[i].line[region_n + kind_n + target_n + 2] = '\0';
    held[i].recent = strcmp(held[i].kind, "view") == 0 &&
                     strcmp(held[i].target, "recent") == 0;
    gdefcore_text_free(catalog, region);
    gdefcore_text_free(catalog, kind);
    gdefcore_text_free(catalog, target);
    *out_n += 1;
  }
  return 0;
}

static int save_panes(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * id, const char * const * lines, size_t n) {
  GDEFCORE_Changeset * cs = NULL;
  GDEFCORE_Value value;
  const char * who;
  size_t who_n = 0;
  const char * texts[32];
  size_t lens[32];
  size_t i;
  GDEFCORE_Result rc;
  if (n > 32) {
    return -1;
  }
  memset(&value, 0, sizeof value);
  value.size = sizeof value;
  value.field = "panes";
  value.field_len = 5;
  for (i = 0; i < n; i++) {
    texts[i] = lines[i];
    lens[i] = strlen(lines[i]);
  }
  value.texts = n == 0 ? NULL : texts;
  value.text_lens = n == 0 ? NULL : lens;
  value.count = n;
  who = gdefcore_request_principal(request, &who_n);
  rc = gdefcore_changeset_prepare(catalog, who, who_n, &cs);
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  rc = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, &value, 1);
  if (rc == GDEFCORE_OK) {
    rc = gdefcore_changeset_apply(cs, "core.display", 12);
  }
  gdefcore_changeset_destroy(cs);
  return rc == GDEFCORE_OK ? 0 : -1;
}

/* A signed form's base is the entity's version when it was signed
 * (gdefcore_form_sign(), since story S6b), which every write moves, a draft
 * included: the form is current only while the entity has not moved. */
static int base_matches(GDEFCORE_Catalog * catalog, const char * id, uint64_t base) {
  uint64_t current = 0;
  if (gdefcore_entity_version(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &current) !=
      GDEFCORE_OK) {
    return 0;
  }
  return current == base;
}

static int redirect(struct GDEFWEB_Admin_Reply * out, const char * location) {
  out->status = 303;
  out->reason = "See Other";
  out->type = "text/plain; charset=utf-8";
  out->location = location;
  out->body = NULL;
  out->len = 0;
  return 0;
}

static int home_page(const GDEFWEB_Allocator * alloc, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_str(&page, "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<title>Home</title><link rel=\"stylesheet\" href=\"/defiant.css\">"
                      "<script defer src=\"/defiant.js\" integrity=\"") != 0) {
    page_free(&page);
    return -1;
  }
  ensure_assets();
  if (page_str(&page, kHash) != 0 ||
      page_str(&page, "\"></script></head><body>"
                      "<p><a href=\"/admin/post\">Write a post</a></p>"
                      "<p><a href=\"/admin/front\">Change the front page</a></p>"
                      "<p><a href=\"/admin/undo\">Undo</a></p>"
                      "<p><a href=\"/admin/templates\">Edit templates</a></p>"
                      "<p><a href=\"/admin/more\">More</a></p>"
                      "</body></html>") != 0) {
    page_free(&page);
    return -1;
  }
  out->status = 200;
  out->reason = "OK";
  out->type = "text/html; charset=utf-8";
  out->body = page.p;
  out->len = page.n;
  return 0;
}

static int more_page(const GDEFWEB_Allocator * alloc, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_begin(&page, "More") != 0 ||
      page_str(&page, "<h2>Server</h2><p>This server is running.</p>") != 0 ||
      page_str(&page, "<h2>Access</h2><p><a href=\"/admin/access\">What is "
                      "granted</a></p>") != 0) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
}

static int unknown_admin(const char * method, struct GDEFWEB_Admin_Reply * out);

/*
 * /admin/access: what is granted, and by whom. Read only. The rows come from
 * gdefcore_access_matrix(), which answers an administrator alone, and every
 * text in them (a role's name, a module's name, a permission's label, a
 * condition's words) was written by someone, so every one is escaped. Nothing
 * is printed raw.
 */
static int access_cell(struct Page * page, const char * open_tag,
    const char * text, size_t n) {
  return page_str(page, open_tag) != 0 || page_esc(page, text, n) != 0 ||
         page_str(page, "</td>") != 0;
}

/* Whether the grant holds anything now. A grant whose condition names a field
 * an enabled module decides per entity holds nothing, and says which modules
 * (access-conditions story A7): the names are a module author's, so escaped. */
static int access_status(struct Page * page,
    const GDEFCORE_Access_Matrix_Row * r) {
  if (!r->suspended) {
    return page_str(page, "<td class=\"status-active\">active</td>");
  }
  return page_str(page, "<td class=\"status-suspended\">suspended by ") != 0 ||
         page_esc(page, r->suspended_by, r->suspended_by_len) != 0 ||
         page_str(page, "</td>");
}

static int access_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  GDEFCORE_Access_Matrix_Row * rows = NULL;
  size_t n = 0;
  size_t i;
  GDEFCORE_Result rc = gdefcore_access_matrix(catalog, request, &rows, &n);
  if (rc == GDEFCORE_ERR_INVALID) {
    return unknown_admin("GET", out);
  }
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_begin(&page, "Access") != 0 ||
      page_str(&page, "<h1>Access</h1><p>What each role and each enabled module "
                      "grants. Nothing denies: a person holds whatever any of "
                      "these gives them.</p>") != 0) {
    goto fail;
  }
  if (n == 0) {
    if (page_str(&page, "<p>Nothing is granted.</p>") != 0) {
      goto fail;
    }
    gdefcore_access_matrix_free(catalog, rows, n);
    return take_page(&page, out);
  }
  if (page_str(&page, "<table><thead><tr><th>Granted by</th><th>Permission</th>"
                      "<th>Action</th><th>On</th><th>When</th><th>Risk</th>"
                      "<th>Status</th></tr></thead><tbody>") != 0) {
    goto fail;
  }
  for (i = 0; i < n; i++) {
    const GDEFCORE_Access_Matrix_Row * r = &rows[i];
    const char * risk = gdefcore_access_risk_name(r->risk);
    if (page_str(&page, "<tr>") != 0 ||
        access_cell(&page, "<td>", r->source, r->source_len) != 0 ||
        access_cell(&page, "<td>", r->permission, r->permission_len) != 0 ||
        access_cell(&page, "<td>", r->action, r->action_len) != 0 ||
        page_str(&page, "<td>") != 0 ||
        page_esc(&page, r->type_name, r->type_name_len) != 0 ||
        (r->field_len > 0 &&
            (page_str(&page, ", field ") != 0 ||
                page_esc(&page, r->field, r->field_len) != 0)) ||
        page_str(&page, "</td>") != 0 ||
        access_cell(&page, "<td>", r->explain, r->explain_len) != 0 ||
        page_str(&page, "<td class=\"risk-") != 0 ||
        page_esc(&page, risk, strlen(risk)) != 0 || page_str(&page, "\">") != 0 ||
        page_esc(&page, risk, strlen(risk)) != 0 ||
        page_str(&page, "</td>") != 0 ||
        access_status(&page, r) != 0 || page_str(&page, "</tr>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</tbody></table>") != 0) {
    goto fail;
  }
  gdefcore_access_matrix_free(catalog, rows, n);
  return take_page(&page, out);
fail:
  gdefcore_access_matrix_free(catalog, rows, n);
  page_free(&page);
  return -1;
}

static int post_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, int saved,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  GDEFCORE_Form * form = NULL;
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Value values[8];
  const char * slots[8];
  size_t lens[8];
  char names[8][64];
  size_t nvalues = 0;
  size_t nfields;
  size_t i;
  char * token = NULL;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (gdefcore_entity_create(catalog, "site.post", 9, id, NULL) != GDEFCORE_OK ||
      gdefcore_form_tree(catalog, "site.post", 9, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2,
          &form) != GDEFCORE_OK) {
    gdefcore_form_destroy(form);
    return -1;
  }
  nfields = gdefcore_form_field_count(form);
  for (i = 0; i < nfields && nvalues < 8; i++) {
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
      gdefcore_form_destroy(form);
      return -1;
    }
    if (!required) {
      continue;
    }
    memcpy(names[nvalues], name, name_len);
    names[nvalues][name_len] = '\0';
    one_value(&values[nvalues], names[nvalues], "", &slots[nvalues], &lens[nvalues]);
    nvalues += 1;
  }
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "post", id, values, nvalues, &token) != 0 ||
      page_begin(&page, "Write a post") != 0 ||
      (saved && page_str(&page, "<p>Saved.</p>") != 0) ||
      page_str(&page, "<form method=\"post\" action=\"/admin/post\">"
                      "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(&page, token, strlen(token)) != 0 ||
      page_str(&page, "\">") != 0) {
    gcu_allocator_free(alloc, token);
    gdefcore_form_destroy(form);
    page_free(&page);
    return -1;
  }
  gcu_allocator_free(alloc, token);
  for (i = 0; i < nfields; i++) {
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
    int area;
    if (gdefcore_form_field(form, i, &name, &name_len, &required, &many, &personal,
            &min_len, &max_len, &has_range, &range_min, &range_max, &pattern,
            &pattern_len, &texts, &text_lens, &count) != GDEFCORE_OK) {
      gdefcore_form_destroy(form);
      page_free(&page);
      return -1;
    }
    area = many || (name_len == 4 && memcmp(name, "body", 4) == 0);
    if (page_str(&page, "<p><label>") != 0 || page_esc(&page, name, name_len) != 0 ||
        page_str(&page, area ? " <textarea name=\"" : " <input name=\"") != 0 ||
        page_esc(&page, name, name_len) != 0 ||
        page_str(&page, area ? "\"></textarea></label></p>" : "\" value=\"\"></label></p>") !=
            0) {
      gdefcore_form_destroy(form);
      page_free(&page);
      return -1;
    }
  }
  gdefcore_form_destroy(form);
  if (page_str(&page, "<p><button type=\"submit\">Save</button></p></form>") != 0) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
}

static int front_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  struct Held held[32];
  size_t npanes = 0;
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char ** regions = NULL;
  size_t nregions = 0;
  char region_store[8][32];
  size_t shown = 0;
  size_t i;
  size_t r;
  GDEFCORE_Value filler[2];
  const char * slots[2];
  size_t lens[2];
  char * place = NULL;
  char * remove = NULL;
  char * order = NULL;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (find_front(catalog, id) != 0 || load_panes(catalog, id, held, 32, &npanes) != 0) {
    return -1;
  }
  one_value(&filler[0], "name", "front", &slots[0], &lens[0]);
  one_value(&filler[1], "layout", "layout", &slots[1], &lens[1]);
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "place", id, filler, 2, &place) != 0 ||
      sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "remove", id, filler, 2, &remove) != 0 ||
      sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "order", id, filler, 2, &order) != 0) {
    gcu_allocator_free(alloc, place);
    gcu_allocator_free(alloc, remove);
    gcu_allocator_free(alloc, order);
    return -1;
  }
  if (gdefcore_render_regions(catalog, "layout", 6, &regions, &nregions) != GDEFCORE_OK) {
    regions = NULL;
    nregions = 0;
  }
  if (nregions == 0) {
    memcpy(region_store[0], "main", 5);
    memcpy(region_store[1], "side", 5);
    shown = 2;
  } else {
    for (r = 0; r < nregions && shown < 8; r++) {
      size_t rn = strlen(regions[r]);
      if (rn == 0 || rn >= sizeof region_store[0]) {
        continue;
      }
      memcpy(region_store[shown], regions[r], rn + 1);
      shown += 1;
    }
  }
  gdefcore_render_names_free(catalog, regions, nregions);
  if (page_begin(&page, "Front page") != 0 ||
      page_str(&page, "<form method=\"post\" action=\"/admin/front\">"
                      "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(&page, place, strlen(place)) != 0 ||
      page_str(&page, "\"><button type=\"submit\">Place recent posts</button></form>"
                      "<form method=\"post\" action=\"/admin/front\">"
                      "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(&page, remove, strlen(remove)) != 0 ||
      page_str(&page, "\"><button type=\"submit\">Remove recent posts</button></form>"
                      "<div id=\"panes\">") != 0) {
    goto fail;
  }
  for (r = 0; r < shown; r++) {
    if (page_str(&page, "<div data-region=\"") != 0 ||
        page_esc(&page, region_store[r], strlen(region_store[r])) != 0 ||
        page_str(&page, "\"><h2>") != 0 ||
        page_esc(&page, region_store[r], strlen(region_store[r])) != 0 ||
        page_str(&page, "</h2>") != 0) {
      goto fail;
    }
    for (i = 0; i < npanes; i++) {
      if (strcmp(held[i].region, region_store[r]) != 0) {
        continue;
      }
      if (page_str(&page, "<div draggable=\"true\" data-pane=\"\">") != 0 ||
          page_esc(&page, held[i].line, strlen(held[i].line)) != 0 ||
          page_str(&page, "</div>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</div>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</div><form id=\"reorder\" method=\"post\" action=\"/admin/front\">"
                     "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(&page, order, strlen(order)) != 0 ||
      page_str(&page, "\"><input type=\"hidden\" name=\"panes\" value=\"\"></form>") != 0) {
    goto fail;
  }
  gcu_allocator_free(alloc, place);
  gcu_allocator_free(alloc, remove);
  gcu_allocator_free(alloc, order);
  return take_page(&page, out);
fail:
  gcu_allocator_free(alloc, place);
  gcu_allocator_free(alloc, remove);
  gcu_allocator_free(alloc, order);
  page_free(&page);
  return -1;
}

static int undo_lines(struct Page * page, GDEFCORE_Catalog * catalog, const char * id,
    const char * label) {
  size_t count = 0;
  size_t i;
  if (gdefcore_entity_revision_count(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &count) !=
          GDEFCORE_OK ||
      count == 0) {
    return 0;
  }
  if (page_str(page, "<p>") != 0 || page_esc(page, label, strlen(label)) != 0 ||
      page_str(page, "</p><ul>") != 0) {
    return -1;
  }
  for (i = 0; i < count; i++) {
    uint64_t seq = 0;
    char * actor = NULL;
    size_t actor_n = 0;
    if (gdefcore_entity_revision_at(catalog, id, GDEFCORE_PUBLIC_ID_LEN, i, &seq,
            &actor, &actor_n) != GDEFCORE_OK) {
      return -1;
    }
    /* The revision's sequence number stays on the server: a bare integer on a
     * page is the shape of an internal id (AD-18 item 8), and the line is
     * already in order. */
    (void)seq;
    if (page_str(page, "<li>") != 0) {
      gdefcore_text_free(catalog, actor);
      return -1;
    }
    if (actor != NULL && actor_n > 0) {
      if (page_esc(page, actor, actor_n) != 0) {
        gdefcore_text_free(catalog, actor);
        return -1;
      }
    }
    gdefcore_text_free(catalog, actor);
    if (page_str(page, "</li>") != 0) {
      return -1;
    }
  }
  return page_str(page, "</ul>");
}

static int undo_form(const GDEFWEB_Allocator * alloc, struct Page * page,
    GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, GDEFCORE_Keyring * keyring,
    const uint8_t * root, size_t root_len, const char * session, size_t session_len,
    const char * id, const char * label, const GDEFCORE_Value * values,
    size_t nvalues) {
  char * token = NULL;
  size_t count = 0;
  if (gdefcore_entity_revision_count(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &count) !=
          GDEFCORE_OK ||
      count == 0) {
    return 0;
  }
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "undo", id, values, nvalues, &token) != 0) {
    return -1;
  }
  if (page_str(page, "<form method=\"post\" action=\"/admin/undo\">") != 0 ||
      undo_lines(page, catalog, id, label) != 0 ||
      page_str(page, "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(page, token, strlen(token)) != 0 ||
      page_str(page, "\"><button type=\"submit\">Undo</button></form>") != 0) {
    gcu_allocator_free(alloc, token);
    return -1;
  }
  gcu_allocator_free(alloc, token);
  return 0;
}

static int undo_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  char front[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Value post_values[2];
  GDEFCORE_Value name_value;
  GDEFCORE_Value display_values[2];
  const char * slots[2];
  size_t lens[2];
  const char * name_slot;
  size_t name_len;
  const char * display_slots[2];
  size_t display_lens[2];
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  one_value(&post_values[0], "title", "x", &slots[0], &lens[0]);
  one_value(&post_values[1], "body", "x", &slots[1], &lens[1]);
  one_value(&name_value, "name", "x", &name_slot, &name_len);
  one_value(&display_values[0], "name", "front", &display_slots[0], &display_lens[0]);
  one_value(&display_values[1], "layout", "layout", &display_slots[1],
      &display_lens[1]);
  if (page_begin(&page, "Undo") != 0) {
    return -1;
  }
  if (gdefcore_entity_ids(catalog, "site.post", 9, &ids, &count) != GDEFCORE_OK) {
    page_free(&page);
    return -1;
  }
  for (i = 0; i < count; i++) {
    char title[128];
    char label[160];
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
      continue;
    }
    if (field_copy(catalog, ids[i], "title", title, sizeof title) != 0) {
      memcpy(label, "Post", 5);
    } else {
      snprintf(label, sizeof label, "Post %s", title);
    }
    if (undo_form(alloc, &page, catalog, request, keyring, root, root_len, session,
            session_len, ids[i], label, post_values, 2) != 0) {
      gdefcore_ids_free(catalog, ids, count);
      page_free(&page);
      return -1;
    }
  }
  gdefcore_ids_free(catalog, ids, count);
  ids = NULL;
  if (find_front(catalog, front) == 0 &&
      undo_form(alloc, &page, catalog, request, keyring, root, root_len, session,
          session_len, front, "Front page", display_values, 2) != 0) {
    page_free(&page);
    return -1;
  }
  if (gdefcore_entity_ids(catalog, "core.role", 9, &ids, &count) != GDEFCORE_OK) {
    page_free(&page);
    return -1;
  }
  for (i = 0; i < count; i++) {
    char name[64];
    char label[96];
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
      continue;
    }
    if (field_copy(catalog, ids[i], "name", name, sizeof name) != 0) {
      memcpy(label, "Role", 5);
    } else {
      snprintf(label, sizeof label, "Role %s", name);
    }
    if (undo_form(alloc, &page, catalog, request, keyring, root, root_len, session,
            session_len, ids[i], label, &name_value, 1) != 0) {
      gdefcore_ids_free(catalog, ids, count);
      page_free(&page);
      return -1;
    }
  }
  gdefcore_ids_free(catalog, ids, count);
  return take_page(&page, out);
}

static int post_write(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  GDEFCORE_Form * tree = NULL;
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  GDEFCORE_Value values[8];
  char names[8][64];
  char * texts[8];
  size_t lens[8];
  const char * slots[8];
  size_t nvalues = 0;
  size_t nfields;
  size_t i;
  char * resigned = NULL;
  size_t resigned_n = 0;
  int conflict = 0;
  int wrote = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  memset(texts, 0, sizeof texts);
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    if (page_begin(&page, "Write a post") != 0 || page_str(&page, "<p>Not saved.</p>") != 0) {
      page_free(&page);
      return -1;
    }
    return take_page(&page, out);
  }
  if (gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "post", 4) != 0 ||
      !base_matches(catalog, target, base)) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    if (page_begin(&page, "Write a post") != 0 || page_str(&page, "<p>Not saved.</p>") != 0) {
      page_free(&page);
      return -1;
    }
    return take_page(&page, out);
  }
  if (gdefcore_form_tree(catalog, "site.post", 9, NULL, 0, "en", 2, &tree) != GDEFCORE_OK) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    return -1;
  }
  nfields = gdefcore_form_field_count(tree);
  for (i = 0; i < nfields && nvalues < 8; i++) {
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
    const char * const * current = NULL;
    const size_t * current_lens = NULL;
    size_t count = 0;
    char * text;
    if (gdefcore_form_field(tree, i, &name, &name_len, &required, &many, &personal,
            &min_len, &max_len, &has_range, &range_min, &range_max, &pattern,
            &pattern_len, &current, &current_lens, &count) != GDEFCORE_OK ||
        name_len == 0 || name_len >= sizeof names[0]) {
      gdefcore_form_destroy(tree);
      goto refuse;
    }
    memcpy(names[nvalues], name, name_len);
    names[nvalues][name_len] = '\0';
    text = (char *)gcu_allocator_malloc(alloc, 8192);
    if (text == NULL) {
      gdefcore_form_destroy(tree);
      goto oom;
    }
    if (form_pair(body, body_len, names[nvalues], text, 8192, &lens[nvalues]) != 0) {
      gcu_allocator_free(alloc, text);
      if (required) {
        gdefcore_form_destroy(tree);
        goto refuse;
      }
      continue;
    }
    texts[nvalues] = text;
    slots[nvalues] = text;
    memset(&values[nvalues], 0, sizeof values[nvalues]);
    values[nvalues].size = sizeof values[nvalues];
    values[nvalues].field = names[nvalues];
    values[nvalues].field_len = name_len;
    values[nvalues].texts = &slots[nvalues];
    values[nvalues].text_lens = &lens[nvalues];
    values[nvalues].count = 1;
    nvalues += 1;
  }
  gdefcore_form_destroy(tree);
  tree = NULL;
  if (gdefcore_form_resign(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, values, nvalues, &resigned,
          &resigned_n) != GDEFCORE_OK ||
      gdefcore_form_submit(catalog, request, keyring, root, root_len, session,
          session_len, resigned, resigned_n, &conflict) != GDEFCORE_OK ||
      conflict != 0 ||
      gdefcore_form_publish(catalog, target, GDEFCORE_PUBLIC_ID_LEN) != GDEFCORE_OK) {
    gdefcore_text_free(catalog, resigned);
    goto refuse;
  }
  gdefcore_text_free(catalog, resigned);
  wrote = 1;
  for (i = 0; i < nvalues; i++) {
    wipe(texts[i], lens[i]);
    gcu_allocator_free(alloc, texts[i]);
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  return redirect(out, "/admin/post/saved");
refuse:
  for (i = 0; i < nvalues; i++) {
    if (texts[i] != NULL) {
      wipe(texts[i], lens[i]);
      gcu_allocator_free(alloc, texts[i]);
    }
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  if (wrote) {
    return -1;
  }
  if (page_begin(&page, "Write a post") != 0 || page_str(&page, "<p>Not saved.</p>") != 0) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
oom:
  for (i = 0; i < nvalues; i++) {
    gcu_allocator_free(alloc, texts[i]);
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  page_free(&page);
  return -1;
}

static int panes_from_post(const char * text, size_t n, char lines[][64],
    size_t * out_n) {
  size_t i = 0;
  *out_n = 0;
  if (n == 0) {
    return -1;
  }
  while (i < n) {
    size_t start = i;
    size_t len;
    while (i < n && text[i] != '\n' && text[i] != '\r') {
      i += 1;
    }
    len = i - start;
    if (len != 16 ||
        (memcmp(text + start, "main view recent", 16) != 0 &&
            memcmp(text + start, "side view recent", 16) != 0)) {
      return -1;
    }
    if (*out_n >= 32) {
      return -1;
    }
    memcpy(lines[*out_n], text + start, len);
    lines[*out_n][len] = '\0';
    *out_n += 1;
    if (i < n && text[i] == '\r') {
      i += 1;
    }
    if (i < n && text[i] == '\n') {
      i += 1;
    }
  }
  return *out_n == 0 ? -1 : 0;
}

static int front_write(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  (void)alloc;
  char token[4096];
  size_t token_n = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  char front[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  struct Held held[32];
  size_t npanes = 0;
  const char * lines[32];
  char posted[32][64];
  size_t nlines = 0;
  size_t i;
  int kind = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    return redirect(out, "/admin/front");
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  if (find_front(catalog, front) != 0 ||
      memcmp(target, front, GDEFCORE_PUBLIC_ID_LEN) != 0 ||
      !base_matches(catalog, front, base) ||
      load_panes(catalog, front, held, 32, &npanes) != 0) {
    return redirect(out, "/admin/front");
  }
  if (form_len == 5 && memcmp(form_id, "place", 5) == 0) {
    kind = 1;
  } else if (form_len == 6 && memcmp(form_id, "remove", 6) == 0) {
    kind = 2;
  } else if (form_len == 5 && memcmp(form_id, "order", 5) == 0) {
    kind = 3;
  } else {
    return redirect(out, "/admin/front");
  }
  if (kind == 1) {
    int already = 0;
    for (i = 0; i < npanes; i++) {
      lines[nlines++] = held[i].line;
      if (held[i].recent) {
        already = 1;
      }
    }
    if (!already && nlines < 32) {
      lines[nlines++] = "side view recent";
      if (save_panes(catalog, request, front, lines, nlines) != 0) {
        return -1;
      }
    }
  } else if (kind == 2) {
    int dropped = 0;
    for (i = 0; i < npanes; i++) {
      if (held[i].recent) {
        dropped = 1;
        continue;
      }
      lines[nlines++] = held[i].line;
    }
    if (dropped && save_panes(catalog, request, front, lines, nlines) != 0) {
      return -1;
    }
  } else {
    char text[4096];
    size_t text_n = 0;
    size_t posted_n = 0;
    if (form_pair(body, body_len, "panes", text, sizeof text, &text_n) != 0 ||
        panes_from_post(text, text_n, posted, &posted_n) != 0) {
      return redirect(out, "/admin/front");
    }
    for (i = 0; i < posted_n; i++) {
      lines[i] = posted[i];
    }
    if (save_panes(catalog, request, front, lines, posted_n) != 0) {
      return -1;
    }
  }
  return redirect(out, "/admin/front");
}

static int undo_write(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  (void)alloc;
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * type = NULL;
  size_t type_n = 0;
  uint64_t base = 0;
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Changeset * cs = NULL;
  int risk = 0;
  int allowed = 0;
  GDEFCORE_Result rc;
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "undo", 4) != 0 ||
      !base_matches(catalog, target, base)) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    return redirect(out, "/admin/undo");
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  if (gdefcore_entity_type(catalog, target, GDEFCORE_PUBLIC_ID_LEN, &type, &type_n) !=
      GDEFCORE_OK) {
    return redirect(out, "/admin/undo");
  }
  if (type_n == 9 && memcmp(type, "site.post", 9) == 0) {
    allowed = 1;
  } else if (type_n == 9 && memcmp(type, "core.role", 9) == 0) {
    allowed = 1;
  } else if (type_n == 12 && memcmp(type, "core.display", 12) == 0) {
    char name[64];
    if (field_copy(catalog, target, "name", name, sizeof name) == 0 &&
        strcmp(name, "front") == 0) {
      allowed = 1;
    }
  }
  if (!allowed) {
    gdefcore_text_free(catalog, type);
    return redirect(out, "/admin/undo");
  }
  who = gdefcore_request_principal(request, &who_n);
  rc = gdefcore_changeset_revert(catalog, target, GDEFCORE_PUBLIC_ID_LEN, who, who_n,
      &cs);
  if (rc == GDEFCORE_OK) {
    rc = gdefcore_changeset_rate(cs, &risk);
  }
  if (rc == GDEFCORE_OK) {
    if (risk == GDEFCORE_RISK_HIGH) {
      rc = gdefcore_changeset_apply(cs, type, type_n);
    } else {
      rc = gdefcore_changeset_apply(cs, NULL, 0);
    }
  }
  gdefcore_changeset_destroy(cs);
  gdefcore_text_free(catalog, type);
  if (rc != GDEFCORE_OK) {
    return redirect(out, "/admin/undo");
  }
  return redirect(out, "/admin/undo");
}

/*
 * The template editor (site-admin story 3): /admin/templates, one template,
 * and its history. Every action is a signed form that answers with a 303.
 * Nothing here needs a script.
 *
 * A principal who may not edit a template gets, for that template, the answer
 * a template that does not exist gets (AD-11). The permission is
 * `core.template.edit`, granted with a condition on the template's kind or
 * classification, and the question is the core's one decision about this
 * template (gdefcore_template_can_edit()), which the write asks too, so the
 * list, the page and the save cannot disagree. The reads below are the
 * unfiltered admin reads and are made only after it said yes.
 *
 * A page that must show the editor's own text after a redirect (a validation,
 * a preview, a refusal) keeps it in a small flash store, bound to the tenant,
 * the session and the template, and reachable only by a one-use-looking path
 * the redirect names. It is not a cache of renders: a preview's output is
 * held for its own session and no one else's, and it is not written to the
 * catalog.
 */

#define TPL_NAME_MAX 128
#define TPL_KIND_MAX 96
#define TPL_BODY_MAX (60u * 1024u)
#define TPL_KINDS_MAX 64
#define TPL_LIST_MAX 128
#define FLASH_N 16
#define FLASH_TTL 600
#define FLASH_NONCE_RAW 16

/* What a redirect back to the editor has to say. */
enum {
  N_NONE = 0,
  N_CHECKED = 1,
  N_PREVIEWED,
  N_SAVED,
  N_REFUSED,
  N_CONFLICT,
  N_WRONG_CONFIRM,
  N_INVALID,
  N_BAD_KIND,
  N_REVERTED,
  N_NOT_REVERTED,
  N_BAD_FORM,
  N_TOO_LARGE,
  N_BAD_TEXT
};

struct Flash {
  int used;
  char nonce[32];
  unsigned char tenant[GSEC_SHA256_DIGEST_LEN];
  unsigned char session[GSEC_SHA256_DIGEST_LEN];
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  time_t at;
  int notice;
  char kind[TPL_KIND_MAX];
  char * body;
  size_t body_len;
  char * issues;
  size_t issues_len;
  char * output;
  size_t output_len;
  int has_output;
};

static struct Flash kFlash[FLASH_N];

static void flash_clear(struct Flash * f) {
  if (f->body != NULL) {
    wipe(f->body, f->body_len);
  }
  free(f->body);
  free(f->issues);
  if (f->output != NULL) {
    wipe(f->output, f->output_len);
  }
  free(f->output);
  memset(f, 0, sizeof *f);
}

static int digest_of(const void * data, size_t n, unsigned char out[GSEC_SHA256_DIGEST_LEN]) {
  return gsec_sha256(data, n, out) == GSEC_OK ? 0 : -1;
}

static char * dup_bytes(const char * s, size_t n) {
  char * p = (char *)malloc(n + 1);
  if (p == NULL) {
    return NULL;
  }
  if (n > 0) {
    memcpy(p, s, n);
  }
  p[n] = '\0';
  return p;
}

/*
 * Keep something for the redirect to show. Returns the nonce in @p nonce, or
 * -1. The body, the issues (HTML we built) and the preview output are copied.
 */
static int flash_put(GDEFCORE_Catalog * catalog, const unsigned char * session,
    size_t session_len, const char * target, int notice, const char * kind,
    const char * body, size_t body_len, const char * issues, size_t issues_len,
    const char * output, size_t output_len, int has_output, char * nonce,
    size_t nonce_cap) {
  size_t i;
  size_t pick = 0;
  struct Flash * f;
  unsigned char rnd[FLASH_NONCE_RAW];
  const uint8_t * tenant;
  size_t tenant_len = 0;
  time_t now = time(NULL);
  static const char kTab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t o = 0;
  tenant = gdefcore_catalog_tenant(catalog, &tenant_len);
  if (tenant == NULL || nonce_cap < 24 || gsec_random_bytes(rnd, sizeof rnd, NULL) != GSEC_OK) {
    return -1;
  }
  for (i = 0; i < FLASH_N; i++) {
    if (!kFlash[i].used || now - kFlash[i].at > FLASH_TTL) {
      pick = i;
      break;
    }
    if (kFlash[i].at < kFlash[pick].at) {
      pick = i;
    }
  }
  f = &kFlash[pick];
  flash_clear(f);
  if (digest_of(tenant, tenant_len, f->tenant) != 0 ||
      digest_of(session, session_len, f->session) != 0) {
    return -1;
  }
  memcpy(f->target, target, GDEFCORE_PUBLIC_ID_LEN);
  f->target[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  f->at = now;
  f->notice = notice;
  if (kind != NULL && strlen(kind) < sizeof f->kind) {
    strcpy(f->kind, kind);
  }
  if (body != NULL) {
    f->body = dup_bytes(body, body_len);
    f->body_len = body_len;
    if (f->body == NULL) {
      flash_clear(f);
      return -1;
    }
  }
  if (issues != NULL && issues_len > 0) {
    f->issues = dup_bytes(issues, issues_len);
    f->issues_len = issues_len;
    if (f->issues == NULL) {
      flash_clear(f);
      return -1;
    }
  }
  if (has_output) {
    f->output = dup_bytes(output == NULL ? "" : output, output_len);
    f->output_len = output_len;
    f->has_output = 1;
    if (f->output == NULL) {
      flash_clear(f);
      return -1;
    }
  }
  for (i = 0; i + 3 <= sizeof rnd; i += 3) {
    unsigned int v = ((unsigned int)rnd[i] << 16) | ((unsigned int)rnd[i + 1] << 8) |
                     (unsigned int)rnd[i + 2];
    f->nonce[o++] = kTab[(v >> 18) & 63u];
    f->nonce[o++] = kTab[(v >> 12) & 63u];
    f->nonce[o++] = kTab[(v >> 6) & 63u];
    f->nonce[o++] = kTab[v & 63u];
  }
  f->nonce[o++] = kTab[(rnd[15] >> 2) & 63u];
  f->nonce[o] = '\0';
  wipe(rnd, sizeof rnd);
  f->used = 1;
  memcpy(nonce, f->nonce, o + 1);
  return 0;
}

/* The entry @p nonce names, if it is this tenant's, this session's and this template's. */
static struct Flash * flash_get(GDEFCORE_Catalog * catalog,
    const unsigned char * session, size_t session_len, const char * target,
    const char * nonce) {
  size_t i;
  unsigned char tdig[GSEC_SHA256_DIGEST_LEN];
  unsigned char sdig[GSEC_SHA256_DIGEST_LEN];
  const uint8_t * tenant;
  size_t tenant_len = 0;
  time_t now = time(NULL);
  tenant = gdefcore_catalog_tenant(catalog, &tenant_len);
  if (tenant == NULL || digest_of(tenant, tenant_len, tdig) != 0 ||
      digest_of(session, session_len, sdig) != 0) {
    return NULL;
  }
  for (i = 0; i < FLASH_N; i++) {
    struct Flash * f = &kFlash[i];
    if (!f->used || now - f->at > FLASH_TTL || strcmp(f->nonce, nonce) != 0) {
      continue;
    }
    if (memcmp(f->tenant, tdig, sizeof tdig) != 0 ||
        memcmp(f->session, sdig, sizeof sdig) != 0 ||
        memcmp(f->target, target, GDEFCORE_PUBLIC_ID_LEN) != 0) {
      return NULL;
    }
    return f;
  }
  return NULL;
}

/* Page helpers --------------------------------------------------------- */

/*
 * Text from the template engine (a message, a name, a contract's words) with
 * its digits written as character references. The response scanner reads a
 * bare integer on a page as a row id leaking (AD-18 item 8), and a line number
 * in a message is a position in the text the reader is looking at, not an id.
 * A character reference is how HTML writes the same digit, shows the same, and
 * is what the scanner is specified to leave alone, so the scanner keeps its
 * meaning for every other number.
 */
static int page_esc_digits(struct Page * page, const char * s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      char ref[12];
      snprintf(ref, sizeof ref, "&#%u;", (unsigned)(unsigned char)c);
      if (page_str(page, ref) != 0) {
        return -1;
      }
    } else if (page_esc(page, &c, 1) != 0) {
      return -1;
    }
  }
  return 0;
}

static int page_number(struct Page * page, long v) {
  char text[32];
  snprintf(text, sizeof text, "%ld", v);
  return page_esc_digits(page, text, strlen(text));
}

/* A name in a URL: unreserved bytes as they are, the rest as %XX. */
static int page_seg(struct Page * page, const char * s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      if (page_add(page, (const char *)&c, 1) != 0) {
        return -1;
      }
    } else {
      char enc[4];
      snprintf(enc, sizeof enc, "%%%02X", (unsigned)c);
      if (page_str(page, enc) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

static int seg_decode(const char * in, size_t n, char * out, size_t cap,
    size_t * out_n) {
  size_t i = 0;
  size_t o = 0;
  while (i < n) {
    unsigned char c = (unsigned char)in[i];
    if (c == '%') {
      int hi;
      int lo;
      if (i + 2 >= n) {
        return -1;
      }
      hi = hex_nibble(in[i + 1]);
      lo = hex_nibble(in[i + 2]);
      if (hi < 0 || lo < 0) {
        return -1;
      }
      c = (unsigned char)((hi << 4) | lo);
      i += 2;
    }
    if (c < 0x20 || c == 0x7f || o + 1 >= cap) {
      return -1;
    }
    out[o++] = (char)c;
    i += 1;
  }
  if (o == 0) {
    return -1;
  }
  out[o] = '\0';
  *out_n = o;
  return 0;
}

/* Form fields, heap-decoded, because a template body is longer than any buffer
 * the other pages keep. @p nth picks the nth field of that name. */
static int form_field_alloc(const char * body, size_t n, const char * name,
    size_t nth, char ** out, size_t * out_n) {
  size_t nlen = strlen(name);
  size_t i = 0;
  size_t seen = 0;
  while (i < n) {
    size_t start = i;
    size_t eq;
    size_t end;
    while (i < n && body[i] != '&') {
      i += 1;
    }
    end = i;
    eq = start;
    while (eq < end && body[eq] != '=') {
      eq += 1;
    }
    if (eq < end && eq - start == nlen && memcmp(body + start, name, nlen) == 0) {
      if (seen == nth) {
        char * buf = (char *)malloc(end - (eq + 1) + 1);
        size_t got = 0;
        if (buf == NULL) {
          return -1;
        }
        if (pct_decode(body + eq + 1, end - (eq + 1), buf, end - (eq + 1) + 1, &got) != 0) {
          free(buf);
          return -1;
        }
        *out = buf;
        *out_n = got;
        return 0;
      }
      seen += 1;
    }
    if (i < n && body[i] == '&') {
      i += 1;
    }
  }
  return -1;
}

/* A line break in a form is CR LF; a template is kept with LF. */
static size_t normalize_newlines(char * s, size_t n) {
  size_t i = 0;
  size_t o = 0;
  while (i < n) {
    if (s[i] == '\r') {
      s[o++] = '\n';
      if (i + 1 < n && s[i + 1] == '\n') {
        i += 1;
      }
    } else {
      s[o++] = s[i];
    }
    i += 1;
  }
  s[o] = '\0';
  return o;
}

/* Reading the store (unfiltered; made only for a principal who may) ------ */

/* The first value of @p field in the entity's draft, where it has one, else in its
 * current revision, malloc'd; an absent field is the empty string. It is what an
 * editor opens to change and what a conflict check (made against the entity's
 * version) is about, so it is what the editor is shown as "the stored version".
 * There is no head apart from the current revision since story S6b. */
static int stored_text(GDEFCORE_Catalog * catalog, const char * id, const char * field,
    char ** out, size_t * out_n) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_DRAFT,
      id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field, strlen(field), &texts, &lens, &count);
  if (rc == GDEFCORE_ERR_INVALID) {
    *out = dup_bytes("", 0);
    *out_n = 0;
    return *out == NULL ? -1 : 0;
  }
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  if (count == 0 || texts[0] == NULL) {
    *out = dup_bytes("", 0);
    *out_n = 0;
  } else {
    *out = dup_bytes(texts[0], lens[0]);
    *out_n = lens[0];
  }
  gdefcore_values_free(catalog, texts, lens, count);
  return *out == NULL ? -1 : 0;
}

/* The id of the entity of @p type whose name is @p name. 0 found, 1 not, -1 error. */
static int find_by_name(GDEFCORE_Catalog * catalog, const char * type, const char * name,
    size_t name_len, char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  int found = 0;
  if (gdefcore_entity_ids(catalog, type, strlen(type), &ids, &count) != GDEFCORE_OK) {
    return -1;
  }
  for (i = 0; i < count && !found; i++) {
    char * got = NULL;
    size_t got_n = 0;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN) {
      continue;
    }
    if (stored_text(catalog, ids[i], "name", &got, &got_n) != 0) {
      gdefcore_ids_free(catalog, ids, count);
      return -1;
    }
    if (got_n == name_len && memcmp(got, name, name_len) == 0) {
      memcpy(id, ids[i], GDEFCORE_PUBLIC_ID_LEN + 1);
      found = 1;
    }
    free(got);
  }
  gdefcore_ids_free(catalog, ids, count);
  return found ? 0 : 1;
}

struct KindRow {
  char name[TPL_KIND_MAX];
  char label[TPL_KIND_MAX];
};

/*
 * The kinds this principal may edit, by label. With @p template_id, the kinds
 * that template may be left as (its own among them, when it may be edited at
 * all); without, the kinds of which some template may be edited. Only such a
 * kind is read at all: the others are not named anywhere.
 */
static int editable_kinds(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * template_id, struct KindRow * rows, size_t cap, size_t * out_n) {
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  size_t n = 0;
  *out_n = 0;
  if (gdefcore_entity_ids(catalog, GDEFCORE_TEMPLATE_KIND_TYPE,
          sizeof GDEFCORE_TEMPLATE_KIND_TYPE - 1, &ids, &count) != GDEFCORE_OK) {
    return -1;
  }
  for (i = 0; i < count && n < cap; i++) {
    char name[TPL_KIND_MAX];
    char label[TPL_KIND_MAX];
    size_t j;
    int ok = 0;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
        field_copy(catalog, ids[i], "name", name, sizeof name) != 0) {
      continue;
    }
    if (template_id != NULL) {
      if (gdefcore_template_can_edit(catalog, request, template_id, name,
              strlen(name), &ok) != GDEFCORE_OK) {
        gdefcore_ids_free(catalog, ids, count);
        return -1;
      }
    } else {
      ok = gdefcore_template_may_edit(catalog, request, name, strlen(name));
    }
    if (!ok) {
      continue;
    }
    if (field_copy(catalog, ids[i], "label", label, sizeof label) != 0 ||
        label[0] == '\0') {
      strcpy(label, name);
    }
    /* Insertion by label, then name. */
    j = n;
    while (j > 0 && strcmp(rows[j - 1].label, label) > 0) {
      rows[j] = rows[j - 1];
      j -= 1;
    }
    memcpy(rows[j].name, name, strlen(name) + 1);
    memcpy(rows[j].label, label, strlen(label) + 1);
    n += 1;
  }
  gdefcore_ids_free(catalog, ids, count);
  *out_n = n;
  return 0;
}

/* The issues of a report as a list. @p page gets "" when there are none. */
static int page_issues(struct Page * page, const GDEFCORE_Template_Report * report) {
  size_t n = gdefcore_template_report_count(report);
  size_t i;
  if (n == 0) {
    return 0;
  }
  if (page_str(page, "<ul class=\"issues\">") != 0) {
    return -1;
  }
  for (i = 0; i < n; i++) {
    GDEFCORE_Template_Issue issue;
    if (gdefcore_template_report_at(report, i, &issue) != GDEFCORE_OK) {
      return -1;
    }
    if (page_str(page, "<li>") != 0) {
      return -1;
    }
    if (issue.line > 0) {
      if (page_str(page, "Line ") != 0 || page_number(page, issue.line) != 0 ||
          page_str(page, ": ") != 0) {
        return -1;
      }
    }
    if (page_esc_digits(page, issue.message, issue.message_len) != 0 ||
        page_str(page, "</li>") != 0) {
      return -1;
    }
  }
  return page_str(page, "</ul>");
}

/* Plain text (a refusal's words, one issue per line) as the same list. */
static int page_issue_text(struct Page * page, const char * text, size_t n) {
  size_t i = 0;
  int any = 0;
  while (i < n) {
    size_t start = i;
    while (i < n && text[i] != '\n') {
      i += 1;
    }
    if (i > start) {
      if (!any && page_str(page, "<ul class=\"issues\">") != 0) {
        return -1;
      }
      any = 1;
      if (page_str(page, "<li>") != 0 || page_esc_digits(page, text + start, i - start) != 0 ||
          page_str(page, "</li>") != 0) {
        return -1;
      }
    }
    i += 1;
  }
  return any ? page_str(page, "</ul>") : 0;
}

static const char * arity_words(int arity) {
  switch (arity) {
    case GDEFCORE_TEMPLATE_ARITY_ANY:
      return "any number of arguments";
    case 0:
      return "no arguments";
    case 1:
      return "one argument";
    case 2:
      return "two arguments";
    case 3:
      return "three arguments";
    default:
      return NULL;
  }
}

static const char * type_word(int type) {
  switch (type) {
    case GDEFCORE_TEMPLATE_TEXT:
      return "text";
    case GDEFCORE_TEMPLATE_MARKUP:
      return "markup";
    case GDEFCORE_TEMPLATE_NUMBER:
      return "number";
    case GDEFCORE_TEMPLATE_LIST:
      return "list";
    case GDEFCORE_TEMPLATE_ENTITY:
      return "entity";
    case GDEFCORE_TEMPLATE_STRUCTURE:
      return "structure";
    default:
      return "unknown";
  }
}

/* The effective contract of @p kind: what a template of it may use. */
static int page_contract(struct Page * page, GDEFCORE_Catalog * catalog, const char * kind) {
  GDEFCORE_Contract * contract = NULL;
  size_t i;
  size_t nv;
  size_t nf;
  if (gdefcore_template_contract_open(catalog, NULL, kind, strlen(kind), &contract,
          NULL) != GDEFCORE_OK) {
    return page_str(page, "<p>The contract of this kind cannot be shown.</p>");
  }
  nv = gdefcore_template_contract_var_count(contract);
  nf = gdefcore_template_contract_fn_count(contract);
  if (page_str(page, "<h2>What this kind may use</h2>") != 0) {
    goto fail;
  }
  if (nv == 0) {
    if (page_str(page, "<p>No variables.</p>") != 0) {
      goto fail;
    }
  } else if (page_str(page, "<h3>Variables</h3><dl class=\"contract\">") != 0) {
    goto fail;
  }
  for (i = 0; i < nv; i++) {
    GDEFCORE_Contract_Var v;
    if (gdefcore_template_contract_var_at(contract, i, &v) != GDEFCORE_OK ||
        page_str(page, "<dt><code>") != 0 || page_esc_digits(page, v.name, v.name_len) != 0 ||
        page_str(page, "</code> (") != 0 || page_str(page, type_word(v.type)) != 0) {
      goto fail;
    }
    if (v.contributed != NULL &&
        (page_str(page, ", from module ") != 0 ||
            page_esc_digits(page, v.contributed->module, v.contributed->module_len) != 0)) {
      goto fail;
    }
    if (page_str(page, ")</dt><dd>") != 0 ||
        page_esc_digits(page, v.description, v.description_len) != 0 ||
        page_str(page, "</dd>") != 0) {
      goto fail;
    }
  }
  if (nv > 0 && page_str(page, "</dl>") != 0) {
    goto fail;
  }
  if (nf == 0) {
    if (page_str(page, "<p>No functions.</p>") != 0) {
      goto fail;
    }
  } else if (page_str(page, "<h3>Functions</h3><ul class=\"contract\">") != 0) {
    goto fail;
  }
  for (i = 0; i < nf; i++) {
    GDEFCORE_Contract_Fn f;
    const char * words;
    if (gdefcore_template_contract_fn_at(contract, i, &f) != GDEFCORE_OK ||
        page_str(page, "<li><code>") != 0 || page_esc_digits(page, f.name, f.name_len) != 0 ||
        page_str(page, "</code> takes ") != 0) {
      goto fail;
    }
    words = arity_words(f.arity);
    if ((words != NULL ? page_str(page, words) : page_number(page, f.arity)) != 0 ||
        (words == NULL && page_str(page, " arguments") != 0) ||
        page_str(page, "</li>") != 0) {
      goto fail;
    }
  }
  if (nf > 0 && page_str(page, "</ul>") != 0) {
    goto fail;
  }
  gdefcore_template_contract_close(contract);
  return 0;
fail:
  gdefcore_template_contract_close(contract);
  return -1;
}

/* Pages ---------------------------------------------------------------- */

static int unknown_admin(const char * method, struct GDEFWEB_Admin_Reply * out);

static const char kDocsUrl[] =
    "https://github.com/Ghoti-io/defiant-core/blob/master/documentation/templates.md";

static int tpl_location(struct GDEFWEB_Admin_Reply * out, const char * name,
    size_t name_len, const char * tail, const char * nonce) {
  char enc[3 * TPL_NAME_MAX + 8];
  size_t i;
  size_t n = 0;
  for (i = 0; i < name_len && n + 4 < sizeof enc; i++) {
    unsigned char c = (unsigned char)name[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      enc[n++] = (char)c;
    } else {
      snprintf(enc + n, 4, "%%%02X", (unsigned)c);
      n += 3;
    }
  }
  enc[n] = '\0';
  if (nonce != NULL) {
    snprintf(out->location_buf, sizeof out->location_buf, "/admin/templates/%s%s/r/%s", enc,
        tail, nonce);
  } else {
    snprintf(out->location_buf, sizeof out->location_buf, "/admin/templates/%s%s", enc, tail);
  }
  return redirect(out, out->location_buf);
}

static int tpl_list_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  struct KindRow kinds[TPL_KINDS_MAX];
  size_t nkinds = 0;
  size_t k;
  char ** ids = NULL;
  size_t count = 0;
  size_t shown = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (editable_kinds(catalog, request, NULL, kinds, TPL_KINDS_MAX, &nkinds) != 0 ||
      gdefcore_entity_ids(catalog, GDEFCORE_TEMPLATE_TYPE,
          sizeof GDEFCORE_TEMPLATE_TYPE - 1, &ids, &count) != GDEFCORE_OK) {
    return -1;
  }
  if (page_begin(&page, "Templates") != 0 || page_str(&page, "<h1>Templates</h1>") != 0) {
    goto fail;
  }
  for (k = 0; k < nkinds; k++) {
    char names[TPL_LIST_MAX][TPL_NAME_MAX];
    size_t nn = 0;
    size_t i;
    size_t j;
    for (i = 0; i < count && nn < TPL_LIST_MAX; i++) {
      char name[TPL_NAME_MAX];
      char kind[TPL_KIND_MAX];
      int ok = 0;
      if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
          field_copy(catalog, ids[i], "kind", kind, sizeof kind) != 0 ||
          strcmp(kind, kinds[k].name) != 0 ||
          field_copy(catalog, ids[i], "name", name, sizeof name) != 0) {
        continue;
      }
      /* A kind is listed when some template of it may be edited; each
       * template is listed when this one may. */
      if (gdefcore_template_can_edit(catalog, request, ids[i], NULL, 0, &ok) !=
              GDEFCORE_OK ||
          !ok) {
        continue;
      }
      j = nn;
      while (j > 0 && strcmp(names[j - 1], name) > 0) {
        memcpy(names[j], names[j - 1], sizeof names[j]);
        j -= 1;
      }
      memcpy(names[j], name, strlen(name) + 1);
      nn += 1;
    }
    if (nn == 0) {
      continue;
    }
    shown += nn;
    if (page_str(&page, "<h2>") != 0 ||
        page_esc(&page, kinds[k].label, strlen(kinds[k].label)) != 0 ||
        page_str(&page, "</h2><ul>") != 0) {
      goto fail;
    }
    for (j = 0; j < nn; j++) {
      if (page_str(&page, "<li><a href=\"/admin/templates/") != 0 ||
          page_seg(&page, names[j], strlen(names[j])) != 0 || page_str(&page, "\">") != 0 ||
          page_esc(&page, names[j], strlen(names[j])) != 0 ||
          page_str(&page, "</a></li>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</ul>") != 0) {
      goto fail;
    }
  }
  if (shown == 0 && page_str(&page, "<p>There are no templates you may edit.</p>") != 0) {
    goto fail;
  }
  gdefcore_ids_free(catalog, ids, count);
  return take_page(&page, out);
fail:
  gdefcore_ids_free(catalog, ids, count);
  page_free(&page);
  return -1;
}

/* The words a redirect's notice shows. */
static int page_notice(struct Page * page, int notice) {
  const char * text = NULL;
  switch (notice) {
    case N_CHECKED:
      return 0;
    case N_PREVIEWED:
      text = "Previewed below. Nothing was saved.";
      break;
    case N_SAVED:
      text = "Saved.";
      break;
    case N_REFUSED:
      text = "That edit was refused. Nothing was saved.";
      break;
    case N_CONFLICT:
      text = "Not saved: this template changed while you were editing it. "
             "Your text is below, and the stored version is further down.";
      break;
    case N_WRONG_CONFIRM:
      text = "Not saved: the name typed does not match this template's name.";
      break;
    case N_INVALID:
      text = "Not saved: the template does not validate.";
      break;
    case N_BAD_KIND:
      text = "Not saved: choose one of the kinds listed.";
      break;
    case N_REVERTED:
      text = "Reverted.";
      break;
    case N_NOT_REVERTED:
      text = "Not reverted. Nothing was changed.";
      break;
    case N_BAD_FORM:
      text = "Not saved: that form is out of date. Your text is below.";
      break;
    case N_TOO_LARGE:
      text = "Not saved: the template is too large.";
      break;
    case N_BAD_TEXT:
      text = "Not saved: the text holds a character that cannot be saved.";
      break;
    default:
      return 0;
  }
  return page_str(page, "<p class=\"notice\">") != 0 || page_str(page, text) != 0 ||
         page_str(page, "</p>");
}

/*
 * The token binds the form to this session, this template and the revision it
 * was opened at. The type's required fields need a value to be signed at all;
 * these carry the name and kind and an empty body, and nothing the token
 * carries is ever read back as the editor's text: that arrives as a field.
 */
static int tpl_sign(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * form_id,
    const char * id, const char * name, const char * kind, char ** token) {
  GDEFCORE_Value values[3];
  const char * slots[3];
  size_t lens[3];
  one_value(&values[0], "name", name, &slots[0], &lens[0]);
  one_value(&values[1], "body", "", &slots[1], &lens[1]);
  one_value(&values[2], "kind", kind, &slots[2], &lens[2]);
  return sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
      form_id, id, values, 3, token);
}

static int page_token(struct Page * page, const char * token) {
  return page_str(page, "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
         page_esc(page, token, strlen(token)) != 0 || page_str(page, "\">");
}

/*
 * The edit page. @p body and @p kind are what the textarea and the select
 * show; they are the stored ones unless a redirect brought the editor's own.
 */
static int tpl_edit_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * id,
    const char * name, const char * body, size_t body_len, const char * kind,
    const struct Flash * flash, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  struct KindRow kinds[TPL_KINDS_MAX];
  size_t nkinds = 0;
  size_t k;
  char * check = NULL;
  char * save = NULL;
  char * stored_body = NULL;
  size_t stored_n = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (editable_kinds(catalog, request, id, kinds, TPL_KINDS_MAX, &nkinds) != 0 ||
      tpl_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "tpl-check", id, name, kind, &check) != 0 ||
      tpl_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "tpl-save", id, name, kind, &save) != 0) {
    goto fail;
  }
  if (page_begin(&page, "Template") != 0 || page_str(&page, "<h1>Template ") != 0 ||
      page_esc(&page, name, strlen(name)) != 0 || page_str(&page, "</h1><p><a href=\"/admin/templates\">All templates</a> "
                                  "<a href=\"/admin/templates/") != 0 ||
      page_seg(&page, name, strlen(name)) != 0 || page_str(&page, "/history\">History</a></p>") != 0) {
    goto fail;
  }
  if (flash != NULL) {
    if (page_notice(&page, flash->notice) != 0) {
      goto fail;
    }
    if (flash->notice == N_CHECKED || flash->notice == N_PREVIEWED) {
      if (flash->issues_len == 0 && page_str(&page, "<p class=\"notice\">No problems found.</p>") != 0) {
        goto fail;
      }
    }
    if (flash->issues != NULL && page_add(&page, flash->issues, flash->issues_len) != 0) {
      goto fail;
    }
    if (flash->has_output &&
        (page_str(&page, "<h2>Preview</h2><p>This template rendered against sample data. "
                         "What it printed:</p><pre class=\"preview\">") != 0 ||
            page_esc(&page, flash->output, flash->output_len) != 0 ||
            page_str(&page, "</pre>") != 0)) {
      goto fail;
    }
  }
  if (page_str(&page, "<form method=\"post\" action=\"/admin/templates/") != 0 ||
      page_seg(&page, name, strlen(name)) != 0 || page_str(&page, "\">") != 0 ||
      page_token(&page, check) != 0 || page_token(&page, save) != 0 ||
      page_str(&page, "<p><label>Kind <select name=\"kind\">") != 0) {
    goto fail;
  }
  for (k = 0; k < nkinds; k++) {
    if (page_str(&page, "<option value=\"") != 0 ||
        page_esc(&page, kinds[k].name, strlen(kinds[k].name)) != 0 ||
        page_str(&page, strcmp(kinds[k].name, kind) == 0 ? "\" selected>" : "\">") != 0 ||
        page_esc(&page, kinds[k].label, strlen(kinds[k].label)) != 0 ||
        page_str(&page, "</option>") != 0) {
      goto fail;
    }
  }
  /* A leading line break in a textarea is dropped by the parser: keep one. */
  if (page_str(&page, "</select></label></p><p><label>Body<br><textarea name=\"body\" "
                      "spellcheck=\"false\">\n") != 0 ||
      page_esc(&page, body, body_len) != 0 ||
      page_str(&page, "</textarea></label></p><p><label>To save, type this template's name "
                      "to confirm: <input name=\"confirm\" autocomplete=\"off\" value=\"\">"
                      "</label></p><p><button type=\"submit\" name=\"do\" value=\"validate\">"
                      "Validate</button> <button type=\"submit\" name=\"do\" value=\"preview\">"
                      "Preview</button> <button type=\"submit\" name=\"do\" value=\"save\">"
                      "Save</button></p></form>") != 0) {
    goto fail;
  }
  if (flash != NULL && flash->notice == N_CONFLICT) {
    if (stored_text(catalog, id, "body", &stored_body, &stored_n) != 0 ||
        page_str(&page, "<h2>The stored version</h2><pre class=\"stored\">") != 0 ||
        page_esc(&page, stored_body, stored_n) != 0 || page_str(&page, "</pre>") != 0) {
      goto fail;
    }
  }
  if (page_contract(&page, catalog, kind) != 0 ||
      page_str(&page, "<p>The rules for a kind's contract are in the <a href=\"") != 0 ||
      page_esc(&page, kDocsUrl, strlen(kDocsUrl)) != 0 ||
      page_str(&page, "\" rel=\"noopener noreferrer\">template documentation</a>.</p>") != 0) {
    goto fail;
  }
  free(stored_body);
  gcu_allocator_free(alloc, check);
  gcu_allocator_free(alloc, save);
  return take_page(&page, out);
fail:
  free(stored_body);
  gcu_allocator_free(alloc, check);
  gcu_allocator_free(alloc, save);
  page_free(&page);
  return -1;
}

/* Handlers ------------------------------------------------------------- */

/* The issues of @p report as HTML, malloc'd; none is the empty string. */
static int issues_html(const GDEFWEB_Allocator * alloc, const GDEFCORE_Template_Report * report,
    char ** html, size_t * html_n) {
  struct Page page;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_issues(&page, report) != 0) {
    page_free(&page);
    return -1;
  }
  *html = dup_bytes(page.p == NULL ? "" : page.p, page.n);
  *html_n = page.n;
  page_free(&page);
  return *html == NULL ? -1 : 0;
}

static int issues_text_html(const GDEFWEB_Allocator * alloc, const char * text, size_t n,
    char ** html, size_t * html_n) {
  struct Page page;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_issue_text(&page, text, n) != 0) {
    page_free(&page);
    return -1;
  }
  *html = dup_bytes(page.p == NULL ? "" : page.p, page.n);
  *html_n = page.n;
  page_free(&page);
  return *html == NULL ? -1 : 0;
}

/*
 * The template @p name, if this principal may edit it. 0 yes, with @p id and
 * the stored @p kind; 1 for anything else: it does not exist, it has no kind,
 * or the principal may not edit it. The three are one answer.
 */
static int tpl_open(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * name, size_t name_len, char id[GDEFCORE_PUBLIC_ID_LEN + 1], char * kind,
    size_t kind_cap) {
  int may = 0;
  int found = find_by_name(catalog, GDEFCORE_TEMPLATE_TYPE, name, name_len, id);
  if (found < 0) {
    return -1;
  }
  if (found != 0) {
    return 1;
  }
  if (field_copy(catalog, id, "kind", kind, kind_cap) != 0 || kind[0] == '\0') {
    return 1;
  }
  if (gdefcore_template_can_edit(catalog, request, id, NULL, 0, &may) != GDEFCORE_OK) {
    return -1;
  }
  return may ? 0 : 1;
}

static int tpl_edit_get(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * name,
    size_t name_len, const char * nonce, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char kind[TPL_KIND_MAX];
  char * body = NULL;
  size_t body_n = 0;
  const struct Flash * flash = NULL;
  const char * show_body;
  size_t show_n;
  const char * show_kind = kind;
  int rc;
  int opened = tpl_open(catalog, request, name, name_len, id, kind, sizeof kind);
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("GET", out);
  }
  if (stored_text(catalog, id, "body", &body, &body_n) != 0) {
    return -1;
  }
  if (nonce != NULL) {
    flash = flash_get(catalog, (const unsigned char *)session, session_len, id, nonce);
  }
  show_body = body;
  show_n = body_n;
  if (flash != NULL && flash->body != NULL) {
    show_body = flash->body;
    show_n = flash->body_len;
  }
  if (flash != NULL && flash->kind[0] != '\0') {
    int may_move = 0;
    if (gdefcore_template_can_edit(catalog, request, id, flash->kind,
            strlen(flash->kind), &may_move) == GDEFCORE_OK &&
        may_move) {
      show_kind = flash->kind;
    }
  }
  rc = tpl_edit_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
      id, name, show_body, show_n, show_kind, flash, out);
  free(body);
  return rc;
}

/* Which of the form's buttons was pressed. */
enum { A_NONE = 0, A_VALIDATE, A_PREVIEW, A_SAVE };

static int tpl_flash_redirect(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    const char * session, size_t session_len, const char * id, const char * name,
    const char * tail, int notice, const char * kind, const char * body, size_t body_n,
    const char * issues, size_t issues_n, const char * output, size_t output_n,
    int has_output, struct GDEFWEB_Admin_Reply * out) {
  char nonce[32];
  (void)alloc;
  if (flash_put(catalog, (const unsigned char *)session, session_len, id, notice, kind, body,
          body_n, issues, issues_n, output, output_n, has_output, nonce, sizeof nonce) != 0) {
    return tpl_location(out, name, strlen(name), tail, NULL);
  }
  return tpl_location(out, name, strlen(name), tail, nonce);
}

/*
 * The write: stage the template as a changeset of the principal, ask the core
 * whether the principal may, preview it, and apply it with the confirmation
 * the core requires of a high-risk change. The page's own typed confirmation
 * was already checked.
 */
static int tpl_save(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, const char * id, const char * name, const char * kind,
    const char * body, size_t body_n, int * notice, char ** issues, size_t * issues_n) {
  GDEFCORE_Changeset * cs = NULL;
  GDEFCORE_Template_Report * report = NULL;
  GDEFCORE_Value values[3];
  const char * slots[3];
  size_t lens[3];
  const char * who;
  size_t who_n = 0;
  int risk = 0;
  int rc;
  *notice = N_REFUSED;
  slots[0] = name;
  lens[0] = strlen(name);
  slots[1] = body;
  lens[1] = body_n;
  slots[2] = kind;
  lens[2] = strlen(kind);
  one_value(&values[0], "name", name, &slots[0], &lens[0]);
  memset(&values[1], 0, sizeof values[1]);
  values[1].size = sizeof values[1];
  values[1].field = "body";
  values[1].field_len = 4;
  values[1].texts = &slots[1];
  values[1].text_lens = &lens[1];
  values[1].count = 1;
  one_value(&values[2], "kind", kind, &slots[2], &lens[2]);
  who = gdefcore_request_principal(request, &who_n);
  if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_entity_save(cs, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, values, 3);
  if (rc != GDEFCORE_OK) {
    gdefcore_changeset_destroy(cs);
    *notice = N_INVALID;
    return 0;
  }
  /* Who may: the permission of the kind (both, for a move) and every module. */
  rc = gdefcore_template_authorize(cs, request);
  if (rc != GDEFCORE_OK) {
    gdefcore_changeset_destroy(cs);
    *notice = N_REFUSED;
    return rc == GDEFCORE_ERR_OOM ? -1 : 0;
  }
  /* Preview: what the template would be held to, and how risky the change is. */
  rc = gdefcore_template_preview(cs, &report, &risk);
  if (rc == GDEFCORE_OK) {
    size_t i;
    int blocking = 0;
    for (i = 0; i < gdefcore_template_report_count(report); i++) {
      GDEFCORE_Template_Issue issue;
      if (gdefcore_template_report_at(report, i, &issue) == GDEFCORE_OK &&
          issue.reason != GDEFCORE_ISSUE_BREAKS) {
        blocking = 1;
      }
    }
    if (blocking) {
      int h = issues_html(alloc, report, issues, issues_n);
      gdefcore_template_report_free(report);
      gdefcore_changeset_destroy(cs);
      *notice = N_INVALID;
      return h;
    }
  }
  gdefcore_template_report_free(report);
  (void)risk;
  /* A template edit is high risk: the core's confirmation is the type. */
  rc = gdefcore_changeset_apply(cs, GDEFCORE_TEMPLATE_TYPE,
      sizeof GDEFCORE_TEMPLATE_TYPE - 1);
  if (rc == GDEFCORE_OK) {
    *notice = N_SAVED;
  } else {
    size_t cn = 0;
    const char * conflict = gdefcore_changeset_conflict(cs, &cn);
    size_t rn = 0;
    const char * refusal = gdefcore_changeset_refusal(cs, &rn);
    if (conflict != NULL && cn > 0) {
      *notice = N_CONFLICT;
    } else if (rc == GDEFCORE_ERR_FORMAT && refusal != NULL && rn > 0) {
      *notice = N_INVALID;
      if (issues_text_html(alloc, refusal, rn, issues, issues_n) != 0) {
        gdefcore_changeset_destroy(cs);
        return -1;
      }
    } else if (rc == GDEFCORE_ERR_OOM) {
      gdefcore_changeset_destroy(cs);
      return -1;
    } else {
      *notice = N_REFUSED;
    }
  }
  gdefcore_changeset_destroy(cs);
  return 0;
}

static int tpl_edit_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * name,
    size_t name_len, const char * post, size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char stored_kind[TPL_KIND_MAX];
  char kind[TPL_KIND_MAX];
  char * action = NULL;
  size_t action_n = 0;
  char * token = NULL;
  size_t token_n = 0;
  char * posted_kind = NULL;
  size_t posted_kind_n = 0;
  char * confirm = NULL;
  size_t confirm_n = 0;
  char * body = NULL;
  size_t body_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  int act = A_NONE;
  int rc = -1;
  int opened = tpl_open(catalog, request, name, name_len, id, stored_kind, sizeof stored_kind);
  int notice = N_REFUSED;
  char * issues = NULL;
  size_t issues_n = 0;
  char * output = NULL;
  size_t output_n = 0;
  int has_output = 0;
  GDEFCORE_Template_Report * report = NULL;
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("POST", out);
  }
  if (form_field_alloc(post, post_n, "do", 0, &action, &action_n) == 0) {
    if (action_n == 8 && memcmp(action, "validate", 8) == 0) {
      act = A_VALIDATE;
    } else if (action_n == 7 && memcmp(action, "preview", 7) == 0) {
      act = A_PREVIEW;
    } else if (action_n == 4 && memcmp(action, "save", 4) == 0) {
      act = A_SAVE;
    }
  }
  free(action);
  if (act == A_NONE) {
    return tpl_location(out, name, name_len, "", NULL);
  }
  /* The text the editor sent, kept so that a refusal can give it back. */
  if (form_field_alloc(post, post_n, "body", 0, &body, &body_n) != 0) {
    body = dup_bytes("", 0);
    body_n = 0;
    if (body == NULL) {
      return -1;
    }
  }
  body_n = normalize_newlines(body, body_n);
  strcpy(kind, stored_kind);
  if (form_field_alloc(post, post_n, "kind", 0, &posted_kind, &posted_kind_n) == 0 &&
      posted_kind_n > 0) {
    if (posted_kind_n >= sizeof kind) {
      posted_kind[0] = '\0';
    }
    strncpy(kind, posted_kind, sizeof kind - 1);
    kind[sizeof kind - 1] = '\0';
  }
  free(posted_kind);
  /* The form must be ours: this session's, this template's, for this action. */
  if (form_field_alloc(post, post_n, "token", act == A_SAVE ? 1 : 0, &token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session, session_len,
          (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) != 0 ||
      strlen(act == A_SAVE ? "tpl-save" : "tpl-check") != form_len ||
      memcmp(form_id, act == A_SAVE ? "tpl-save" : "tpl-check", form_len) != 0) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_FORM,
        stored_kind, body, body_n, NULL, 0, NULL, 0, 0, out);
    goto done;
  }
  /* A NUL ends a C string: the validator would read less than would be stored. */
  if (memchr(body, '\0', body_n) != NULL) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_TEXT,
        stored_kind, NULL, 0, NULL, 0, NULL, 0, 0, out);
    goto done;
  }
  if (body_n > TPL_BODY_MAX) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_TOO_LARGE,
        stored_kind, NULL, 0, NULL, 0, NULL, 0, 0, out);
    goto done;
  }
  /* Stale: the template changed after this form was signed. */
  if (!base_matches(catalog, id, base)) {
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_CONFLICT,
        kind, body, body_n, NULL, 0, NULL, 0, 0, out);
    goto done;
  }
  /* A kind this template may not be left as, by this principal, is a kind
   * that is not there: the core's decision about the template, which the
   * save asks again. */
  {
    int may_leave = 0;
    if (gdefcore_template_can_edit(catalog, request, id, kind, strlen(kind),
            &may_leave) != GDEFCORE_OK) {
      goto done;
    }
    if (!may_leave) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", N_BAD_KIND,
          stored_kind, body, body_n, NULL, 0, NULL, 0, 0, out);
      goto done;
    }
  }
  if (act == A_SAVE) {
    if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
        confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
      rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
          N_WRONG_CONFIRM, kind, body, body_n, NULL, 0, NULL, 0, 0, out);
      goto done;
    }
    if (tpl_save(alloc, catalog, request, id, name, kind, body, body_n, &notice, &issues,
            &issues_n) != 0) {
      goto done;
    }
    rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "", notice, kind,
        notice == N_SAVED ? NULL : body, notice == N_SAVED ? 0 : body_n, issues, issues_n, NULL,
        0, 0, out);
    goto done;
  }
  /* Validate and preview write nothing. */
  if (gdefcore_template_validate(catalog, NULL, kind, strlen(kind), body, body_n, &report) !=
      GDEFCORE_OK) {
    goto done;
  }
  if (issues_html(alloc, report, &issues, &issues_n) != 0) {
    goto done;
  }
  if (act == A_PREVIEW) {
    /*
     * Rendered under an overlay of its own. A fragment under any overlay but
     * `live` is not offered to a shared cache (gdefcore_access_shared_cache()),
     * so what an editor is trying out is never served to anyone else.
     */
    static const char kOverlay[] = "preview";
    int shareable = 0;
    char * bytes = NULL;
    size_t bytes_n = 0;
    GDEFCORE_Result r = gdefcore_render_sample(catalog, request, kind, strlen(kind), body, body_n,
        kOverlay, sizeof kOverlay - 1, GDEFCORE_RENDER_PAGE_FUEL, &bytes, &bytes_n, &shareable);
    if (r == GDEFCORE_OK) {
      output = dup_bytes(bytes, bytes_n);
      output_n = bytes_n;
      has_output = 1;
      gdefcore_text_free(catalog, bytes);
      if (output == NULL) {
        goto done;
      }
    } else if (r == GDEFCORE_ERR_OOM) {
      goto done;
    }
  }
  rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "",
      act == A_PREVIEW ? N_PREVIEWED : N_CHECKED, kind, body, body_n, issues, issues_n, output,
      output_n, has_output, out);
done:
  gdefcore_template_report_free(report);
  free(issues);
  if (output != NULL) {
    wipe(output, output_n);
  }
  free(output);
  free(confirm);
  if (token != NULL) {
    wipe(token, token_n);
  }
  free(token);
  wipe(raw, sizeof raw);
  if (body != NULL) {
    wipe(body, body_n);
  }
  free(body);
  return rc;
}

/* History ---------------------------------------------------------------- */

static int tpl_history_get(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * name,
    size_t name_len, const char * nonce, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char kind[TPL_KIND_MAX];
  struct Page page;
  const struct Flash * flash = NULL;
  size_t count = 0;
  size_t i;
  char * token = NULL;
  int opened = tpl_open(catalog, request, name, name_len, id, kind, sizeof kind);
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("GET", out);
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (nonce != NULL) {
    flash = flash_get(catalog, (const unsigned char *)session, session_len, id, nonce);
  }
  if (gdefcore_entity_revision_count(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &count) !=
      GDEFCORE_OK) {
    return -1;
  }
  if (page_begin(&page, "History") != 0 || page_str(&page, "<h1>History of ") != 0 ||
      page_esc(&page, name, name_len) != 0 ||
      page_str(&page, "</h1><p><a href=\"/admin/templates\">All templates</a> <a "
                      "href=\"/admin/templates/") != 0 ||
      page_seg(&page, name, name_len) != 0 || page_str(&page, "\">Edit</a></p>") != 0) {
    goto fail;
  }
  if (flash != NULL && page_notice(&page, flash->notice) != 0) {
    goto fail;
  }
  if (page_str(&page, "<ol class=\"history\">") != 0) {
    goto fail;
  }
  /* Newest first. The sequence numbers stay on the server (AD-18 item 8). */
  for (i = count; i > 0; i--) {
    uint64_t seq = 0;
    char * actor = NULL;
    size_t actor_n = 0;
    if (gdefcore_entity_revision_at(catalog, id, GDEFCORE_PUBLIC_ID_LEN, i - 1, &seq, &actor,
            &actor_n) != GDEFCORE_OK) {
      goto fail;
    }
    (void)seq;
    if (page_str(&page, "<li>") != 0 ||
        page_str(&page, i == count ? "The current version, by " : "By ") != 0) {
      gdefcore_text_free(catalog, actor);
      goto fail;
    }
    if (actor != NULL && actor_n > 0) {
      if (page_esc(&page, actor, actor_n) != 0) {
        gdefcore_text_free(catalog, actor);
        goto fail;
      }
    } else if (page_str(&page, "the installation") != 0) {
      gdefcore_text_free(catalog, actor);
      goto fail;
    }
    gdefcore_text_free(catalog, actor);
    if (page_str(&page, "</li>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</ol>") != 0) {
    goto fail;
  }
  if (count >= 2) {
    if (tpl_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
            "tpl-revert", id, name, kind, &token) != 0) {
      goto fail;
    }
    if (page_str(&page, "<form method=\"post\" action=\"/admin/templates/") != 0 ||
        page_seg(&page, name, name_len) != 0 || page_str(&page, "/history\">") != 0 ||
        page_token(&page, token) != 0 ||
        page_str(&page, "<p><label>To restore the version before the current one, type this "
                        "template's name to confirm: <input name=\"confirm\" "
                        "autocomplete=\"off\" value=\"\"></label></p><p><button type=\"submit\" "
                        "name=\"do\" value=\"revert\">Revert the latest change</button></p>"
                        "</form>") != 0) {
      goto fail;
    }
  } else if (page_str(&page, "<p>There is no earlier version to restore.</p>") != 0) {
    goto fail;
  }
  gcu_allocator_free(alloc, token);
  return take_page(&page, out);
fail:
  gcu_allocator_free(alloc, token);
  page_free(&page);
  return -1;
}

static int tpl_history_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * name,
    size_t name_len, const char * post, size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char kind[TPL_KIND_MAX];
  char * token = NULL;
  size_t token_n = 0;
  char * confirm = NULL;
  size_t confirm_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  int notice = N_NOT_REVERTED;
  int rc = -1;
  int opened = tpl_open(catalog, request, name, name_len, id, kind, sizeof kind);
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("POST", out);
  }
  if (form_field_alloc(post, post_n, "token", 0, &token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session, session_len,
          (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) != 0 || form_len != 10 ||
      memcmp(form_id, "tpl-revert", 10) != 0 || !base_matches(catalog, id, base)) {
    notice = N_NOT_REVERTED;
  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
    notice = N_WRONG_CONFIRM;
  } else {
    GDEFCORE_Changeset * cs = NULL;
    const char * who;
    size_t who_n = 0;
    GDEFCORE_Result r;
    who = gdefcore_request_principal(request, &who_n);
    r = gdefcore_changeset_revert(catalog, id, GDEFCORE_PUBLIC_ID_LEN, who, who_n, &cs);
    if (r == GDEFCORE_OK) {
      gdefcore_changeset_set_request(cs, request);
      r = gdefcore_changeset_apply(cs, GDEFCORE_TEMPLATE_TYPE,
          sizeof GDEFCORE_TEMPLATE_TYPE - 1);
      gdefcore_changeset_destroy(cs);
    }
    notice = r == GDEFCORE_OK ? N_REVERTED : N_NOT_REVERTED;
    if (r == GDEFCORE_ERR_OOM) {
      goto done;
    }
  }
  rc = tpl_flash_redirect(alloc, catalog, session, session_len, id, name, "/history", notice,
      NULL, NULL, 0, NULL, 0, NULL, 0, 0, out);
done:
  free(confirm);
  if (token != NULL) {
    wipe(token, token_n);
  }
  free(token);
  wipe(raw, sizeof raw);
  return rc;
}

/* Routing ---------------------------------------------------------------- */

static int starts_with(const char * s, const char * prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

/*
 * /admin/templates                       the list
 * /admin/templates/<name>                edit (GET), act (POST)
 * /admin/templates/<name>/r/<nonce>      edit, with what the redirect had to say
 * /admin/templates/<name>/history        history (GET), revert (POST)
 * /admin/templates/<name>/history/r/<n>  history, with what the redirect said
 */
static int tpl_route(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * method,
    const char * path, const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  static const char kBase[] = "/admin/templates";
  const char * rest = path + sizeof kBase - 1;
  const char * slash;
  const char * tail;
  const char * nonce = NULL;
  char name[TPL_NAME_MAX];
  size_t name_len = 0;
  int is_get = strcmp(method, "GET") == 0;
  int is_post = strcmp(method, "POST") == 0;
  int history = 0;
  if (*rest == '\0') {
    return is_get ? tpl_list_page(alloc, catalog, request, out) : unknown_admin(method, out);
  }
  if (*rest != '/' || rest[1] == '\0') {
    return unknown_admin(method, out);
  }
  rest += 1;
  slash = strchr(rest, '/');
  tail = slash == NULL ? rest + strlen(rest) : slash;
  if (seg_decode(rest, (size_t)(tail - rest), name, sizeof name, &name_len) != 0) {
    return unknown_admin(method, out);
  }
  if (*tail == '\0') {
    /* the edit page itself */
  } else if (starts_with(tail, "/r/") && tail[3] != '\0' && strchr(tail + 3, '/') == NULL) {
    nonce = tail + 3;
  } else if (strcmp(tail, "/history") == 0) {
    history = 1;
  } else if (starts_with(tail, "/history/r/") && tail[11] != '\0' &&
             strchr(tail + 11, '/') == NULL) {
    history = 1;
    nonce = tail + 11;
  } else {
    return unknown_admin(method, out);
  }
  if (history) {
    if (is_get) {
      return tpl_history_get(alloc, catalog, request, keyring, root, root_len, session,
          session_len, name, name_len, nonce, out);
    }
    if (is_post && nonce == NULL) {
      return tpl_history_post(alloc, catalog, request, keyring, root, root_len, session,
          session_len, name, name_len, body, body_len, out);
    }
    return unknown_admin(method, out);
  }
  if (is_get) {
    return tpl_edit_get(alloc, catalog, request, keyring, root, root_len, session, session_len,
        name, name_len, nonce, out);
  }
  if (is_post && nonce == NULL) {
    return tpl_edit_post(alloc, catalog, request, keyring, root, root_len, session,
        session_len, name, name_len, body, body_len, out);
  }
  return unknown_admin(method, out);
}

static int unknown_admin(const char * method, struct GDEFWEB_Admin_Reply * out) {
  if (strcmp(method, "GET") == 0) {
    out->status = 200;
    out->reason = "OK";
    out->type = "text/html; charset=utf-8";
  } else {
    out->status = 404;
    out->reason = "Not Found";
    out->type = "text/plain; charset=utf-8";
  }
  out->body = NULL;
  out->len = 0;
  return 0;
}

int gdefweb_admin_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * method, const char * path, const char * cookie,
    const char * body, size_t body_len, const char * ctype,
    struct GDEFWEB_Admin_Reply * out) {
  unsigned char raw[768];
  size_t raw_n = 0;
  int is_get;
  int is_post;
  int rc;
  if (alloc == NULL || catalog == NULL || request == NULL || out == NULL ||
      method == NULL || path == NULL) {
    return -1;
  }
  memset(out, 0, sizeof *out);
  is_get = strcmp(method, "GET") == 0;
  is_post = strcmp(method, "POST") == 0;
  if (!is_get && !is_post) {
    return unknown_admin(method, out);
  }
  if (is_post &&
      (ctype == NULL || strncmp(ctype, "application/x-www-form-urlencoded", 33) != 0)) {
    out->status = 400;
    out->reason = "Bad Request";
    out->type = "text/plain; charset=utf-8";
    out->body = NULL;
    out->len = 0;
    return 0;
  }
  if (session_raw(cookie, raw, sizeof raw, &raw_n) != 0) {
    return -1;
  }
  if (strcmp(path, "/admin") == 0) {
    rc = is_get ? home_page(alloc, out) : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/more") == 0) {
    rc = more_page(alloc, out);
  } else if (strcmp(path, "/admin/access") == 0) {
    rc = is_get ? access_page(alloc, catalog, request, out)
                : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/post") == 0) {
    rc = is_get ? post_page(alloc, catalog, request, keyring, root, root_len,
                            (const char *)raw, raw_n, 0, out)
                : post_write(alloc, catalog, request, keyring, root, root_len,
                      (const char *)raw, raw_n, body, body_len, out);
  } else if (strcmp(path, "/admin/post/saved") == 0) {
    rc = is_get ? post_page(alloc, catalog, request, keyring, root, root_len,
                            (const char *)raw, raw_n, 1, out)
                : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/front") == 0) {
    rc = is_get ? front_page(alloc, catalog, request, keyring, root, root_len,
                             (const char *)raw, raw_n, out)
                : front_write(alloc, catalog, request, keyring, root, root_len,
                      (const char *)raw, raw_n, body, body_len, out);
  } else if (strcmp(path, "/admin/undo") == 0) {
    rc = is_get ? undo_page(alloc, catalog, request, keyring, root, root_len,
                            (const char *)raw, raw_n, out)
                : undo_write(alloc, catalog, request, keyring, root, root_len,
                      (const char *)raw, raw_n, body, body_len, out);
  } else if (strcmp(path, "/admin/templates") == 0 ||
             starts_with(path, "/admin/templates/")) {
    rc = tpl_route(alloc, catalog, request, keyring, root, root_len, (const char *)raw, raw_n,
        method, path, body, body_len, out);
  } else {
    rc = unknown_admin(method, out);
  }
  wipe(raw, sizeof raw);
  return rc;
}
