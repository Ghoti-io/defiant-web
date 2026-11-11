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
#include "nav.h"

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/adminarea.h>
#include <ghoti.io/defiant-core/asset.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/form.h>
#include <ghoti.io/defiant-core/layout.h>
#include <ghoti.io/defiant-core/layoutedit.h>
#include <ghoti.io/defiant-core/layoutwhy.h>
#include <ghoti.io/defiant-core/region.h>
#include <ghoti.io/defiant-core/menu.h>
#include <ghoti.io/defiant-core/message.h>
#include <ghoti.io/defiant-core/module.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/settings.h>
#include <ghoti.io/defiant-core/siteasset.h>
#include <ghoti.io/defiant-core/template.h>
#include <ghoti.io/security/random.h>
#include <ghoti.io/security/sha256.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The scripts are kept in parts, each a string a compiler must accept, and
 * served as one. */
static const char kJsPanes[] =
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
    "        lines.push(regions[i].getAttribute(\"data-region\") + \" recent\");\n"
    "      }\n"
    "    }\n"
    "    field = form.querySelector(\"[name=panes]\");\n"
    "    if (!field) return;\n"
    "    field.value = lines.join(\"\\n\");\n"
    "    form.submit();\n"
    "  });\n"
    "}());\n";

static const char kJsMenuA[] =
    /* The menu editor: drag and the keys make the post the move form makes. */
    "(function () {\n"
    "  var table = document.getElementById(\"menu-editor\");\n"
    "  var form = document.getElementById(\"menu-move-form\");\n"
    "  var live = document.getElementById(\"menu-live\");\n"
    "  var KEYS = {\"ArrowUp\": \"up\", \"ArrowDown\": \"down\", \"ArrowRight\": \"indent\", \"ArrowLeft\": \"outdent\"};\n"
    "  var WORDS = {\"up\": \"up\", \"down\": \"down\", \"indent\": \"under the entry above it\", \"outdent\": \"out from under its parent\"};\n"
    "  var dragged = null;\n"
    "  var marked = null;\n"
    "  var notes;\n"
    "  var hash;\n"
    "  var here;\n"
    "  var i;\n"
    "  var said = [];\n"
    "  if (!table || !form || !live) return;\n"
    "  function rows() {\n"
    "    return Array.prototype.slice.call(table.querySelectorAll(\"tr[data-entry]\"));\n"
    "  }\n"
    "  function field(name) {\n"
    "    return form.querySelector(\"[name=\" + name + \"]\");\n"
    "  }\n"
    "  function say(text) {\n"
    "    live.textContent = text;\n"
    "  }\n"
    "  function label(row) {\n"
    "    return row.getAttribute(\"data-label\") || row.getAttribute(\"data-entry\");\n"
    "  }\n"
    "  function rowOf(node) {\n"
    "    while (node && node !== table) {\n"
    "      if (node.nodeType === 1 && node.hasAttribute(\"data-entry\")) return node;\n"
    "      node = node.parentNode;\n"
    "    }\n"
    "    return null;\n"
    "  }\n"
    "  function send(action, row, parent, position) {\n"
    "    field(\"action\").value = action;\n"
    "    field(\"name\").value = row.getAttribute(\"data-entry\");\n"
    "    if (parent !== null) {\n"
    "      field(\"parent\").value = parent;\n"
    "      field(\"position\").value = String(position);\n"
    "    }\n"
    "    form.submit();\n"
    "  }\n"
    "  function siblings(menu, parent, skip) {\n"
    "    return rows().filter(function (r) {\n"
    "      return r !== skip && r.getAttribute(\"data-menu\") === menu &&\n"
    "        r.getAttribute(\"data-parent\") === parent;\n"
    "    });\n"
    "  }\n"
    "  function inside(root, row) {\n"
    "    var guard = 0;\n"
    "    var parent;\n"
    "    while (row && guard < 64) {\n"
    "      if (row === root) return true;\n"
    "      parent = row.getAttribute(\"data-parent\");\n"
    "      row = parent ? document.getElementById(\"e-\" + parent) : null;\n"
    "      guard += 1;\n"
    "    }\n"
    "    return false;\n"
    "  }\n"
    "  function zone(event, target) {\n"
    "    var box = target.getBoundingClientRect();\n"
    "    var y = (event.clientY - box.top) / box.height;\n"
    "    return y < 0.33 ? \"before\" : (y > 0.67 ? \"after\" : \"into\");\n"
    "  }\n"
    "  function place(row, target, where) {\n"
    "    var menu = row.getAttribute(\"data-menu\");\n"
    "    var parent;\n"
    "    var list;\n"
    "    var position;\n"
    "    if (where === \"into\") {\n"
    "      parent = target.getAttribute(\"data-entry\");\n"
    "      list = siblings(menu, parent, row);\n"
    "      position = list.length;\n"
    "    } else {\n"
    "      parent = target.getAttribute(\"data-parent\");\n"
    "      list = siblings(menu, parent, row);\n"
    "      position = list.indexOf(target) + (where === \"after\" ? 1 : 0);\n"
    "    }\n"
    "    return {parent: parent, position: position};\n"
    "  }\n"
    "  function clear() {\n"
    "    if (marked) {\n"
    "      marked.classList.remove(\"drop-before\");\n"
    "      marked.classList.remove(\"drop-after\");\n"
    "      marked.classList.remove(\"drop-into\");\n"
    "      marked = null;\n"
    "    }\n"
    "  }\n"
    "  function usable(target) {\n"
    "    return dragged && target && target !== dragged && !inside(dragged, target) &&\n"
    "      target.getAttribute(\"data-menu\") === dragged.getAttribute(\"data-menu\");\n"
    "  }\n";

static const char kJsMenuB[] =
    "  table.addEventListener(\"keydown\", function (event) {\n"
    "    var row = event.target;\n"
    "    var action = KEYS[event.key];\n"
    "    var all;\n"
    "    var at;\n"
    "    var to;\n"
    "    if (!row || !row.hasAttribute || !row.hasAttribute(\"data-entry\") || !action) return;\n"
    "    if (event.altKey && event.shiftKey && !event.ctrlKey && !event.metaKey) {\n"
    "      event.preventDefault();\n"
    "      say(\"Moving \" + label(row) + \" \" + WORDS[action] + \".\");\n"
    "      send(action, row, null, 0);\n"
    "    } else if (!event.altKey && !event.shiftKey && !event.ctrlKey && !event.metaKey) {\n"
    "      all = rows();\n"
    "      at = all.indexOf(row);\n"
    "      to = action === \"up\" ? at - 1 : (action === \"down\" ? at + 1 : -1);\n"
    "      if (to >= 0 && to < all.length) {\n"
    "        event.preventDefault();\n"
    "        all[to].focus();\n"
    "      }\n"
    "    }\n"
    "  });\n"
    "  table.addEventListener(\"dragstart\", function (event) {\n"
    "    var row = rowOf(event.target);\n"
    "    if (!row) return;\n"
    "    dragged = row;\n"
    "    event.dataTransfer.effectAllowed = \"move\";\n"
    "    event.dataTransfer.setData(\"text/plain\", row.getAttribute(\"data-entry\"));\n"
    "  });\n"
    "  table.addEventListener(\"dragover\", function (event) {\n"
    "    var target = rowOf(event.target);\n"
    "    clear();\n"
    "    if (!usable(target)) return;\n"
    "    event.preventDefault();\n"
    "    marked = target;\n"
    "    marked.classList.add(\"drop-\" + zone(event, target));\n"
    "  });\n"
    "  table.addEventListener(\"drop\", function (event) {\n"
    "    var target = rowOf(event.target);\n"
    "    var spot;\n"
    "    var where;\n"
    "    clear();\n"
    "    if (!usable(target)) return;\n"
    "    event.preventDefault();\n"
    "    where = zone(event, target);\n"
    "    spot = place(dragged, target, where);\n"
    "    say(\"Moving \" + label(dragged) + \".\");\n"
    "    send(\"move\", dragged, spot.parent, spot.position);\n"
    "    dragged = null;\n"
    "  });\n"
    "  table.addEventListener(\"dragend\", function () {\n"
    "    clear();\n"
    "    dragged = null;\n"
    "  });\n"
    "  notes = document.querySelectorAll(\".message\");\n"
    "  for (i = 0; i < notes.length; i += 1) {\n"
    "    said.push(notes[i].textContent);\n"
    "  }\n"
    "  if (said.length > 0) say(said.join(\" \"));\n"
    "  hash = window.location.hash;\n"
    "  if (hash.indexOf(\"#e-\") === 0) {\n"
    "    here = document.getElementById(hash.slice(1));\n"
    "    if (here && here.focus) here.focus();\n"
    "  }\n"
    "}());\n";


static const char kJsLayout[] =
    /* The layout editor: drag and the keys fill in a placement's own move form and
     * submit it, so the post is the one the form makes. */
    "(function () {\n"
    "  var root = document.getElementById(\"layout-editor\");\n"
    "  var live = document.getElementById(\"layout-live\");\n"
    "  var dragged = null;\n"
    "  var marked = null;\n"
    "  var notes;\n"
    "  var said = [];\n"
    "  var i;\n"
    "  if (!root || !live) return;\n"
    "  function say(text) {\n"
    "    live.textContent = text;\n"
    "  }\n"
    "  function items(list, skip) {\n"
    "    return Array.prototype.slice.call(list.querySelectorAll(\"li[data-placement]\")).filter(function (li) {\n"
    "      return li !== skip;\n"
    "    });\n"
    "  }\n"
    "  function lists() {\n"
    "    return Array.prototype.slice.call(root.querySelectorAll(\"ol[data-region]\"));\n"
    "  }\n"
    "  function itemOf(node) {\n"
    "    while (node && node !== root) {\n"
    "      if (node.nodeType === 1 && node.hasAttribute(\"data-placement\")) return node;\n"
    "      node = node.parentNode;\n"
    "    }\n"
    "    return null;\n"
    "  }\n"
    "  function listOf(node) {\n"
    "    while (node && node !== root) {\n"
    "      if (node.nodeType === 1 && node.hasAttribute(\"data-region\")) return node;\n"
    "      node = node.parentNode;\n"
    "    }\n"
    "    return null;\n"
    "  }\n"
    "  function moveForm(item) {\n"
    "    var act = item.querySelector(\"form input[name=act][value=move]\");\n"
    "    return act ? act.form : null;\n"
    "  }\n"
    "  function label(item) {\n"
    "    return item.getAttribute(\"data-widget\") || \"the placement\";\n"
    "  }\n"
    "  function send(item, region, position) {\n"
    "    var form = moveForm(item);\n"
    "    var total = items(listFor(region), item).length + 1;\n"
    "    if (!form) return;\n"
    "    form.querySelector(\"[name=region]\").value = region;\n"
    "    form.querySelector(\"[name=position]\").value = String(position);\n"
    "    say(\"Moving \" + label(item) + \" to \" + region + \", position \" + String(position + 1) + \" of \" + String(total) + \".\");\n"
    "    form.submit();\n"
    "  }\n"
    "  function listFor(region) {\n"
    "    var all = lists();\n"
    "    var j;\n"
    "    for (j = 0; j < all.length; j += 1) {\n"
    "      if (all[j].getAttribute(\"data-region\") === region) return all[j];\n"
    "    }\n"
    "    return all[0];\n"
    "  }\n"
    "  function spot(event, list) {\n"
    "    var all = items(list, dragged);\n"
    "    var j;\n"
    "    var box;\n"
    "    for (j = 0; j < all.length; j += 1) {\n"
    "      box = all[j].getBoundingClientRect();\n"
    "      if (event.clientY < box.top + box.height / 2) return j;\n"
    "    }\n"
    "    return all.length;\n"
    "  }\n"
    "  function clear() {\n"
    "    if (marked) {\n"
    "      marked.classList.remove(\"drop-here\");\n"
    "      marked = null;\n"
    "    }\n"
    "  }\n"
;

static const char kJsLayoutB[] =
    "  root.addEventListener(\"keydown\", function (event) {\n"
    "    var item = itemOf(event.target);\n"
    "    var all;\n"
    "    var list;\n"
    "    var at;\n"
    "    var regions;\n"
    "    var here;\n"
    "    var key = event.key;\n"
    "    var details;\n"
    "    if (!item || event.target !== item) return;\n"
    "    list = listOf(item);\n"
    "    if (key === \"Enter\" && !event.altKey && !event.shiftKey) {\n"
    "      details = item.querySelector(\"details\");\n"
    "      if (details) {\n"
    "        event.preventDefault();\n"
    "        details.open = true;\n"
    "        details.querySelector(\"input[name=title]\").focus();\n"
    "        say(\"Configuring \" + label(item) + \".\");\n"
    "      }\n"
    "      return;\n"
    "    }\n"
    "    if (key !== \"ArrowUp\" && key !== \"ArrowDown\" && key !== \"ArrowLeft\" && key !== \"ArrowRight\") return;\n"
    "    if (event.altKey && event.shiftKey && !event.ctrlKey && !event.metaKey) {\n"
    "      event.preventDefault();\n"
    "      all = items(list, null);\n"
    "      at = all.indexOf(item);\n"
    "      if (key === \"ArrowUp\" && at > 0) {\n"
    "        send(item, list.getAttribute(\"data-region\"), at - 1);\n"
    "      } else if (key === \"ArrowDown\" && at < all.length - 1) {\n"
    "        send(item, list.getAttribute(\"data-region\"), at + 1);\n"
    "      } else if (key === \"ArrowLeft\" || key === \"ArrowRight\") {\n"
    "        regions = lists();\n"
    "        here = regions.indexOf(list) + (key === \"ArrowLeft\" ? -1 : 1);\n"
    "        if (here >= 0 && here < regions.length) {\n"
    "          send(item, regions[here].getAttribute(\"data-region\"), Math.min(at, items(regions[here], null).length));\n"
    "        } else {\n"
    "          say(\"There is no region that way.\");\n"
    "        }\n"
    "      } else {\n"
    "        say(\"It cannot move further that way.\");\n"
    "      }\n"
    "    } else if (!event.altKey && !event.shiftKey && !event.ctrlKey && !event.metaKey &&\n"
    "        (key === \"ArrowUp\" || key === \"ArrowDown\")) {\n"
    "      all = Array.prototype.slice.call(root.querySelectorAll(\"li[data-placement]\"));\n"
    "      at = all.indexOf(item) + (key === \"ArrowUp\" ? -1 : 1);\n"
    "      if (at >= 0 && at < all.length) {\n"
    "        event.preventDefault();\n"
    "        all[at].focus();\n"
    "      }\n"
    "    }\n"
    "  });\n"
    "  root.addEventListener(\"dragstart\", function (event) {\n"
    "    var item = itemOf(event.target);\n"
    "    if (!item || event.target !== item) return;\n"
    "    dragged = item;\n"
    "    event.dataTransfer.effectAllowed = \"move\";\n"
    "    event.dataTransfer.setData(\"text/plain\", label(item));\n"
    "  });\n"
    "  root.addEventListener(\"dragover\", function (event) {\n"
    "    var list = listOf(event.target);\n"
    "    clear();\n"
    "    if (!dragged || !list) return;\n"
    "    event.preventDefault();\n"
    "    marked = list;\n"
    "    marked.classList.add(\"drop-here\");\n"
    "  });\n"
    "  root.addEventListener(\"drop\", function (event) {\n"
    "    var list = listOf(event.target);\n"
    "    var item = dragged;\n"
    "    clear();\n"
    "    if (!item || !list) return;\n"
    "    event.preventDefault();\n"
    "    dragged = null;\n"
    "    send(item, list.getAttribute(\"data-region\"), spot(event, list));\n"
    "  });\n"
    "  root.addEventListener(\"dragend\", function () {\n"
    "    clear();\n"
    "    dragged = null;\n"
    "  });\n"
    "  notes = document.querySelectorAll(\".message\");\n"
    "  for (i = 0; i < notes.length; i += 1) {\n"
    "    said.push(notes[i].textContent);\n"
    "  }\n"
    "  if (said.length > 0) say(said.join(\" \"));\n"
    "}());\n";

static const char * const kJsParts[] = {kJsPanes, kJsMenuA, kJsMenuB, kJsLayout, kJsLayoutB};
static char kJs[sizeof kJsPanes + sizeof kJsMenuA + sizeof kJsMenuB + sizeof kJsLayout + sizeof kJsLayoutB];
static size_t kJsLen;

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
    "}\n"
    ".message {\n"
    "  margin: 0.5rem 0;\n"
    "  padding: 0.5rem 0.75rem;\n"
    "  border: 1px solid GrayText;\n"
    "  border-left-width: 0.5rem;\n"
    "}\n"
    ".message.error, .message.refusal {\n"
    "  border-style: double;\n"
    "  border-left-width: 0.5rem;\n"
    "}\n"
    "tr[data-entry] {\n"
    "  cursor: grab;\n"
    "}\n"
    "tr[data-entry]:focus {\n"
    "  outline: 2px solid Highlight;\n"
    "  outline-offset: -2px;\n"
    "}\n"
    "tr.drop-before {\n"
    "  box-shadow: inset 0 3px 0 Highlight;\n"
    "}\n"
    "tr.drop-after {\n"
    "  box-shadow: inset 0 -3px 0 Highlight;\n"
    "}\n"
    "tr.drop-into {\n"
    "  outline: 2px dashed Highlight;\n"
    "  outline-offset: -2px;\n"
    "}\n"
    "li[data-placement]:focus {\n"
    "  outline: 2px solid Highlight;\n"
    "  outline-offset: 2px;\n"
    "}\n"
    "ol.drop-here {\n"
    "  outline: 2px dashed Highlight;\n"
    "  outline-offset: 2px;\n"
    "}\n"
    ".live {\n"
    "  min-height: 1.5em;\n"
    "}\n";

static char kHash[80];
static char kPolicy[320];
static char kPublic[480];
/* The policy of a public page: the site's own stylesheets, scripts, images and
 * fonts, served from this origin, and nothing inline, nothing evaluated and no
 * other origin. kPublicBase is it with no hash source, for when the hash of
 * the admin's script could not be made. */
static const char kPublicHead[] = "default-src 'none'; style-src 'self'; script-src";
static const char kPublicTail[] =
    "img-src 'self'; font-src 'self'; frame-ancestors 'none'";
static const char kPublicBase[] =
    "default-src 'none'; style-src 'self'; script-src 'self'; img-src 'self'; "
    "font-src 'self'; frame-ancestors 'none'";
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
  /* The same bytes whoever gets here first. */
  for (i = 0, o = 0; i < sizeof kJsParts / sizeof kJsParts[0]; i++) {
    size_t n = strlen(kJsParts[i]);
    memcpy(kJs + o, kJsParts[i], n);
    o += n;
  }
  kJs[o] = '\0';
  kJsLen = o;
  i = 0;
  o = 0;
  if (gsec_sha256(kJs, kJsLen, dig) != GSEC_OK) {
    snprintf(kPolicy, sizeof kPolicy,
        "default-src 'none'; script-src 'none'; style-src 'self'; "
        "frame-ancestors 'none'");
    snprintf(kPublic, sizeof kPublic, "%s", kPublicBase);
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
  snprintf(kPublic, sizeof kPublic, "%s 'self' '%s'; %s", kPublicHead, kHash,
      kPublicTail);
  kAssets = 1;
}

