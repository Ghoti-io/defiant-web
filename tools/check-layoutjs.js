#!/usr/bin/env node
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
 *
 * The layout editor's script, run against a small fake of the page. The script
 * the server sends must, for a drag and for the keys, fill in the placement's own
 * move form and submit it (so the post is the form's), say what it is doing, and
 * leave everything else alone. Then each of a list of defects is made in a copy of
 * the script by an exact text edit, and the check must fail on every one.
 *
 * Usage: check-layoutjs.js <the script as the server sends it>
 */
'use strict';
const fs = require('fs');
const vm = require('vm');

class El {
  constructor(tag, attrs) {
    this.tag = tag; this.attrs = attrs || {}; this.children = []; this.parentNode = null;
    this.nodeType = 1; this.listeners = {}; this.value = this.attrs.value || '';
    this.textContent = ''; this.open = false; this.submitted = 0; this.posted = null;
    this.classes = new Set(); this.box = {top: 0, height: 20};
    this.classList = {add: (c) => this.classes.add(c), remove: (c) => this.classes.delete(c)};
  }
  add(c) { c.parentNode = this; this.children.push(c); return c; }
  hasAttribute(n) { return n in this.attrs; }
  getAttribute(n) { return this.attrs[n] === undefined ? null : this.attrs[n]; }
  addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
  getBoundingClientRect() { return {top: this.box.top, height: this.box.height}; }
  focus() { this.world.focused = this; }
  all() { const out = []; for (const c of this.children) { out.push(c, ...c.all()); } return out; }
  matches(sel) {
    const tag = sel.match(/^([a-z]+)/);
    if (tag && tag[1] !== this.tag) return false;
    for (const p of sel.matchAll(/\[([a-z-]+)(?:=([^\]]+))?\]/g)) {
      if (!(p[1] in this.attrs)) return false;
      if (p[2] !== undefined && this.attrs[p[1]] !== p[2]) return false;
    }
    return true;
  }
  querySelectorAll(sel) {
    const chain = sel.split(' ');
    return this.all().filter((e) => {
      if (!e.matches(chain[chain.length - 1])) return false;
      if (chain.length === 1) return true;
      for (let p = e.parentNode; p; p = p.parentNode) { if (p.matches(chain[0])) return true; }
      return false;
    });
  }
  querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
  get form() { let p = this.parentNode; while (p && p.tag !== 'form') p = p.parentNode; return p; }
  submit() {
    this.submitted += 1;
    this.posted = {};
    for (const e of this.all()) { if (e.attrs.name) this.posted[e.attrs.name] = e.value; }
  }
  fire(type, ev) {
    ev.target = ev.target || this;
    ev.preventDefault = () => { ev.prevented = true; };
    for (const f of this.listeners[type] || []) f(ev);
    return ev;
  }
}

function world(script) {
  const w = {focused: null};
  const root = new El('div', {id: 'layout-editor'});
  const live = root.add(new El('div', {id: 'layout-live'}));
  const region = (name, widgets) => {
    const ol = root.add(new El('ol', {'data-region': name}));
    const lis = widgets.map((widget, k) => {
      const li = ol.add(new El('li', {'data-placement': String(k), 'data-widget': widget,
        tabindex: '0', draggable: 'true'}));
      li.box = {top: k * 20, height: 20};
      const f = li.add(new El('form'));
      f.add(new El('input', {name: 'act', value: 'move'}));
      f.add(new El('input', {name: 'index', value: String(k)}));
      f.add(new El('select', {name: 'region', value: name}));
      f.add(new El('input', {name: 'position', value: String(k)}));
      li.add(new El('details')).add(new El('input', {name: 'title'}));
      for (const act of ['unplace', 'remove']) {
        const g = li.add(new El('form'));
        g.add(new El('input', {name: 'act', value: act}));
        g.add(new El('input', {name: 'index', value: String(k)}));
      }
      return li;
    });
    return {ol, lis};
  };
  const main = region('main', ['content', 'menu', 'recent']);
  const side = region('side', ['tabs']);
  const catalog = root.add(new El('div', {id: 'layout-catalog', hidden: ''}));
  catalog.hidden = true;
  const types = ['recent', 'menu'].map((t) => catalog.add(new El('li', {'data-widget-type': t, tabindex: '0', draggable: 'true'})));
  const place = root.add(new El('form'));
  place.add(new El('input', {name: 'act', value: 'place'}));
  place.add(new El('select', {name: 'widget', value: 'content'}));
  place.add(new El('select', {name: 'region', value: 'main'}));
  place.add(new El('input', {name: 'position', value: '0'}));
  for (const e of [root, ...root.all()]) { e.world = w; }
  const document = {
    getElementById: (id) => (id === 'layout-editor' ? root : id === 'layout-live' ? live :
      id === 'layout-catalog' ? catalog : null),
    querySelectorAll: () => [],
  };
  vm.runInNewContext(script, {document, window: {location: {hash: ''}}});
  return {w, root, live, main, side, catalog, types, place};
}

