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
 * What the admin pages share: the page builder, the signed-form helpers, the
 * redirects and the messages. Not a public header. admin.c defines them and
 * each page file (content.c) uses them, so a page need not live in admin.c.
 * The names are prefixed here so that none of them can meet another file's.
 */

#ifndef GHOTI_IO_GDEFWEB_ADMINKIT_H
#define GHOTI_IO_GDEFWEB_ADMINKIT_H

#include <ghoti.io/defiant-web/macros.h>
#include "admin.h"

#include <ghoti.io/defiant-web/allocator.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/request.h>

#include <stddef.h>
#include <stdint.h>

/* A page being built: a growing, NUL-terminated buffer. */
struct Page {
  const GDEFWEB_Allocator * alloc;
  char * p;
  size_t n;
  size_t cap;
};


#define wipe gdefweb_kit_wipe
#define b64_decode gdefweb_kit_b64_decode
#define b64_encode gdefweb_kit_b64_encode
#define pct_decode gdefweb_kit_pct_decode
#define form_field_alloc gdefweb_kit_form_field_alloc
#define session_raw gdefweb_kit_session_raw
#define token_meta gdefweb_kit_token_meta
#define sign_b64 gdefweb_kit_sign_b64
#define one_value gdefweb_kit_one_value
#define redirect gdefweb_kit_redirect
#define redirect_to gdefweb_kit_redirect_to
#define say gdefweb_kit_say
#define take_page gdefweb_kit_take_page
#define unknown_admin gdefweb_kit_unknown_admin
#define touched_confirm gdefweb_kit_touched_confirm
#define base_matches gdefweb_kit_base_matches
#define page_add gdefweb_kit_page_add
#define page_str gdefweb_kit_page_str
#define page_esc gdefweb_kit_page_esc
#define page_begin gdefweb_kit_page_begin
#define page_end gdefweb_kit_page_end
#define page_free gdefweb_kit_page_free
#define page_token gdefweb_kit_page_token
#define page_seg gdefweb_kit_page_seg
#define field_copy gdefweb_kit_field_copy
#define holds gdefweb_kit_holds
#define starts_with gdefweb_kit_starts_with

void wipe(void * p, size_t n);
int b64_decode(const char * in, unsigned char * out, size_t cap, size_t * out_n);
char * b64_encode(const GDEFWEB_Allocator * alloc, const unsigned char * in, size_t n, size_t * out_n);
int pct_decode(const char * in, size_t n, char * out, size_t cap, size_t * out_n);
int form_field_alloc(const char * body, size_t n, const char * name, size_t nth, char ** out, size_t * out_n);
int session_raw(const char * session, unsigned char * raw, size_t cap, size_t * out_n);
int token_meta(const unsigned char * raw, size_t n, char * form_id, size_t form_cap, size_t * form_len, char * target, uint64_t * base);
int sign_b64(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len, const char * session, size_t session_len, const char * form_id, const char * target, const GDEFCORE_Value * values, size_t nvalues, char ** out);
int one_value(GDEFCORE_Value * value, const char * field, const char * text, const char ** slot, size_t * lens);
int redirect(struct GDEFWEB_Admin_Reply * out, const char * location);
int redirect_to(struct GDEFWEB_Admin_Reply * out, const char * path);
void say(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, int kind, const char * text);
int take_page(struct Page * page, struct GDEFWEB_Admin_Reply * out);
int unknown_admin(const char * method, struct GDEFWEB_Admin_Reply * out);
size_t touched_confirm(GDEFCORE_Changeset * cs, char * buf, size_t cap);
int base_matches(GDEFCORE_Catalog * catalog, const char * id, uint64_t base);
int page_add(struct Page * page, const char * s, size_t n);
int page_str(struct Page * page, const char * s);
int page_esc(struct Page * page, const char * s, size_t n);
int page_begin(struct Page * page, const char * title);
int page_end(struct Page * page);
void page_free(struct Page * page);
int page_token(struct Page * page, const char * token);
int page_seg(struct Page * page, const char * s, size_t n);
int field_copy(GDEFCORE_Catalog * catalog, const char * id, const char * field, char * out, size_t cap);
int holds(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, const char * permission);
int starts_with(const char * s, const char * prefix);

#endif