const char * gdefweb_public_csp(void) {
  ensure_assets();
  return kPublic;
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

const char * gdefweb_admin_signout_failed_page(void) {
  /* Nothing here asks the store or the catalog: the page is answered because
   * the store could not be, so it must not need it. It says first that the
   * session is still active, because a person who sees an error page after
   * pressing Sign out may take it for done. It carries no form: a retry needs a
   * fresh signed token, which may need the same store. */
  static const char kPage[] =
      "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<title>Still signed in</title>"
      "<link rel=\"stylesheet\" href=\"/defiant.css\"></head><body>"
      "<h1>You are still signed in</h1>"
      "<p>Signing out did not complete, so your session is still active.</p>"
      "<p>Go back to the page you were on and press Sign out again in a moment. "
      "If it keeps failing, ask an administrator to end your session before you "
      "leave this computer.</p>"
      "<p><a href=\"/admin\">Back to the admin</a></p></body></html>";
  return kPage;
}

int gdefweb_admin_static(const char * path, const char ** body, size_t * len,
    const char ** type) {
  if (path == NULL || body == NULL || len == NULL || type == NULL) {
    return 0;
  }
  if (strcmp(path, "/defiant.js") == 0) {
    ensure_assets();
    *body = kJs;
    *len = kJsLen;
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

int gdefweb_admin_authenticated(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session) {
  unsigned char raw[768];
  size_t raw_n = 0;
  int ok = 0;
  if (session == NULL || session[0] == '\0' ||
      b64_decode(session, raw, sizeof raw, &raw_n) != 0) {
    return 0;
  }
  if (gdefcore_account_check(catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n) == GDEFCORE_OK) {
    ok = 1;
  }
  wipe(raw, sizeof raw);
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
      page_str(page, "\"></script></head><body>" GDEFWEB_NAV_MARK) != 0) {
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

static int session_raw(const char * session, unsigned char * raw, size_t cap,
    size_t * out_n) {
  if (session == NULL || session[0] == '\0') {
    return -1;
  }
  return b64_decode(session, raw, cap, out_n);
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
    /* region widget [target]: no trailing space when there is no target. */
    memcpy(held[i].line, region, region_n);
    held[i].line[region_n] = ' ';
    memcpy(held[i].line + region_n + 1, kind, kind_n);
    if (target_n > 0) {
      held[i].line[region_n + 1 + kind_n] = ' ';
      memcpy(held[i].line + region_n + kind_n + 2, target, target_n);
      held[i].line[region_n + kind_n + target_n + 2] = '\0';
    } else {
      held[i].line[region_n + 1 + kind_n] = '\0';
    }
    held[i].recent = strcmp(held[i].kind, "recent") == 0 &&
                     (target_n == 0 || strcmp(held[i].target, "recent") == 0);
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
  /* The write asks the same decision the page did: the layouts, for this
   * principal (a grant of the rules alone does not edit a display). */
  gdefcore_changeset_set_request(cs, request);
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

/* A redirect to a path this file made (an admin page's own). */
static int redirect_to(struct GDEFWEB_Admin_Reply * out, const char * path) {
  if (snprintf(out->location_buf, sizeof out->location_buf, "%s", path) >=
      (int)sizeof out->location_buf) {
    return redirect(out, "/admin");
  }
  (void)redirect(out, out->location_buf);
  return 0;
}

/*
 * Leave the session a message for the page a redirect goes to (the core's
 * message queue: sealed, bound to this session, shown once by the messages
 * widget in the page's navigation). A request with no queue, or a store that
 * cannot take it, loses the message and not the redirect: the write it reports
 * has been decided already.
 */
static void say(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, int kind,
    const char * text) {
  (void)gdefcore_message_post(catalog, request, kind, text, strlen(text));
}

/* Bind the session's queue to the request, for a request that carries a live
 * admin session. Nothing is bound for any other. */
void gdefweb_admin_bind_messages(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session) {
  unsigned char raw[768];
  size_t raw_n = 0;
  if (catalog == NULL || request == NULL || session == NULL ||
      session_raw(session, raw, sizeof raw, &raw_n) != 0) {
    return;
  }
  (void)gdefcore_message_bind(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);
  wipe(raw, sizeof raw);
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
  /* The way to every page is the menu, drawn where the marker is. */
  if (page_str(&page, kHash) != 0 ||
      page_str(&page, "\"></script></head><body>" GDEFWEB_NAV_MARK "</body></html>") != 0) {
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

/*
 * /admin/more: every page the admin menu leads to that this administrator may
 * follow, grouped by the module that declared it. Nothing here names a page: the
 * entries are the admin menu's (the kernel's links, the links of every enabled
 * module, the site's own), computed for this request, so an entry whose route
 * the viewer's own check refuses is absent, and a module that is enabled brings
 * its heading with it. An entry's module is the part of its machine name before
 * the first dot, which a manifest is held to (AD-35); a module's heading is its
 * machine name, and the kernel's is `core`.
 */
#define MORE_MODULES 64

struct MoreItem {
  char module[64];
  size_t module_len;
  const char * label;
  size_t label_len;
  const char * href;
  size_t href_len;
};

static int more_module_cmp(const void * a, const void * b) {
  const char * x = (const char *)a;
  const char * y = (const char *)b;
  /* The kernel first, then by name. */
  if (strcmp(x, "core") == 0 || strcmp(y, "core") == 0) {
    return strcmp(x, "core") == 0 ? (strcmp(y, "core") == 0 ? 0 : -1) : 1;
  }
  return strcmp(x, y);
}

static int more_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Routes * routes,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  GDEFCORE_Route_Match * match = NULL;
  GDEFCORE_Menu * menu = NULL;
  struct MoreItem * items = NULL;
  char modules[MORE_MODULES][64];
  size_t nmodules = 0;
  size_t n;
  size_t nitems = 0;
  size_t i;
  size_t m;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (routes != NULL) {
    (void)gdefcore_routes_match(routes, request, "/admin/more", 11, &match);
    if (gdefcore_menu_tree(catalog, routes, request, match, "admin", 5, &menu) !=
        GDEFCORE_OK) {
      menu = NULL;
    }
  }
  n = gdefcore_menu_count(menu);
  if (n > 0) {
    items = (struct MoreItem *)gcu_allocator_calloc(alloc, n, sizeof *items);
    if (items == NULL) {
      goto fail;
    }
  }
  for (i = 0; i < n; i++) {
    GDEFCORE_Menu_Item it;
    const char * dot;
    size_t len;
    int seen = 0;
    memset(&it, 0, sizeof it);
    it.size = sizeof it;
    if (gdefcore_menu_item(menu, i, &it) != GDEFCORE_OK) {
      goto fail;
    }
    /* An entry that only groups others has no page; the page being shown is
     * not offered to the one reading it. */
    if (it.href_len == 0 || it.current) {
      continue;
    }
    dot = (const char *)memchr(it.name, '.', it.name_len);
    len = dot == NULL ? it.name_len : (size_t)(dot - it.name);
    if (len == 0 || len >= sizeof items[0].module) {
      continue;
    }
    memcpy(items[nitems].module, it.name, len);
    items[nitems].module[len] = '\0';
    items[nitems].module_len = len;
    items[nitems].label = it.label;
    items[nitems].label_len = it.label_len;
    items[nitems].href = it.href;
    items[nitems].href_len = it.href_len;
    for (m = 0; m < nmodules; m++) {
      if (strcmp(modules[m], items[nitems].module) == 0) {
        seen = 1;
      }
    }
    if (!seen && nmodules < MORE_MODULES) {
      memcpy(modules[nmodules++], items[nitems].module, len + 1);
    }
    nitems += 1;
  }
  qsort(modules, nmodules, sizeof modules[0], more_module_cmp);
  if (page_begin(&page, "More") != 0 || page_str(&page, "<h1>More</h1>") != 0) {
    goto fail;
  }
  for (m = 0; m < nmodules; m++) {
    if (page_str(&page, "<section class=\"more-module\"><h2>") != 0 ||
        page_esc(&page, modules[m], strlen(modules[m])) != 0 ||
        page_str(&page, "</h2><ul>") != 0) {
      goto fail;
    }
    for (i = 0; i < nitems; i++) {
      if (strcmp(items[i].module, modules[m]) != 0) {
        continue;
      }
      if (page_str(&page, "<li><a href=\"") != 0 ||
          page_esc(&page, items[i].href, items[i].href_len) != 0 ||
          page_str(&page, "\">") != 0 ||
          page_esc(&page, items[i].label, items[i].label_len) != 0 ||
          page_str(&page, "</a></li>") != 0) {
        goto fail;
      }
    }
    if (page_str(&page, "</ul></section>") != 0) {
      goto fail;
    }
  }
  gcu_allocator_free(alloc, items);
  gdefcore_menu_free(menu);
  gdefcore_route_match_free(match);
  return take_page(&page, out);
fail:
  gcu_allocator_free(alloc, items);
  gdefcore_menu_free(menu);
  gdefcore_route_match_free(match);
  page_free(&page);
  return -1;
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
    size_t root_len, const char * session, size_t session_len,
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

/* Whether the request holds @p permission: the core's one question, asked by
 * the action's name. The undo page covers several kinds of thing, and each is
 * undone only by who holds what governs that kind. */
static int holds(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * permission) {
  return gdefcore_access_permits(catalog, request, permission, strlen(permission));
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

static void menu_say_refused(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * refusal);
static size_t menu_confirm(GDEFCORE_Changeset * cs, char * buf, size_t cap);

/*
 * The undo of the last menu edit. A menu edit touches several entries at once,
 * so the entity-by-entity undo below cannot take it back whole; the core keeps
 * the record of the last edit (gdefcore_menu_stage_undo()) and puts every
 * entry back in one changeset, or refuses if any was written since. Shown to
 * who may edit menus, and decided again entry by entry when it is applied.
 */
static int undo_menu_section(const GDEFWEB_Allocator * alloc, struct Page * page,
    GDEFCORE_Catalog * catalog, GDEFCORE_Request * request, GDEFCORE_Keyring * keyring,
    const uint8_t * root, size_t root_len, const char * session, size_t session_len) {
  char * token = NULL;
  size_t who_n = 0;
  const char * principal;
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  if (!gdefcore_menu_may_edit(catalog, request)) {
    return 0;
  }
  principal = gdefcore_request_principal(request, &who_n);
  if (principal == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return 0;
  }
  memcpy(who, principal, GDEFCORE_PUBLIC_ID_LEN);
  who[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "menu-undo", who, NULL, 0, &token) != 0) {
    return -1;
  }
  if (page_str(page, "<h2>Menus</h2><form method=\"post\" action=\"/admin/undo\">"
                     "<p>Take back the last change made to the menus, whatever it was "
                     "(a move, a rename, an entry added or removed). Every entry it "
                     "wrote is put back as it was. It cannot be taken back once the "
                     "menus have been changed again.</p>"
                     "<input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(page, token, strlen(token)) != 0 ||
      page_str(page, "\"><button type=\"submit\">Undo the last menu change</button>"
                     "</form>") != 0) {
    gcu_allocator_free(alloc, token);
    return -1;
  }
  gcu_allocator_free(alloc, token);
  return 0;
}

/* Handle a post of the menu undo form. @p handled is set when the token was
 * this form's, whatever came of it; any other post is the caller's. */
static int undo_menu_write(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * session, size_t session_len, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out, int * handled) {
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Changeset * cs = NULL;
  char confirm[96];
  size_t confirm_n;
  GDEFCORE_Result rc;
  *handled = 0;
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 9 || memcmp(form_id, "menu-undo", 9) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    return 0;
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  *handled = 1;
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN ||
      memcmp(target, who, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was undone.");
    return redirect(out, "/admin/undo");
  }
  if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Nothing was undone.");
    return redirect(out, "/admin/undo");
  }
  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_menu_stage_undo(cs, request);
  if (rc != GDEFCORE_OK) {
    menu_say_refused(catalog, request, gdefcore_changeset_refusal(cs, NULL));
  } else {
    confirm_n = menu_confirm(cs, confirm, sizeof confirm);
    rc = confirm_n > 0 ? gdefcore_changeset_apply(cs, confirm, confirm_n)
                       : GDEFCORE_ERR_INVALID;
    if (rc == GDEFCORE_OK) {
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, "The last menu change was undone.");
    } else {
      menu_say_refused(catalog, request, gdefcore_changeset_refusal(cs, NULL));
    }
  }
  gdefcore_changeset_destroy(cs);
  return redirect(out, "/admin/undo");
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
  int may_posts = holds(catalog, request, GDEFCORE_POST_WRITE);
  int may_front = holds(catalog, request, GDEFCORE_LAYOUT_EDIT);
  int may_roles = holds(catalog, request, GDEFCORE_ROLE_REVERT);
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
  for (i = 0; may_posts && i < count; i++) {
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
  if (may_front && find_front(catalog, front) == 0 &&
      undo_form(alloc, &page, catalog, request, keyring, root, root_len, session,
          session_len, front, "Front page", display_values, 2) != 0) {
    page_free(&page);
    return -1;
  }
  if (gdefcore_entity_ids(catalog, "core.role", 9, &ids, &count) != GDEFCORE_OK) {
    page_free(&page);
    return -1;
  }
  for (i = 0; may_roles && i < count; i++) {
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
  if (undo_menu_section(alloc, &page, catalog, request, keyring, root, root_len,
          session, session_len) != 0) {
    page_free(&page);
    return -1;
  }
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
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Not saved.");
    return redirect(out, "/admin/post");
  }
  /* What the token says it is for is read here to refuse early. Whether it is
   * genuine is settled once, by gdefcore_form_resign() below, which opens it
   * against this session before anything is signed or written; a second
   * signature check in this function could never be seen to matter, because
   * that one refuses first, and so is not here. */
  if (token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "post", 4) != 0 ||
      !base_matches(catalog, target, base)) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Not saved.");
    return redirect(out, "/admin/post");
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
  say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Saved.");
  return redirect(out, "/admin/post");
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
  say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Not saved.");
  return redirect(out, "/admin/post");
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
    if (len != 11 ||
        (memcmp(text + start, "main recent", 11) != 0 &&
            memcmp(text + start, "side recent", 11) != 0)) {
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

/* The write behind the page asks the decision the page asked and may say no (a
 * person who may edit the layout rules and not the displays reaches this page).
 * The answer is a fixed sentence that names nothing. */
static int front_refused(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    struct GDEFWEB_Admin_Reply * out) {
  say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That change was not made.");
  return redirect(out, "/admin/front");
}

static int front_write(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  (void)alloc;
  const char * stale = "That form is out of date. Nothing was changed.";
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
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, stale);
    return redirect(out, "/admin/front");
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  if (find_front(catalog, front) != 0 ||
      memcmp(target, front, GDEFCORE_PUBLIC_ID_LEN) != 0 ||
      !base_matches(catalog, front, base) ||
      load_panes(catalog, front, held, 32, &npanes) != 0) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, stale);
    return redirect(out, "/admin/front");
  }
  if (form_len == 5 && memcmp(form_id, "place", 5) == 0) {
    kind = 1;
  } else if (form_len == 6 && memcmp(form_id, "remove", 6) == 0) {
    kind = 2;
  } else if (form_len == 5 && memcmp(form_id, "order", 5) == 0) {
    kind = 3;
  } else {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, stale);
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
      lines[nlines++] = "side recent";
      if (save_panes(catalog, request, front, lines, nlines) != 0) {
        return front_refused(catalog, request, out);
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
      return front_refused(catalog, request, out);
    }
  } else {
    char text[4096];
    size_t text_n = 0;
    size_t posted_n = 0;
    if (form_pair(body, body_len, "panes", text, sizeof text, &text_n) != 0 ||
        panes_from_post(text, text_n, posted, &posted_n) != 0) {
      say(catalog, request, GDEFCORE_MESSAGE_ERROR,
          "That order was not understood. Nothing was changed.");
      return redirect(out, "/admin/front");
    }
    for (i = 0; i < posted_n; i++) {
      lines[i] = posted[i];
    }
    if (save_panes(catalog, request, front, lines, posted_n) != 0) {
      return front_refused(catalog, request, out);
    }
  }
  say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Saved.");
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
  int menu_handled = 0;
  int menu_rc;
  char * type = NULL;
  size_t type_n = 0;
  uint64_t base = 0;
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Changeset * cs = NULL;
  int risk = 0;
  int allowed = 0;
  GDEFCORE_Result rc;
  menu_rc = undo_menu_write(catalog, request, keyring, root, root_len, session,
      session_len, body, body_len, out, &menu_handled);
  if (menu_handled) {
    return menu_rc;
  }
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 4 || memcmp(form_id, "undo", 4) != 0 ||
      !base_matches(catalog, target, base)) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was undone.");
    return redirect(out, "/admin/undo");
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  if (gdefcore_entity_type(catalog, target, GDEFCORE_PUBLIC_ID_LEN, &type, &type_n) !=
      GDEFCORE_OK) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Nothing was undone.");
    return redirect(out, "/admin/undo");
  }
  /* What is undone here is what the principal holds the permission of: a
   * post, a role, the front page. The page that offered it asked the same. */
  if (type_n == 9 && memcmp(type, "site.post", 9) == 0) {
    allowed = holds(catalog, request, GDEFCORE_POST_WRITE);
  } else if (type_n == 9 && memcmp(type, "core.role", 9) == 0) {
    allowed = holds(catalog, request, GDEFCORE_ROLE_REVERT);
  } else if (type_n == 12 && memcmp(type, "core.display", 12) == 0) {
    char name[64];
    if (field_copy(catalog, target, "name", name, sizeof name) == 0 &&
        strcmp(name, "front") == 0) {
      allowed = holds(catalog, request, GDEFCORE_LAYOUT_EDIT);
    }
  }
  if (!allowed) {
    gdefcore_text_free(catalog, type);
    say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That cannot be undone here.");
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
    say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Nothing was undone.");
    return redirect(out, "/admin/undo");
  }
  say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Undone.");
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
  /* The way to all templates and to the history is the breadcrumbs and the tabs. */
  if (page_begin(&page, "Template") != 0 || page_str(&page, "<h1>Template ") != 0 ||
      page_esc(&page, name, strlen(name)) != 0 || page_str(&page, "</h1>") != 0) {
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
      page_esc(&page, name, name_len) != 0 || page_str(&page, "</h1>") != 0) {
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

/*
 * /admin/menus: every entry of every menu and every tab, with whether the site
 * hid it, changed it, and whether this administrator would see it; and the
 * forms to change them. Only who may edit menus is shown the page at all
 * (the route's own check, asked by the caller), and a write is decided again,
 * entry by entry, when its changeset is applied (gdefcore_menu_authorize()).
 *
 * Every action is a form: add, rename, hide or show, remove, move up or down,
 * indent, outdent, and move to a parent and a position. Each posts, redirects
 * to the page and leaves a message. A script (the one /defiant.js) makes the
 * same posts from a drag and from the keys, by filling in the move form and
 * submitting it, and announces them in a live region; it takes nothing from the
 * server but the page, and it writes labels only as text.
 *
 * A form names the version of the menus the page showed. If the menus are not
 * that when the form is posted, nothing is written, and the page the post
 * redirects to is the menus as they are now.
 *
 * A generated entry is changed with an override that stores only what is set,
 * keyed by the entry's machine name (the core writes it); Reset takes the
 * override away. A site entry is edited as it is. Every text in a row was
 * written by someone, so every one is escaped.
 */


static const char kTypeEntry[] = GDEFCORE_MENU_ENTRY_TYPE;
static const char kTypeOverride[] = GDEFCORE_MENU_OVERRIDE_TYPE;
static const char kTypeChange[] = GDEFCORE_MENU_CHANGE_TYPE;

static const char * menu_source_word(int source) {
  switch (source) {
  case GDEFCORE_MENU_SOURCE_KERNEL:
    return "kernel";
  case GDEFCORE_MENU_SOURCE_MODULE:
    return "module";
  case GDEFCORE_MENU_SOURCE_TAB:
    return "tab";
  case GDEFCORE_MENU_SOURCE_SITE:
    return "site";
  default:
    return "page";
  }
}

/* A hidden field, with the value escaped. */
static int menu_hidden(struct Page * page, const char * name, const char * value,
    size_t n) {
  return page_str(page, "<input type=\"hidden\" name=\"") != 0 ||
         page_str(page, name) != 0 || page_str(page, "\" value=\"") != 0 ||
         page_esc(page, value, n) != 0 || page_str(page, "\">") != 0;
}

/* A labelled text field, with the value escaped. */
static int menu_field(struct Page * page, const char * label, const char * name,
    const char * value, size_t n) {
  /* Digits are written as character references, as every number in these pages
   * is, so that a weight is not mistaken for an internal id in a response. */
  return page_str(page, "<label>") != 0 || page_str(page, label) != 0 ||
         page_str(page, " <input type=\"text\" name=\"") != 0 ||
         page_str(page, name) != 0 || page_str(page, "\" value=\"") != 0 ||
         page_esc_digits(page, value, n) != 0 || page_str(page, "\"></label> ");
}

/* The start of a form of this page: @p id may be NULL. */
static int menu_form_start(struct Page * page, const char * id, const char * token) {
  if (page_str(page, "<form method=\"post\" action=\"/admin/menus\"") != 0 ||
      (id != NULL && (page_str(page, " id=\"") != 0 || page_str(page, id) != 0 ||
                         page_str(page, "\"") != 0)) ||
      page_str(page, "><input type=\"hidden\" name=\"token\" value=\"") != 0 ||
      page_esc(page, token, strlen(token)) != 0 || page_str(page, "\">") != 0) {
    return -1;
  }
  return 0;
}

static int menu_form_open(struct Page * page, const char * token) {
  return menu_form_start(page, NULL, token);
}

/* A submit button that says which action it is. */
static int menu_button(struct Page * page, const char * action, const char * text) {
  return page_str(page, "<button type=\"submit\" name=\"action\" value=\"") != 0 ||
         page_str(page, action) != 0 || page_str(page, "\">") != 0 ||
         page_str(page, text) != 0 || page_str(page, "</button> ") != 0;
}

/* The purposes of the page's forms: the form's name is what its token says it
 * is for. */
enum { kTokOverride, kTokReset, kTokEntry, kTokForget, kTokEdit, kTokMove, kTokCount };
static const char * const kMenuPurpose[kTokCount] = {"menu-override", "menu-reset",
    "menu-entry", "menu-forget", "menu-edit", "menu-move"};

struct MenuTokens {
  char * t[kTokCount];
};

static void menu_tokens_free(const GDEFWEB_Allocator * alloc, struct MenuTokens * t) {
  int i;
  for (i = 0; i < kTokCount; i++) {
    gcu_allocator_free(alloc, t->t[i]);
  }
  memset(t, 0, sizeof *t);
}

/* A token for each form. The target is the administrator, who exists; the token
 * is bound to the session as every form's is. */
static int menu_tokens(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, struct MenuTokens * t) {
  size_t who_n = 0;
  const char * principal = gdefcore_request_principal(request, &who_n);
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  int i;
  memset(t, 0, sizeof *t);
  if (principal == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return -1;
  }
  /* A copy: signing checks the session again, which sets the request's
   * principal afresh, and the id it was is not kept where it was. */
  memcpy(who, principal, GDEFCORE_PUBLIC_ID_LEN);
  who[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  for (i = 0; i < kTokCount; i++) {
    if (sign_b64(alloc, catalog, request, keyring, root, root_len, session,
            session_len, kMenuPurpose[i], who, NULL, 0, &t->t[i]) != 0) {
      menu_tokens_free(alloc, t);
      return -1;
    }
  }
  return 0;
}

/* The name of the entry the item sits under in this list, or "". */
static void menu_parent_name(const GDEFCORE_Menu * menu, const GDEFCORE_Menu_Item * it,
    const char ** name, size_t * n) {
  GDEFCORE_Menu_Item up;
  *name = "";
  *n = 0;
  if (it->up == GDEFCORE_MENU_NO_PARENT) {
    return;
  }
  memset(&up, 0, sizeof up);
  up.size = sizeof up;
  if (gdefcore_menu_item(menu, it->up, &up) == GDEFCORE_OK) {
    *name = up.name;
    *n = up.name_len;
  }
}

/* The cell of a row with the forms that act on it. */
static int menu_row_forms(struct Page * page, const struct MenuTokens * tokens,
    const char * version, const GDEFCORE_Menu_Item * it) {
  int site = it->source == GDEFCORE_MENU_SOURCE_SITE;
  int tab = it->source == GDEFCORE_MENU_SOURCE_TAB;
  if (!tab) {
    /* Where it stands among its siblings: one form, four buttons. */
    if (menu_form_start(page, NULL, tokens->t[kTokMove]) != 0 ||
        menu_hidden(page, "version", version, strlen(version)) != 0 ||
        menu_hidden(page, "name", it->name, it->name_len) != 0 ||
        menu_button(page, "up", "Move up") != 0 ||
        menu_button(page, "down", "Move down") != 0 ||
        menu_button(page, "indent", "Indent") != 0 ||
        menu_button(page, "outdent", "Outdent") != 0 ||
        page_str(page, "</form>") != 0) {
      return -1;
    }
  }
  /* Its name, whether it is shown, and for a site entry that it goes. */
  if (menu_form_start(page, NULL, tokens->t[kTokEdit]) != 0 ||
      menu_hidden(page, "version", version, strlen(version)) != 0 ||
      menu_hidden(page, "name", it->name, it->name_len) != 0 ||
      menu_field(page, "Name", "label", it->label, it->label_len) != 0 ||
      menu_button(page, "rename", "Rename") != 0 ||
      menu_button(page, it->hidden ? "show" : "hide", it->hidden ? "Show" : "Hide") != 0 ||
      (site && menu_button(page, "remove", "Remove") != 0) ||
      page_str(page, "</form>") != 0) {
    return -1;
  }
  if (site) {
    if (menu_form_open(page, tokens->t[kTokEntry]) != 0 ||
        menu_hidden(page, "name", it->name, it->name_len) != 0 ||
        menu_field(page, "Label", "label", it->label, it->label_len) != 0 ||
        menu_field(page, "Menu", "menu", it->menu, it->menu_len) != 0 ||
        menu_field(page, "Route", "route", it->route, it->route_len) != 0 ||
        page_str(page, "<button type=\"submit\">Save</button></form>") != 0 ||
        menu_form_open(page, tokens->t[kTokForget]) != 0 ||
        menu_hidden(page, "name", it->name, it->name_len) != 0 ||
        page_str(page, "<button type=\"submit\">Forget</button></form>") != 0) {
      return -1;
    }
  } else {
    if (menu_form_open(page, tokens->t[kTokOverride]) != 0 ||
        menu_hidden(page, "name", it->name, it->name_len) != 0 ||
        menu_field(page, "Menu", "menu", "", 0) != 0 ||
        page_str(page, "<button type=\"submit\">Change the menu</button></form>") != 0) {
      return -1;
    }
    if (it->overridden &&
        (menu_form_open(page, tokens->t[kTokReset]) != 0 ||
            menu_hidden(page, "name", it->name, it->name_len) != 0 ||
            page_str(page, "<button type=\"submit\">Reset</button></form>") != 0)) {
      return -1;
    }
  }
  return 0;
}

static int menus_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    GDEFCORE_Routes * routes, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  struct MenuTokens tokens;
  GDEFCORE_Route_Match * match = NULL;
  GDEFCORE_Menu * menu = NULL;
  char version[GDEFCORE_MENU_VERSION_LEN + 1];
  size_t i;
  int rc = -1;
  memset(&page, 0, sizeof page);
  memset(&tokens, 0, sizeof tokens);
  page.alloc = alloc;
  if (gdefcore_routes_match(routes, request, "/admin/menus", 12, &match) !=
          GDEFCORE_OK ||
      gdefcore_menu_entries(catalog, routes, request, match, &menu) != GDEFCORE_OK ||
      gdefcore_menu_version(menu, version, sizeof version) != GDEFCORE_OK ||
      menu_tokens(alloc, catalog, request, keyring, root, root_len, session,
          session_len, &tokens) != 0) {
    goto done;
  }
  if (page_begin(&page, "Menus") != 0 || page_str(&page, "<h1>Menus</h1>") != 0) {
    goto done;
  }
  /* How to move an entry, and the one place the script says what it did. */
  if (page_str(&page,
          "<p id=\"menu-help\">Each entry has buttons to move it up or down among "
          "its siblings, to indent it under the entry above, and to take it out "
          "from under its parent; the form below moves one to any parent and "
          "position. With scripts on, drag a row onto another (above it, onto it "
          "to go under it, or below it), or focus a row and press Alt and Shift "
          "with an arrow key: Alt+Shift+Up and Alt+Shift+Down move it, "
          "Alt+Shift+Right indents it, Alt+Shift+Left outdents it. Every move "
          "is one change that Undo takes back.</p>"
          "<p id=\"menu-live\" class=\"live\" role=\"status\" aria-live=\"polite\">"
          "</p>") != 0 ||
      menu_form_start(&page, "menu-move-form", tokens.t[kTokMove]) != 0 ||
      menu_hidden(&page, "action", "move", 4) != 0 ||
      menu_hidden(&page, "version", version, strlen(version)) != 0 ||
      page_str(&page, "<h2>Move an entry</h2>") != 0 ||
      menu_field(&page, "Entry (its machine name)", "name", "", 0) != 0 ||
      menu_field(&page, "Under (a machine name, or nothing for the top)", "parent", "",
          0) != 0 ||
      menu_field(&page, "Position (counting from the first, which is zero)", "position", "0", 1) != 0 ||
      page_str(&page, "<button type=\"submit\">Move</button></form>") != 0) {
    goto done;
  }
  if (page_str(&page, "<table id=\"menu-editor\" class=\"menus\"><tr><th>Entry</th>"
                      "<th>Name</th><th>Menu</th><th>Goes to</th><th>State</th>"
                      "<th>Change</th></tr>") != 0) {
    goto done;
  }
  for (i = 0; i < gdefcore_menu_count(menu); i++) {
    GDEFCORE_Menu_Item it;
    const char * parent;
    size_t parent_n;
    int d;
    memset(&it, 0, sizeof it);
    it.size = sizeof it;
    if (gdefcore_menu_item(menu, i, &it) != GDEFCORE_OK) {
      goto done;
    }
    menu_parent_name(menu, &it, &parent, &parent_n);
    if (it.source == GDEFCORE_MENU_SOURCE_TAB) {
      if (page_str(&page, "<tr>") != 0) {
        goto done;
      }
    } else if (page_str(&page, "<tr id=\"e-") != 0 ||
               page_esc(&page, it.name, it.name_len) != 0 ||
               page_str(&page, "\" data-entry=\"") != 0 ||
               page_esc(&page, it.name, it.name_len) != 0 ||
               page_str(&page, "\" data-menu=\"") != 0 ||
               page_esc(&page, it.menu, it.menu_len) != 0 ||
               page_str(&page, "\" data-parent=\"") != 0 ||
               page_esc(&page, parent, parent_n) != 0 ||
               page_str(&page, "\" data-label=\"") != 0 ||
               page_esc(&page, it.label, it.label_len) != 0 ||
               page_str(&page, "\" tabindex=\"&#48;\" draggable=\"true\">") != 0) {
      goto done;
    }
    if (page_str(&page, "<td>") != 0) {
      goto done;
    }
    for (d = 0; d < it.depth; d++) {
      if (page_str(&page, "&ndash; ") != 0) {
        goto done;
      }
    }
    if (page_esc(&page, it.label, it.label_len) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, it.name, it.name_len) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, it.menu, it.menu_len) != 0 ||
        page_str(&page, it.menu_len == 0 ? "(tab)" : "") != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, it.route, it.route_len) != 0 ||
        page_str(&page, "</td><td>") != 0 ||
        page_str(&page, menu_source_word(it.source)) != 0 ||
        page_str(&page, it.hidden ? ", hidden" : "") != 0 ||
        page_str(&page, it.overridden ? ", changed" : "") != 0 ||
        page_str(&page, it.visible ? "" : ", not shown to you") != 0 ||
        page_str(&page, "</td><td>") != 0 ||
        menu_row_forms(&page, &tokens, version, &it) != 0 ||
        page_str(&page, "</td></tr>") != 0) {
      goto done;
    }
  }
  if (page_str(&page, "</table><h2>Add an entry</h2>") != 0 ||
      menu_form_open(&page, tokens.t[kTokEdit]) != 0 ||
      menu_hidden(&page, "version", version, strlen(version)) != 0 ||
      menu_field(&page, "Name (after site.)", "suffix", "", 0) != 0 ||
      menu_field(&page, "Menu", "menu", "main", 4) != 0 ||
      menu_field(&page, "Label", "label", "", 0) != 0 ||
      menu_field(&page, "Route", "route", "", 0) != 0 ||
      menu_field(&page, "Under", "parent", "", 0) != 0 ||
      menu_button(&page, "add", "Add") != 0 || page_str(&page, "</form>") != 0) {
    goto done;
  }
  rc = take_page(&page, out);
  if (rc == 0) {
    memset(&page, 0, sizeof page);
  }
done:
  if (rc != 0) {
    page_free(&page);
  }
  menu_tokens_free(alloc, &tokens);
  gdefcore_menu_free(menu);
  gdefcore_route_match_free(match);
  return rc;
}

/* One posted field, trimmed of nothing: what the person typed. Empty when it
 * was not posted. */
static size_t menu_post(const char * body, size_t body_len, const char * name,
    char * out, size_t cap) {
  size_t n = 0;
  if (form_pair(body, body_len, name, out, cap, &n) != 0) {
    out[0] = '\0';
    return 0;
  }
  return n;
}

/* The override already stored for @p name, or none: each field as stored, empty
 * when the override does not set it. 0 on success, -1 on a failure to read. */
static int menu_override_stored(GDEFCORE_Catalog * catalog, const char * name,
    size_t name_len, char * label, char * weight, char * hidden, char * parent,
    char * menu, size_t cap) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  struct { const char * field; char * to; } want[] = {
      {"label", label}, {"weight", weight}, {"hidden", hidden}, {"parent", parent},
      {"menu", menu}};
  size_t k;
  int found;
  label[0] = weight[0] = hidden[0] = parent[0] = menu[0] = '\0';
  found = find_by_name(catalog, kTypeOverride, name, name_len, id);
  if (found < 0) {
    return -1;
  }
  if (found == 1) {
    return 0;
  }
  for (k = 0; k < sizeof want / sizeof want[0]; k++) {
    char * text = NULL;
    size_t n = 0;
    if (stored_text(catalog, id, want[k].field, &text, &n) != 0) {
      return -1;
    }
    if (n < cap) {
      memcpy(want[k].to, text, n);
      want[k].to[n] = '\0';
    }
    free(text);
  }
  return 0;
}

