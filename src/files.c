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

/* The upload page of the admin and the public address of a kept file (files.h). */

#include "adminkit.h"
#include "files.h"
#include "multipart.h"
#include "nav.h"

#include <ghoti.io/defiant-web/uploads.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/message.h>
#include <ghoti.io/defiant-core/upload.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The binding of uploads.h. It is set before the server accepts. */
static const GDEFCORE_Blob_Store * g_blobs = NULL;
static const GDEFCORE_Upload_Policy * g_policy = NULL;

#define FILES_BASE "/admin/files"
#define FILES_FORM "upload-file"
/* A form's other parts, the boundaries and the part heads. */
#define BODY_ROOM (16u * 1024u)
#define LIST_MAX 200

GDEFWEB_Result gdefweb_uploads_bind(const GDEFCORE_Blob_Store * blobs,
    const GDEFCORE_Upload_Policy * policy) {
  if (blobs == NULL && policy == NULL) {
    g_blobs = NULL;
    g_policy = NULL;
    return GDEFWEB_OK;
  }
  if (blobs == NULL || policy == NULL || blobs->size != sizeof *blobs ||
      policy->size != sizeof *policy || blobs->put == NULL || blobs->get == NULL ||
      blobs->remove == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  g_blobs = blobs;
  g_policy = policy;
  return GDEFWEB_OK;
}

int gdefweb_files_enabled(void) {
  return g_blobs != NULL && g_policy != NULL;
}

size_t gdefweb_files_body_cap(void) {
  uint64_t max;
  if (!gdefweb_files_enabled()) {
    return 0;
  }
  max = g_policy->max_bytes == 0 || g_policy->max_bytes > GDEFCORE_UPLOAD_HARD_MAX
            ? GDEFCORE_UPLOAD_HARD_MAX
            : g_policy->max_bytes;
  return (size_t)max + BODY_ROOM;
}

int gdefweb_files_path(const char * path) {
  return path != NULL && strcmp(path, FILES_BASE) == 0;
}

/* ---- the admin page --------------------------------------------------------- */

static int principal_of(GDEFCORE_Request * request, char out[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  size_t n = 0;
  const char * p = gdefcore_request_principal(request, &n);
  if (p == NULL || n != GDEFCORE_PUBLIC_ID_LEN) {
    return 0;
  }
  memcpy(out, p, n);
  out[n] = '\0';
  return 1;
}

static int first_value(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, const char * id,
    const char * field, char * out, size_t cap) {
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  int locked = 0;
  int ok = 0;
  out[0] = '\0';
  if (gdefcore_access_read(catalog, request, id, GDEFCORE_PUBLIC_ID_LEN, "en", 2, field,
          strlen(field), &texts, &lens, &count, &locked) != GDEFCORE_OK) {
    return 0;
  }
  if (count > 0 && texts != NULL && texts[0] != NULL && lens[0] < cap) {
    memcpy(out, texts[0], lens[0]);
    out[lens[0]] = '\0';
    ok = 1;
  }
  gdefcore_values_free(catalog, texts, lens, count);
  return ok;
}

static int draw_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * token = NULL;
  GDEFCORE_Value filler[1];
  const char * slot[1];
  size_t lens[1];
  char ** ids = NULL;
  size_t n = 0;
  size_t i;
  size_t shown = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_begin(&page, "Files") != 0) {
    return -1;
  }
  if (!gdefweb_files_enabled()) {
    if (page_str(&page, "<p>Uploads are not turned on for this site.</p>") != 0) {
      page_free(&page);
      return -1;
    }
    return take_page(&page, out);
  }
  if (!principal_of(request, who)) {
    page_free(&page);
    return -1;
  }
  one_value(&filler[0], "name", "", &slot[0], &lens[0]);
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len, FILES_FORM,
          who, filler, 0, &token) != 0) {
    page_free(&page);
    return -1;
  }
  if (page_str(&page, "<form method=\"post\" action=\"" FILES_BASE "\" "
                      "enctype=\"multipart/form-data\"><input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(&page, token, strlen(token)) != 0 ||
      page_str(&page, "\"><p><label>File <input type=\"file\" name=\"file\" required></label></p>"
                      "<p><button type=\"submit\">Upload</button></p></form>"
                      "<h2>Your files</h2>") != 0) {
    gcu_allocator_free(alloc, token);
    page_free(&page);
    return -1;
  }
  gcu_allocator_free(alloc, token);
  if (gdefcore_entity_ids(catalog, GDEFCORE_UPLOAD_TYPE, sizeof GDEFCORE_UPLOAD_TYPE - 1, &ids, &n) ==
          GDEFCORE_OK) {
    if (page_str(&page, "<table><thead><tr><th>Name</th><th>Type</th><th>Size</th><th>State</th>"
                        "<th>Id</th></tr></thead><tbody>") != 0) {
      gdefcore_ids_free(catalog, ids, n);
      page_free(&page);
      return -1;
    }
    for (i = 0; i < n && shown < LIST_MAX; i++) {
      char name[GDEFCORE_UPLOAD_NAME_MAX + 1];
      char media[130];
      char size[24];
      char state[16];
      if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
          !first_value(catalog, request, ids[i], "name", name, sizeof name)) {
        continue;  /* a file the person may not see is not listed */
      }
      (void)first_value(catalog, request, ids[i], "media", media, sizeof media);
      (void)first_value(catalog, request, ids[i], "size", size, sizeof size);
      (void)first_value(catalog, request, ids[i], "state", state, sizeof state);
      if (page_str(&page, "<tr><td>") != 0 || page_esc(&page, name, strlen(name)) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_esc(&page, media, strlen(media)) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_esc(&page, size, strlen(size)) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_esc(&page, state, strlen(state)) != 0 ||
          page_str(&page, "</td><td><code>") != 0 ||
          page_esc(&page, ids[i], strlen(ids[i])) != 0 || page_str(&page, "</code></td></tr>") != 0) {
        gdefcore_ids_free(catalog, ids, n);
        page_free(&page);
        return -1;
      }
      shown++;
    }
    gdefcore_ids_free(catalog, ids, n);
    if (page_str(&page, "</tbody></table>") != 0) {
      page_free(&page);
      return -1;
    }
  }
  return take_page(&page, out);
}

