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

/* A directory that keeps files by tenant and digest (uploads.h). */

#include <ghoti.io/defiant-web/macros.h>
#include <ghoti.io/defiant-web/uploads.h>
#include <ghoti.io/cutil/dir.h>
#include <ghoti.io/cutil/file.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GDEFWEB_Blob_Dir {
  char * root;
  size_t root_len;
  GDEFCORE_Blob_Store store;
};

#define DIGEST_LEN GDEFCORE_UPLOAD_DIGEST_LEN
#define TENANT_HEX_MAX 128

static int digest_ok(const char * d) {
  size_t i;
  if (d == NULL || strlen(d) != DIGEST_LEN) {
    return 0;
  }
  for (i = 0; i < DIGEST_LEN; i++) {
    if (!((d[i] >= '0' && d[i] <= '9') || (d[i] >= 'a' && d[i] <= 'f'))) {
      return 0;
    }
  }
  return 1;
}

/* `<root>/<tenant hex>/<aa>/<digest>`; the shard directory is @p shard_len
 * bytes shorter. A name made of hex only, so no input can leave the root. */
static int path_of(const struct GDEFWEB_Blob_Dir * d, const uint8_t * tenant, size_t tenant_len,
    const char * digest, char * out, size_t cap, size_t * shard_len) {
  static const char h[] = "0123456789abcdef";
  char hex[TENANT_HEX_MAX * 2 + 1];
  size_t i;
  int n;
  if (tenant == NULL || tenant_len == 0 || tenant_len > TENANT_HEX_MAX || !digest_ok(digest)) {
    return -1;
  }
  for (i = 0; i < tenant_len; i++) {
    hex[2 * i] = h[tenant[i] >> 4];
    hex[2 * i + 1] = h[tenant[i] & 15];
  }
  hex[2 * tenant_len] = '\0';
  n = snprintf(out, cap, "%s/%s/%.2s/%s", d->root, hex, digest, digest);
  if (n < 0 || (size_t)n >= cap) {
    return -1;
  }
  *shard_len = DIGEST_LEN + 1;
  return 0;
}

#define PATH_MAX_BYTES 4096

static int blob_put(void * ctx, const uint8_t * tenant, size_t tenant_len, const char * digest,
    const uint8_t * bytes, size_t len) {
  const struct GDEFWEB_Blob_Dir * d = (const struct GDEFWEB_Blob_Dir *)ctx;
  char path[PATH_MAX_BYTES];
  char dir[PATH_MAX_BYTES];
  size_t shard = 0;
  if (path_of(d, tenant, tenant_len, digest, path, sizeof path, &shard) != 0 || bytes == NULL ||
      len == 0) {
    return 1;
  }
  /* The same digest is the same bytes: a file that is there is the answer. */
  if (gcu_file_exists(path)) {
    return 0;
  }
  memcpy(dir, path, strlen(path) - shard);
  dir[strlen(path) - shard] = '\0';
  if (gcu_dir_create_all(dir, NULL) != GCU_FILE_OK) {
    return 1;
  }
  return gcu_file_write_atomic(path, bytes, len, GCU_FILE_SYNC_FULL, GCU_FILE_PERMS_PRIVATE,
             NULL) == GCU_FILE_OK
             ? 0
             : 1;
}

static int blob_get(void * ctx, const uint8_t * tenant, size_t tenant_len, const char * digest,
    uint8_t * buf, size_t cap, size_t * out_len) {
  const struct GDEFWEB_Blob_Dir * d = (const struct GDEFWEB_Blob_Dir *)ctx;
  char path[PATH_MAX_BYTES];
  size_t shard = 0;
  void * data = NULL;
  size_t n = 0;
  if (path_of(d, tenant, tenant_len, digest, path, sizeof path, &shard) != 0 || buf == NULL) {
    return 1;
  }
  /* A file bigger than the buffer is refused, never truncated. */
  if (gcu_file_read(path, cap, NULL, &data, &n) != GCU_FILE_OK) {
    return 1;
  }
  memcpy(buf, data, n);
  gcu_file_free(NULL, data);
  *out_len = n;
  return 0;
}

static int blob_remove(void * ctx, const uint8_t * tenant, size_t tenant_len, const char * digest) {
  const struct GDEFWEB_Blob_Dir * d = (const struct GDEFWEB_Blob_Dir *)ctx;
  char path[PATH_MAX_BYTES];
  size_t shard = 0;
  GCU_File_Result r;
  if (path_of(d, tenant, tenant_len, digest, path, sizeof path, &shard) != 0) {
    return 1;
  }
  r = gcu_file_remove(path);
  return r == GCU_FILE_OK || r == GCU_FILE_ERR_NOT_FOUND ? 0 : 1;
}

GDEFWEB_Result gdefweb_blob_dir_open(const char * root, GDEFWEB_Blob_Dir ** out) {
  struct GDEFWEB_Blob_Dir * d;
  size_t n;
  if (root == NULL || out == NULL || !gcu_file_is_directory(root)) {
    return GDEFWEB_ERR_INVALID;
  }
  n = strlen(root);
  while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) {
    n--;
  }
  d = (struct GDEFWEB_Blob_Dir *)calloc(1, sizeof *d);
  if (d == NULL) {
    return GDEFWEB_ERR_OOM;
  }
  d->root = (char *)malloc(n + 1);
  if (d->root == NULL) {
    free(d);
    return GDEFWEB_ERR_OOM;
  }
  memcpy(d->root, root, n);
  d->root[n] = '\0';
  d->root_len = n;
  d->store.size = sizeof d->store;
  d->store.ctx = d;
  d->store.put = blob_put;
  d->store.get = blob_get;
  d->store.remove = blob_remove;
  *out = d;
  return GDEFWEB_OK;
}

const GDEFCORE_Blob_Store * gdefweb_blob_dir_store(const GDEFWEB_Blob_Dir * dir) {
  return dir == NULL ? NULL : &dir->store;
}

void gdefweb_blob_dir_close(GDEFWEB_Blob_Dir * dir) {
  if (dir != NULL) {
    free(dir->root);
    free(dir);
  }
}