/* The field of the site entry called @p name as stored, empty when it has none
 * or there is no such entry. 0 on success, -1 on a failure to read. */
static int menu_entry_stored(GDEFCORE_Catalog * catalog, const char * name,
    size_t name_len, const char * field, char * out, size_t cap) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * text = NULL;
  size_t n = 0;
  int found = find_by_name(catalog, kTypeEntry, name, name_len, id);
  out[0] = '\0';
  if (found < 0) {
    return -1;
  }
  if (found == 1) {
    return 0;
  }
  if (stored_text(catalog, id, field, &text, &n) != 0) {
    return -1;
  }
  if (n < cap) {
    memcpy(out, text, n);
    out[n] = '\0';
  }
  free(text);
  return 0;
}

/* Copy @p from into @p to, or leave @p to empty if it does not fit. */
static void menu_copy(char * to, size_t cap, const char * from) {
  size_t n = strlen(from);
  if (n + 1 > cap) {
    n = 0;
  }
  memcpy(to, from, n);
  to[n] = '\0';
}

static int menu_int(const char * s, int * out) {
  size_t i = 0;
  long v = 0;
  int neg = 0;
  if (s[0] == '-') {
    neg = 1;
    i = 1;
  }
  if (s[i] == '\0' || strlen(s + i) > 9) {
    return -1;
  }
  for (; s[i] != '\0'; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return -1;
    }
    v = v * 10 + (s[i] - '0');
  }
  *out = (int)(neg ? -v : v);
  return 0;
}

/* The typed confirmation of a write of menus: the names of the types the
 * changeset writes, sorted, one to a line. 0 when it writes anything else. */
static size_t menu_confirm(GDEFCORE_Changeset * cs, char * buf, size_t cap) {
  int has_change = 0;
  int has_entry = 0;
  int has_override = 0;
  size_t i;
  size_t n = 0;
  for (i = 0; i < gdefcore_changeset_touched_count(cs); i++) {
    const char * type = NULL;
    size_t type_n = 0;
    if (gdefcore_changeset_touched_at(cs, i, NULL, NULL, &type, &type_n, NULL) !=
            GDEFCORE_OK ||
        type == NULL) {
      return 0;
    }
    if (type_n == sizeof kTypeChange - 1 && memcmp(type, kTypeChange, type_n) == 0) {
      has_change = 1;
    } else if (type_n == sizeof kTypeEntry - 1 && memcmp(type, kTypeEntry, type_n) == 0) {
      has_entry = 1;
    } else if (type_n == sizeof kTypeOverride - 1 &&
               memcmp(type, kTypeOverride, type_n) == 0) {
      has_override = 1;
    } else {
      return 0;
    }
  }
  buf[0] = '\0';
  if (has_change) {
    n += (size_t)snprintf(buf + n, cap - n, "%s%s", n > 0 ? "\n" : "", kTypeChange);
  }
  if (has_entry) {
    n += (size_t)snprintf(buf + n, cap - n, "%s%s", n > 0 ? "\n" : "", kTypeEntry);
  }
  if (has_override) {
    n += (size_t)snprintf(buf + n, cap - n, "%s%s", n > 0 ? "\n" : "", kTypeOverride);
  }
  return n;
}

/* Apply a changeset staged by @p stage as the administrator, with the request
 * (so the core asks who may edit menus), the record of what it wrote for Undo,
 * and the types it writes as the typed confirmation. */
static int menu_apply(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    int (*stage)(GDEFCORE_Changeset *, void *), void * ctx) {
  GDEFCORE_Changeset * cs = NULL;
  const char * who;
  size_t who_n = 0;
  char confirm[96];
  size_t confirm_n;
  GDEFCORE_Result rc;
  who = gdefcore_request_principal(request, &who_n);
  if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, request);
  rc = stage(cs, ctx) == 0 ? gdefcore_menu_stage_record(cs) : GDEFCORE_ERR_INVALID;
  confirm_n = rc == GDEFCORE_OK ? menu_confirm(cs, confirm, sizeof confirm) : 0;
  if (rc == GDEFCORE_OK) {
    rc = confirm_n > 0 ? gdefcore_changeset_apply(cs, confirm, confirm_n)
                       : GDEFCORE_ERR_INVALID;
  }
  gdefcore_changeset_destroy(cs);
  return rc == GDEFCORE_OK ? 0 : -1;
}

struct MenuWrite {
  char name[160];
  char label[512];
  char weight[16];
  char hidden[8];
  char parent[160];
  char menu[80];
  char route[160];
  int has_weight_value;
  int weight_value;
};

static int menu_stage_override(GDEFCORE_Changeset * cs, void * ctx) {
  struct MenuWrite * w = (struct MenuWrite *)ctx;
  GDEFCORE_Menu_Override o;
  memset(&o, 0, sizeof o);
  o.size = sizeof o;
  o.label = w->label[0] != '\0' ? w->label : NULL;
  o.label_len = strlen(w->label);
  o.has_weight = w->has_weight_value;
  o.weight = w->weight_value;
  o.hidden = w->hidden[0] == '1' ? 1 : (w->hidden[0] == '0' ? 0 : -1);
  o.parent = w->parent[0] != '\0' ? w->parent : NULL;
  o.parent_len = strlen(w->parent);
  o.menu = w->menu[0] != '\0' ? w->menu : NULL;
  o.menu_len = strlen(w->menu);
  return gdefcore_menu_stage_override(cs, w->name, strlen(w->name), &o) ==
                 GDEFCORE_OK
             ? 0
             : -1;
}

static int menu_stage_entry(GDEFCORE_Changeset * cs, void * ctx) {
  struct MenuWrite * w = (struct MenuWrite *)ctx;
  return gdefcore_menu_stage_entry(cs, w->name, strlen(w->name), w->menu,
             strlen(w->menu), w->label, strlen(w->label), w->route,
             strlen(w->route), w->parent, strlen(w->parent), w->weight_value) ==
                 GDEFCORE_OK
             ? 0
             : -1;
}

static int menu_stage_forget(GDEFCORE_Changeset * cs, void * ctx) {
  struct MenuWrite * w = (struct MenuWrite *)ctx;
  return gdefcore_menu_stage_forget(cs, w->name, strlen(w->name)) == GDEFCORE_OK
             ? 0
             : -1;
}

/* What an editor action is, by the name the form's button gives it, and which
 * form (purpose) carries it. 0 for none. */
struct MenuAction {
  const char * word;
  int code;
  int purpose;
  const char * done;   /* "Moved <name> ..." and so on, before the name */
  const char * after;  /* ... and after it */
};

static const struct MenuAction kMenuActions[] = {
    {"add", GDEFCORE_MENU_ACT_ADD, kTokEdit, "Added ", "."},
    {"rename", GDEFCORE_MENU_ACT_RENAME, kTokEdit, "Renamed ", "."},
    {"hide", GDEFCORE_MENU_ACT_HIDE, kTokEdit, "Hid ", "."},
    {"show", GDEFCORE_MENU_ACT_SHOW, kTokEdit, "Showed ", "."},
    {"remove", GDEFCORE_MENU_ACT_REMOVE, kTokEdit, "Removed ", "."},
    {"move", GDEFCORE_MENU_ACT_MOVE, kTokMove, "Moved ", "."},
    {"up", GDEFCORE_MENU_ACT_UP, kTokMove, "Moved ", " up."},
    {"down", GDEFCORE_MENU_ACT_DOWN, kTokMove, "Moved ", " down."},
    {"indent", GDEFCORE_MENU_ACT_INDENT, kTokMove, "Moved ", " under the entry above."},
    {"outdent", GDEFCORE_MENU_ACT_OUTDENT, kTokMove, "Moved ", " out from under its parent."},
};

/* The sentences the core refuses an edit with, which are fixed text and safe to
 * say as they are. */
static const char * menu_refusal_text(const char * refusal) {
  static const char * const kKnown[] = {
      GDEFCORE_MENU_REFUSED_STALE, GDEFCORE_MENU_REFUSED_CYCLE,
      GDEFCORE_MENU_REFUSED_PARENT, GDEFCORE_MENU_REFUSED_ENTRY,
      GDEFCORE_MENU_REFUSED_PLACE, GDEFCORE_MENU_REFUSED_REMOVE,
      GDEFCORE_MENU_REFUSED_MANY, GDEFCORE_MENU_REFUSED_NOTHING,
      GDEFCORE_MENU_REFUSED_UNDO, "That edit is not allowed."};
  size_t i;
  if (refusal == NULL) {
    return NULL;
  }
  for (i = 0; i < sizeof kKnown / sizeof kKnown[0]; i++) {
    if (strcmp(refusal, kKnown[i]) == 0) {
      return kKnown[i];
    }
  }
  return NULL;
}

/* Say what happened to the edit that was asked for. */
static void menu_say_refused(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * refusal) {
  const char * text = menu_refusal_text(refusal);
  if (text == NULL) {
    say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That change was not made.");
  } else {
    say(catalog, request,
        strcmp(text, GDEFCORE_MENU_REFUSED_STALE) == 0 ? GDEFCORE_MESSAGE_ERROR
                                                       : GDEFCORE_MESSAGE_REFUSAL,
        text);
  }
}

/* A name that is safe to put after a # in a redirect: what a machine name is
 * made of. */
static int menu_name_safe(const char * s) {
  size_t i;
  if (s[0] == '\0' || strlen(s) > 128) {
    return 0;
  }
  for (i = 0; s[i] != '\0'; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-')) {
      return 0;
    }
  }
  return 1;
}

/*
 * One editor action: stage it with the core, which refuses a stale version, a
 * cycle, a parent that is not there and the rest with nothing written, then
 * apply the one changeset as the administrator with the request, so that the
 * core asks who may edit each entry. The reply is a redirect to the page, with
 * a message, and the focus fragment of the entry when it is still there.
 */
static int menu_edit_post(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Routes * routes, const struct MenuAction * act, const char * body,
    size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  char name[160];
  char suffix[140] = "";
  char label[512];
  char menu[80];
  char route[160];
  char parent[160];
  char position[16];
  char version[40];
  char said[400];
  GDEFCORE_Menu_Edit e;
  GDEFCORE_Changeset * cs = NULL;
  GDEFCORE_Route_Match * match = NULL;
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Result rc;
  char confirm[96];
  size_t confirm_n;
  int position_value = 0;
  int ok = 0;
  menu_post(body, body_len, "name", name, sizeof name);
  menu_post(body, body_len, "label", label, sizeof label);
  menu_post(body, body_len, "menu", menu, sizeof menu);
  menu_post(body, body_len, "route", route, sizeof route);
  menu_post(body, body_len, "parent", parent, sizeof parent);
  menu_post(body, body_len, "position", position, sizeof position);
  menu_post(body, body_len, "version", version, sizeof version);
  if (act->code == GDEFCORE_MENU_ACT_ADD) {
    menu_post(body, body_len, "suffix", suffix, sizeof suffix);
    if (snprintf(name, sizeof name, "%s%s", GDEFCORE_MENU_SITE_PREFIX, suffix) >=
        (int)sizeof name) {
      name[0] = '\0';
    }
  }
  if (act->code == GDEFCORE_MENU_ACT_MOVE &&
      (position[0] == '\0' || menu_int(position, &position_value) != 0 ||
          position_value < 0)) {
    say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That change was not made.");
    return redirect(out, "/admin/menus");
  }
  memset(&e, 0, sizeof e);
  e.size = sizeof e;
  e.action = act->code;
  e.name = name;
  e.name_len = strlen(name);
  e.label = label;
  e.label_len = strlen(label);
  e.menu = menu;
  e.menu_len = strlen(menu);
  e.route = route;
  e.route_len = strlen(route);
  e.parent = parent;
  e.parent_len = strlen(parent);
  e.position = (size_t)position_value;
  e.version = version;
  e.version_len = strlen(version);
  who = gdefcore_request_principal(request, &who_n);
  if (e.name_len == 0 || gdefcore_routes_match(routes, request, "/admin/menus", 12,
                             &match) != GDEFCORE_OK ||
      gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    gdefcore_route_match_free(match);
    say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That change was not made.");
    return redirect(out, "/admin/menus");
  }
  gdefcore_changeset_set_request(cs, request);
  rc = gdefcore_menu_stage_edit(cs, routes, request, match, &e);
  if (rc != GDEFCORE_OK) {
    menu_say_refused(catalog, request, gdefcore_changeset_refusal(cs, NULL));
  } else if (gdefcore_changeset_touched_count(cs) == 0) {
    say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Nothing changed.");
    ok = 1;
  } else {
    confirm_n = menu_confirm(cs, confirm, sizeof confirm);
    rc = confirm_n > 0 ? gdefcore_changeset_apply(cs, confirm, confirm_n)
                       : GDEFCORE_ERR_INVALID;
    if (rc == GDEFCORE_OK) {
      ok = 1;
      if (snprintf(said, sizeof said, "%s%s%s", act->done, name, act->after) >=
          (int)sizeof said) {
        snprintf(said, sizeof said, "%s", "Saved.");
      }
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, said);
    } else if (gdefcore_changeset_conflict(cs, NULL) != NULL) {
      /* Written by someone between the page and the apply. */
      say(catalog, request, GDEFCORE_MESSAGE_ERROR, GDEFCORE_MENU_REFUSED_STALE);
    } else {
      menu_say_refused(catalog, request, gdefcore_changeset_refusal(cs, NULL));
    }
  }
  gdefcore_changeset_destroy(cs);
  gdefcore_route_match_free(match);
  if (ok && act->code != GDEFCORE_MENU_ACT_REMOVE && menu_name_safe(name)) {
    char where[200];
    snprintf(where, sizeof where, "/admin/menus#e-%s", name);
    return redirect_to(out, where);
  }
  return redirect(out, "/admin/menus");
}

static int menus_write(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  const char * who;
  size_t who_n = 0;
  struct MenuWrite w;
  int done = -1;
  int has_weight;
  size_t k;
  memset(&w, 0, sizeof w);
  /* A form that was not signed for this session, this administrator and this
   * purpose writes nothing. */
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was changed.");
    return redirect(out, "/admin/menus");
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN ||
      memcmp(target, who, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was changed.");
    return redirect(out, "/admin/menus");
  }
  /* The editor's actions: the token says which form it is for, and the action
   * the button names has to be one of that form's. */
  for (k = 0; k < sizeof kMenuActions / sizeof kMenuActions[0]; k++) {
    char word[16];
    const struct MenuAction * act = &kMenuActions[k];
    size_t purpose_n = strlen(kMenuPurpose[act->purpose]);
    menu_post(body, body_len, "action", word, sizeof word);
    if (form_len == purpose_n && memcmp(form_id, kMenuPurpose[act->purpose], purpose_n) == 0 &&
        strcmp(word, act->word) == 0) {
      return menu_edit_post(catalog, request, routes, act, body, body_len, out);
    }
  }
  menu_post(body, body_len, "name", w.name, sizeof w.name);
  menu_post(body, body_len, "label", w.label, sizeof w.label);
  menu_post(body, body_len, "hidden", w.hidden, sizeof w.hidden);
  /* The page says hide or show; the core says 1 or 0. */
  if (strcmp(w.hidden, "hide") == 0) {
    menu_copy(w.hidden, sizeof w.hidden, "1");
  } else if (strcmp(w.hidden, "show") == 0) {
    menu_copy(w.hidden, sizeof w.hidden, "0");
  } else {
    w.hidden[0] = '\0';
  }
  menu_post(body, body_len, "menu", w.menu, sizeof w.menu);
  menu_post(body, body_len, "route", w.route, sizeof w.route);
  if (form_len == 13 && memcmp(form_id, "menu-override", 13) == 0) {
    /* What is set stays set: a field left blank keeps the stored override's. The
     * weight and the parent are the moves', and stay as they are. */
    char label[512];
    char weight[16];
    char hidden[8];
    char parent[160];
    char menu[80];
    if (menu_override_stored(catalog, w.name, strlen(w.name), label, weight, hidden,
            parent, menu, sizeof label) != 0) {
      return -1;
    }
    if (w.label[0] == '\0') {
      menu_copy(w.label, sizeof w.label, label);
    }
    has_weight = weight[0] != '\0' && menu_int(weight, &w.weight_value) == 0;
    w.has_weight_value = has_weight;
    if (w.hidden[0] == '\0') {
      menu_copy(w.hidden, sizeof w.hidden, hidden);
    }
    menu_copy(w.parent, sizeof w.parent, parent);
    if (w.menu[0] == '\0') {
      menu_copy(w.menu, sizeof w.menu, menu);
    }
    done = menu_apply(catalog, request, menu_stage_override, &w);
  } else if (form_len == 10 && memcmp(form_id, "menu-reset", 10) == 0) {
    done = menu_apply(catalog, request, menu_stage_forget, &w);
  } else if (form_len == 10 && memcmp(form_id, "menu-entry", 10) == 0) {
    /* The parent and the weight are the moves', and stay as they are. */
    char weight[16];
    if (menu_entry_stored(catalog, w.name, strlen(w.name), "parent", w.parent,
            sizeof w.parent) != 0 ||
        menu_entry_stored(catalog, w.name, strlen(w.name), "weight", weight,
            sizeof weight) != 0 ||
        (weight[0] != '\0' && menu_int(weight, &w.weight_value) != 0)) {
      return -1;
    }
    done = menu_apply(catalog, request, menu_stage_entry, &w);
  } else if (form_len == 11 && memcmp(form_id, "menu-forget", 11) == 0) {
    done = menu_apply(catalog, request, menu_stage_forget, &w);
  } else {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was changed.");
    return redirect(out, "/admin/menus");
  }
  if (done != 0) {
    say(catalog, request, GDEFCORE_MESSAGE_REFUSAL, "That change was not made.");
    return redirect(out, "/admin/menus");
  }
  say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Saved.");
  return redirect(out, "/admin/menus");
}

