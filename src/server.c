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
 * One listener. HTTP/1.1 in, a tenant page out. The kernel decides the
 * tenant, the session and the page. This file decides the connection.
 */

#define _POSIX_C_SOURCE 200809L

#include <ghoti.io/defiant-web/server.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/request.h>
#include "admin.h"
#include <ghoti.io/cutil/fiber.h>
#include <ghoti.io/http/parser.h>
#include <ghoti.io/http/writer.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#define kAttempts 64
#define kAttemptKey 512
#define kBodyCap (64u * 1024u)
#define kStack (1024u * 1024u)
#define kSessionLife ((uint64_t)24 * 60 * 60)

static const char kFixed[] = "Not found\n";
static const char kActive[] = "active";

struct Attempt {
  char key[kAttemptKey];
  size_t key_len;
  int fails;
  uint64_t until_ms;
  int used;
};

struct GDEFWEB_Server {
  const GDEFWEB_Allocator * alloc;
  GCU_Loop * loop;
  GCU_Socket * listener;
  GCU_Socket_Address local;
  char * listen;
  size_t listen_len;
  char * proxies;
  size_t proxies_len;
  GDEFCORE_Inst * inst;
  GDEFCORE_Host * host;
  uint8_t root[GDEFCORE_ACCOUNT_ROOT_LEN];
  int has_root;
  GCU_Fiber * fiber;
  GCU_Loop_Op op;
  struct Attempt attempts[kAttempts];
  int done;
  int serving;
};

struct InReq {
  char * method;
  char * path;
  char * host;
  char * xhost;
  char * xfor;
  char * cookie;
  char * origin;
  char * ctype;
  char * body;
  size_t body_len;
};

static void wipe(void * p, size_t n) {
  volatile unsigned char * b = (volatile unsigned char *)p;
  size_t i;
  for (i = 0; i < n; i++) {
    b[i] = 0;
  }
}

static void * mem_alloc(const GDEFWEB_Allocator * alloc, size_t n) {
  return alloc->malloc_fn(alloc->ctx, n == 0 ? 1 : n);
}

static void mem_free(const GDEFWEB_Allocator * alloc, void * p) {
  if (p != NULL) {
    alloc->free_fn(alloc->ctx, p);
  }
}

static char * dup_n(const GDEFWEB_Allocator * alloc, const char * s, size_t n) {
  char * out = (char *)mem_alloc(alloc, n + 1);
  if (out == NULL) {
    return NULL;
  }
  if (n > 0 && s != NULL) {
    memcpy(out, s, n);
  }
  out[n] = '\0';
  return out;
}

static uint64_t now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int setting(const GDEFCORE_Inst_Config * config, const char * name,
    const char ** out, size_t * out_len) {
  const char * source = NULL;
  return gdefcore_inst_config_setting(config, name, strlen(name), out, out_len,
      &source) == GDEFCORE_OK;
}