static int post_file(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * ctype,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  struct GDEFWEB_Mp mp;
  const struct GDEFWEB_Mp_Part * tok;
  const struct GDEFWEB_Mp_Part * file;
  char token[4096];
  unsigned char raw[3072];
  size_t raw_n = 0;
  char got_form[32];
  size_t got_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  const char * media;
  size_t media_len;
  uint64_t now;
  GDEFCORE_Result rc;
  (void)alloc;
  if (gdefweb_mp_parse(ctype, body, body_len, &mp) != 0 ||
      (tok = gdefweb_mp_find(&mp, "token")) == NULL || tok->filename != NULL ||
      (file = gdefweb_mp_find(&mp, "file")) == NULL || file->filename == NULL ||
      tok->len == 0 || tok->len >= sizeof token || !principal_of(request, who)) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "That upload was not understood.");
    return redirect_to(out, FILES_BASE);
  }
  memcpy(token, tok->data, tok->len);
  token[tok->len] = '\0';
  if (b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session, session_len,
          (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, got_form, sizeof got_form, &got_len, target, &base) != 0 ||
      got_len != sizeof FILES_FORM - 1 || memcmp(got_form, FILES_FORM, got_len) != 0 ||
      memcmp(target, who, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "That upload was not understood.");
    return redirect_to(out, FILES_BASE);
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  /* The type the browser declared is what the policy is asked about. */
  media = file->ctype != NULL ? file->ctype : "";
  media_len = file->ctype != NULL ? file->ctype_len : 0;
  now = (uint64_t)gdefcore_request_time(request);
  if (now == 0) {
    now = (uint64_t)time(NULL);
  }
  now *= 1000;
  rc = gdefcore_upload_stage(catalog, request, g_blobs, g_policy, file->filename,
      file->filename_len, media, media_len, file->data, file->len, now, id);
  if (rc == GDEFCORE_ERR_OOM) {
    return -1;
  }
  if (rc != GDEFCORE_OK) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That file was not accepted: its name, its type or its size is not allowed.");
    return redirect_to(out, FILES_BASE);
  }
  say(catalog, request, GDEFCORE_MESSAGE_STATUS,
      "Uploaded. Choose it in a content form's file field.");
  return redirect_to(out, FILES_BASE);
}

int gdefweb_files_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * ctype, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  int is_get = strcmp(method, "GET") == 0;
  if (!gdefweb_nav_follows(routes, request, path, "core.admin-files")) {
    return unknown_admin(method, out);
  }
  if (is_get) {
    return draw_page(alloc, catalog, request, keyring, root, root_len, session, session_len, out);
  }
  if (!gdefweb_files_enabled() || ctype == NULL) {
    return unknown_admin(method, out);
  }
  return post_file(alloc, catalog, request, keyring, root, root_len, session, session_len, ctype,
      body, body_len, out);
}