/*
 * /admin/settings: the settings of the kernel and of the enabled modules.
 *
 * The core generates the form of a group (gdefcore_settings_form_html()); this
 * file draws the page around it, signs it, and reads the post. A group has a
 * page here only where something placed one in the admin menu: the kernel's
 * (/admin/settings/core), or a route an enabled module declares at
 * /admin/settings/<module>[/<group>], with tabs for its groups. A path no route
 * matches, or one the route's own check refuses the administrator, is a page
 * that does not exist. A write is decided by the core entry by entry, when the
 * changeset is applied (gdefcore_settings_authorize()).
 */

static int settings_name_ok(const char * s, size_t n) {
  size_t i;
  if (n == 0 || n > 128) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-')) {
      return 0;
    }
  }
  return 1;
}

/* The module and the group a path names; group[0] == 0 when it names none. */
static int settings_parse(const char * path, char * module, size_t mcap, char * group,
    size_t gcap) {
  static const char kBase[] = "/admin/settings/";
  const char * rest;
  const char * slash;
  size_t mn;
  size_t gn = 0;
  if (strncmp(path, kBase, sizeof kBase - 1) != 0) {
    return -1;
  }
  rest = path + sizeof kBase - 1;
  slash = strchr(rest, '/');
  mn = slash == NULL ? strlen(rest) : (size_t)(slash - rest);
  if (!settings_name_ok(rest, mn) || mn + 1 > mcap) {
    return -1;
  }
  memcpy(module, rest, mn);
  module[mn] = '\0';
  group[0] = '\0';
  if (slash != NULL) {
    gn = strlen(slash + 1);
    if (!settings_name_ok(slash + 1, gn) || gn + 1 > gcap) {
      return -1;
    }
    memcpy(group, slash + 1, gn);
    group[gn] = '\0';
  }
  return 0;
}

/* The key (`<module>.<group>`) of the group a path names: the group it says, or
 * the module's first. 0 when the tenant has no such group. */
static int settings_key(GDEFCORE_Catalog * catalog, const char * module,
    const char * group, char * key, size_t cap) {
  size_t n = gdefcore_settings_group_count(catalog);
  size_t i;
  for (i = 0; i < n; i++) {
    GDEFCORE_Settings_Group g;
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    if (gdefcore_settings_group_at(catalog, i, &g) != GDEFCORE_OK ||
        strcmp(g.module, module) != 0) {
      continue;
    }
    if (group[0] == '\0' || strcmp(g.name, group) == 0) {
      if (g.key_len + 1 > cap) {
        return 0;
      }
      memcpy(key, g.key, g.key_len + 1);
      return 1;
    }
  }
  return 0;
}

/*
 * The routes of the table that answer @p path itself: the match also holds the
 * routes that only pass through it (a page under /admin/settings/shop is below
 * the page at /admin/settings/shop), and a default tab answers its parent's
 * path. A route answers the path when its pattern is the path, or it is the
 * default tab of the route whose pattern is. @p group, if not NULL, receives
 * the group the default tab shows (the path's own last segment), when there is
 * one.
 */
static int settings_routed(GDEFCORE_Routes * routes, GDEFCORE_Request * request,
    const char * path, char * group, size_t gcap) {
  GDEFCORE_Route_Match * match = NULL;
  size_t n;
  size_t i;
  int yes = 0;
  size_t plen = strlen(path);
  if (group != NULL) {
    group[0] = '\0';
  }
  if (routes == NULL ||
      gdefcore_routes_match(routes, request, path, plen, &match) != GDEFCORE_OK) {
    return 0;
  }
  n = gdefcore_route_match_route_count(match);
  for (i = 0; i < n; i++) {
    const char * machine = NULL;
    size_t machine_len = 0;
    GDEFCORE_Route_Info info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (gdefcore_route_match_route(match, i, &machine, &machine_len) != GDEFCORE_OK ||
        gdefcore_routes_info(routes, machine, machine_len, &info) != GDEFCORE_OK) {
      continue;
    }
    if (info.path_len == plen && memcmp(info.path, path, plen) == 0) {
      yes = 1;
    } else if (info.kind == GDEFCORE_ROUTE_TAB && info.is_default && group != NULL) {
      /* /admin/settings/<module>/<group> */
      char m[130];
      char g[130];
      char full[300];
      if (info.path_len < sizeof full) {
        memcpy(full, info.path, info.path_len);
        full[info.path_len] = '\0';
        if (settings_parse(full, m, sizeof m, g, sizeof g) == 0 && g[0] != '\0' &&
            strlen(g) < gcap) {
          memcpy(group, g, strlen(g) + 1);
        }
      }
    }
  }
  gdefcore_route_match_free(match);
  return yes;
}

static int settings_index(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Routes * routes,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  size_t n = gdefcore_settings_group_count(catalog);
  size_t i;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_begin(&page, "Settings") != 0 || page_str(&page, "<h1>Settings</h1><ul>") != 0) {
    page_free(&page);
    return -1;
  }
  for (i = 0; i < n; i++) {
    GDEFCORE_Settings_Group g;
    char path[400];
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    if (gdefcore_settings_group_at(catalog, i, &g) != GDEFCORE_OK) {
      continue;
    }
    snprintf(path, sizeof path, "/admin/settings/%s/%s", g.module, g.name);
    if (page_str(&page, "<li>") != 0) {
      page_free(&page);
      return -1;
    }
    if (settings_routed(routes, request, path, NULL, 0)) {
      if (page_str(&page, "<a href=\"") != 0 || page_esc(&page, path, strlen(path)) != 0 ||
          page_str(&page, "\">") != 0 ||
          page_esc_digits(&page, g.key, g.key_len) != 0 || page_str(&page, "</a>") != 0) {
        page_free(&page);
        return -1;
      }
    } else if (page_esc_digits(&page, g.key, g.key_len) != 0 ||
               page_str(&page, " (no page in the admin menu)") != 0) {
      page_free(&page);
      return -1;
    }
    if (page_str(&page, "</li>") != 0) {
      page_free(&page);
      return -1;
    }
  }
  if (page_str(&page, "</ul>") != 0) {
    page_free(&page);
    return -1;
  }
  return take_page(&page, out);
}

static int settings_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * path,
    const char * key, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char * token = NULL;
  char * html = NULL;
  size_t html_len = 0;
  const char * who;
  size_t who_n = 0;
  char self[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Result rc;
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return -1;
  }
  memcpy(self, who, GDEFCORE_PUBLIC_ID_LEN);
  self[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "settings-save", self, NULL, 0, &token) != 0) {
    return -1;
  }
  rc = gdefcore_settings_form_html(catalog, request, key, strlen(key), path, token,
      &html, &html_len);
  gcu_allocator_free(alloc, token);
  if (rc != GDEFCORE_OK) {
    return -2;
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_begin(&page, "Settings") != 0 || page_str(&page, "<h1>") != 0 ||
      page_esc_digits(&page, key, strlen(key)) != 0 || page_str(&page, "</h1>") != 0) {
    gdefcore_text_free(catalog, html);
    page_free(&page);
    return -1;
  }
  /* The core's own markup: every text in it is escaped there. */
  if (page_add(&page, html, html_len) != 0) {
    gdefcore_text_free(catalog, html);
    page_free(&page);
    return -1;
  }
  gdefcore_text_free(catalog, html);
  return take_page(&page, out);
}

static int settings_write(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * session, size_t session_len, const char * path, const char * key,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  const char * who;
  size_t who_n = 0;
  char version[64];
  size_t version_n = 0;
  GDEFCORE_Setting_Change changes[GDEFCORE_SETTINGS_SAVE_MAX];
  char * owned[GDEFCORE_SETTINGS_SAVE_MAX];
  char * names[GDEFCORE_SETTINGS_SAVE_MAX];
  size_t count = 0;
  size_t total;
  size_t i;
  size_t written = 0;
  char * message = NULL;
  size_t message_len = 0;
  GDEFCORE_Result rc;
  /* A form that was not signed for this session, this administrator and this
   * purpose writes nothing. */
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) != 0 ||
      b64_decode(token, raw, sizeof raw, &raw_n) != 0 ||
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) != GDEFCORE_OK ||
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) != 0 ||
      form_len != 13 || memcmp(form_id, "settings-save", 13) != 0) {
    wipe(token, sizeof token);
    wipe(raw, sizeof raw);
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was saved.");
    return redirect_to(out, path);
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN ||
      memcmp(target, who, GDEFCORE_PUBLIC_ID_LEN) != 0) {
    say(catalog, request, GDEFCORE_MESSAGE_ERROR,
        "That form is out of date. Nothing was saved.");
    return redirect_to(out, path);
  }
  version[0] = '\0';
  (void)form_pair(body, body_len, "version", version, sizeof version, &version_n);
  /* One change for each setting of the group the form posted. A secret that
   * was left blank is left alone; one with Clear ticked goes back to its
   * default. */
  total = gdefcore_settings_count(catalog);
  memset(owned, 0, sizeof owned);
  memset(names, 0, sizeof names);
  for (i = 0; i < total && count < GDEFCORE_SETTINGS_SAVE_MAX; i++) {
    GDEFCORE_Setting s;
    char field[200];
    char * value = NULL;
    size_t value_n = 0;
    char * clear = NULL;
    size_t clear_n = 0;
    memset(&s, 0, sizeof s);
    s.size = sizeof s;
    if (gdefcore_settings_at(catalog, i, &s) != GDEFCORE_OK ||
        strcmp(s.group, key) != 0 || s.name_len > 150) {
      continue;
    }
    snprintf(field, sizeof field, "c.%.*s", (int)s.name_len, s.name);
    if (form_field_alloc(body, body_len, field, 0, &clear, &clear_n) == 0) {
      free(clear);
      value = dup_bytes(s.default_value, s.default_len);
      value_n = s.default_len;
    } else {
      snprintf(field, sizeof field, "s.%.*s", (int)s.name_len, s.name);
      if (form_field_alloc(body, body_len, field, 0, &value, &value_n) != 0) {
        continue;
      }
      value_n = normalize_newlines(value, value_n);
      if (s.secret && value_n == 0) {
        free(value);
        continue;
      }
    }
    if (value == NULL) {
      continue;
    }
    names[count] = dup_bytes(s.name, s.name_len);
    if (names[count] == NULL) {
      free(value);
      continue;
    }
    owned[count] = value;
    changes[count].size = sizeof changes[count];
    changes[count].name = names[count];
    changes[count].name_len = s.name_len;
    changes[count].value = value;
    changes[count].value_len = value_n;
    count += 1;
  }
  rc = gdefcore_settings_save(catalog, request, key, strlen(key),
      version[0] != '\0' ? version : NULL, changes, count, &written, &message,
      &message_len);
  for (i = 0; i < count; i++) {
    wipe(owned[i], changes[i].value_len);
    free(owned[i]);
    free(names[i]);
  }
  /* Post, redirect, message: the page the redirect lands on is the form again,
   * drawn fresh against the version that is stored now, with what happened. */
  if (rc == GDEFCORE_OK) {
    say(catalog, request, GDEFCORE_MESSAGE_STATUS,
        written > 0 ? "Saved." : "Nothing changed.");
  } else {
    /* What the core said is what happened: it names the setting and why. A
     * text too long for a message is not one the form can use, so it is the
     * short version. */
    if (message == NULL || message_len == 0 ||
        gdefcore_message_post(catalog, request, GDEFCORE_MESSAGE_ERROR, message,
            message_len) != GDEFCORE_OK) {
      say(catalog, request, GDEFCORE_MESSAGE_ERROR, "Not saved.");
    }
  }
  gdefcore_text_free(catalog, message);
  return redirect_to(out, path);
}

static int settings_route(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, GDEFCORE_Routes * routes,
    const char * method, const char * path, const char * body, size_t body_len,
    struct GDEFWEB_Admin_Reply * out) {
  char module[130];
  char group[130];
  char key[GDEFCORE_SETTING_GROUP_MAX + 1];
  int is_get = strcmp(method, "GET") == 0;
  int rc;
  if (strcmp(path, "/admin/settings") == 0) {
    if (!is_get || !gdefweb_nav_follows(routes, request, path, "core.admin-settings")) {
      return unknown_admin(method, out);
    }
    return settings_index(alloc, catalog, request, routes, out);
  }
  /* A page there is only where a route the administrator may follow answers. */
  if (settings_parse(path, module, sizeof module, group, sizeof group) != 0 ||
      !settings_routed(routes, request, path, group[0] == '\0' ? group : NULL,
          sizeof group) ||
      !settings_key(catalog, module, group, key, sizeof key)) {
    return unknown_admin(method, out);
  }
  if (is_get) {
    rc = settings_page(alloc, catalog, request, keyring, root, root_len, session,
        session_len, path, key, out);
  } else {
    rc = settings_write(catalog, request, keyring, root, root_len, session,
        session_len, path, key, body, body_len, out);
  }
  /* A principal who may not edit settings is told what an unknown page is. */
  return rc == -2 ? unknown_admin(method, out) : rc;
}

/* The form id of the token that /logout asks for. A token the admin pages signed
 * for any other purpose does not end a session. */
static const char kLogoutPurpose[] = "logout";

/* The sign-out form: a post to /logout with the signed token, bound to this
 * session as every form's is. The token's target is the person, who exists. The
 * result is a string for gcu_allocator_free(). */
static int signout_form(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, char ** out) {
  static const char kHead[] = "<form method=\"post\" action=\"/logout\">"
                              "<input type=\"hidden\" name=\"token\" value=\"";
  static const char kTail[] = "\"><button type=\"submit\">Sign out</button></form>";
  size_t who_n = 0;
  const char * principal = gdefcore_request_principal(request, &who_n);
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * token = NULL;
  struct Page page;
  if (principal == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return -1;
  }
  /* A copy: signing checks the session again, which sets the request's
   * principal afresh. */
  memcpy(who, principal, GDEFCORE_PUBLIC_ID_LEN);
  who[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
          kLogoutPurpose, who, NULL, 0, &token) != 0) {
    return -1;
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (page_str(&page, kHead) != 0 || page_esc(&page, token, strlen(token)) != 0 ||
      page_str(&page, kTail) != 0) {
    gcu_allocator_free(alloc, token);
    page_free(&page);
    return -1;
  }
  gcu_allocator_free(alloc, token);
  *out = page.p;
  page.p = NULL;
  return 0;
}

/* Whether the request is a sign-out the session's own page offered: its body
 * carries a token that verifies for this session and was signed for sign-out.
 * No token, a damaged one, one signed for a different sign-in or another form, is no. The
 * Origin is the caller's check. */