/* Every check says what it saw; the first failure of a run ends it. */
function run(script) {
  const log = [];
  const ok = (cond, what) => { log.push((cond ? 'ok   ' : 'FAIL ') + what); if (!cond) throw new Error(what); };
  try {
    const x = world(script);
    const form = (li) => li.querySelector('form');
    let ev = x.root.fire('keydown', {target: x.main.lis[1], key: 'ArrowDown', altKey: true, shiftKey: true});
    ok(form(x.main.lis[1]).submitted === 1, 'the keys submit the placement\'s own form');
    ok(JSON.stringify(form(x.main.lis[1]).posted) ===
        '{"act":"move","index":"1","region":"main","position":"2"}', 'down is the move form with position 2');
    ok(ev.prevented === true, 'the key is taken');
    ok(/Moving menu to main, position 3 of 3\./.test(x.live.textContent), 'it says what it is doing');
    x.root.fire('keydown', {target: x.main.lis[0], key: 'ArrowUp', altKey: true, shiftKey: true});
    ok(form(x.main.lis[0]).submitted === 0 && /cannot move further/.test(x.live.textContent),
      'up on the first submits nothing and says so');
    x.root.fire('keydown', {target: x.main.lis[2], key: 'ArrowRight', altKey: true, shiftKey: true});
    ok(form(x.main.lis[2]).posted.region === 'side' && form(x.main.lis[2]).posted.position === '1',
      'right moves to the next region at the same place');
    x.root.fire('keydown', {target: x.side.lis[0], key: 'ArrowRight', altKey: true, shiftKey: true});
    ok(form(x.side.lis[0]).submitted === 0 && /no region that way/.test(x.live.textContent),
      'there is nothing beyond the last region');
    x.root.fire('keydown', {target: x.main.lis[0], key: 'ArrowDown', altKey: false, shiftKey: false});
    ok(x.w.focused === x.main.lis[1] && form(x.main.lis[0]).submitted === 0,
      'a plain arrow moves the focus and writes nothing');
    ev = x.root.fire('keydown', {target: x.main.lis[1], key: 'x', altKey: true, shiftKey: true});
    ok(!ev.prevented, 'other keys are left alone');
    ev = x.root.fire('keydown', {target: x.main.lis[0], key: 'Enter', altKey: false, shiftKey: false});
    ok(x.main.lis[0].querySelector('details').open === true && ev.prevented, 'Enter opens configure');
    const dragged = x.main.lis[2];
    form(dragged).submitted = 0;
    x.root.fire('dragstart', {target: dragged, dataTransfer: {setData() {}}});
    ev = x.root.fire('dragover', {target: x.side.ol, clientY: 100});
    ok(ev.prevented && x.side.ol.classes.has('drop-here'), 'a list takes the drop');
    x.root.fire('drop', {target: x.side.ol, clientY: 100});
    ok(form(dragged).submitted === 1 && JSON.stringify(form(dragged).posted) ===
        '{"act":"move","index":"2","region":"side","position":"1"}', 'a drop is the move form with the spot');
    ok(!x.side.ol.classes.has('drop-here'), 'the mark is cleared');
    const above = x.main.lis[0];
    x.root.fire('dragstart', {target: above, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: x.side.ol, clientY: -5});
    ok(form(above).posted.region === 'side' && form(above).posted.position === '0', 'above the first is position 0');
    const stay = x.main.lis[1];
    form(stay).submitted = 0;
    x.root.fire('dragstart', {target: stay, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: new El('p'), clientY: 0});
    ok(form(stay).submitted === 0, 'a drop that is not on a list does nothing');
    /* The Delete key sets a placement aside; with Shift it removes it; with Alt it does nothing. */
    const sub = (li, act) => li.querySelectorAll('form').filter((f) => f.querySelector('[name=act]').value === act)[0];
    ev = x.root.fire('keydown', {target: x.main.lis[1], key: 'Delete', altKey: false, shiftKey: false});
    ok(ev.prevented && sub(x.main.lis[1], 'unplace').submitted === 1 &&
        JSON.stringify(sub(x.main.lis[1], 'unplace').posted) === '{"act":"unplace","index":"1"}',
      'Delete is the placement\'s own set-aside form');
    ok(/Setting aside menu\./.test(x.live.textContent), 'it says what it is doing');
    ok(sub(x.main.lis[1], 'remove').submitted === 0, 'Delete does not remove');
    x.root.fire('keydown', {target: x.main.lis[2], key: 'Delete', altKey: false, shiftKey: true});
    ok(sub(x.main.lis[2], 'remove').submitted === 1 && sub(x.main.lis[2], 'unplace').submitted === 0 &&
        /Removing recent\./.test(x.live.textContent), 'Shift and Delete is the remove form');
    x.root.fire('keydown', {target: x.main.lis[0], key: 'Delete', altKey: true, shiftKey: false});
    ok(sub(x.main.lis[0], 'unplace').submitted === 0 && sub(x.main.lis[0], 'remove').submitted === 0,
      'Alt and Delete is left alone');
    /* The widget catalog is drawn hidden and the script shows it. */
    ok(x.catalog.hidden === false, 'the script shows the catalog');
    x.catalog.fire('dragstart', {target: x.types[0], dataTransfer: {setData() {}}});
    ev = x.root.fire('dragover', {target: x.side.ol, clientY: 100});
    ok(ev.prevented && x.side.ol.classes.has('drop-here'), 'a region takes a widget from the catalog');
    x.root.fire('drop', {target: x.side.ol, clientY: 100});
    ok(x.place.submitted === 1 && JSON.stringify(x.place.posted) ===
        '{"act":"place","widget":"recent","region":"side","position":"1"}', 'a drop is the place form with the spot');
    ok(/Placing recent in side, position 2\./.test(x.live.textContent), 'it says where');
    x.place.submitted = 0;
    x.catalog.fire('dragstart', {target: x.types[1], dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: new El('p'), clientY: 0});
    ok(x.place.submitted === 0, 'a catalog drop that is not on a region does nothing');
    x.catalog.fire('dragend', {});
    x.root.fire('drop', {target: x.side.ol, clientY: 100});
    ok(x.place.submitted === 0, 'a drop after the drag ended places nothing');
    ev = x.catalog.fire('keydown', {target: x.types[1], key: 'Enter'});
    ok(ev.prevented && x.place.querySelector('[name=widget]').value === 'menu' &&
        x.w.focused === x.place.querySelector('[name=region]') && x.place.submitted === 0,
      'Enter on a catalog widget chooses it and asks where, writing nothing');
    ok(!/innerHTML|eval\(|document\.write/.test(script), 'the script writes text only');
  } catch (e) {
    log.push('  (' + e.message + ')');
    return {pass: false, log};
  }
  return {pass: true, log};
}

const MUTATIONS = [
  ['submit-removed', '    form.submit();\n  }\n  function listFor', '  }\n  function listFor'],
  ['position-off-by-one', 'form.querySelector(\"[name=position]\").value = String(position);\n    say(\"Moving ',
    'form.querySelector(\"[name=position]\").value = String(position + 1);\n    say(\"Moving '],
  ['region-not-set', '    form.querySelector(\"[name=region]\").value = region;\n    form.querySelector(\"[name=position]\").value = String(position);\n    say(\"Moving ',
    '    form.querySelector(\"[name=position]\").value = String(position);\n    say(\"Moving '],
  ['nothing-said', '    say(\"Moving \" + label(item) + \" to \"', '    (\"Moving \" + label(item) + \" to \"'],
  ['plain-arrow-writes', 'if (event.altKey && event.shiftKey && !event.ctrlKey && !event.metaKey) {\n      event.preventDefault();\n      all = items(list, null);',
    'if (true) {\n      event.preventDefault();\n      all = items(list, null);'],
  ['drop-spot-ignores-the-pointer', '    var all = items(list, dragged);\n    var j;\n    var box;\n    for (j = 0; j < all.length; j += 1) {\n      box = all[j].getBoundingClientRect();\n      if (event.clientY < box.top + box.height / 2) return j;',
    '    var all = items(list, dragged);\n    var j;\n    var box;\n    for (j = 0; j < all.length; j += 1) {\n      box = all[j].getBoundingClientRect();\n      return j;'],
  ['drop-off-a-list-moves', '    if (!item || !list) return;\n    event.preventDefault();\n    dragged = null;', '    if (!item) return;\n    event.preventDefault();\n    dragged = null;'],
  ['enter-does-not-open', '        details.open = true;\n', ''],
  ['delete-removes', '(event.shiftKey ? \"remove\" : \"unplace\")', '\"remove\"'],
  ['delete-with-alt', 'if (key === \"Delete\" && !event.altKey && !event.ctrlKey', 'if (key === \"Delete\" && !event.ctrlKey'],
  ['catalog-region-not-set', '    form.querySelector(\"[name=region]\").value = region;\n    form.querySelector(\"[name=position]\").value = String(position);\n    say(\"Placing ',
    '    form.querySelector(\"[name=position]\").value = String(position);\n    say(\"Placing '],
  ['catalog-widget-not-set', '    form.querySelector(\"[name=widget]\").value = widget;\n', ''],
  ['catalog-drop-without-a-region', '    if (widget && list) {', '    if (widget) {'],
  ['catalog-never-shown', '    catalog.hidden = false;\n', ''],
  ['catalog-place-not-submitted', 'String(position + 1) + \".\");\n    form.submit();\n  }\n  root.addEventListener(\"keydown\"', 'String(position + 1) + \".\");\n  }\n  root.addEventListener(\"keydown\"'],
  ['markup-written', '  function say(text) {\n    live.textContent = text;\n  }\n  function items(',
    '  function say(text) {\n    live.innerHTML = text;\n  }\n  function items('],
];

const script = fs.readFileSync(process.argv[2], 'utf8');
const base = run(script);
if (!base.pass) {
  console.log(base.log.join('\n'));
  console.log('check-layoutjs: the script fails the check');
  process.exit(1);
}
console.log('check-layoutjs: the script passes (' + base.log.length + ' checks)');
let bad = 0;
for (const [name, from, to] of MUTATIONS) {
  const at = script.indexOf(from);
  if (at < 0 || script.indexOf(from, at + 1) >= 0) {
    console.log('check-layoutjs: ' + name + ': THE EDIT DID NOT APPLY (' + (at < 0 ? 'not found' : 'found twice') + '), nothing was tested');
    bad += 1;
    continue;
  }
  const r = run(script.slice(0, at) + to + script.slice(at + from.length));
  if (r.pass) {
    console.log('check-layoutjs: ' + name + ': PASSED with the defect made');
    bad += 1;
  } else {
    console.log('check-layoutjs: ' + name + ': made, and the check failed (' + r.log[r.log.length - 1].trim() + ')');
  }
}
if (bad) {
  process.exit(1);
}
console.log('check-layoutjs: every defect made turned the check red');