/* ---- a file to a visitor ------------------------------------------------------- */

#define PUBLIC_BASE "/files/"

int gdefweb_files_public_path(const char * path) {
  return path != NULL && strncmp(path, PUBLIC_BASE, sizeof PUBLIC_BASE - 1) == 0;
}

/* A type a browser runs or renders as a document is never served as itself
 * from the site's own address: it is sent as opaque bytes. */
static int active_type(const char * media) {
  static const char * const kActive[] = {"text/html", "application/xhtml+xml", "image/svg+xml",
      "text/xml", "application/xml", "text/javascript", "application/javascript",
      "application/x-javascript", "text/css", "application/pdf", "text/x-component", NULL};
  size_t i;
  for (i = 0; kActive[i] != NULL; i++) {
    if (strcmp(media, kActive[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

static int inline_type(const char * media) {
  return strcmp(media, "image/png") == 0 || strcmp(media, "image/jpeg") == 0 ||
         strcmp(media, "image/gif") == 0 || strcmp(media, "image/webp") == 0;
}

static void disposition_of(const char * name, int inline_ok, char * out, size_t cap) {
  char plain[GDEFCORE_UPLOAD_NAME_MAX + 1];
  char enc[3 * GDEFCORE_UPLOAD_NAME_MAX + 1];
  size_t i;
  size_t n = 0;
  size_t e = 0;
  for (i = 0; name[i] != '\0' && n < 60 && e < 140; i++) {
    unsigned char c = (unsigned char)name[i];
    plain[n++] = (char)((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'
                            ? c
                            : '_');
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '-' || c == '_' || c == '~') {
      enc[e++] = (char)c;
    } else {
      static const char hex[] = "0123456789ABCDEF";
      enc[e++] = '%';
      enc[e++] = hex[c >> 4];
      enc[e++] = hex[c & 15];
    }
  }
  plain[n] = '\0';
  enc[e] = '\0';
  snprintf(out, cap, "%s; filename=\"%.60s\"; filename*=UTF-8''%.150s", inline_ok ? "inline" : "attachment",
      plain, enc);
}

static int not_found(struct GDEFWEB_Admin_Reply * out) {
  out->status = 404;
  out->reason = "Not Found";
  out->type = "text/plain; charset=utf-8";
  out->body = NULL;
  out->len = 0;
  return 0;
}

int gdefweb_files_public(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, const char * path, struct GDEFWEB_Admin_Reply * out) {
  const char * rest = path + sizeof PUBLIC_BASE - 1;
  const char * slash = strchr(rest, '/');
  size_t idn = slash == NULL ? strlen(rest) : (size_t)(slash - rest);
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Upload_File * file = NULL;
  GDEFCORE_Result rc;
  struct Page page;
  int active;
  const char * media;
  memset(out, 0, sizeof *out);
  if (!gdefweb_files_enabled() || idn != GDEFCORE_PUBLIC_ID_LEN ||
      gdefcore_id_canonical(rest, idn, id) != GDEFCORE_OK) {
    return not_found(out);
  }
  rc = gdefcore_upload_read(catalog, request, g_blobs, id, GDEFCORE_PUBLIC_ID_LEN, &file);
  if (rc == GDEFCORE_ERR_INVALID) {
    return not_found(out);
  }
  if (rc != GDEFCORE_OK) {
    return -1;
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_add(&page, (const char *)gdefcore_upload_file_bytes(file),
          gdefcore_upload_file_size(file)) != 0) {
    gdefcore_upload_file_free(file);
    page_free(&page);
    return -1;
  }
  media = gdefcore_upload_file_media(file);
  active = active_type(media);
  out->status = 200;
  out->reason = "OK";
  snprintf(out->type_buf, sizeof out->type_buf, "%s", active ? "application/octet-stream" : media);
  out->type = out->type_buf;
  /* Nothing in a served file may run or draw as the site: a policy with no
   * source at all, and a sandbox for whatever a browser still tries to render. */
  out->csp = "default-src 'none'; sandbox";
  disposition_of(gdefcore_upload_file_name(file), !active && inline_type(media),
      out->disposition, sizeof out->disposition);
  out->body = page.p;
  out->len = page.n;
  page.p = NULL;
  gdefcore_upload_file_free(file);
  return 0;
}