static int logout_allowed(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, const char * body, size_t body_len,
    const char * ctype) {
  unsigned char sraw[768];
  size_t sraw_n = 0;
  char token[4096];
  size_t token_n = 0;
  unsigned char raw[3072];
  size_t raw_n = 0;
  char form_id[32];
  size_t form_len = 0;
  char target[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint64_t base = 0;
  int ok = 0;
  if (catalog == NULL || request == NULL || ctype == NULL ||
      strncmp(ctype, "application/x-www-form-urlencoded", 33) != 0 ||
      session_raw(session, sraw, sizeof sraw, &sraw_n) != 0) {
    return 0;
  }
  if (form_pair(body, body_len, "token", token, sizeof token, &token_n) == 0 &&
      b64_decode(token, raw, sizeof raw, &raw_n) == 0 &&
      gdefcore_form_check(catalog, request, keyring, root, root_len,
          (const char *)sraw, sraw_n, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&
      token_meta(raw, raw_n, form_id, sizeof form_id, &form_len, target, &base) == 0 &&
      form_len == sizeof kLogoutPurpose - 1 &&
      memcmp(form_id, kLogoutPurpose, form_len) == 0) {
    ok = 1;
  }
  wipe(token, sizeof token);
  wipe(raw, sizeof raw);
  wipe(sraw, sizeof sraw);
  return ok;
}

enum GDEFWEB_Signout gdefweb_admin_signout(GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, const char * body, size_t body_len,
    const char * ctype) {
  unsigned char raw[768];
  size_t raw_n = 0;
  size_t who_n = 0;
  GDEFCORE_Result rc;
  enum GDEFWEB_Signout out;
  if (catalog == NULL || request == NULL || keyring == NULL) {
    return GDEFWEB_SIGNOUT_FAILED;
  }
  /* A cookie that is absent or does not decode names no session. */
  if (session_raw(session, raw, sizeof raw, &raw_n) != 0) {
    return GDEFWEB_SIGNOUT_NO_SESSION;
  }
  /* Whether the session is live is core's answer, asked afresh here: a live one
   * binds the principal to the request. A token core refuses as invalid (a bad
   * tag, another tenant, expired) and one it accepts but no longer lets in (an
   * older epoch, a revoked session) are both a session that is already over. A
   * failure to find out is neither, and must not be taken for a dead session,
   * or the cookie would be cleared while the token still worked once the store
   * came back. */
  rc = gdefcore_account_check(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);
  if (rc != GDEFCORE_OK && rc != GDEFCORE_ERR_INVALID) {
    wipe(raw, sizeof raw);
    return GDEFWEB_SIGNOUT_FAILED;
  }
  if (rc != GDEFCORE_OK || gdefcore_request_principal(request, &who_n) == NULL) {
    wipe(raw, sizeof raw);
    return GDEFWEB_SIGNOUT_NO_SESSION;
  }
  if (!logout_allowed(catalog, request, keyring, root, root_len, session, body,
          body_len, ctype)) {
    wipe(raw, sizeof raw);
    return GDEFWEB_SIGNOUT_REFUSED;
  }
  /* This session only. Anything but success is not a sign-out. */
  rc = gdefcore_account_session_revoke(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);
  out = rc == GDEFCORE_OK ? GDEFWEB_SIGNOUT_ENDED : GDEFWEB_SIGNOUT_FAILED;
  wipe(raw, sizeof raw);
  return out;
}

/*
 * The navigation goes where the page left its marker: the admin menu, and the
 * breadcrumbs and tabs where the page has them, drawn by the core's widgets
 * (nav.c). A page with no marker (the sign-in page, a redirect) is as it was.
 */
static int nav_splice(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Routes * routes, GDEFCORE_Request * request, GDEFCORE_Keyring * keyring,
    const uint8_t * root, size_t root_len, const char * session, size_t session_len,
    const char * path, struct GDEFWEB_Admin_Reply * out) {
  static const char kMark[] = GDEFWEB_NAV_MARK;
  char * html;
  size_t html_len = 0;
  char * out_form = NULL;
  size_t form_len;
  const char * close_at;
  size_t mid;
  char * at;
  char * joined;
  size_t head;
  size_t tail;
  if (out->status != 200 || out->body == NULL || out->type == NULL ||
      strncmp(out->type, "text/html", 9) != 0) {
    return 0;
  }
  at = strstr(out->body, kMark);
  if (at == NULL) {
    return 0;
  }
  /* The way out is offered with the navigation, and carries the token that
   * /logout asks for. */
  if (signout_form(alloc, catalog, request, keyring, root, root_len, session,
          session_len, &out_form) != 0) {
    return -1;
  }
  html = gdefweb_nav_html(alloc, catalog, routes, request, path, &html_len);
  if (html == NULL) {
    static const char kHome[] = "<p><a href=\"/admin\">Home</a></p>";
    html = (char *)gcu_allocator_malloc(alloc, sizeof kHome);
    if (html == NULL) {
      gcu_allocator_free(alloc, out_form);
      return -1;
    }
    memcpy(html, kHome, sizeof kHome);
    html_len = sizeof kHome - 1;
  }
  head = (size_t)(at - out->body);
  tail = out->len - head - (sizeof kMark - 1);
  form_len = strlen(out_form);
  /* The sign-out form goes last in the body, after the page's own forms, so that
   * the order of the forms a page offers is the page's. A page with no closing
   * tag has it where the navigation is. */
  close_at = strstr(at + sizeof kMark - 1, "</body>");
  mid = close_at != NULL ? (size_t)(close_at - (at + sizeof kMark - 1)) : 0;
  joined = (char *)gcu_allocator_malloc(alloc, head + html_len + form_len + tail + 1);
  if (joined == NULL) {
    gcu_allocator_free(alloc, html);
    gcu_allocator_free(alloc, out_form);
    return -1;
  }
  memcpy(joined, out->body, head);
  memcpy(joined + head, html, html_len);
  if (close_at != NULL) {
    memcpy(joined + head + html_len, at + sizeof kMark - 1, mid);
    memcpy(joined + head + html_len + mid, out_form, form_len);
    memcpy(joined + head + html_len + mid + form_len, close_at, tail - mid);
  } else {
    memcpy(joined + head + html_len, out_form, form_len);
    memcpy(joined + head + html_len + form_len, at + sizeof kMark - 1, tail);
  }
  joined[head + html_len + form_len + tail] = '\0';
  gcu_allocator_free(alloc, html);
  gcu_allocator_free(alloc, out_form);
  gcu_allocator_free(alloc, out->body);
  out->body = joined;
  out->len = head + html_len + form_len + tail;
  return 0;
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

/*
 * The site's own stylesheets and scripts (layout editor story 21): a list of
 * every file of one kind (the site's, which can be edited, and the modules',
 * which are shown and cannot), one file to edit or delete, and its history.
 * /admin/stylesheets and /admin/scripts are the same page for the two kinds,
 * and are two pages because they are two permissions (siteasset.h): the route's
 * own check asks the one, and the apply asks it again for each file.
 *
 * Every action is a signed form that answers with a 303 and a message. A form
 * that is refused (the text, the name, a stale version, a missing confirmation)
 * writes nothing and is drawn again with what was typed, in the reply to the
 * post, so that no text is lost to a redirect. A script is saved only with its
 * name typed, since it runs with the authority of whoever loads it; a delete and
 * a revert type the name for both kinds. Nothing here needs a script, and a
 * file is public: it is served to anyone who asks, so the page says so.
 */

#define ASSET_TEXT_MAX (32u * 1024u)
#define ASSET_LIST_MAX 256

struct AssetKind {
  int kind;
  const char * base;
  const char * machine;
  const char * type;
  const char * action;
  const char * title;
  const char * one;
  int typed;
};

static const struct AssetKind kAssetKinds[] = {
    {GDEFCORE_ASSET_CSS, "/admin/stylesheets", "core.admin-stylesheets",
        GDEFCORE_ASSET_CSS_TYPE, GDEFCORE_ASSET_CSS_EDIT, "Stylesheets",
        "stylesheet", 0},
    {GDEFCORE_ASSET_JS, "/admin/scripts", "core.admin-scripts",
        GDEFCORE_ASSET_JS_TYPE, GDEFCORE_ASSET_JS_EDIT, "Scripts", "script", 1},
};

/* What a refused form says, drawn above the form it came from. */
enum {
  AN_NONE = 0,
  AN_REFUSED,
  AN_CONFLICT,
  AN_WRONG_CONFIRM,
  AN_BAD_FORM,
  AN_BAD_NAME,
  AN_BAD_TEXT,
  AN_TOO_LARGE,
  AN_EXISTS
};

static const char * asset_notice_text(int notice) {
  switch (notice) {
    case AN_REFUSED:
      return "That edit was refused. Nothing was saved.";
    case AN_CONFLICT:
      return "Not saved: this file changed while you were editing it. "
             "Your text is below.";
    case AN_WRONG_CONFIRM:
      return "Not done: the name typed does not match this file's name.";
    case AN_BAD_FORM:
      return "Not done: that form is out of date. Your text is below.";
    case AN_BAD_NAME:
      return "Not saved: a file name is 1 to 128 letters, digits, dots, "
             "underscores and hyphens, starting with a letter or digit, with "
             "no two dots in a row.";
    case AN_BAD_TEXT:
      return "Not saved: the text is empty, or holds a character that a file "
             "cannot hold.";
    case AN_TOO_LARGE:
      return "Not saved: the text is too large for this form.";
    case AN_EXISTS:
      return "Not saved: the site already has a file of that name.";
    default:
      return NULL;
  }
}

/* The kind a path belongs to, or NULL. */
static const struct AssetKind * asset_kind_of(const char * path) {
  size_t i;
  for (i = 0; i < sizeof kAssetKinds / sizeof kAssetKinds[0]; i++) {
    size_t n = strlen(kAssetKinds[i].base);
    if (strncmp(path, kAssetKinds[i].base, n) == 0 &&
        (path[n] == '\0' || path[n] == '/')) {
      return &kAssetKinds[i];
    }
  }
  return NULL;
}

/* The file called @p name, if this principal may edit it. 0 yes, with @p id; 1
 * for anything else (there is none, or it is not theirs: one answer); -1 for an
 * error. */
static int asset_open(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const struct AssetKind * k, const char * name, size_t name_len,
    char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  int ok = 0;
  int found;
  if (!gdefcore_siteasset_name_ok(name, name_len)) {
    return 1;
  }
  found = find_by_name(catalog, k->type, name, name_len, id);
  if (found < 0) {
    return -1;
  }
  if (found != 0) {
    return 1;
  }
  if (gdefcore_access_can(catalog, request, k->action, strlen(k->action), k->type,
          strlen(k->type), id, &ok) != GDEFCORE_OK) {
    return -1;
  }
  return ok ? 0 : 1;
}

/* A token for a form about the file @p id (with its name, so the type's fields
 * have something to sign), or, with a NULL @p name, about the principal @p id. */
static int asset_sign(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * form_id,
    const char * id, const char * name, char ** token) {
  GDEFCORE_Value values[2];
  const char * slots[2];
  size_t lens[2];
  if (name == NULL) {
    return sign_b64(alloc, catalog, request, keyring, root, root_len, session,
        session_len, form_id, id, NULL, 0, token);
  }
  one_value(&values[0], "name", name, &slots[0], &lens[0]);
  one_value(&values[1], "text", "", &slots[1], &lens[1]);
  return sign_b64(alloc, catalog, request, keyring, root, root_len, session,
      session_len, form_id, id, values, 2, token);
}

/* Whether the form's token is ours: this session's, for @p form_id, and for the
 * entity @p id. On success @p base is the version the form was signed at. */
static int asset_token_ok(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    GDEFCORE_Keyring * keyring, const uint8_t * root, size_t root_len,
    const char * session, size_t session_len, const char * post, size_t post_n,
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
  if (form_field_alloc(post, post_n, "token", 0, &token, &token_n) == 0 &&
      b64_decode(token, raw, sizeof raw, &raw_n) == 0 &&
      gdefcore_form_check(catalog, request, keyring, root, root_len, session,
          session_len, (const char *)raw, raw_n, NULL) == GDEFCORE_OK &&
      token_meta(raw, raw_n, got_form, sizeof got_form, &got_len, target, &b) == 0 &&
      memcmp(target, id, GDEFCORE_PUBLIC_ID_LEN) == 0 &&
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

static int asset_pair(struct Page * page, const char * a, const char * b) {
  return page_str(page, a) != 0 || page_esc(page, b, strlen(b)) != 0;
}

/* The notice, if there is one, as a paragraph. */
static int asset_notice(struct Page * page, int notice, const char * said,
    size_t said_n) {
  const char * text = asset_notice_text(notice);
  if (text == NULL) {
    return 0;
  }
  if (page_str(page, "<p class=\"notice\">") != 0 || page_str(page, text) != 0) {
    return -1;
  }
  if (said != NULL && said_n > 0 &&
      (page_str(page, " ") != 0 || page_esc(page, said, said_n) != 0)) {
    return -1;
  }
  return page_str(page, "</p>");
}

/* The text a form shows, drawn the way every textarea of the admin is: a
 * leading line break is dropped by the parser, so one is kept. */
static int asset_textarea(struct Page * page, const char * text, size_t n) {
  return page_str(page, "<p><label>Text<br><textarea name=\"text\" "
                        "spellcheck=\"false\">\n") != 0 ||
         page_esc(page, text, n) != 0 || page_str(page, "</textarea></label></p>");
}

static int asset_hash_of(GDEFCORE_Catalog * catalog, const char * name,
    char hash[GDEFCORE_ASSET_HASH_LEN + 1], size_t * bytes) {
  char ref[GDEFCORE_ASSET_NAME_MAX + 8];
  GDEFCORE_Asset a;
  memset(&a, 0, sizeof a);
  a.size = sizeof a;
  if (snprintf(ref, sizeof ref, "site/%s", name) >= (int)sizeof ref ||
      gdefcore_asset_find(catalog, NULL, ref, strlen(ref), &a) != GDEFCORE_OK) {
    return -1;
  }
  memcpy(hash, a.hash, GDEFCORE_ASSET_HASH_LEN + 1);
  *bytes = a.bytes_len;
  gdefcore_asset_release(&a);
  return 0;
}

/* The page of one kind: the site's files, the form for a new one, and the files
 * modules ship. */
static int asset_list_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, int notice, const char * said, size_t said_n,
    const char * new_name, const char * new_text, size_t new_text_n,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char ** ids = NULL;
  size_t count = 0;
  char names[ASSET_LIST_MAX][GDEFCORE_ASSET_NAME_MAX + 1];
  size_t nn = 0;
  size_t i;
  size_t j;
  const GDEFCORE_Module_Declared_Asset ** items = NULL;
  size_t nitems = 0;
  GDEFCORE_Host * host = gdefcore_catalog_host(catalog);
  const char * who;
  size_t who_n = 0;
  char self[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * token = NULL;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN ||
      gdefcore_entity_ids(catalog, k->type, strlen(k->type), &ids, &count) !=
          GDEFCORE_OK) {
    return -1;
  }
  memcpy(self, who, GDEFCORE_PUBLIC_ID_LEN);
  self[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  for (i = 0; i < count && nn < ASSET_LIST_MAX; i++) {
    char name[GDEFCORE_ASSET_NAME_MAX + 1];
    int ok = 0;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        gdefcore_access_can(catalog, request, k->action, strlen(k->action), k->type,
            strlen(k->type), ids[i], &ok) != GDEFCORE_OK ||
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
  gdefcore_ids_free(catalog, ids, count);
  if (page_begin(&page, k->title) != 0 || asset_pair(&page, "<h1>", k->title) != 0 ||
      page_str(&page, "</h1><p>A file here is public: it is sent to anyone who asks "
                      "for its address, so nothing secret belongs in one.</p>") != 0 ||
      asset_notice(&page, notice, said, said_n) != 0 ||
      page_str(&page, "<h2>The site's files</h2>") != 0) {
    goto fail;
  }
  if (nn == 0) {
    if (page_str(&page, "<p>The site has none yet.</p>") != 0) {
      goto fail;
    }
  } else if (page_str(&page, "<table><thead><tr><th>Name</th><th>Version</th>"
                             "<th>Bytes</th><th>Made by</th></tr></thead><tbody>") != 0) {
    goto fail;
  }
  for (j = 0; j < nn; j++) {
    char hash[GDEFCORE_ASSET_HASH_LEN + 1];
    size_t bytes = 0;
    int have = asset_hash_of(catalog, names[j], hash, &bytes) == 0;
    char made_by[GDEFCORE_ASSET_NAME_MAX + 1];
    char number[32];
    if (!gdefcore_siteasset_generator(catalog, names[j], strlen(names[j]), made_by)) {
      made_by[0] = '\0';
    }
    snprintf(number, sizeof number, "%zu", bytes);
    if (page_str(&page, "<tr><td><a href=\"") != 0 ||
        page_str(&page, k->base) != 0 || page_str(&page, "/") != 0 ||
        page_seg(&page, names[j], strlen(names[j])) != 0 ||
        page_str(&page, "\">") != 0 ||
        page_esc(&page, names[j], strlen(names[j])) != 0 ||
        page_str(&page, "</a></td><td>") != 0 ||
        (have && page_esc(&page, hash, 12) != 0) ||
        page_str(&page, "</td><td>") != 0 || (have && page_str(&page, number) != 0) ||
        page_str(&page, "</td><td>") != 0 ||
        (made_by[0] != '\0' &&
            (page_str(&page, "the structure of the page template ") != 0 ||
                page_esc(&page, made_by, strlen(made_by)) != 0)) ||
        page_str(&page, "</td></tr>") != 0) {
      goto fail;
    }
  }
  if (nn > 0 && page_str(&page, "</tbody></table>") != 0) {
    goto fail;
  }
  if (gdefcore_siteasset_may_edit(catalog, request, k->kind)) {
    if (asset_sign(alloc, catalog, request, keyring, root, root_len, session,
            session_len, "asset-new", self, NULL, &token) != 0) {
      goto fail;
    }
    if (page_str(&page, "<h2>A new ") != 0 || page_str(&page, k->one) != 0 ||
        page_str(&page, "</h2><form method=\"post\" action=\"") != 0 ||
        page_str(&page, k->base) != 0 || page_str(&page, "\">") != 0 ||
        page_token(&page, token) != 0 ||
        page_str(&page, "<p><label>Name <input name=\"name\" autocomplete=\"off\" "
                        "value=\"") != 0 ||
        (new_name != NULL && page_esc(&page, new_name, strlen(new_name)) != 0) ||
        page_str(&page, "\"></label></p>") != 0 ||
        asset_textarea(&page, new_text == NULL ? "" : new_text, new_text_n) != 0) {
      goto fail;
    }
    if (k->typed &&
        page_str(&page, "<p><label>To save a script, type its name again to confirm: "
                        "<input name=\"confirm\" autocomplete=\"off\" value=\"\">"
                        "</label></p>") != 0) {
      goto fail;
    }
    if (page_str(&page, "<p><button type=\"submit\" name=\"do\" value=\"create\">"
                        "Save</button></p></form>") != 0) {
      goto fail;
    }
  }
  if (host != NULL) {
    size_t tenant_n = 0;
    const uint8_t * tenant = gdefcore_catalog_tenant(catalog, &tenant_n);
    int shown = 0;
    if (tenant != NULL &&
        gdefcore_module_assets(host, tenant, tenant_n, NULL, &items, &nitems) ==
            GDEFCORE_OK) {
      for (i = 0; i < nitems; i++) {
        const GDEFCORE_Module_Declared_Asset * a = items[i];
        char number[32];
        if (a->kind != k->kind) {
          continue;
        }
        if (!shown &&
            page_str(&page, "<h2>Shipped by modules</h2><p>These are read only. A "
                            "module ships them, and they stop when it is "
                            "disabled.</p><table><thead><tr><th>File</th><th>Version"
                            "</th><th>Bytes</th></tr></thead><tbody>") != 0) {
          goto fail;
        }
        shown = 1;
        snprintf(number, sizeof number, "%zu", a->bytes_len);
        if (page_str(&page, "<tr><td>") != 0 ||
            page_esc(&page, a->module, a->module_len) != 0 ||
            page_str(&page, "/") != 0 || page_esc(&page, a->name, a->name_len) != 0 ||
            page_str(&page, "</td><td>") != 0 || page_esc(&page, a->hash, 12) != 0 ||
            page_str(&page, "</td><td>") != 0 || page_str(&page, number) != 0 ||
            page_str(&page, "</td></tr>") != 0) {
          goto fail;
        }
      }
      if (shown && page_str(&page, "</tbody></table>") != 0) {
        goto fail;
      }
      gdefcore_module_declared_free(host, items);
      items = NULL;
    }
  }
  gcu_allocator_free(alloc, token);
  return take_page(&page, out);
fail:
  if (items != NULL) {
    gdefcore_module_declared_free(host, items);
  }
  gcu_allocator_free(alloc, token);
  page_free(&page);
  return -1;
}

/* One file's page. @p text is what the textarea shows (the stored text unless a
 * refused form brought its own). */
static int asset_edit_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * id, const char * name,
    const char * text, size_t text_n, int notice, const char * said, size_t said_n,
    struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char * save = NULL;
  char * drop = NULL;
  char * stored = NULL;
  size_t stored_n = 0;
  char hash[GDEFCORE_ASSET_HASH_LEN + 1];
  char made_by[GDEFCORE_ASSET_NAME_MAX + 1];
  size_t bytes = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (gdefcore_siteasset_generator(catalog, name, strlen(name), made_by)) {
    /* A file a page template's structure makes is shown and not offered a form. */
    if (text == NULL && stored_text(catalog, id, "text", &stored, &stored_n) != 0) {
      goto fail;
    }
    if (page_begin(&page, k->title) != 0 || asset_pair(&page, "<h1>", name) != 0 ||
        page_str(&page, "</h1><p>This file is generated from the structure of the page "
                        "template <code>") != 0 ||
        page_esc(&page, made_by, strlen(made_by)) != 0 ||
        page_str(&page, "</code>. It cannot be edited here: change the structure, or mark "
                        "the template custom to write it by hand.</p>") != 0 ||
        asset_notice(&page, notice, said, said_n) != 0 ||
        page_str(&page, "<pre class=\"stored\">") != 0 ||
        page_esc(&page, text != NULL ? text : stored, text != NULL ? text_n : stored_n) != 0 ||
        page_str(&page, "</pre>") != 0) {
      goto fail;
    }
    free(stored);
    return take_page(&page, out);
  }
  if (asset_sign(alloc, catalog, request, keyring, root, root_len, session,
          session_len, "asset-save", id, name, &save) != 0 ||
      asset_sign(alloc, catalog, request, keyring, root, root_len, session,
          session_len, "asset-delete", id, name, &drop) != 0) {
    goto fail;
  }
  if (text == NULL) {
    if (stored_text(catalog, id, "text", &stored, &stored_n) != 0) {
      goto fail;
    }
    text = stored;
    text_n = stored_n;
  }
  if (page_begin(&page, k->title) != 0 || asset_pair(&page, "<h1>", name) != 0 ||
      page_str(&page, "</h1>") != 0 ||
      asset_notice(&page, notice, said, said_n) != 0) {
    goto fail;
  }
  if (asset_hash_of(catalog, name, hash, &bytes) == 0 &&
      (page_str(&page, "<p>Public address: <code>/assets/site/") != 0 ||
          page_esc(&page, name, strlen(name)) != 0 ||
          page_str(&page, "?v=") != 0 || page_esc(&page, hash, strlen(hash)) != 0 ||
          page_str(&page, "</code>. Anyone can read this file.</p>") != 0)) {
    goto fail;
  }
  if (page_str(&page, "<form method=\"post\" action=\"") != 0 ||
      page_str(&page, k->base) != 0 || page_str(&page, "/") != 0 ||
      page_seg(&page, name, strlen(name)) != 0 || page_str(&page, "\">") != 0 ||
      page_token(&page, save) != 0 || asset_textarea(&page, text, text_n) != 0) {
    goto fail;
  }
  if (k->typed &&
      page_str(&page, "<p><label>To save a script, type its name to confirm: "
                      "<input name=\"confirm\" autocomplete=\"off\" value=\"\">"
                      "</label></p>") != 0) {
    goto fail;
  }
  if (page_str(&page, "<p><button type=\"submit\" name=\"do\" value=\"save\">Save"
                      "</button></p></form><h2>Delete</h2><form method=\"post\" "
                      "action=\"") != 0 ||
      page_str(&page, k->base) != 0 || page_str(&page, "/") != 0 ||
      page_seg(&page, name, strlen(name)) != 0 || page_str(&page, "\">") != 0 ||
      page_token(&page, drop) != 0 ||
      page_str(&page, "<p><label>To delete this file, type its name: <input "
                      "name=\"confirm\" autocomplete=\"off\" value=\"\"></label></p>"
                      "<p><button type=\"submit\" name=\"do\" value=\"delete\">Delete"
                      "</button></p></form>") != 0) {
    goto fail;
  }
  free(stored);
  gcu_allocator_free(alloc, save);
  gcu_allocator_free(alloc, drop);
  return take_page(&page, out);
fail:
  free(stored);
  gcu_allocator_free(alloc, save);
  gcu_allocator_free(alloc, drop);
  page_free(&page);
  return -1;
}

/* The location of one file's page, or of the list when @p name is NULL. */
static int asset_location(struct GDEFWEB_Admin_Reply * out, const struct AssetKind * k,
    const char * name, const char * tail) {
  if (name == NULL) {
    return redirect_to(out, k->base);
  }
  if (snprintf(out->location_buf, sizeof out->location_buf, "%s/%s%s", k->base, name,
          tail) >= (int)sizeof out->location_buf) {
    return redirect(out, "/admin");
  }
  return redirect(out, out->location_buf);
}

/* Stage and apply one change, then say how it went. Returns the notice, 0 for
 * done; -1 for an error that is not the editor's. @p said gets the core's own
 * sentence when it refused. */
static int asset_apply(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const struct AssetKind * k, const char * name, const char * text, size_t text_n,
    int forget, char * said, size_t said_cap) {
  GDEFCORE_Changeset * cs = NULL;
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Result rc;
  int notice = AN_REFUSED;
  who = gdefcore_request_principal(request, &who_n);
  if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, request);
  rc = forget ? gdefcore_siteasset_stage_forget(cs, name, strlen(name))
              : gdefcore_siteasset_stage(cs, name, strlen(name), k->kind, text,
                    text_n);
  if (rc == GDEFCORE_ERR_OOM) {
    gdefcore_changeset_destroy(cs);
    return -1;
  }
  if (rc != GDEFCORE_OK) {
    gdefcore_changeset_destroy(cs);
    return forget ? AN_REFUSED : AN_BAD_TEXT;
  }
  /* A file is configuration, so the core's confirmation is the type. */
  rc = gdefcore_changeset_apply(cs, k->type, strlen(k->type));
  if (rc == GDEFCORE_OK) {
    notice = 0;
  } else if (rc == GDEFCORE_ERR_OOM) {
    notice = -1;
  } else if (gdefcore_changeset_conflict(cs, NULL) != NULL) {
    notice = AN_CONFLICT;
  } else {
    size_t rn = 0;
    const char * refusal = gdefcore_changeset_refusal(cs, &rn);
    if (refusal != NULL && rn > 0 && rn < said_cap) {
      memcpy(said, refusal, rn);
      said[rn] = '\0';
    }
  }
  gdefcore_changeset_destroy(cs);
  return notice;
}

static int asset_edit_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * name, size_t name_len, const char * post,
    size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * action = NULL;
  size_t action_n = 0;
  char * text = NULL;
  size_t text_n = 0;
  char * confirm = NULL;
  size_t confirm_n = 0;
  char said[400];
  uint64_t base = 0;
  int save;
  int notice;
  int rc = -1;
  int opened = asset_open(catalog, request, k, name, name_len, id);
  said[0] = '\0';
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("POST", out);
  }
  {
    /* What a structure makes is not written here (the core refuses it too). */
    char made_by[GDEFCORE_ASSET_NAME_MAX + 1];
    if (gdefcore_siteasset_generator(catalog, name, name_len, made_by)) {
      return asset_edit_page(alloc, catalog, request, keyring, root, root_len, session,
          session_len, k, id, name, NULL, 0, AN_REFUSED, said, 0, out);
    }
  }
  if (form_field_alloc(post, post_n, "do", 0, &action, &action_n) != 0) {
    return asset_location(out, k, name, "");
  }
  save = action_n == 4 && memcmp(action, "save", 4) == 0;
  if (!save && !(action_n == 6 && memcmp(action, "delete", 6) == 0)) {
    free(action);
    return asset_location(out, k, name, "");
  }
  free(action);
  if (form_field_alloc(post, post_n, "text", 0, &text, &text_n) != 0) {
    text = dup_bytes("", 0);
    text_n = 0;
    if (text == NULL) {
      return -1;
    }
  }
  text_n = normalize_newlines(text, text_n);
  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len,
          post, post_n, save ? "asset-save" : "asset-delete", id, &base)) {
    notice = AN_BAD_FORM;
  } else if (save && memchr(text, '\0', text_n) != NULL) {
    notice = AN_BAD_TEXT;
  } else if (save && text_n > ASSET_TEXT_MAX) {
    notice = AN_TOO_LARGE;
  } else if (!base_matches(catalog, id, base)) {
    notice = AN_CONFLICT;
  } else if ((save && !k->typed) ? 0
             : (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
                   confirm_n != name_len || memcmp(confirm, name, name_len) != 0)) {
    notice = AN_WRONG_CONFIRM;
  } else {
    notice = asset_apply(catalog, request, k, name, text, text_n, !save, said,
        sizeof said);
    if (notice < 0) {
      goto done;
    }
    if (notice == 0) {
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, save ? "Saved." : "Deleted.");
      rc = asset_location(out, k, save ? name : NULL, "");
      goto done;
    }
  }
  /* Refused: the same page again, with what was typed. */
  rc = asset_edit_page(alloc, catalog, request, keyring, root, root_len, session,
      session_len, k, id, name, save ? text : NULL, save ? text_n : 0, notice, said,
      strlen(said), out);
done:
  free(confirm);
  if (text != NULL) {
    wipe(text, text_n);
  }
  free(text);
  return rc;
}