GDEFWEB_Result gdefweb_server_create(const GDEFCORE_Inst_Config * config,
    const GDEFWEB_Allocator * allocator, GDEFWEB_Server ** out) {
  const char * listen = NULL;
  const char * proxies = NULL;
  size_t listen_len = 0;
  size_t proxies_len = 0;
  GDEFWEB_Server * server;
  if (config == NULL || out == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  if (!setting(config, "DEFIANT_LISTEN", &listen, &listen_len) ||
      !setting(config, "DEFIANT_TRUSTED_PROXIES", &proxies, &proxies_len)) {
    return GDEFWEB_ERR_INVALID;
  }
  allocator = allocator == NULL ? gdefweb_allocator_default() : allocator;
  server = (GDEFWEB_Server *)mem_alloc(allocator, sizeof *server);
  if (server == NULL) {
    return GDEFWEB_ERR_OOM;
  }
  memset(server, 0, sizeof *server);
  server->alloc = allocator;
  server->listen = dup_n(allocator, listen, listen_len);
  server->proxies = dup_n(allocator, proxies, proxies_len);
  server->listen_len = listen_len;
  server->proxies_len = proxies_len;
  if (server->listen == NULL || server->proxies == NULL ||
      gcu_loop_create(&server->loop, NULL) != GCU_LOOP_OK) {
    gdefweb_server_destroy(server);
    return GDEFWEB_ERR_OOM;
  }
  *out = server;
  return GDEFWEB_OK;
}

void gdefweb_server_destroy(GDEFWEB_Server * server) {
  if (server == NULL) {
    return;
  }
  /* Cancel a parked accept first. The fiber runs to completion on that
   * cancellation, and only then is it safe to free. */
  if (server->loop != NULL) {
    gcu_loop_destroy(server->loop);
    server->loop = NULL;
  }
  if (server->fiber != NULL) {
    gcu_fiber_destroy(server->fiber);
    server->fiber = NULL;
  }
  if (server->listener != NULL) {
    gcu_socket_close(server->listener);
    server->listener = NULL;
  }
  wipe(server->root, sizeof server->root);
  wipe(server->attempts, sizeof server->attempts);
  mem_free(server->alloc, server->listen);
  mem_free(server->alloc, server->proxies);
  mem_free(server->alloc, server);
}

GDEFWEB_Result gdefweb_server_set_inst(GDEFWEB_Server * server,
    GDEFCORE_Inst * inst, const uint8_t * root, size_t root_len) {
  if (server == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  if (inst == NULL) {
    server->inst = NULL;
    server->has_root = 0;
    wipe(server->root, sizeof server->root);
    return GDEFWEB_OK;
  }
  if (root == NULL || root_len != GDEFCORE_ACCOUNT_ROOT_LEN) {
    return GDEFWEB_ERR_INVALID;
  }
  memcpy(server->root, root, GDEFCORE_ACCOUNT_ROOT_LEN);
  server->has_root = 1;
  server->inst = inst;
  return GDEFWEB_OK;
}

GDEFWEB_Result gdefweb_server_set_host(GDEFWEB_Server * server,
    GDEFCORE_Host * host) {
  if (server == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  server->host = host;
  return GDEFWEB_OK;
}

static int parse_listen(const char * text, size_t n, GCU_Socket_Address * out) {
  char buf[128];
  char host[128];
  unsigned port = 0;
  size_t host_len = 0;
  size_t i;
  if (n == 0 || n >= sizeof buf) {
    return 0;
  }
  memcpy(buf, text, n);
  buf[n] = '\0';
  if (buf[0] == '[') {
    char * end = strchr(buf, ']');
    if (end == NULL || end[1] != ':' || end == buf + 1) {
      return 0;
    }
    host_len = (size_t)(end - (buf + 1));
    if (host_len >= sizeof host) {
      return 0;
    }
    memcpy(host, buf + 1, host_len);
    host[host_len] = '\0';
    for (i = (size_t)(end + 2 - buf); buf[i] != '\0'; i++) {
      if (buf[i] < '0' || buf[i] > '9') {
        return 0;
      }
      port = port * 10u + (unsigned)(buf[i] - '0');
      if (port > 65535u) {
        return 0;
      }
    }
  } else {
    char * colon = strrchr(buf, ':');
    if (colon == NULL || colon == buf || colon[1] == '\0') {
      return 0;
    }
    host_len = (size_t)(colon - buf);
    if (host_len >= sizeof host) {
      return 0;
    }
    memcpy(host, buf, host_len);
    host[host_len] = '\0';
    for (i = 1; colon[i] != '\0'; i++) {
      if (colon[i] < '0' || colon[i] > '9') {
        return 0;
      }
      port = port * 10u + (unsigned)(colon[i] - '0');
      if (port > 65535u) {
        return 0;
      }
    }
  }
  return gcu_socket_address_parse(out, host, (uint16_t)port) == GCU_SOCKET_OK;
}

GDEFWEB_Result gdefweb_server_listen(GDEFWEB_Server * server) {
  GCU_Socket_Address addr;
  GCU_Socket * socket = NULL;
  GCU_Socket_Family family;
  if (server == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  if (server->listener != NULL) {
    if (gcu_socket_close(server->listener) != GCU_SOCKET_OK) {
      return GDEFWEB_ERR_BUSY;
    }
    server->listener = NULL;
  }
  memset(&server->local, 0, sizeof server->local);
  if (server->listen_len == 0) {
    return GDEFWEB_OK;
  }
  if (!parse_listen(server->listen, server->listen_len, &addr)) {
    return GDEFWEB_ERR_INVALID;
  }
  family = addr.family;
  if (gcu_socket_create(&socket, family, GCU_SOCKET_STREAM, NULL) !=
          GCU_SOCKET_OK ||
      gcu_socket_set_option(socket, GCU_SOCKET_OPT_REUSE_ADDRESS, 1) !=
          GCU_SOCKET_OK ||
      gcu_socket_bind(socket, &addr) != GCU_SOCKET_OK ||
      gcu_socket_listen(socket, 16) != GCU_SOCKET_OK ||
      gcu_socket_local_address(socket, &server->local) != GCU_SOCKET_OK) {
    gcu_socket_close(socket);
    return GDEFWEB_ERR_IO;
  }
  server->listener = socket;
  return GDEFWEB_OK;
}

int gdefweb_server_accepting(const GDEFWEB_Server * server) {
  return server != NULL && server->listener != NULL;
}

GDEFWEB_Result gdefweb_server_local(const GDEFWEB_Server * server,
    GCU_Socket_Address * out) {
  if (server == NULL || out == NULL || server->listener == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  *out = server->local;
  return GDEFWEB_OK;
}

GCU_Loop * gdefweb_server_loop(GDEFWEB_Server * server) {
  return server == NULL ? NULL : server->loop;
}

int gdefweb_server_serving(const GDEFWEB_Server * server) {
  return server != NULL && server->serving;
}

int gdefweb_server_done(const GDEFWEB_Server * server) {
  return server != NULL && server->done;
}

static void host_key(const char * in, size_t n, const char ** out, size_t * out_n) {
  size_t i;
  int colons = 0;
  size_t colon = 0;
  int digits = 1;
  if (n >= 2 && in[0] == '[') {
    for (i = 1; i < n; i++) {
      if (in[i] == ']') {
        *out = in + 1;
        *out_n = i - 1;
        return;
      }
    }
  }
  for (i = 0; i < n; i++) {
    if (in[i] == ':') {
      colons += 1;
      colon = i;
    }
  }
  if (colons == 1 && colon + 1 < n) {
    for (i = colon + 1; i < n; i++) {
      if (in[i] < '0' || in[i] > '9') {
        digits = 0;
      }
    }
    if (digits) {
      *out = in;
      *out_n = colon;
      return;
    }
  }
  *out = in;
  *out_n = n;
}

static void peer_text(const GCU_Socket_Address * addr, char * out, size_t cap) {
  if (addr->family == GCU_SOCKET_IPV4) {
    snprintf(out, cap, "%u.%u.%u.%u", addr->bytes[0], addr->bytes[1],
        addr->bytes[2], addr->bytes[3]);
    return;
  }
  snprintf(out, cap, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
      "%02x%02x:%02x%02x",
      addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3],
      addr->bytes[4], addr->bytes[5], addr->bytes[6], addr->bytes[7],
      addr->bytes[8], addr->bytes[9], addr->bytes[10], addr->bytes[11],
      addr->bytes[12], addr->bytes[13], addr->bytes[14], addr->bytes[15]);
}

static void truncate_addr(const char * in, char * out, size_t cap) {
  unsigned a = 0;
  unsigned b = 0;
  unsigned c = 0;
  unsigned d = 0;
  char tail = 0;
  size_t n;
  size_t keep;
  if (in != NULL && sscanf(in, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
      a < 256u && b < 256u && c < 256u && d < 256u) {
    snprintf(out, cap, "%u.%u.0.0", a, b);
    return;
  }
  n = in == NULL ? 0 : strlen(in);
  keep = n / 2;
  if (keep >= cap) {
    keep = cap - 1;
  }
  if (keep > 0) {
    memcpy(out, in, keep);
  }
  out[keep] = '\0';
}

static int listed(const char * list, size_t list_len, const char * peer) {
  size_t plen;
  size_t i = 0;
  if (list == NULL || peer == NULL) {
    return 0;
  }
  plen = strlen(peer);
  if (plen == 0) {
    return 0;
  }
  while (i < list_len) {
    size_t start;
    size_t end;
    while (i < list_len &&
        (list[i] == ' ' || list[i] == '\t' || list[i] == ',')) {
      i += 1;
    }
    start = i;
    while (i < list_len && list[i] != ',') {
      i += 1;
    }
    end = i;
    while (end > start && (list[end - 1] == ' ' || list[end - 1] == '\t')) {
      end -= 1;
    }
    if (end - start == plen && memcmp(list + start, peer, plen) == 0) {
      return 1;
    }
  }
  return 0;
}

static void first_item(const char * in, char * out, size_t cap) {
  size_t i = 0;
  size_t o = 0;
  if (in == NULL) {
    out[0] = '\0';
    return;
  }
  while (in[i] == ' ' || in[i] == '\t') {
    i += 1;
  }
  while (in[i] != '\0' && in[i] != ',' && o + 1 < cap) {
    out[o++] = in[i++];
  }
  while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
    o -= 1;
  }
  out[o] = '\0';
}

static int listen_host(const GDEFWEB_Server * server, const char * host,
    size_t n) {
  char text[80];
  if (server->listener == NULL || host == NULL) {
    return 0;
  }
  if (server->local.family == GCU_SOCKET_IPV4) {
    snprintf(text, sizeof text, "%u.%u.%u.%u", server->local.bytes[0],
        server->local.bytes[1], server->local.bytes[2], server->local.bytes[3]);
  } else {
    peer_text(&server->local, text, sizeof text);
  }
  return n == strlen(text) && memcmp(host, text, n) == 0;
}

static int span_eq(const char * a, size_t an, const char * b) {
  size_t bn = strlen(b);
  return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

static int ascii_eq(const char * a, const char * b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca >= 'A' && ca <= 'Z') {
      ca = (unsigned char)(ca - 'A' + 'a');
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = (unsigned char)(cb - 'A' + 'a');
    }
    if (ca != cb) {
      return 0;
    }
  }
  return 1;
}

static int origin_ok(const char * origin, const char * host) {
  const char * rest;
  size_t n;
  size_t host_n;
  size_t i;
  if (origin == NULL || host == NULL || host[0] == '\0') {
    return 0;
  }
  if (strncmp(origin, "http://", 7) == 0) {
    rest = origin + 7;
  } else if (strncmp(origin, "https://", 8) == 0) {
    rest = origin + 8;
  } else {
    return 0;
  }
  n = strlen(rest);
  host_n = strlen(host);
  if (n < host_n || !ascii_eq(rest, host, host_n)) {
    return 0;
  }
  rest += host_n;
  n -= host_n;
  if (n == 0) {
    return 1;
  }
  if (rest[0] != ':') {
    return 0;
  }
  for (i = 1; i < n; i++) {
    if (rest[i] < '0' || rest[i] > '9') {
      return 0;
    }
  }
  return n > 1;
}

static const char * header_get(const GHTTP_Headers * headers, const char * name) {
  GHTTP_Field field;
  size_t index = ghttp_headers_find(headers, name, 0);
  if (index == GHTTP_HEADERS_NPOS) {
    return NULL;
  }
  if (ghttp_headers_at(headers, index, &field) != GHTTP_OK) {
    return NULL;
  }
  return field.value;
}

static int b64_encode(const unsigned char * in, size_t n, char * out, size_t cap,
    size_t * out_n) {
  static const char kTab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t i = 0;
  size_t o = 0;
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
    if (o + 4 >= cap) {
      return -1;
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
  size_t n = strlen(in);
  size_t i = 0;
  size_t o = 0;
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
    if (c == -2) {
      out[o++] = (unsigned char)((v >> 16) & 0xffu);
      break;
    }
    if (c < 0) {
      return -1;
    }
    v |= (unsigned int)c << 6;
    out[o++] = (unsigned char)((v >> 16) & 0xffu);
    if (o >= cap) {
      return -1;
    }
    if (d == -2) {
      out[o++] = (unsigned char)((v >> 8) & 0xffu);
      break;
    }
    if (d < 0) {
      return -1;
    }
    v |= (unsigned int)d;
    out[o++] = (unsigned char)((v >> 8) & 0xffu);
    if (o >= cap) {
      return -1;
    }
    out[o++] = (unsigned char)(v & 0xffu);
    i += 4;
  }
  *out_n = o;
  return 0;
}

static int cookie_token(const char * cookie, char * out, size_t cap) {
  static const char kName[] = "__Host-session=";
  const char * p = cookie;
  size_t nlen = sizeof kName - 1;
  if (cookie == NULL) {
    return -1;
  }
  while (*p != '\0') {
    while (*p == ' ' || *p == ';') {
      p += 1;
    }
    if (strncmp(p, kName, nlen) == 0) {
      size_t i = 0;
      p += nlen;
      while (p[i] != '\0' && p[i] != ';') {
        i += 1;
      }
      if (i >= cap) {
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

static struct Attempt * find_attempt(GDEFWEB_Server * server, const char * key) {
  size_t n = strlen(key);
  size_t i;
  if (n == 0 || n >= kAttemptKey) {
    return NULL;
  }
  for (i = 0; i < kAttempts; i++) {
    if (server->attempts[i].used && server->attempts[i].key_len == n &&
        memcmp(server->attempts[i].key, key, n) == 0) {
      return &server->attempts[i];
    }
  }
  return NULL;
}

static struct Attempt * attempt_slot(GDEFWEB_Server * server, const char * key) {
  size_t n = strlen(key);
  size_t i;
  struct Attempt * free_slot = NULL;
  struct Attempt * expired = NULL;
  uint64_t now;
  if (n == 0 || n >= kAttemptKey) {
    return NULL;
  }
  now = now_ms();
  for (i = 0; i < kAttempts; i++) {
    struct Attempt * slot = &server->attempts[i];
    if (slot->used && slot->key_len == n && memcmp(slot->key, key, n) == 0) {
      return slot;
    }
    if (!slot->used && free_slot == NULL) {
      free_slot = slot;
    } else if (slot->used && slot->until_ms != 0 && now >= slot->until_ms &&
               expired == NULL) {
      expired = slot;
    }
  }
  if (free_slot == NULL) {
    free_slot = expired;
  }
  if (free_slot == NULL) {
    return NULL;
  }
  memset(free_slot, 0, sizeof *free_slot);
  memcpy(free_slot->key, key, n);
  free_slot->key_len = n;
  free_slot->used = 1;
  return free_slot;
}

static int throttled(GDEFWEB_Server * server, const char * key) {
  struct Attempt * slot = find_attempt(server, key);
  if (slot == NULL) {
    return 0;
  }
  return slot->until_ms != 0 && now_ms() < slot->until_ms;
}

static void note_failure(GDEFWEB_Server * server, const char * key) {
  struct Attempt * slot = attempt_slot(server, key);
  int exp;
  uint64_t sec;
  if (slot == NULL) {
    return;
  }
  slot->fails += 1;
  if (slot->fails < 8) {
    slot->until_ms = 0;
    return;
  }
  exp = slot->fails - 8;
  if (exp > 6) {
    exp = 6;
  }
  sec = 1ull << exp;
  if (sec > 60ull) {
    sec = 60ull;
  }
  slot->until_ms = now_ms() + sec * 1000ull;
}

static void note_success(GDEFWEB_Server * server, const char * key) {
  struct Attempt * slot;
  size_t n = strlen(key);
  size_t i;
  for (i = 0; i < kAttempts; i++) {
    slot = &server->attempts[i];
    if (slot->used && slot->key_len == n && memcmp(slot->key, key, n) == 0) {
      memset(slot, 0, sizeof *slot);
      return;
    }
  }
}

static void log_line(const char * method, const char * path, int status,
    const char * client) {
  char shown[80];
  truncate_addr(client, shown, sizeof shown);
  fprintf(stdout, "defiant-web method=%s path=%s status=%d client=%s\n",
      method == NULL ? "-" : method, path == NULL ? "-" : path, status, shown);
  fflush(stdout);
}

static int write_all(GDEFWEB_Server * server, GCU_Socket * sock, const void * data,
    size_t n) {
  if (n == 0) {
    return 0;
  }
  if (gcu_loop_write(server->loop, &server->op, sock, data, n) != GCU_LOOP_OK) {
    return -1;
  }
  if (gcu_loop_fiber_wait(&server->op) != GCU_LOOP_OK) {
    return -1;
  }
  return server->op.result.status == GCU_LOOP_OK ? 0 : -1;
}

struct Cookies {
  const char * values[2];
};

static int reply_ex(GDEFWEB_Server * server, GCU_Socket * sock, int status,
    const char * reason, const void * body, size_t body_len,
    const struct Cookies * cookies, const char * type, const char * method,
    const char * path, const char * client, const char * location) {
  GHTTP_Response * response = NULL;
  GHTTP_Buffer * buffer = NULL;
  GHTTP_Headers * headers;
  char length[32];
  const void * bytes;
  size_t n;
  int rc = -1;
  if (ghttp_response_create(NULL, &response) != GHTTP_OK ||
      ghttp_response_set_status(response, status, reason) != GHTTP_OK) {
    goto done;
  }
  headers = ghttp_response_headers(response);
  snprintf(length, sizeof length, "%zu", body_len);
  if (ghttp_headers_add(headers, "Content-Length", length) != GHTTP_OK ||
      ghttp_headers_add(headers, "Content-Type",
          type == NULL ? "text/plain; charset=utf-8" : type) != GHTTP_OK ||
      ghttp_headers_add(headers, "Content-Security-Policy", gdefweb_admin_csp()) !=
          GHTTP_OK ||
      ghttp_headers_add(headers, "Connection", "close") != GHTTP_OK) {
    goto done;
  }
  if (location != NULL &&
      ghttp_headers_add(headers, "Location", location) != GHTTP_OK) {
    goto done;
  }
  if (cookies != NULL) {
    int i;
    for (i = 0; i < 2; i++) {
      if (cookies->values[i] != NULL &&
          ghttp_headers_add(headers, "Set-Cookie", cookies->values[i]) != GHTTP_OK) {
        goto done;
      }
    }
  }
  if (ghttp_buffer_create(NULL, &buffer) != GHTTP_OK ||
      ghttp_write_response_head(buffer, response) != GHTTP_OK ||
      ghttp_buffer_append(buffer, body, body_len) != GHTTP_OK) {
    goto done;
  }
  bytes = ghttp_buffer_data(buffer);
  n = ghttp_buffer_size(buffer);
  if (write_all(server, sock, bytes, n) != 0) {
    goto done;
  }
  log_line(method, path, status, client);
  rc = 0;
done:
  ghttp_buffer_destroy(buffer);
  ghttp_response_destroy(response);
  return rc;
}

static int reply(GDEFWEB_Server * server, GCU_Socket * sock, int status,
    const char * reason, const void * body, size_t body_len,
    const struct Cookies * cookies, const char * type, const char * method,
    const char * path, const char * client) {
  return reply_ex(server, sock, status, reason, body, body_len, cookies, type,
      method, path, client, NULL);
}

static char * copy_header(const GDEFWEB_Allocator * alloc, const GHTTP_Headers * h,
    const char * name) {
  const char * value = header_get(h, name);
  if (value == NULL) {
    return dup_n(alloc, "", 0);
  }
  return dup_n(alloc, value, strlen(value));
}

static void in_free(const GDEFWEB_Allocator * alloc, struct InReq * in) {
  if (in->body != NULL && in->body_len > 0) {
    wipe(in->body, in->body_len);
  }
  mem_free(alloc, in->method);
  mem_free(alloc, in->path);
  mem_free(alloc, in->host);
  mem_free(alloc, in->xhost);
  mem_free(alloc, in->xfor);
  if (in->cookie != NULL) {
    wipe(in->cookie, strlen(in->cookie));
  }
  mem_free(alloc, in->cookie);
  mem_free(alloc, in->origin);
  mem_free(alloc, in->ctype);
  mem_free(alloc, in->body);
  memset(in, 0, sizeof *in);
}

static int read_request(GDEFWEB_Server * server, GCU_Socket * sock, struct InReq * in) {
  GHTTP_Parser * parser = NULL;
  char buf[4096];
  char * body = NULL;
  size_t body_len = 0;
  int saw_end = 0;
  int rc = -1;
  const GHTTP_Request * request;
  const char * target;
  const char * path = NULL;
  size_t path_len = 0;
  const GHTTP_Headers * headers;
  memset(in, 0, sizeof *in);
  if (ghttp_parser_create(GHTTP_PARSE_REQUEST, NULL, NULL, &parser) != GHTTP_OK) {
    return -1;
  }
  while (!saw_end) {
    size_t off = 0;
    size_t n;
    GHTTP_Event ev = GHTTP_EVENT_NEED_MORE;
    int spins = 0;
    if (gcu_loop_read(server->loop, &server->op, sock, buf, sizeof buf) !=
        GCU_LOOP_OK) {
      goto done;
    }
    if (gcu_loop_fiber_wait(&server->op) != GCU_LOOP_OK ||
        server->op.result.status != GCU_LOOP_OK) {
      goto done;
    }
    n = server->op.result.bytes;
    if (n == 0) {
      if (ghttp_parser_finish(parser, &ev) == GHTTP_OK && ev == GHTTP_EVENT_END) {
        saw_end = 1;
      }
      break;
    }
    /* A head with no body reports END on the next feed, including an empty
     * one. Stopping at the first NEED_MORE is what asks the socket for more. */
    do {
      size_t used = 0;
      if (ghttp_parser_feed(parser, buf + off, n - off, &used, &ev) != GHTTP_OK) {
        goto done;
      }
      off += used;
      if (ev == GHTTP_EVENT_BODY) {
        const void * data = NULL;
        size_t len = 0;
        char * next;
        ghttp_parser_body(parser, &data, &len);
        if (body_len + len > kBodyCap) {
          goto done;
        }
        next = (char *)server->alloc->realloc_fn(server->alloc->ctx, body,
            body_len + len + 1);
        if (next == NULL) {
          goto done;
        }
        body = next;
        memcpy(body + body_len, data, len);
        body_len += len;
        body[body_len] = '\0';
      } else if (ev == GHTTP_EVENT_END) {
        saw_end = 1;
      }
      if (++spins > 64) {
        goto done;
      }
    } while (ev != GHTTP_EVENT_NEED_MORE && !saw_end);
  }
  if (!saw_end) {
    goto done;
  }
  request = ghttp_parser_request(parser);
  if (request == NULL) {
    goto done;
  }
  target = ghttp_request_target(request);
  if (ghttp_request_target_split(request, &path, &path_len, NULL, NULL) !=
      GHTTP_OK) {
    goto done;
  }
  headers = ghttp_request_headers_const(request);
  in->method = dup_n(server->alloc, ghttp_request_method(request),
      strlen(ghttp_request_method(request)));
  in->path = dup_n(server->alloc, path, path_len);
  in->host = copy_header(server->alloc, headers, "Host");
  in->xhost = copy_header(server->alloc, headers, "X-Forwarded-Host");
  in->xfor = copy_header(server->alloc, headers, "X-Forwarded-For");
  in->cookie = copy_header(server->alloc, headers, "Cookie");
  in->origin = copy_header(server->alloc, headers, "Origin");
  in->ctype = copy_header(server->alloc, headers, "Content-Type");
  in->body = body;
  in->body_len = body_len;
  body = NULL;
  if (in->method == NULL || in->path == NULL || in->host == NULL ||
      in->xhost == NULL || in->xfor == NULL || in->cookie == NULL ||
      in->origin == NULL || in->ctype == NULL) {
    goto done;
  }
  (void)target;
  rc = 0;
done:
  if (body != NULL && body_len > 0) {
    wipe(body, body_len);
  }
  mem_free(server->alloc, body);
  ghttp_parser_destroy(parser);
  if (rc != 0) {
    in_free(server->alloc, in);
  }
  return rc;
}

static void render_path(const char * http_path, const char ** out, size_t * out_n) {
  size_t n = strlen(http_path);
  if (n == 1 && http_path[0] == '/') {
    *out = http_path;
    *out_n = 1;
    return;
  }
  if (n > 0 && http_path[0] == '/') {
    *out = http_path + 1;
    *out_n = n - 1;
    return;
  }
  *out = http_path;
  *out_n = n;
}

static int fixed_404(GDEFWEB_Server * server, GCU_Socket * sock, const char * method,
    const char * path, const char * client) {
  return reply(server, sock, 404, "Not Found", kFixed, strlen(kFixed), NULL,
      "text/plain; charset=utf-8", method, path, client);
}

static void serve_connection(GDEFWEB_Server * server, GCU_Socket * sock,
    const GCU_Socket_Address * peer) {
  struct InReq in;
  char peer_addr[80];
  char client[160];
  char forwarded[160];
  const char * key_src;
  const char * key = NULL;
  size_t key_len = 0;
  int health;
  int trusted;
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * placement = NULL;
  char * edition = NULL;
  char * status = NULL;
  size_t placement_n = 0;
  size_t edition_n = 0;
  size_t status_n = 0;
  GDEFSTOR_Limits limits;
  GDEFCORE_Result rc;
  GDEFCORE_Catalog * catalog = NULL;
  GDEFCORE_Request * request = NULL;
  GDEFCORE_Keyring * keyring = NULL;
  uint8_t storage[16];
  int state_change;
  int origin_good;
  char user_key[kAttemptKey];
  char addr_key[kAttemptKey];
  peer_text(peer, peer_addr, sizeof peer_addr);
  snprintf(client, sizeof client, "%s", peer_addr);
  memset(&in, 0, sizeof in);
  if (read_request(server, sock, &in) != 0) {
    reply(server, sock, 400, "Bad Request", "", 0, NULL, NULL, "-", "/",
        client);
    gcu_socket_close(sock);
    return;
  }
  health = span_eq(in.path, strlen(in.path), "/alive") ||
      span_eq(in.path, strlen(in.path), "/ready");
  trusted = !health && listed(server->proxies, server->proxies_len, peer_addr);
  if (trusted) {
    first_item(in.xfor, forwarded, sizeof forwarded);
    if (forwarded[0] != '\0') {
      snprintf(client, sizeof client, "%s", forwarded);
    }
  }
  if (span_eq(in.path, strlen(in.path), "/alive")) {
    reply(server, sock, server->listener != NULL ? 200 : 503,
        server->listener != NULL ? "OK" : "Service Unavailable", "", 0, NULL,
        NULL, in.method, in.path, peer_addr);
    goto out;
  }
  if (span_eq(in.path, strlen(in.path), "/ready")) {
    int open = server->inst != NULL;
    reply(server, sock, open ? 200 : 503, open ? "OK" : "Service Unavailable",
        "", 0, NULL, NULL, in.method, in.path, peer_addr);
    goto out;
  }
  if (server->inst == NULL || !server->has_root) {
    reply(server, sock, 503, "Service Unavailable", "", 0, NULL, NULL, in.method,
        in.path, client);
    goto out;
  }
  if (trusted && in.xhost[0] != '\0') {
    key_src = in.xhost;
  } else {
    key_src = in.host;
  }
  host_key(key_src, strlen(key_src), &key, &key_len);
  rc = gdefcore_inst_resolve(server->inst, key, key_len, id, &placement,
      &placement_n, &edition, &edition_n, &status, &status_n, &limits);
  if (rc == GDEFCORE_ERR_OOM) {
    reply(server, sock, 500, "Internal Server Error", "", 0, NULL, NULL,
        in.method, in.path, client);
    goto out;
  }
  if (rc != GDEFCORE_OK) {
    if (listen_host(server, key, key_len) &&
        gdefcore_inst_default(server->inst, id) == GDEFCORE_OK) {
      status = NULL;
      status_n = strlen(kActive);
    } else {
      fixed_404(server, sock, in.method, in.path, client);
      goto out;
    }
  } else if (!(status_n == strlen(kActive) &&
                 memcmp(status, kActive, status_n) == 0)) {
    gdefcore_inst_text_free(server->inst, placement);
    gdefcore_inst_text_free(server->inst, edition);
    gdefcore_inst_text_free(server->inst, status);
    placement = NULL;
    edition = NULL;
    status = NULL;
    fixed_404(server, sock, in.method, in.path, client);
    goto out;
  } else {
    gdefcore_inst_text_free(server->inst, placement);
    gdefcore_inst_text_free(server->inst, edition);
    gdefcore_inst_text_free(server->inst, status);
    placement = NULL;
    edition = NULL;
    status = NULL;
  }
  if (gdefcore_inst_storage_key(server->inst, id, strlen(id), storage) !=
          GDEFCORE_OK ||
      gdefcore_inst_catalog_open(server->inst, id, strlen(id), &catalog) !=
          GDEFCORE_OK) {
    reply(server, sock, 500, "Internal Server Error", "", 0, NULL, NULL,
        in.method, in.path, client);
    goto out;
  }
  /* The modules the process runs are asked about template writes, and the
   * contracts they extend are the ones templates are validated against. */
  gdefcore_catalog_bind_host(catalog, server->host);
  keyring = gdefcore_inst_tenant_keyring(server->inst, id, strlen(id));
  if (keyring == NULL ||
      gdefcore_request_create(storage, sizeof storage, 0, 0, NULL, &request) !=
          GDEFCORE_OK) {
    reply(server, sock, 500, "Internal Server Error", "", 0, NULL, NULL,
        in.method, in.path, client);
    goto out;
  }
  if (in.cookie[0] != '\0') {
    char encoded[1024];
    unsigned char raw[768];
    size_t raw_n = 0;
    if (cookie_token(in.cookie, encoded, sizeof encoded) == 0 &&
        b64_decode(encoded, raw, sizeof raw, &raw_n) == 0) {
      (void)gdefcore_account_check(catalog, request, keyring, server->root,
          sizeof server->root, (const char *)raw, raw_n);
      wipe(raw, sizeof raw);
    }
    wipe(encoded, sizeof encoded);
  }
  state_change = span_eq(in.method, strlen(in.method), "POST") ||
      span_eq(in.method, strlen(in.method), "PUT") ||
      span_eq(in.method, strlen(in.method), "PATCH") ||
      span_eq(in.method, strlen(in.method), "DELETE");
  {
    char host_z[512];
    if (key_len >= sizeof host_z) {
      origin_good = 0;
    } else {
      memcpy(host_z, key, key_len);
      host_z[key_len] = '\0';
      origin_good = origin_ok(in.origin, host_z);
    }
  }
  if (state_change && !origin_good) {
    reply(server, sock, 403, "Forbidden", "", 0, NULL, NULL, in.method, in.path,
        client);
    goto out;
  }
  if (span_eq(in.method, strlen(in.method), "POST") &&
      span_eq(in.path, strlen(in.path), "/login")) {
    char username[256];
    char password[GDEFCORE_ACCOUNT_PASSWORD_MAX + 1];
    size_t user_n = 0;
    size_t pass_n = 0;
    char * token = NULL;
    size_t token_n = 0;
    char encoded[1024];
    size_t encoded_n = 0;
    char session_cookie[1200];
    char admin_cookie[1200];
    struct Cookies cookies;
    int form_ok;
    snprintf(user_key, sizeof user_key, "u:");
    snprintf(addr_key, sizeof addr_key, "a:%s", client);
    if (strncmp(in.ctype, "application/x-www-form-urlencoded", 33) != 0 ||
        form_pair(in.body, in.body_len, "username", username, sizeof username,
            &user_n) != 0 ||
        form_pair(in.body, in.body_len, "password", password, sizeof password,
            &pass_n) != 0) {
      reply(server, sock, 400, "Bad Request", "", 0, NULL, NULL, in.method,
          in.path, client);
      wipe(password, sizeof password);
      wipe(encoded, sizeof encoded);
      wipe(session_cookie, sizeof session_cookie);
      wipe(admin_cookie, sizeof admin_cookie);
      goto out;
    }
    form_ok = 1;
    if (strlen(user_key) + user_n >= sizeof user_key) {
      form_ok = 0;
    } else {
      memcpy(user_key + 2, username, user_n);
      user_key[2 + user_n] = '\0';
    }
    if (!form_ok || throttled(server, user_key) || throttled(server, addr_key)) {
      reply(server, sock, 429, "Too Many Requests", "", 0, NULL, NULL, in.method,
          in.path, client);
      wipe(password, sizeof password);
      wipe(username, sizeof username);
      wipe(encoded, sizeof encoded);
      wipe(session_cookie, sizeof session_cookie);
      wipe(admin_cookie, sizeof admin_cookie);
      goto out;
    }
    rc = gdefcore_account_login(catalog, keyring, server->root, sizeof server->root,
        username, user_n, password, pass_n, kSessionLife, &token, &token_n);
    wipe(password, sizeof password);
    wipe(username, sizeof username);
    if (rc != GDEFCORE_OK || token == NULL ||
        b64_encode((const unsigned char *)token, token_n, encoded, sizeof encoded,
            &encoded_n) != 0) {
      gdefcore_text_free(catalog, token);
      note_failure(server, user_key);
      note_failure(server, addr_key);
      {
        const char * page = gdefweb_admin_login_page(1);
        reply(server, sock, 401, "Unauthorized", page, strlen(page), NULL,
            "text/html; charset=utf-8", in.method, in.path, client);
      }
      wipe(encoded, sizeof encoded);
      wipe(session_cookie, sizeof session_cookie);
      wipe(admin_cookie, sizeof admin_cookie);
      goto out;
    }
    note_success(server, user_key);
    (void)gdefcore_account_check(catalog, request, keyring, server->root,
        sizeof server->root, token, token_n);
    snprintf(session_cookie, sizeof session_cookie,
        "__Host-session=%s; Secure; HttpOnly; SameSite=Lax; Path=/", encoded);
    cookies.values[0] = session_cookie;
    cookies.values[1] = NULL;
    if (gdefweb_admin_is_admin(catalog, request)) {
      snprintf(admin_cookie, sizeof admin_cookie,
          "__Host-admin=%s; Secure; HttpOnly; SameSite=Strict; Path=/", encoded);
      cookies.values[1] = admin_cookie;
    }
    gdefcore_text_free(catalog, token);
    reply_ex(server, sock, 303, "See Other", "", 0, &cookies, NULL, in.method,
        in.path, client, cookies.values[1] != NULL ? "/admin" : "/");
    wipe(encoded, sizeof encoded);
    wipe(session_cookie, sizeof session_cookie);
    wipe(admin_cookie, sizeof admin_cookie);
    goto out;
  }
  if (span_eq(in.method, strlen(in.method), "POST") &&
      span_eq(in.path, strlen(in.path), "/logout")) {
    struct Cookies cookies;
    cookies.values[0] =
        "__Host-session=; Secure; HttpOnly; SameSite=Lax; Path=/; Max-Age=0";
    cookies.values[1] =
        "__Host-admin=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
    reply(server, sock, 200, "OK", "", 0, &cookies, NULL, in.method, in.path,
        client);
    goto out;
  }
  if (span_eq(in.method, strlen(in.method), "GET") &&
      span_eq(in.path, strlen(in.path), "/login")) {
    const char * page = gdefweb_admin_login_page(0);
    reply(server, sock, 200, "OK", page, strlen(page), NULL,
        "text/html; charset=utf-8", in.method, in.path, client);
    goto out;
  }
  if (span_eq(in.method, strlen(in.method), "GET")) {
    const char * asset = NULL;
    size_t asset_n = 0;
    const char * asset_type = NULL;
    if (gdefweb_admin_static(in.path, &asset, &asset_n, &asset_type)) {
      reply(server, sock, 200, "OK", asset, asset_n, NULL, asset_type, in.method,
          in.path, client);
      goto out;
    }
  }
  if (gdefweb_admin_path(in.path) &&
      gdefweb_admin_allowed(catalog, request, keyring, server->root,
          sizeof server->root, in.cookie)) {
    struct GDEFWEB_Admin_Reply page;
    memset(&page, 0, sizeof page);
    if (gdefweb_admin_serve(server->alloc, catalog, request, keyring,
            server->root, sizeof server->root, in.method, in.path, in.cookie,
            in.body, in.body_len, in.ctype, &page) != 0) {
      reply(server, sock, 500, "Internal Server Error", "", 0, NULL, NULL,
          in.method, in.path, client);
    } else {
      reply_ex(server, sock, page.status, page.reason, page.body, page.len, NULL,
          page.type, in.method, in.path, client, page.location);
    }
    gcu_allocator_free(server->alloc, page.body);
    goto out;
  }
  if (span_eq(in.method, strlen(in.method), "GET")) {
    const char * page_path = NULL;
    size_t page_n = 0;
    char * bytes = NULL;
    size_t bytes_n = 0;
    render_path(in.path, &page_path, &page_n);
    rc = gdefcore_render_page(catalog, NULL, request, page_path, page_n,
        GDEFCORE_RENDER_PAGE_FUEL, &bytes, &bytes_n);
    if (rc == GDEFCORE_ERR_OOM) {
      reply(server, sock, 500, "Internal Server Error", "", 0, NULL,
          "text/html; charset=utf-8", in.method, in.path, client);
    } else if (rc != GDEFCORE_OK) {
      reply(server, sock, 200, "OK", "", 0, NULL, "text/html; charset=utf-8",
          in.method, in.path, client);
    } else {
      reply(server, sock, 200, "OK", bytes, bytes_n, NULL,
          "text/html; charset=utf-8", in.method, in.path, client);
    }
    gdefcore_text_free(catalog, bytes);
    goto out;
  }
  reply(server, sock, 404, "Not Found", "", 0, NULL, NULL, in.method, in.path,
      client);
out:
  gdefcore_request_destroy(request);
  gdefcore_catalog_destroy(catalog);
  gdefcore_inst_text_free(server->inst, placement);
  gdefcore_inst_text_free(server->inst, edition);
  gdefcore_inst_text_free(server->inst, status);
  in_free(server->alloc, &in);
  gcu_socket_close(sock);
}

static void serve_fiber(void * arg) {
  GDEFWEB_Server * server = (GDEFWEB_Server *)arg;
  GCU_Socket * sock;
  GCU_Socket_Address peer;
  gcu_loop_op_init(&server->op, NULL, NULL);
  if (gcu_loop_accept(server->loop, &server->op, server->listener) !=
          GCU_LOOP_OK ||
      gcu_loop_fiber_wait(&server->op) != GCU_LOOP_OK ||
      server->op.result.status != GCU_LOOP_OK ||
      server->op.result.accepted == NULL) {
    server->done = 1;
    return;
  }
  sock = server->op.result.accepted;
  peer = server->op.result.peer;
  server->serving = 1;
  serve_connection(server, sock, &peer);
  server->serving = 0;
  server->done = 1;
}

GDEFWEB_Result gdefweb_server_accept_one(GDEFWEB_Server * server) {
  if (server == NULL || server->listener == NULL || server->loop == NULL) {
    return GDEFWEB_ERR_INVALID;
  }
  if (server->fiber != NULL) {
    if (!gcu_fiber_is_finished(server->fiber)) {
      return GDEFWEB_ERR_BUSY;
    }
    gcu_fiber_destroy(server->fiber);
    server->fiber = NULL;
  }
  server->done = 0;
  server->serving = 0;
  if (gcu_fiber_create(&server->fiber, serve_fiber, server, kStack, NULL) !=
      GCU_FIBER_OK) {
    return GDEFWEB_ERR_OOM;
  }
  if (gcu_fiber_switch_to(server->fiber) != GCU_FIBER_OK) {
    gcu_fiber_destroy(server->fiber);
    server->fiber = NULL;
    return GDEFWEB_ERR_IO;
  }
  return GDEFWEB_OK;
}
