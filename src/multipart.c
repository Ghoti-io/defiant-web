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

/* A strict multipart/form-data reader (multipart.h). */

#include "multipart.h"

#include <string.h>

static int ci_eq(const char * a, size_t n, const char * lower) {
  size_t i;
  if (strlen(lower) != n) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    char c = a[i];
    if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
    if (c != lower[i]) {
      return 0;
    }
  }
  return 1;
}

/* RFC 2046 bchars, without the space the grammar allows inside but not at the
 * end: the space is refused outright, which no real client sends. */
static int bchar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         c == '\'' || c == '(' || c == ')' || c == '+' || c == '_' || c == ',' || c == '-' ||
         c == '.' || c == '/' || c == ':' || c == '=' || c == '?';
}

/* The boundary of "multipart/form-data; boundary=...". Copied; 0 on success. */
static int boundary_of(const char * ct, char out[GDEFWEB_MP_MAX_BOUNDARY + 1], size_t * out_n) {
  const char * p;
  size_t n = 0;
  int quoted = 0;
  if (ct == NULL || strlen(ct) < 19 || !ci_eq(ct, 19, "multipart/form-data")) {
    return -1;
  }
  p = ct + 19;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p++ != ';') {
    return -1;
  }
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (strlen(p) < 9 || !ci_eq(p, 8, "boundary") || p[8] != '=') {
    return -1;
  }
  p += 9;
  if (*p == '"') {
    quoted = 1;
    p++;
  }
  while (*p != '\0' && n <= GDEFWEB_MP_MAX_BOUNDARY && ((quoted && *p != '"') || (!quoted && *p != ';' && *p != ' '))) {
    if (!bchar(*p)) {
      return -1;
    }
    out[n++] = *p++;
  }
  if (quoted) {
    if (*p++ != '"') {
      return -1;
    }
  }
  /* Nothing may follow but a parameter we do not read. */
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p != '\0' && *p != ';') {
    return -1;
  }
  if (n == 0 || n > GDEFWEB_MP_MAX_BOUNDARY) {
    return -1;
  }
  out[n] = '\0';
  *out_n = n;
  return 0;
}

/* The next occurrence of @p needle (n bytes) in [p, end), or NULL. */
static const char * find(const char * p, const char * end, const char * needle, size_t n) {
  while (p + n <= end) {
    const char * q = (const char *)memchr(p, needle[0], (size_t)(end - p) - n + 1);
    if (q == NULL) {
      return NULL;
    }
    if (memcmp(q, needle, n) == 0) {
      return q;
    }
    p = q + 1;
  }
  return NULL;
}

/* A header value's parameter "key=\"value\"": the value as a span, or -1 when it
 * is there and not a plain quoted string. 0 and a NULL span when it is absent. */
static int param(const char * v, size_t n, const char * key, const char ** out, size_t * out_n) {
  size_t k = strlen(key);
  size_t i;
  *out = NULL;
  *out_n = 0;
  for (i = 0; i + k + 2 <= n; i++) {
    if ((i == 0 || v[i - 1] == ' ' || v[i - 1] == ';') && memcmp(v + i, key, k) == 0 &&
        v[i + k] == '=') {
      size_t s = i + k + 1;
      size_t e;
      if (s >= n || v[s] != '"') {
        return -1;
      }
      s++;
      for (e = s; e < n && v[e] != '"'; e++) {
        unsigned char c = (unsigned char)v[e];
        if (c < 0x20 || c == 0x7f || c == '\\') {
          return -1;
        }
      }
      if (e >= n) {
        return -1;
      }
      *out = v + s;
      *out_n = e - s;
      return 0;
    }
  }
  return 0;
}