static int asset_create_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * post, size_t post_n,
    struct GDEFWEB_Admin_Reply * out) {
  char * name = NULL;
  size_t name_n = 0;
  char * text = NULL;
  size_t text_n = 0;
  char * confirm = NULL;
  size_t confirm_n = 0;
  char * action = NULL;
  size_t action_n = 0;
  char said[400];
  char ref[GDEFCORE_ASSET_NAME_MAX + 8];
  GDEFCORE_Asset found;
  const char * who;
  size_t who_n = 0;
  char self[GDEFCORE_PUBLIC_ID_LEN + 1];
  int notice;
  int rc = -1;
  said[0] = '\0';
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return -1;
  }
  /* The request's principal is rebound when a form is checked, so it is copied
   * first. */
  memcpy(self, who, GDEFCORE_PUBLIC_ID_LEN);
  self[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (form_field_alloc(post, post_n, "do", 0, &action, &action_n) != 0 ||
      action_n != 6 || memcmp(action, "create", 6) != 0) {
    free(action);
    return asset_location(out, k, NULL, "");
  }
  free(action);
  if (form_field_alloc(post, post_n, "name", 0, &name, &name_n) != 0) {
    name = dup_bytes("", 0);
    name_n = 0;
  }
  if (form_field_alloc(post, post_n, "text", 0, &text, &text_n) != 0) {
    text = dup_bytes("", 0);
    text_n = 0;
  }
  if (name == NULL || text == NULL) {
    goto done;
  }
  text_n = normalize_newlines(text, text_n);
  memset(&found, 0, sizeof found);
  found.size = sizeof found;
  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len,
          post, post_n, "asset-new", self, NULL)) {
    notice = AN_BAD_FORM;
  } else if (!gdefcore_siteasset_name_ok(name, name_n)) {
    notice = AN_BAD_NAME;
  } else if (memchr(text, '\0', text_n) != NULL || text_n == 0) {
    notice = AN_BAD_TEXT;
  } else if (text_n > ASSET_TEXT_MAX) {
    notice = AN_TOO_LARGE;
  } else if (snprintf(ref, sizeof ref, "site/%s", name) < (int)sizeof ref &&
             gdefcore_asset_find(catalog, NULL, ref, strlen(ref), &found) ==
                 GDEFCORE_OK) {
    gdefcore_asset_release(&found);
    notice = AN_EXISTS;
  } else if (k->typed &&
             (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
                 confirm_n != name_n || memcmp(confirm, name, name_n) != 0)) {
    notice = AN_WRONG_CONFIRM;
  } else {
    notice = asset_apply(catalog, request, k, name, text, text_n, 0, said, sizeof said);
    if (notice < 0) {
      goto done;
    }
    if (notice == 0) {
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Saved.");
      rc = asset_location(out, k, name, "");
      goto done;
    }
  }
  rc = asset_list_page(alloc, catalog, request, keyring, root, root_len, session,
      session_len, k, notice, said, strlen(said), name, text, text_n, out);
done:
  free(confirm);
  free(name);
  if (text != NULL) {
    wipe(text, text_n);
  }
  free(text);
  return rc;
}

static int asset_history_get(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * name, size_t name_len, int notice,
    struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  struct Page page;
  size_t count = 0;
  size_t i;
  char * token = NULL;
  int opened = asset_open(catalog, request, k, name, name_len, id);
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("GET", out);
  }
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (gdefcore_entity_revision_count(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &count) !=
      GDEFCORE_OK) {
    return -1;
  }
  if (page_begin(&page, "History") != 0 || page_str(&page, "<h1>History of ") != 0 ||
      page_esc(&page, name, name_len) != 0 || page_str(&page, "</h1>") != 0 ||
      asset_notice(&page, notice, NULL, 0) != 0 ||
      page_str(&page, "<ol class=\"history\">") != 0) {
    goto fail;
  }
  /* Newest first. The sequence numbers stay on the server. */
  for (i = count; i > 0; i--) {
    uint64_t seq = 0;
    char * actor = NULL;
    size_t actor_n = 0;
    if (gdefcore_entity_revision_at(catalog, id, GDEFCORE_PUBLIC_ID_LEN, i - 1, &seq,
            &actor, &actor_n) != GDEFCORE_OK) {
      goto fail;
    }
    (void)seq;
    if (page_str(&page, "<li>") != 0 ||
        page_str(&page, i == count ? "The current version, by " : "By ") != 0 ||
        (actor != NULL && actor_n > 0 ? page_esc(&page, actor, actor_n)
                                       : page_str(&page, "the installation")) != 0 ||
        page_str(&page, "</li>") != 0) {
      gdefcore_text_free(catalog, actor);
      goto fail;
    }
    gdefcore_text_free(catalog, actor);
  }
  if (page_str(&page, "</ol>") != 0) {
    goto fail;
  }
  if (count >= 2) {
    if (asset_sign(alloc, catalog, request, keyring, root, root_len, session,
            session_len, "asset-revert", id, name, &token) != 0 ||
        page_str(&page, "<form method=\"post\" action=\"") != 0 ||
        page_str(&page, k->base) != 0 || page_str(&page, "/") != 0 ||
        page_seg(&page, name, name_len) != 0 || page_str(&page, "/history\">") != 0 ||
        page_token(&page, token) != 0 ||
        page_str(&page, "<p><label>To restore the version before the current one, "
                        "type this file's name to confirm: <input name=\"confirm\" "
                        "autocomplete=\"off\" value=\"\"></label></p><p><button "
                        "type=\"submit\" name=\"do\" value=\"revert\">Revert the "
                        "latest change</button></p></form>") != 0) {
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

static int asset_history_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * name, size_t name_len, const char * post,
    size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * confirm = NULL;
  size_t confirm_n = 0;
  uint64_t base = 0;
  int notice = AN_BAD_FORM;
  int rc = -1;
  int opened = asset_open(catalog, request, k, name, name_len, id);
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("POST", out);
  }
  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len,
          post, post_n, "asset-revert", id, &base) ||
      !base_matches(catalog, id, base)) {
    notice = AN_BAD_FORM;
  } else if (form_field_alloc(post, post_n, "confirm", 0, &confirm, &confirm_n) != 0 ||
             confirm_n != name_len || memcmp(confirm, name, name_len) != 0) {
    notice = AN_WRONG_CONFIRM;
  } else {
    GDEFCORE_Changeset * cs = NULL;
    const char * who;
    size_t who_n = 0;
    GDEFCORE_Result r;
    who = gdefcore_request_principal(request, &who_n);
    r = gdefcore_changeset_revert(catalog, id, GDEFCORE_PUBLIC_ID_LEN, who, who_n, &cs);
    if (r == GDEFCORE_OK) {
      gdefcore_changeset_set_request(cs, request);
      r = gdefcore_changeset_apply(cs, k->type, strlen(k->type));
      gdefcore_changeset_destroy(cs);
    }
    if (r == GDEFCORE_ERR_OOM) {
      goto done;
    }
    if (r == GDEFCORE_OK) {
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Reverted.");
      rc = asset_location(out, k, name, "/history");
      goto done;
    }
    notice = AN_REFUSED;
  }
  rc = asset_history_get(alloc, catalog, request, keyring, root, root_len, session,
      session_len, k, name, name_len, notice, out);
done:
  free(confirm);
  return rc;
}

/*
 * /admin/stylesheets and /admin/scripts           the list (GET), a new file (POST)
 * .../<name>                                      one file (GET), save or delete (POST)
 * .../<name>/history                              its history (GET), revert (POST)
 */
static int asset_route(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len,
    const struct AssetKind * k, const char * method, const char * path,
    const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  const char * rest = path + strlen(k->base);
  const char * slash;
  const char * tail;
  char name[GDEFCORE_ASSET_NAME_MAX + 1];
  size_t name_len = 0;
  int is_get = strcmp(method, "GET") == 0;
  int is_post = strcmp(method, "POST") == 0;
  int history = 0;
  if (*rest == '\0') {
    if (is_get) {
      return asset_list_page(alloc, catalog, request, keyring, root, root_len, session,
          session_len, k, AN_NONE, NULL, 0, NULL, NULL, 0, out);
    }
    return is_post ? asset_create_post(alloc, catalog, request, keyring, root, root_len,
                         session, session_len, k, body, body_len, out)
                   : unknown_admin(method, out);
  }
  if (*rest != '/' || rest[1] == '\0') {
    return unknown_admin(method, out);
  }
  rest += 1;
  slash = strchr(rest, '/');
  tail = slash == NULL ? rest + strlen(rest) : slash;
  /* A name is what a file name is made of, so a segment holding anything else
   * (an encoded form, a dot segment, a NUL) is not looked up. */
  if ((size_t)(tail - rest) > GDEFCORE_ASSET_NAME_MAX ||
      seg_decode(rest, (size_t)(tail - rest), name, sizeof name, &name_len) != 0 ||
      !gdefcore_siteasset_name_ok(name, name_len) ||
      name_len != (size_t)(tail - rest)) {
    return unknown_admin(method, out);
  }
  if (*tail == '\0') {
    /* the file's own page */
  } else if (strcmp(tail, "/history") == 0) {
    history = 1;
  } else {
    return unknown_admin(method, out);
  }
  if (history) {
    if (is_get) {
      return asset_history_get(alloc, catalog, request, keyring, root, root_len,
          session, session_len, k, name, name_len, AN_NONE, out);
    }
    return is_post ? asset_history_post(alloc, catalog, request, keyring, root,
                         root_len, session, session_len, k, name, name_len, body,
                         body_len, out)
                   : unknown_admin(method, out);
  }
  if (is_get) {
    char id[GDEFCORE_PUBLIC_ID_LEN + 1];
    int opened = asset_open(catalog, request, k, name, name_len, id);
    if (opened < 0) {
      return -1;
    }
    if (opened > 0) {
      return unknown_admin(method, out);
    }
    return asset_edit_page(alloc, catalog, request, keyring, root, root_len, session,
        session_len, k, id, name, NULL, 0, AN_NONE, NULL, 0, out);
  }
  return is_post ? asset_edit_post(alloc, catalog, request, keyring, root, root_len,
                       session, session_len, k, name, name_len, body, body_len, out)
                 : unknown_admin(method, out);
}

/*
 * The layout editor (layout editor story 24): /admin/layouts, the layouts the
 * principal may edit, and /admin/layouts/<name>, one layout drawn as the regions
 * its page template names, in the template's order, each with its placements,
 * and the Not placed list. Every action is a form that works with scripts off:
 * place a widget, move a placement to a region and a position, configure its
 * title and when it shows, set it aside, remove it, place a set-aside one, and
 * undo the last change. Each posts, and is one signed post, one changeset and
 * one revision of the one layout.
 *
 * A form carries the version of the layout the page drew (the core's, in a hidden
 * field) and is signed against the layout's own version, so a layout that
 * changed since is refused with its fixed sentence and the page that answers is
 * the layout as it is now. Who may edit a layout is the core's one decision about
 * that layout (core.layout.edit), asked for the list, for the page and, again,
 * when the write is applied. Every text on the page (a region's label, a widget's
 * name, a title, a role) was written by someone and is escaped.
 */

#define LAYOUT_LIST_MAX 128
#define LAYOUT_NAME_MAX 128

/* What a refused form says above the layout drawn as it is now. */
enum {
  LN_NONE = 0,
  LN_REFUSED,
  LN_BAD_FORM,
  LN_STALE,
  LN_NOT_UNDONE
};

static const char kLayoutType[] = "core.display";

static int asset_location_layout(struct GDEFWEB_Admin_Reply * out, const char * name);

/* The id of layout @p name if this principal may edit it: 0 yes, 1 no (there is
 * none, or it is not theirs: one answer), -1 an error. */
static int layout_open(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    const char * name, size_t name_len, char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
  int ok = 0;
  int found;
  if (name_len == 0 || name_len > LAYOUT_NAME_MAX) {
    return 1;
  }
  found = find_by_name(catalog, kLayoutType, name, name_len, id);
  if (found < 0) {
    return -1;
  }
  if (found != 0) {
    return 1;
  }
  if (gdefcore_access_can(catalog, request, GDEFCORE_LAYOUT_EDIT,
          sizeof GDEFCORE_LAYOUT_EDIT - 1, kLayoutType, sizeof kLayoutType - 1, id, &ok) !=
      GDEFCORE_OK) {
    return -1;
  }
  return ok ? 0 : 1;
}

/* A token for a form about layout @p id (named @p name). */
static int layout_sign(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * form_id,
    const char * id, const char * name, char ** token) {
  GDEFCORE_Value values[2];
  const char * slots[2];
  size_t lens[2];
  one_value(&values[0], "name", name, &slots[0], &lens[0]);
  one_value(&values[1], "layout", "", &slots[1], &lens[1]);
  return sign_b64(alloc, catalog, request, keyring, root, root_len, session, session_len,
      form_id, id, values, 2, token);
}

static const char * layout_notice_text(int notice) {
  switch (notice) {
    case LN_BAD_FORM:
      return "That form is out of date. Nothing was changed. Showing the current layout.";
    case LN_NOT_UNDONE:
      return "Nothing was undone.";
    default:
      return NULL;
  }
}

static int layout_notice(struct Page * page, int notice, const char * said, size_t said_n) {
  const char * text = layout_notice_text(notice);
  if (notice == LN_REFUSED || notice == LN_STALE) {
    if (said != NULL && said_n > 0) {
      return page_str(page, "<p class=\"notice\">") != 0 || page_esc(page, said, said_n) != 0 ||
             page_str(page, "</p>");
    }
    text = "That change was refused. Nothing was changed.";
  }
  if (text == NULL) {
    return 0;
  }
  return page_str(page, "<p class=\"notice\">") != 0 || page_str(page, text) != 0 ||
         page_str(page, "</p>");
}

/* <option> of each region, @p selected marked. */
static int layout_region_options(struct Page * page, const GDEFCORE_Layout_View * v,
    const char * selected) {
  size_t i;
  for (i = 0; i < v->nregions; i++) {
    if (page_str(page, "<option value=\"") != 0 ||
        page_esc(page, v->regions[i], strlen(v->regions[i])) != 0 ||
        page_str(page, selected != NULL && strcmp(selected, v->regions[i]) == 0 ? "\" selected>"
                                                                               : "\">") != 0 ||
        page_esc(page, v->regions[i], strlen(v->regions[i])) != 0 ||
        page_str(page, "</option>") != 0) {
      return -1;
    }
  }
  return 0;
}

/* The hidden fields every form of the page carries. */
static int layout_hidden(struct Page * page, const char * token, const GDEFCORE_Layout_View * v,
    const char * action, size_t index, int from_unplaced) {
  char number[24];
  snprintf(number, sizeof number, "%zu", index);
  return page_token(page, token) != 0 ||
         page_str(page, "<input type=\"hidden\" name=\"version\" value=\"") != 0 ||
         page_esc(page, v->version, strlen(v->version)) != 0 ||
         page_str(page, "\"><input type=\"hidden\" name=\"act\" value=\"") != 0 ||
         page_str(page, action) != 0 ||
         page_str(page, "\"><input type=\"hidden\" name=\"index\" value=\"") != 0 ||
         page_str(page, number) != 0 ||
         page_str(page, from_unplaced ? "\"><input type=\"hidden\" name=\"from\" value=\"unplaced\">"
                                      : "\">") != 0;
}

/* The settings of a placement as the configure form shows them. */
static int layout_settings_form(struct Page * page, const char * base, const char * name,
    const char * token, const GDEFCORE_Layout_View * v, size_t index, int from_unplaced,
    const GDEFCORE_Placement * p) {
  size_t i;
  if (page_str(page, "<details><summary>Configure</summary><form method=\"post\" action=\"") != 0 ||
      page_str(page, base) != 0 || page_str(page, "/") != 0 ||
      page_seg(page, name, strlen(name)) != 0 || page_str(page, "\">") != 0 ||
      layout_hidden(page, token, v, "configure", index, from_unplaced) != 0 ||
      page_str(page, "<p><label>Title <input name=\"title\" value=\"") != 0 ||
      page_esc(page, p->title, strlen(p->title)) != 0 ||
      page_str(page, "\"></label></p><p><label>Target <input name=\"target\" value=\"") != 0 ||
      page_esc(page, p->target, strlen(p->target)) != 0 ||
      page_str(page, "\"></label></p><p><label>Only on this path <input name=\"path\" value=\"") != 0 ||
      page_esc(page, p->path, strlen(p->path)) != 0 ||
      page_str(page, "\"></label></p><p><label>Only for this type <input name=\"type\" value=\"") != 0 ||
      page_esc(page, p->type, strlen(p->type)) != 0 ||
      page_str(page, "\"></label></p><p><label>Only for these roles (separated by spaces) "
                     "<input name=\"roles\" value=\"") != 0) {
    return -1;
  }
  for (i = 0; i < p->nroles; i++) {
    if ((i > 0 && page_str(page, " ") != 0) || page_esc(page, p->roles[i], strlen(p->roles[i])) != 0) {
      return -1;
    }
  }
  return page_str(page, "\"></label></p><p><button type=\"submit\">Save the settings</button></p>"
                        "</form></details>");
}

static int layout_summary(struct Page * page, const GDEFCORE_Placement * p) {
  size_t i;
  if (page_str(page, "<strong>") != 0 || page_esc(page, p->widget, strlen(p->widget)) != 0 ||
      page_str(page, "</strong>") != 0) {
    return -1;
  }
  if (p->target[0] != '\0' &&
      (page_str(page, " (target ") != 0 || page_esc(page, p->target, strlen(p->target)) != 0 ||
          page_str(page, ")") != 0)) {
    return -1;
  }
  if (p->title[0] != '\0' &&
      (page_str(page, ", titled “") != 0 || page_esc(page, p->title, strlen(p->title)) != 0 ||
          page_str(page, "”") != 0)) {
    return -1;
  }
  if (p->path[0] != '\0' &&
      (page_str(page, ", on ") != 0 || page_esc(page, p->path, strlen(p->path)) != 0)) {
    return -1;
  }
  if (p->type[0] != '\0' &&
      (page_str(page, ", for ") != 0 || page_esc(page, p->type, strlen(p->type)) != 0)) {
    return -1;
  }
  for (i = 0; i < p->nroles; i++) {
    if (page_str(page, i == 0 ? ", for the role " : ", ") != 0 ||
        page_esc(page, p->roles[i], strlen(p->roles[i])) != 0) {
      return -1;
    }
  }
  return 0;
}

static int layout_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * id,
    const char * name, int notice, const char * said, size_t said_n,
    struct GDEFWEB_Admin_Reply * out) {
  static const char kBase[] = "/admin/layouts";
  struct Page page;
  GDEFCORE_Layout_View v;
  char * edit = NULL;
  char * undo = NULL;
  size_t r;
  size_t i;
  size_t revisions = 0;
  size_t widgets;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  memset(&v, 0, sizeof v);
  v.size = sizeof v;
  if (gdefcore_layout_view(catalog, name, strlen(name), &v) != GDEFCORE_OK ||
      layout_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "layout-edit", id, name, &edit) != 0 ||
      layout_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "layout-undo", id, name, &undo) != 0 ||
      gdefcore_entity_revision_count(catalog, id, GDEFCORE_PUBLIC_ID_LEN, &revisions) !=
          GDEFCORE_OK) {
    goto fail;
  }
  if (page_begin(&page, "Layout") != 0 || page_str(&page, "<div id=\"layout-editor\"><h1>Layout ") != 0 ||
      page_esc(&page, name, strlen(name)) != 0 ||
      page_str(&page, "</h1><p>It is drawn with the page template <code>") != 0 ||
      page_esc(&page, v.template_name, strlen(v.template_name)) != 0 ||
      page_str(&page, "</code>.</p><div id=\"layout-live\" class=\"live\" role=\"status\" aria-live=\"polite\"></div>") != 0 ||
      layout_notice(&page, notice, said, said_n) != 0) {
    goto fail;
  }
  if (revisions >= 2 &&
      (page_str(&page, "<form method=\"post\" action=\"") != 0 || page_str(&page, kBase) != 0 ||
          page_str(&page, "/") != 0 || page_seg(&page, name, strlen(name)) != 0 ||
          page_str(&page, "\">") != 0 || layout_hidden(&page, undo, &v, "undo", 0, 0) != 0 ||
          page_str(&page, "<p><button type=\"submit\">Undo the last change</button></p></form>") != 0)) {
    goto fail;
  }
  for (r = 0; r < v.nregions; r++) {
    GDEFCORE_Region_Info info;
    size_t seen = 0;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (gdefcore_region_describe(catalog, v.template_name, strlen(v.template_name), v.regions[r],
            strlen(v.regions[r]), &info) != GDEFCORE_OK) {
      goto fail;
    }
    if (page_str(&page, "<h2>") != 0 || page_esc(&page, info.label, info.label_len) != 0 ||
        page_str(&page, "</h2>") != 0 ||
        (info.description_len > 0 &&
            (page_str(&page, "<p>") != 0 ||
                page_esc(&page, info.description, info.description_len) != 0 ||
                page_str(&page, "</p>") != 0)) ||
        page_str(&page, "<ol data-region=\"") != 0 ||
        page_esc(&page, v.regions[r], strlen(v.regions[r])) != 0 || page_str(&page, "\">") != 0) {
      gdefcore_region_info_free(catalog, &info);
      goto fail;
    }
    gdefcore_region_info_free(catalog, &info);
    for (i = 0; i < v.nplaced; i++) {
      char number[24];
      char index_text[24];
      if (strcmp(v.placed[i].region, v.regions[r]) != 0) {
        continue;
      }
      snprintf(number, sizeof number, "%zu", seen);
      snprintf(index_text, sizeof index_text, "%zu", i);
      if (page_str(&page, "<li data-placement=\"") != 0 || page_str(&page, index_text) != 0 ||
          page_str(&page, "\" data-widget=\"") != 0 ||
          page_esc(&page, v.placed[i].widget, strlen(v.placed[i].widget)) != 0 ||
          page_str(&page, "\" tabindex=\"0\" draggable=\"true\">") != 0 ||
          layout_summary(&page, &v.placed[i]) != 0 ||
          page_str(&page, "<form method=\"post\" action=\"") != 0 || page_str(&page, kBase) != 0 ||
          page_str(&page, "/") != 0 || page_seg(&page, name, strlen(name)) != 0 ||
          page_str(&page, "\">") != 0 ||
          layout_hidden(&page, edit, &v, "move", i, 0) != 0 ||
          page_str(&page, "<label>Move to <select name=\"region\">") != 0 ||
          layout_region_options(&page, &v, v.regions[r]) != 0 ||
          page_str(&page, "</select></label> <label>at position <input name=\"position\" "
                          "type=\"number\" min=\"0\" value=\"") != 0 ||
          page_str(&page, number) != 0 ||
          page_str(&page, "\"></label> <button type=\"submit\">Move</button></form>") != 0 ||
          layout_settings_form(&page, kBase, name, edit, &v, i, 0, &v.placed[i]) != 0) {
        goto fail;
      }
      /* Set aside and remove are two more forms of the same shape. */
      {
        static const char * const kActs[2] = {"unplace", "remove"};
        static const char * const kLabels[2] = {"Set aside", "Remove"};
        int a;
        for (a = 0; a < 2; a++) {
          if (page_str(&page, "<form method=\"post\" action=\"") != 0 || page_str(&page, kBase) != 0 ||
              page_str(&page, "/") != 0 || page_seg(&page, name, strlen(name)) != 0 ||
              page_str(&page, "\">") != 0 || layout_hidden(&page, edit, &v, kActs[a], i, 0) != 0 ||
              page_str(&page, "<button type=\"submit\">") != 0 || page_str(&page, kLabels[a]) != 0 ||
              page_str(&page, "</button></form>") != 0) {
            goto fail;
          }
        }
      }
      if (page_str(&page, "</li>") != 0) {
        goto fail;
      }
      seen += 1;
    }
    if (seen == 0 && page_str(&page, "<li>Nothing is placed here.</li>") != 0) {
      goto fail;
    }
    if (page_str(&page, "</ol>") != 0) {
      goto fail;
    }
  }
  /* Place a widget. */
  widgets = gdefcore_layout_widget_count(catalog);
  if (page_str(&page, "<h2>Place a widget</h2><form method=\"post\" action=\"") != 0 ||
      page_str(&page, kBase) != 0 || page_str(&page, "/") != 0 ||
      page_seg(&page, name, strlen(name)) != 0 || page_str(&page, "\">") != 0 ||
      layout_hidden(&page, edit, &v, "place", 0, 0) != 0 ||
      page_str(&page, "<p><label>Widget <select name=\"widget\">") != 0) {
    goto fail;
  }
  for (i = 0; i < widgets; i++) {
    GDEFCORE_Layout_Widget w;
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    if (gdefcore_layout_widget_at(catalog, i, &w) != GDEFCORE_OK) {
      goto fail;
    }
    if (page_str(&page, "<option value=\"") != 0 || page_esc(&page, w.name, w.name_len) != 0 ||
        page_str(&page, "\">") != 0 || page_esc(&page, w.name, w.name_len) != 0 ||
        page_str(&page, "</option>") != 0) {
      goto fail;
    }
  }
  if (page_str(&page, "</select></label> <label>in <select name=\"region\">") != 0 ||
      layout_region_options(&page, &v, NULL) != 0 ||
      page_str(&page, "</select></label> <label>at position <input name=\"position\" type=\"number\" "
                      "min=\"0\" value=\"0\"></label> <button type=\"submit\">Place</button></p></form>") != 0) {
    goto fail;
  }
  /* The Not placed list. */
  if (page_str(&page, "<h2>Not placed</h2>") != 0) {
    goto fail;
  }
  if (v.nunplaced == 0 && page_str(&page, "<p>Nothing is set aside.</p>") != 0) {
    goto fail;
  }
  if (v.nunplaced > 0 && page_str(&page, "<ul>") != 0) {
    goto fail;
  }
  for (i = 0; i < v.nunplaced; i++) {
    if (page_str(&page, "<li>") != 0 || layout_summary(&page, &v.unplaced[i]) != 0 ||
        page_str(&page, "<form method=\"post\" action=\"") != 0 || page_str(&page, kBase) != 0 ||
        page_str(&page, "/") != 0 || page_seg(&page, name, strlen(name)) != 0 ||
        page_str(&page, "\">") != 0 || layout_hidden(&page, edit, &v, "place-unplaced", i, 1) != 0 ||
        page_str(&page, "<label>Place in <select name=\"region\">") != 0 ||
        layout_region_options(&page, &v, NULL) != 0 ||
        page_str(&page, "</select></label> <label>at position <input name=\"position\" type=\"number\" "
                        "min=\"0\" value=\"0\"></label> <button type=\"submit\">Place</button></form>") != 0 ||
        layout_settings_form(&page, kBase, name, edit, &v, i, 1, &v.unplaced[i]) != 0 ||
        page_str(&page, "<form method=\"post\" action=\"") != 0 || page_str(&page, kBase) != 0 ||
        page_str(&page, "/") != 0 || page_seg(&page, name, strlen(name)) != 0 ||
        page_str(&page, "\">") != 0 || layout_hidden(&page, edit, &v, "remove", i, 1) != 0 ||
        page_str(&page, "<button type=\"submit\">Remove</button></form></li>") != 0) {
      goto fail;
    }
  }
  if ((v.nunplaced > 0 && page_str(&page, "</ul>") != 0) || page_str(&page, "</div>") != 0) {
    goto fail;
  }
  gcu_allocator_free(alloc, edit);
  gcu_allocator_free(alloc, undo);
  gdefcore_layout_view_free(&v);
  return take_page(&page, out);
