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
#include <ghoti.io/security/sha256.h>

#include <stdio.h>
#include <string.h>

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

static int field_copy(GDEFCORE_Catalog * catalog, const char * id, const char * field,
    char * out, size_t cap) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  GDEFCORE_Result rc = gdefcore_entity_values(catalog, NULL, GDEFCORE_VIEW_PUBLISHED,
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

static int base_matches(GDEFCORE_Catalog * catalog, const char * id, uint64_t base) {
  uint64_t current = 0;
  if (gdefcore_entity_revision(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &current) !=
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
      page_str(&page, "<h2>Server</h2><p>This server is running.</p>") != 0) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
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
      if (page_str(&page, "<div draggable=\"true\" data-pane=\"1\">") != 0 ||
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
    char seq_text[32];
    int n;
    if (gdefcore_entity_revision_at(catalog, id, GDEFCORE_PUBLIC_ID_LEN, i, &seq,
            &actor, &actor_n) != GDEFCORE_OK) {
      return -1;
    }
    n = snprintf(seq_text, sizeof seq_text, "%llu", (unsigned long long)seq);
    if (n <= 0 || page_str(page, "<li>") != 0 ||
        page_esc(page, seq_text, (size_t)n) != 0) {
      gdefcore_text_free(catalog, actor);
      return -1;
    }
    if (actor != NULL && actor_n > 0) {
      if (page_str(page, " ") != 0 || page_esc(page, actor, actor_n) != 0) {
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
  } else {
    rc = unknown_admin(method, out);
  }
  wipe(raw, sizeof raw);
  return rc;
}