/* One part's head [h, h+n) into the part. 0 or -1. */
static int head(const char * h, size_t n, struct GDEFWEB_Mp_Part * part) {
  size_t pos = 0;
  int have_disp = 0;
  while (pos < n) {
    const char * line = h + pos;
    const char * eol = find(line, h + n, "\r\n", 2);
    size_t ln;
    const char * colon;
    size_t nlen;
    const char * val;
    size_t vlen;
    if (eol == NULL) {
      eol = h + n;
    }
    ln = (size_t)(eol - line);
    if (ln == 0) {
      return -1;
    }
    colon = (const char *)memchr(line, ':', ln);
    if (colon == NULL || colon == line) {
      return -1;
    }
    nlen = (size_t)(colon - line);
    val = colon + 1;
    vlen = ln - nlen - 1;
    while (vlen > 0 && (*val == ' ' || *val == '\t')) {
      val++;
      vlen--;
    }
    {
      size_t i;
      for (i = 0; i < ln; i++) {
        if ((unsigned char)line[i] < 0x20 && line[i] != '\t') {
          return -1;
        }
      }
    }
    if (ci_eq(line, nlen, "content-disposition")) {
      if (have_disp || vlen < 10 || !ci_eq(val, 10, "form-data;")) {
        return -1;
      }
      have_disp = 1;
      if (param(val, vlen, "name", &part->name, &part->name_len) != 0 ||
          param(val, vlen, "filename", &part->filename, &part->filename_len) != 0) {
        return -1;
      }
    } else if (ci_eq(line, nlen, "content-type")) {
      if (part->ctype != NULL) {
        return -1;
      }
      part->ctype = val;
      part->ctype_len = vlen;
    }
    pos = (size_t)(eol - h) + (eol == h + n ? 0 : 2);
    if (eol == h + n) {
      break;
    }
  }
  if (!have_disp || part->name == NULL || part->name_len == 0 ||
      part->name_len > GDEFWEB_MP_MAX_NAME ||
      (part->filename != NULL && part->filename_len > GDEFWEB_MP_MAX_FILENAME)) {
    return -1;
  }
  return 0;
}

int gdefweb_mp_parse(const char * content_type, const char * body, size_t len,
    struct GDEFWEB_Mp * out) {
  char delim[GDEFWEB_MP_MAX_BOUNDARY + 8];
  char boundary[GDEFWEB_MP_MAX_BOUNDARY + 1];
  size_t bn = 0;
  size_t dn;
  const char * p;
  const char * end;
  if (out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof *out);
  if (body == NULL || boundary_of(content_type, boundary, &bn) != 0) {
    return -1;
  }
  end = body + len;
  /* "--boundary" opens the body: no text before it. */
  dn = bn + 2;
  memcpy(delim, "--", 2);
  memcpy(delim + 2, boundary, bn);
  if (len < dn || memcmp(body, delim, dn) != 0) {
    return -1;
  }
  p = body + dn;
  for (;;) {
    struct GDEFWEB_Mp_Part part;
    const char * hend;
    const char * next;
    char sep[GDEFWEB_MP_MAX_BOUNDARY + 8];
    size_t sn;
    if (p + 2 <= end && p[0] == '-' && p[1] == '-') {
      /* The closing boundary: only an optional line end may follow. */
      p += 2;
      if (p == end || (p + 2 == end && p[0] == '\r' && p[1] == '\n')) {
        return out->count > 0 ? 0 : (memset(out, 0, sizeof *out), -1);
      }
      break;
    }
    if (p + 2 > end || p[0] != '\r' || p[1] != '\n') {
      break;
    }
    p += 2;
    if (out->count == GDEFWEB_MP_MAX_PARTS) {
      break;
    }
    hend = find(p, end, "\r\n\r\n", 4);
    if (hend == NULL || (size_t)(hend - p) > GDEFWEB_MP_MAX_HEAD) {
      break;
    }
    memset(&part, 0, sizeof part);
    if (head(p, (size_t)(hend - p), &part) != 0) {
      break;
    }
    p = hend + 4;
    memcpy(sep, "\r\n", 2);
    memcpy(sep + 2, delim, dn);
    sn = dn + 2;
    next = find(p, end, sep, sn);
    if (next == NULL) {
      break;
    }
    part.data = (const uint8_t *)p;
    part.len = (size_t)(next - p);
    out->parts[out->count++] = part;
    p = next + sn;
  }
  memset(out, 0, sizeof *out);
  return -1;
}

const struct GDEFWEB_Mp_Part * gdefweb_mp_find(const struct GDEFWEB_Mp * mp, const char * name) {
  const struct GDEFWEB_Mp_Part * found = NULL;
  size_t i;
  size_t n = strlen(name);
  if (mp == NULL) {
    return NULL;
  }
  for (i = 0; i < mp->count; i++) {
    if (mp->parts[i].name_len == n && memcmp(mp->parts[i].name, name, n) == 0) {
      if (found != NULL) {
        return NULL;
      }
      found = &mp->parts[i];
    }
  }
  return found;
}