fail:
  gcu_allocator_free(alloc, edit);
  gcu_allocator_free(alloc, undo);
  gdefcore_layout_view_free(&v);
  page_free(&page);
  return -1;
}

static int layouts_list_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, struct GDEFWEB_Admin_Reply * out) {
  struct Page page;
  char ** ids = NULL;
  size_t count = 0;
  char names[LAYOUT_LIST_MAX][LAYOUT_NAME_MAX + 1];
  size_t nn = 0;
  size_t i;
  size_t j;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  if (gdefcore_entity_ids(catalog, kLayoutType, sizeof kLayoutType - 1, &ids, &count) !=
      GDEFCORE_OK) {
    return -1;
  }
  for (i = 0; i < count && nn < LAYOUT_LIST_MAX; i++) {
    char name[LAYOUT_NAME_MAX + 1];
    int ok = 0;
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
        field_copy(catalog, ids[i], "name", name, sizeof name) != 0 ||
        gdefcore_access_can(catalog, request, GDEFCORE_LAYOUT_EDIT,
            sizeof GDEFCORE_LAYOUT_EDIT - 1, kLayoutType, sizeof kLayoutType - 1, ids[i], &ok) !=
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
  gdefcore_ids_free(catalog, ids, count);
  if (page_begin(&page, "Layouts") != 0 || page_str(&page, "<h1>Layouts</h1>") != 0) {
    goto fail;
  }
  if (nn == 0 && page_str(&page, "<p>There are no layouts you may edit.</p>") != 0) {
    goto fail;
  }
  if (nn > 0 && page_str(&page, "<ul>") != 0) {
    goto fail;
  }
  for (j = 0; j < nn; j++) {
    if (page_str(&page, "<li><a href=\"/admin/layouts/") != 0 ||
        page_seg(&page, names[j], strlen(names[j])) != 0 || page_str(&page, "\">") != 0 ||
        page_esc(&page, names[j], strlen(names[j])) != 0 || page_str(&page, "</a></li>") != 0) {
      goto fail;
    }
  }
  if (nn > 0 && page_str(&page, "</ul>") != 0) {
    goto fail;
  }
  return take_page(&page, out);
fail:
  page_free(&page);
  return -1;
}

static int layout_field_int(const char * post, size_t post_n, const char * field, size_t * out) {
  char * text = NULL;
  size_t n = 0;
  int bad;
  int v = 0;
  if (form_field_alloc(post, post_n, field, 0, &text, &n) != 0) {
    return -1;
  }
  bad = n == 0 || n > 6 || memchr(text, '\0', n) != NULL || menu_int(text, &v) != 0 || v < 0;
  free(text);
  if (bad) {
    return -1;
  }
  *out = (size_t)v;
  return 0;
}

/* Copy a form field into a fixed buffer; absent is empty, too long is -1. */
static int layout_field_text(const char * post, size_t post_n, const char * field, char * out,
    size_t cap) {
  char * text = NULL;
  size_t n = 0;
  out[0] = '\0';
  if (form_field_alloc(post, post_n, field, 0, &text, &n) != 0) {
    return 0;
  }
  if (n + 1 > cap || memchr(text, '\0', n) != NULL) {
    free(text);
    return -1;
  }
  memcpy(out, text, n);
  out[n] = '\0';
  free(text);
  return 0;
}

static int layouts_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * name,
    size_t name_len, const char * post, size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char act[24];
  char version[GDEFCORE_LAYOUT_VERSION_LEN + 8];
  char region[GDEFCORE_REGION_NAME_MAX + 2];
  char widget[GDEFCORE_LAYOUT_NAME_MAX + 1];
  char roles[GDEFCORE_PLACEMENT_ROLES_MAX * (GDEFCORE_PLACEMENT_ROLE_MAX + 1) + 8];
  char from[16];
  char said[400];
  size_t index = 0;
  size_t position = 0;
  GDEFCORE_Layout_Edit edit;
  GDEFCORE_Changeset * cs = NULL;
  const char * who;
  size_t who_n = 0;
  uint64_t base = 0;
  int notice = LN_BAD_FORM;
  int is_undo;
  GDEFCORE_Result rc = GDEFCORE_OK;
  int opened = layout_open(catalog, request, name, name_len, id);
  said[0] = '\0';
  if (opened < 0) {
    return -1;
  }
  if (opened > 0) {
    return unknown_admin("POST", out);
  }
  if (layout_field_text(post, post_n, "act", act, sizeof act) != 0) {
    act[0] = '\0';
  }
  is_undo = strcmp(act, "undo") == 0;
  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post,
          post_n, is_undo ? "layout-undo" : "layout-edit", id, &base) ||
      !base_matches(catalog, id, base)) {
    return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
        id, name, LN_BAD_FORM, NULL, 0, out);
  }
  who = gdefcore_request_principal(request, &who_n);
  if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
    return -1;
  }
  gdefcore_changeset_set_request(cs, request);
  if (is_undo) {
    gdefcore_changeset_destroy(cs);
    cs = NULL;
    rc = gdefcore_changeset_revert(catalog, id, GDEFCORE_PUBLIC_ID_LEN, who, who_n, &cs);
    if (rc == GDEFCORE_OK) {
      gdefcore_changeset_set_request(cs, request);
    } else {
      return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
          id, name, LN_NOT_UNDONE, NULL, 0, out);
    }
  } else {
    memset(&edit, 0, sizeof edit);
    edit.size = sizeof edit;
    edit.placement.size = sizeof edit.placement;
    edit.layout = name;
    edit.layout_len = name_len;
    if (layout_field_text(post, post_n, "version", version, sizeof version) != 0 ||
        layout_field_text(post, post_n, "region", region, sizeof region) != 0 ||
        layout_field_text(post, post_n, "widget", widget, sizeof widget) != 0 ||
        layout_field_text(post, post_n, "from", from, sizeof from) != 0) {
      gdefcore_changeset_destroy(cs);
      return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
          id, name, LN_BAD_FORM, NULL, 0, out);
    }
    edit.version = version;
    edit.version_len = strlen(version);
    edit.region = region;
    edit.region_len = strlen(region);
    edit.from_unplaced = strcmp(from, "unplaced") == 0;
    (void)layout_field_int(post, post_n, "index", &index);
    (void)layout_field_int(post, post_n, "position", &position);
    edit.index = index;
    edit.position = position;
    if (strcmp(act, "place") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_PLACE;
      snprintf(edit.placement.widget, sizeof edit.placement.widget, "%s", widget);
    } else if (strcmp(act, "place-unplaced") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_PLACE_UNPLACED;
    } else if (strcmp(act, "move") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_MOVE;
    } else if (strcmp(act, "configure") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_CONFIGURE;
      if (layout_field_text(post, post_n, "title", edit.placement.title,
              sizeof edit.placement.title) != 0 ||
          layout_field_text(post, post_n, "target", edit.placement.target,
              sizeof edit.placement.target) != 0 ||
          layout_field_text(post, post_n, "path", edit.placement.path,
              sizeof edit.placement.path) != 0 ||
          layout_field_text(post, post_n, "type", edit.placement.type,
              sizeof edit.placement.type) != 0 ||
          layout_field_text(post, post_n, "roles", roles, sizeof roles) != 0) {
        gdefcore_changeset_destroy(cs);
        return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
            id, name, LN_REFUSED, GDEFCORE_LAYOUT_REFUSED_PART,
            sizeof GDEFCORE_LAYOUT_REFUSED_PART - 1, out);
      }
      {
        /* Roles are words separated by spaces; each is checked by the core. */
        size_t at = 0;
        size_t len = strlen(roles);
        while (at < len) {
          size_t e = at;
          while (e < len && roles[e] != ' ') {
            e += 1;
          }
          if (e > at) {
            if (edit.placement.nroles >= GDEFCORE_PLACEMENT_ROLES_MAX ||
                e - at > GDEFCORE_PLACEMENT_ROLE_MAX) {
              gdefcore_changeset_destroy(cs);
              return layout_page(alloc, catalog, request, keyring, root, root_len, session,
                  session_len, id, name, LN_REFUSED, GDEFCORE_LAYOUT_REFUSED_PART,
                  sizeof GDEFCORE_LAYOUT_REFUSED_PART - 1, out);
            }
            memcpy(edit.placement.roles[edit.placement.nroles], roles + at, e - at);
            edit.placement.roles[edit.placement.nroles][e - at] = '\0';
            edit.placement.nroles += 1;
          }
          at = e + 1;
        }
      }
    } else if (strcmp(act, "unplace") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_UNPLACE;
    } else if (strcmp(act, "remove") == 0) {
      edit.action = GDEFCORE_LAYOUT_ACT_REMOVE;
    } else {
      gdefcore_changeset_destroy(cs);
      return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
          id, name, LN_BAD_FORM, NULL, 0, out);
    }
    rc = gdefcore_layout_stage_edit(cs, &edit);
    if (rc == GDEFCORE_ERR_OOM) {
      gdefcore_changeset_destroy(cs);
      return -1;
    }
  }
  if (rc == GDEFCORE_OK && gdefcore_changeset_touched_count(cs) == 0) {
    gdefcore_changeset_destroy(cs);
    say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Nothing changed.");
    return asset_location_layout(out, name);
  }
  if (rc == GDEFCORE_OK) {
    rc = gdefcore_changeset_apply(cs, kLayoutType, sizeof kLayoutType - 1);
  }
  if (rc == GDEFCORE_OK) {
    gdefcore_changeset_destroy(cs);
    say(catalog, request, GDEFCORE_MESSAGE_STATUS, is_undo ? "Undone." : "Saved.");
    return asset_location_layout(out, name);
  }
  {
    size_t rn = 0;
    const char * refusal = gdefcore_changeset_refusal(cs, &rn);
    if (gdefcore_changeset_conflict(cs, NULL) != NULL) {
      snprintf(said, sizeof said, "%s", GDEFCORE_LAYOUT_REFUSED_STALE);
      notice = LN_STALE;
    } else if (refusal != NULL && rn > 0 && rn < sizeof said) {
      memcpy(said, refusal, rn);
      said[rn] = '\0';
      notice = strcmp(said, GDEFCORE_LAYOUT_REFUSED_STALE) == 0 ? LN_STALE : LN_REFUSED;
    } else {
      notice = LN_REFUSED;
    }
  }
  gdefcore_changeset_destroy(cs);
  /* The layout as it is now, with what was said. */
  return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len, id,
      name, notice, said, strlen(said), out);
}

static int asset_location_layout(struct GDEFWEB_Admin_Reply * out, const char * name) {
  if (snprintf(out->location_buf, sizeof out->location_buf, "/admin/layouts/%s", name) >=
      (int)sizeof out->location_buf) {
    return redirect(out, "/admin/layouts");
  }
  return redirect(out, out->location_buf);
}

/*
 * /admin/layouts              the layouts the principal may edit
 * /admin/layouts/<name>       one layout (GET), an action on it (POST)
 */
static int layouts_route(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * method,
    const char * path, const char * body, size_t body_len, struct GDEFWEB_Admin_Reply * out) {
  static const char kBase[] = "/admin/layouts";
  const char * rest = path + sizeof kBase - 1;
  char name[LAYOUT_NAME_MAX + 1];
  size_t name_len = 0;
  int is_get = strcmp(method, "GET") == 0;
  int is_post = strcmp(method, "POST") == 0;
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  if (*rest == '\0') {
    return is_get ? layouts_list_page(alloc, catalog, request, out) : unknown_admin(method, out);
  }
  if (*rest != '/' || rest[1] == '\0' || strchr(rest + 1, '/') != NULL ||
      strlen(rest + 1) > LAYOUT_NAME_MAX ||
      seg_decode(rest + 1, strlen(rest + 1), name, sizeof name, &name_len) != 0 ||
      !gdefcore_siteasset_name_ok(name, name_len) || name_len != strlen(rest + 1)) {
    return unknown_admin(method, out);
  }
  if (is_get) {
    int opened = layout_open(catalog, request, name, name_len, id);
    if (opened < 0) {
      return -1;
    }
    if (opened > 0) {
      return unknown_admin(method, out);
    }
    return layout_page(alloc, catalog, request, keyring, root, root_len, session, session_len, id,
        name, LN_NONE, NULL, 0, out);
  }
  return is_post ? layouts_post(alloc, catalog, request, keyring, root, root_len, session,
                       session_len, name, name_len, body, body_len, out)
                 : unknown_admin(method, out);
}

/*
 * The selector rules and the checker (layout editor story 26): /admin/layout-rules
 * lists the site's rules in the order the engine uses (the more specific pattern
 * first, then the lower weight, then the name), each with the rank the engine gives
 * its pattern as a plain number, and has a form to add one, to change one and to
 * remove one. A form that changes a rule carries a fingerprint of the rule the
 * page showed and is refused when the rule is not that now.
 *
 * Beside the list is the question an editor asks of it: which layout does this path
 * get? The answer is the renderer's (gdefcore_layout_why() runs the one decision),
 * with the offers in the engine's order and why each other lost. The checker asks
 * as an anonymous visitor or as the person signed in; a page that is an entity is
 * resolved as the renderer resolves it, so a rule that names a type is tried.
 */

#define RULE_LIST_MAX 128

struct RuleRow {
  char name[GDEFCORE_LAYOUT_NAME_MAX + 1];
  char path[GDEFCORE_ROUTE_PATH_MAX + 1];
  char layout[GDEFCORE_LAYOUT_NAME_MAX + 1];
  char type[GDEFCORE_LAYOUT_NAME_MAX + 1];
  char exact[8];
  char weight[16];
  int64_t rank;
  int weight_n;
};

static const char kRuleType[] = GDEFCORE_LAYOUT_RULE_TYPE;

/* What identifies a rule as the page drew it. */
static void rule_fingerprint(const struct RuleRow * r, char out[GDEFCORE_ASSET_HASH_LEN + 1]) {
  char buf[1024];
  unsigned char dig[GSEC_SHA256_DIGEST_LEN];
  static const char kHex[] = "0123456789abcdef";
  size_t k;
  int n = snprintf(buf, sizeof buf, "%s\n%s\n%s\n%s\n%s\n%s", r->name, r->path, r->layout,
      r->type, r->exact, r->weight);
  out[0] = '\0';
  if (gsec_sha256(buf, n < 0 ? 0 : (size_t)n, dig) != GSEC_OK) {
    return;
  }
  for (k = 0; k < GDEFCORE_ASSET_HASH_LEN / 2; k++) {
    out[k * 2] = kHex[dig[k] >> 4];
    out[k * 2 + 1] = kHex[dig[k] & 0x0fu];
  }
  out[GDEFCORE_ASSET_HASH_LEN] = '\0';
}

static int rule_cmp(const void * a, const void * b) {
  const struct RuleRow * x = (const struct RuleRow *)a;
  const struct RuleRow * y = (const struct RuleRow *)b;
  if (x->rank != y->rank) {
    return x->rank > y->rank ? -1 : 1;
  }
  if (x->weight_n != y->weight_n) {
    return x->weight_n < y->weight_n ? -1 : 1;
  }
  return strcmp(x->name, y->name);
}

/* The site's rules this principal may edit, in the engine's order. */
static int rules_load(GDEFCORE_Catalog * catalog, GDEFCORE_Request * request,
    struct RuleRow * rows, size_t cap, size_t * out_n) {
  char ** ids = NULL;
  size_t count = 0;
  size_t i;
  size_t n = 0;
  if (gdefcore_entity_ids(catalog, kRuleType, sizeof kRuleType - 1, &ids, &count) !=
      GDEFCORE_OK) {
    return -1;
  }
  for (i = 0; i < count && n < cap; i++) {
    struct RuleRow * r = &rows[n];
    int ok = 0;
    char weight[16];
    memset(r, 0, sizeof *r);
    if (strlen(ids[i]) != GDEFCORE_PUBLIC_ID_LEN ||
        field_copy(catalog, ids[i], "name", r->name, sizeof r->name) != 0 ||
        field_copy(catalog, ids[i], "path", r->path, sizeof r->path) != 0 ||
        field_copy(catalog, ids[i], "layout", r->layout, sizeof r->layout) != 0 ||
        gdefcore_access_can(catalog, request, GDEFCORE_LAYOUT_EDIT,
            sizeof GDEFCORE_LAYOUT_EDIT - 1, kRuleType, sizeof kRuleType - 1, ids[i], &ok) !=
            GDEFCORE_OK ||
        !ok) {
      continue;
    }
    (void)field_copy(catalog, ids[i], "type", r->type, sizeof r->type);
    if (field_copy(catalog, ids[i], "exact", r->exact, sizeof r->exact) != 0) {
      r->exact[0] = '\0';
    }
    if (field_copy(catalog, ids[i], "weight", weight, sizeof weight) == 0) {
      snprintf(r->weight, sizeof r->weight, "%s", weight);
      r->weight_n = atoi(weight);
    }
    if (gdefcore_route_pattern_rank(r->path, strlen(r->path), r->exact[0] != '\0',
            r->type[0] != '\0', &r->rank) != GDEFCORE_OK) {
      continue;
    }
    n += 1;
  }
  gdefcore_ids_free(catalog, ids, count);
  qsort(rows, n, sizeof rows[0], rule_cmp);
  *out_n = n;
  return 0;
}

static const char * rule_why_word(int why) {
  switch (why) {
    case GDEFCORE_WHY_CHOSEN:
      return "chosen";
    case GDEFCORE_WHY_LESS_SPECIFIC:
      return "a less specific pattern";
    case GDEFCORE_WHY_HEAVIER:
      return "a heavier weight at the same specificity";
    case GDEFCORE_WHY_LATER:
      return "later in the order at the same specificity and weight";
    default:
      return "its layout cannot be applied";
  }
}

/* The public site's own route table, as the server builds it for a page. */
static GDEFCORE_Routes * rules_public_routes(GDEFCORE_Catalog * catalog) {
  GDEFCORE_Routes * routes = NULL;
  GDEFCORE_Host * host = gdefcore_catalog_host(catalog);
  if (gdefcore_routes_create(NULL, &routes) != GDEFCORE_OK) {
    return NULL;
  }
  if (gdefcore_routes_set_check(routes, gdefcore_access_route_check, catalog) != GDEFCORE_OK) {
    gdefcore_routes_destroy(routes);
    return NULL;
  }
  if (host != NULL) {
    size_t tenant_n = 0;
    const uint8_t * tenant = gdefcore_catalog_tenant(catalog, &tenant_n);
    char * err = NULL;
    size_t err_n = 0;
    (void)gdefcore_module_add_routes(host, tenant, tenant_n, NULL, routes, &err, &err_n);
    gdefcore_routes_free(routes, err);
  }
  return routes;
}

static int rules_page(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * notice,
    const char * checked_path, int checked_as, struct GDEFWEB_Admin_Reply * out) {
  static const char kAction[] = "/admin/layout-rules";
  struct Page page;
  struct RuleRow * rows = (struct RuleRow *)malloc(RULE_LIST_MAX * sizeof *rows);
  size_t n = 0;
  size_t i;
  char * token = NULL;
  char self[GDEFCORE_PUBLIC_ID_LEN + 1];
  const char * who;
  size_t who_n = 0;
  GDEFCORE_Layout_Why why;
  int have_why = 0;
  memset(&page, 0, sizeof page);
  page.alloc = alloc;
  memset(&why, 0, sizeof why);
  why.size = sizeof why;
  if (rows == NULL) {
    return -1;
  }
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    free(rows);
    return -1;
  }
  memcpy(self, who, GDEFCORE_PUBLIC_ID_LEN);
  self[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (rules_load(catalog, request, rows, RULE_LIST_MAX, &n) != 0 ||
      asset_sign(alloc, catalog, request, keyring, root, root_len, session, session_len,
          "rule-edit", self, NULL, &token) != 0) {
    free(rows);
    return -1;
  }
  if (checked_path != NULL && checked_path[0] == '/') {
    GDEFCORE_Routes * routes = rules_public_routes(catalog);
    GDEFCORE_Request * asked = request;
    char * resolved = NULL;
    size_t resolved_len = 0;
    if (checked_as == 0) {
      size_t tn = 0;
      const uint8_t * tenant = gdefcore_catalog_tenant(catalog, &tn);
      asked = NULL;
      if (gdefcore_request_create(tenant, tn, 0, 0, NULL, &asked) != GDEFCORE_OK) {
        asked = NULL;
      }
    }
    if (routes != NULL && asked != NULL) {
      /* As the server resolves a page: the path, then an alias, which is spelled
       * without its slash. */
      if (strcmp(checked_path, "/") != 0) {
        if (gdefcore_form_resolve(routes, catalog, asked, checked_path, strlen(checked_path),
                &resolved, &resolved_len) != GDEFCORE_OK) {
          resolved = NULL;
          if (gdefcore_form_resolve(routes, catalog, asked, checked_path + 1,
                  strlen(checked_path + 1), &resolved, &resolved_len) != GDEFCORE_OK) {
            resolved = NULL;
          }
        }
        if (resolved != NULL) {
          (void)gdefcore_request_set_resolution(asked, resolved, resolved_len);
          gdefcore_text_free(catalog, resolved);
        }
      }
      have_why = gdefcore_layout_why(catalog, routes, asked, checked_path, strlen(checked_path),
                     &why) == GDEFCORE_OK;
    }
    if (asked != NULL && asked != request) {
      gdefcore_request_destroy(asked);
    }
    gdefcore_routes_destroy(routes);
  }
  if (page_begin(&page, "Layout rules") != 0 || page_str(&page, "<h1>Layout rules</h1>") != 0 ||
      (notice != NULL && (page_str(&page, "<p class=\"notice\">") != 0 ||
                             page_esc(&page, notice, strlen(notice)) != 0 ||
                             page_str(&page, "</p>") != 0))) {
    goto fail;
  }
  if (page_str(&page, "<p>The site's rules, in the order the engine uses them: the more specific "
                      "pattern first, then the lower weight, then the name. A module may add rules "
                      "of its own that are not listed here.</p>") != 0) {
    goto fail;
  }
  if (n == 0 && page_str(&page, "<p>The site has no rules you may edit.</p>") != 0) {
    goto fail;
  }
  if (n > 0 && page_str(&page, "<table><thead><tr><th>Rule</th><th>Pattern</th><th>Layout</th>"
                               "<th>Type</th><th>Exact</th><th>Weight</th><th>Rank</th><th></th></tr>"
                               "</thead><tbody>") != 0) {
    goto fail;
  }
  for (i = 0; i < n; i++) {
    char rank[32];
    char fp[GDEFCORE_ASSET_HASH_LEN + 1];
    snprintf(rank, sizeof rank, "%lld", (long long)rows[i].rank);
    rule_fingerprint(&rows[i], fp);
    if (page_str(&page, "<tr><td>") != 0 || page_esc(&page, rows[i].name, strlen(rows[i].name)) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, rows[i].path, strlen(rows[i].path)) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, rows[i].layout, strlen(rows[i].layout)) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, rows[i].type, strlen(rows[i].type)) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_str(&page, rows[i].exact[0] != '\0' ? "yes" : "no") != 0 ||
        page_str(&page, "</td><td>") != 0 || page_esc(&page, rows[i].weight[0] != '\0' ? rows[i].weight : "0",
            strlen(rows[i].weight[0] != '\0' ? rows[i].weight : "0")) != 0 ||
        page_str(&page, "</td><td>") != 0 || page_str(&page, rank) != 0 ||
        page_str(&page, "</td><td><details><summary>Change</summary><form method=\"post\" action=\"") != 0 ||
        page_str(&page, kAction) != 0 || page_str(&page, "\">") != 0 || page_token(&page, token) != 0 ||
        page_str(&page, "<input type=\"hidden\" name=\"act\" value=\"edit\"><input type=\"hidden\" "
                        "name=\"name\" value=\"") != 0 ||
        page_esc(&page, rows[i].name, strlen(rows[i].name)) != 0 ||
        page_str(&page, "\"><input type=\"hidden\" name=\"was\" value=\"") != 0 ||
        page_esc(&page, fp, strlen(fp)) != 0 ||
        page_str(&page, "\"><p><label>Pattern <input name=\"path\" value=\"") != 0 ||
        page_esc(&page, rows[i].path, strlen(rows[i].path)) != 0 ||
        page_str(&page, "\"></label></p><p><label>Layout <input name=\"layout\" value=\"") != 0 ||
        page_esc(&page, rows[i].layout, strlen(rows[i].layout)) != 0 ||
        page_str(&page, "\"></label></p><p><label>Type <input name=\"type\" value=\"") != 0 ||
        page_esc(&page, rows[i].type, strlen(rows[i].type)) != 0 ||
        page_str(&page, "\"></label></p><p><label><input type=\"checkbox\" name=\"exact\" value=\"1\"") != 0 ||
        page_str(&page, rows[i].exact[0] != '\0' ? " checked" : "") != 0 ||
        page_str(&page, "> Only this path</label></p><p><label>Weight <input name=\"weight\" "
                        "type=\"number\" value=\"") != 0 ||
        page_esc(&page, rows[i].weight[0] != '\0' ? rows[i].weight : "0",
            strlen(rows[i].weight[0] != '\0' ? rows[i].weight : "0")) != 0 ||
        page_str(&page, "\"></label></p><p><button type=\"submit\">Save the rule</button></p></form>"
                        "<form method=\"post\" action=\"") != 0 ||
        page_str(&page, kAction) != 0 || page_str(&page, "\">") != 0 || page_token(&page, token) != 0 ||
        page_str(&page, "<input type=\"hidden\" name=\"act\" value=\"remove\"><input type=\"hidden\" "
                        "name=\"name\" value=\"") != 0 ||
        page_esc(&page, rows[i].name, strlen(rows[i].name)) != 0 ||
        page_str(&page, "\"><input type=\"hidden\" name=\"was\" value=\"") != 0 ||
        page_esc(&page, fp, strlen(fp)) != 0 ||
        page_str(&page, "\"><button type=\"submit\">Remove the rule</button></form></details></td></tr>") != 0) {
      goto fail;
    }
  }
  if (n > 0 && page_str(&page, "</tbody></table>") != 0) {
    goto fail;
  }
  if (page_str(&page, "<h2>A new rule</h2><form method=\"post\" action=\"") != 0 ||
      page_str(&page, kAction) != 0 || page_str(&page, "\">") != 0 || page_token(&page, token) != 0 ||
      page_str(&page, "<input type=\"hidden\" name=\"act\" value=\"add\"><p><label>Name "
                      "<input name=\"name\" autocomplete=\"off\"> (written as site.<em>name</em>)</label></p>"
                      "<p><label>Pattern <input name=\"path\" value=\"/\"></label></p>"
                      "<p><label>Layout <input name=\"layout\"></label></p>"
                      "<p><label>Type <input name=\"type\"></label></p>"
                      "<p><label><input type=\"checkbox\" name=\"exact\" value=\"1\"> Only this path</label></p>"
                      "<p><label>Weight <input name=\"weight\" type=\"number\" value=\"0\"></label></p>"
                      "<p><button type=\"submit\">Add the rule</button></p></form>") != 0) {
    goto fail;
  }
  /* Which layout for this path? */
  if (page_str(&page, "<h2>Which layout does a path get?</h2><form method=\"post\" action=\"") != 0 ||
      page_str(&page, kAction) != 0 || page_str(&page, "\">") != 0 || page_token(&page, token) != 0 ||
      page_str(&page, "<input type=\"hidden\" name=\"act\" value=\"check\"><p><label>Path "
                      "<input name=\"path\" value=\"") != 0 ||
      page_esc(&page, checked_path != NULL ? checked_path : "/", strlen(checked_path != NULL ? checked_path : "/")) != 0 ||
      page_str(&page, "\"></label> <label>as <select name=\"as\"><option value=\"anonymous\"") != 0 ||
      page_str(&page, checked_as == 0 ? " selected" : "") != 0 ||
      page_str(&page, ">a visitor who is not signed in</option><option value=\"me\"") != 0 ||
      page_str(&page, checked_as != 0 ? " selected" : "") != 0 ||
      page_str(&page, ">me</option></select></label> <button type=\"submit\">Check</button></p></form>") != 0) {
    goto fail;
  }
  if (checked_path != NULL && checked_path[0] != '/' &&
      page_str(&page, "<p class=\"notice\">A path starts with a slash.</p>") != 0) {
    goto fail;
  }
  if (have_why) {
    if (page_str(&page, "<p>") != 0 ||
        (why.final[0] != '\0'
                ? (page_str(&page, "The page gets the layout <strong>") != 0 ||
                      page_esc(&page, why.final, strlen(why.final)) != 0 || page_str(&page, "</strong>.") != 0)
                : page_str(&page, "No layout can be applied to that path, so it is drawn with none.") != 0) ||
        (why.refined &&
            (page_str(&page, " A module's refinement changed the choice from ") != 0 ||
                page_esc(&page, why.ordered, strlen(why.ordered)) != 0 ||
                page_str(&page, " (which module is not recorded).") != 0)) ||
        page_str(&page, "</p>") != 0) {
      goto fail;
    }
    if (why.noffers > 0 &&
        page_str(&page, "<table><thead><tr><th>Layout</th><th>Offered by</th><th>Rank</th><th>Weight</th>"
                        "<th>Result</th></tr></thead><tbody>") != 0) {
      goto fail;
    }
    for (i = 0; i < why.noffers; i++) {
      char rank[32];
      char weight[16];
      snprintf(rank, sizeof rank, "%lld", (long long)why.offers[i].rank);
      snprintf(weight, sizeof weight, "%d", why.offers[i].weight);
      if (page_str(&page, "<tr><td>") != 0 ||
          page_esc(&page, why.offers[i].layout, strlen(why.offers[i].layout)) != 0 ||
          page_str(&page, "</td><td>") != 0 ||
          page_esc(&page, why.offers[i].source, strlen(why.offers[i].source)) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_str(&page, rank) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_str(&page, weight) != 0 ||
          page_str(&page, "</td><td>") != 0 || page_str(&page, rule_why_word(why.offers[i].why)) != 0 ||
          page_str(&page, "</td></tr>") != 0) {
        goto fail;
      }
    }
    if (why.noffers > 0 && page_str(&page, "</tbody></table>") != 0) {
      goto fail;
    }
  }
  gdefcore_layout_why_free(&why);
  gcu_allocator_free(alloc, token);
  free(rows);
  return take_page(&page, out);
fail:
  gdefcore_layout_why_free(&why);
  gcu_allocator_free(alloc, token);
  free(rows);
  page_free(&page);
  return -1;
}

static int rules_post(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * session, size_t session_len, const char * post,
    size_t post_n, struct GDEFWEB_Admin_Reply * out) {
  char act[16];
  char suffix[GDEFCORE_LAYOUT_NAME_MAX + 8];
  char path[GDEFCORE_ROUTE_PATH_MAX + 8];
  char layout[GDEFCORE_LAYOUT_NAME_MAX + 8];
  char type[GDEFCORE_LAYOUT_NAME_MAX + 8];
  char exact[8];
  char weight[16];
  char was[GDEFCORE_ASSET_HASH_LEN + 8];
  char as[16];
  char self[GDEFCORE_PUBLIC_ID_LEN + 1];
  const char * who;
  size_t who_n = 0;
  const char * notice = NULL;
  GDEFCORE_Changeset * cs = NULL;
  GDEFCORE_Result rc = GDEFCORE_OK;
  who = gdefcore_request_principal(request, &who_n);
  if (who == NULL || who_n != GDEFCORE_PUBLIC_ID_LEN) {
    return -1;
  }
  memcpy(self, who, GDEFCORE_PUBLIC_ID_LEN);
  self[GDEFCORE_PUBLIC_ID_LEN] = '\0';
  if (layout_field_text(post, post_n, "act", act, sizeof act) != 0 ||
      layout_field_text(post, post_n, "name", suffix, sizeof suffix) != 0 ||
      layout_field_text(post, post_n, "path", path, sizeof path) != 0 ||
      layout_field_text(post, post_n, "layout", layout, sizeof layout) != 0 ||
      layout_field_text(post, post_n, "type", type, sizeof type) != 0 ||
      layout_field_text(post, post_n, "exact", exact, sizeof exact) != 0 ||
      layout_field_text(post, post_n, "weight", weight, sizeof weight) != 0 ||
      layout_field_text(post, post_n, "was", was, sizeof was) != 0 ||
      layout_field_text(post, post_n, "as", as, sizeof as) != 0) {
    return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
        "That form was not understood. Nothing was changed.", NULL, 0, out);
  }
  if (!asset_token_ok(catalog, request, keyring, root, root_len, session, session_len, post, post_n,
          "rule-edit", self, NULL)) {
    return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
        "That form is out of date. Nothing was changed. Showing the current rules.", NULL, 0, out);
  }
  /* The request's principal is rebound when the form is checked. */
  who = gdefcore_request_principal(request, &who_n);
  if (strcmp(act, "check") == 0) {
    return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len, NULL,
        path, strcmp(as, "me") == 0, out);
  }
  if (strcmp(act, "add") == 0 || strcmp(act, "edit") == 0 || strcmp(act, "remove") == 0) {
    char full[GDEFCORE_LAYOUT_NAME_MAX + 16];
    GDEFCORE_Layout_Rule rule;
    int is_add = strcmp(act, "add") == 0;
    snprintf(full, sizeof full, "%s%s", is_add ? GDEFCORE_LAYOUT_SITE_PREFIX : "", suffix);
    if (!is_add) {
      /* The rule the page drew: its fingerprint is what it is now. */
      struct RuleRow * rows = (struct RuleRow *)malloc(RULE_LIST_MAX * sizeof *rows);
      size_t n = 0;
      size_t i;
      int match = 0;
      if (rows == NULL) {
        return -1;
      }
      if (rules_load(catalog, request, rows, RULE_LIST_MAX, &n) == 0) {
        for (i = 0; i < n; i++) {
          char fp[GDEFCORE_ASSET_HASH_LEN + 1];
          rule_fingerprint(&rows[i], fp);
          if (strcmp(rows[i].name, full) == 0 && strcmp(fp, was) == 0) {
            match = 1;
          }
        }
      }
      free(rows);
      if (!match) {
        return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
            "That rule changed, or is not there. Nothing was changed. Showing the current rules.",
            NULL, 0, out);
      }
    }
    {
      int w = 0;
      if (weight[0] != '\0' && menu_int(weight, &w) != 0) {
        return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
            "That form was not understood: a weight is a whole number. Nothing was changed.", NULL, 0, out);
      }
    }
    memset(&rule, 0, sizeof rule);
    rule.size = sizeof rule;
    rule.path = path;
    rule.path_len = strlen(path);
    rule.layout = layout;
    rule.layout_len = strlen(layout);
    rule.type = type[0] != '\0' ? type : NULL;
    rule.type_len = strlen(type);
    rule.exact = exact[0] != '\0';
    rule.weight = weight[0] != '\0' ? atoi(weight) : 0;
    if (gdefcore_changeset_prepare(catalog, who, who_n, &cs) != GDEFCORE_OK) {
      return -1;
    }
    gdefcore_changeset_set_request(cs, request);
    rc = strcmp(act, "remove") == 0
             ? gdefcore_layout_stage_forget(cs, full, strlen(full))
             : gdefcore_layout_stage_rule(cs, full, strlen(full), &rule);
    if (rc == GDEFCORE_OK) {
      rc = gdefcore_changeset_apply(cs, kRuleType, sizeof kRuleType - 1);
    }
    if (rc == GDEFCORE_OK) {
      gdefcore_changeset_destroy(cs);
      say(catalog, request, GDEFCORE_MESSAGE_STATUS, "Saved.");
      return redirect(out, "/admin/layout-rules");
    }
    if (rc == GDEFCORE_ERR_OOM) {
      gdefcore_changeset_destroy(cs);
      return -1;
    }
    {
      size_t rn = 0;
      const char * refusal = gdefcore_changeset_refusal(cs, &rn);
      static char said[300];
      if (refusal != NULL && rn > 0 && rn < sizeof said) {
        memcpy(said, refusal, rn);
        said[rn] = '\0';
        notice = said;
      } else {
        notice = "That rule was refused. Nothing was changed. Check the name, the pattern and the layout.";
      }
    }
    gdefcore_changeset_destroy(cs);
    return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len, notice,
        NULL, 0, out);
  }
  return rules_page(alloc, catalog, request, keyring, root, root_len, session, session_len,
      "That form was not understood. Nothing was changed.", NULL, 0, out);
}

int gdefweb_admin_serve(const GDEFWEB_Allocator * alloc, GDEFCORE_Catalog * catalog,
    GDEFCORE_Request * request, GDEFCORE_Keyring * keyring, const uint8_t * root,
    size_t root_len, const char * method, const char * path, const char * session,
    const char * body, size_t body_len, const char * ctype,
    struct GDEFWEB_Admin_Reply * out) {
  unsigned char raw[768];
  size_t raw_n = 0;
  int is_get;
  int is_post;
  int rc;
  GDEFCORE_Routes * routes;
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
  if (session_raw(session, raw, sizeof raw, &raw_n) != 0) {
    return -1;
  }
  /* The session's messages are drawn by the navigation of whatever page is
   * served below, and posted by whatever handler redirects. */
  (void)gdefcore_message_bind(catalog, request, keyring, root, root_len,
      (const char *)raw, raw_n);
  routes = gdefweb_nav_open(catalog);
  /* A page is there for whoever its route's own check lets follow it, and for
   * no one else: the session said who this is, and this says what they may do.
   * The answer to a page the check refuses is the answer to a page that does
   * not exist. */
  if (strcmp(path, "/admin") == 0) {
    rc = is_get && gdefweb_nav_follows(routes, request, path, "core.admin")
             ? home_page(alloc, out)
             : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/more") == 0) {
    rc = is_get && gdefweb_nav_follows(routes, request, path, "core.admin-more")
             ? more_page(alloc, catalog, request, routes, out)
             : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/access") == 0) {
    rc = is_get && gdefweb_nav_follows(routes, request, path, "core.admin-access")
             ? access_page(alloc, catalog, request, out)
             : unknown_admin(method, out);
  } else if (strcmp(path, "/admin/post") == 0) {
    if (!gdefweb_nav_follows(routes, request, path, "core.admin-post")) {
      rc = unknown_admin(method, out);
    } else if (is_get) {
      rc = post_page(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, out);
    } else {
      rc = post_write(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, body, body_len, out);
    }
  } else if (strcmp(path, "/admin/front") == 0) {
    if (!gdefweb_nav_follows(routes, request, path, "core.admin-front")) {
      rc = unknown_admin(method, out);
    } else if (is_get) {
      rc = front_page(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, out);
    } else {
      rc = front_write(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, body, body_len, out);
    }
  } else if (strcmp(path, "/admin/undo") == 0) {
    if (!gdefweb_nav_follows(routes, request, path, "core.admin-undo")) {
      rc = unknown_admin(method, out);
    } else if (is_get) {
      rc = undo_page(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, out);
    } else {
      rc = undo_write(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, body, body_len, out);
    }
  } else if (strcmp(path, "/admin/menus") == 0) {
    /* The page is there for whoever the route's own check lets follow it. */
    if (!gdefweb_nav_follows(routes, request, path, "core.admin-menus")) {
      rc = unknown_admin(method, out);
    } else if (is_get) {
      rc = menus_page(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, routes, out);
    } else {
      rc = menus_write(catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, routes, body, body_len, out);
    }
  } else if (strcmp(path, "/admin/settings") == 0 ||
             starts_with(path, "/admin/settings/")) {
    rc = settings_route(alloc, catalog, request, keyring, root, root_len,
        (const char *)raw, raw_n, routes, method, path, body, body_len, out);
  } else if (strcmp(path, "/admin/templates") == 0 ||
             starts_with(path, "/admin/templates/")) {
    /* The editor decides per template; whether there is an editor at all is
     * the route's own check, so that someone who may edit none is told what a
     * page that does not exist is told. */
    if (!gdefweb_nav_follows(routes, request, "/admin/templates",
            "core.admin-templates")) {
      rc = unknown_admin(method, out);
    } else {
      rc = tpl_route(alloc, catalog, request, keyring, root, root_len, (const char *)raw, raw_n,
        method, path, body, body_len, out);
    }
  } else if (strcmp(path, "/admin/layout-rules") == 0) {
    if (!gdefweb_nav_follows(routes, request, path, "core.admin-layout-rules")) {
      rc = unknown_admin(method, out);
    } else if (is_get) {
      rc = rules_page(alloc, catalog, request, keyring, root, root_len, (const char *)raw, raw_n,
          NULL, NULL, 0, out);
    } else {
      rc = rules_post(alloc, catalog, request, keyring, root, root_len, (const char *)raw, raw_n,
          body, body_len, out);
    }
  } else if (strcmp(path, "/admin/layouts") == 0 || starts_with(path, "/admin/layouts/")) {
    /* The list is there for whoever the route's own check lets follow it, and a
     * layout for whoever the core says may edit it. */
    if (!gdefweb_nav_follows(routes, request, "/admin/layouts", "core.admin-layouts")) {
      rc = unknown_admin(method, out);
    } else {
      rc = layouts_route(alloc, catalog, request, keyring, root, root_len, (const char *)raw,
          raw_n, method, path, body, body_len, out);
    }
  } else if (asset_kind_of(path) != NULL) {
    /* One page for each kind of file, because each is its own permission; the
     * route's own check asks it, and a file is asked again by its own. */
    const struct AssetKind * k = asset_kind_of(path);
    if (!gdefweb_nav_follows(routes, request, k->base, k->machine)) {
      rc = unknown_admin(method, out);
    } else {
      rc = asset_route(alloc, catalog, request, keyring, root, root_len,
          (const char *)raw, raw_n, k, method, path, body, body_len, out);
    }
  } else {
    rc = unknown_admin(method, out);
  }
  if (rc == 0 && routes != NULL) {
    rc = nav_splice(alloc, catalog, routes, request, keyring, root, root_len,
        (const char *)raw, raw_n, path, out);
  } else if (rc == 0) {
    /* No table: the pages still have their one way home. */
    rc = nav_splice(alloc, catalog, NULL, request, keyring, root, root_len,
        (const char *)raw, raw_n, path, out);
  }
  gdefweb_nav_close(routes);
  wipe(raw, sizeof raw);
  return rc;
}
