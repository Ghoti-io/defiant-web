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
 * The page structure editor's script, run against a small fake of the page. The
 * script the server sends must, for a drag and for the keys, fill in the cell's or
 * the row's own move form and submit it (so the post is the form's), say what it
 * is doing, and leave everything else alone. Then each of a list of defects is made in a copy of
 * the script by an exact text edit, and the check must fail on every one.
 *
 * Usage: check-structurejs.js <the script as the server sends it>
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
    const tag = sel.match(/^([a-z0-9]+)/);
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
  const root = new El('div', {id: 'structure-editor'});
  const live = root.add(new El('div', {id: 'structure-live'}));
  const rows = [];
  const layout = [['main', 'side'], ['footer'], ['credits', 'legal']];
  layout.forEach((names, r) => {
    const head = root.add(new El('h2', {'data-rowhead': String(r), tabindex: '0', draggable: 'true'}));
    head.box = {top: r * 100, height: 20};
    const moves = [];
    const addMove = () => {
      const f = root.add(new El('form'));
      f.add(new El('input', {name: 'act', value: 'move-row'}));
      f.add(new El('input', {name: 'row', value: String(r)}));
      f.add(new El('input', {name: 'to_row', value: '0'}));
      moves.push(f);
    };
    if (r > 0) addMove();
    if (r + 1 < layout.length) addMove();
    const ol = root.add(new El('ol', {'data-row': String(r)}));
    const lis = names.map((name, k) => {
      const li = ol.add(new El('li', {'data-cell': name, tabindex: '0', draggable: 'true'}));
      li.box = {top: k * 20, height: 20};
      const f = li.add(new El('form'));
      f.add(new El('input', {name: 'act', value: 'move-cell'}));
      f.add(new El('input', {name: 'row', value: String(r)}));
      f.add(new El('input', {name: 'cell', value: String(k)}));
      f.add(new El('input', {name: 'to_row', value: String(r)}));
      f.add(new El('input', {name: 'to_cell', value: '0'}));
      const g = li.add(new El('form'));
      g.add(new El('input', {name: 'width', value: '6'}));
      g.add(new El('input', {name: 'region', value: name}));
      return li;
    });
    rows.push({head, ol, lis, moves});
  });
  for (const e of [root, ...root.all()]) { e.world = w; }
  const document = {
    getElementById: (id) => (id === 'structure-editor' ? root : id === 'structure-live' ? live : null),
    querySelectorAll: () => [],
  };
  vm.runInNewContext(script, {document, window: {location: {hash: ''}}});
  return {w, root, live, rows};
}

/* Every check says what it saw; the first failure of a run ends it. */
function run(script) {
  const log = [];
  const ok = (cond, what) => { log.push((cond ? 'ok   ' : 'FAIL ') + what); if (!cond) throw new Error(what); };
  try {
    const x = world(script);
    const form = (li) => li.querySelector('form');
    const cell = (r, k) => x.rows[r].lis[k];
    const key = (target, k, mods) => x.root.fire('keydown', Object.assign({target, key: k,
      altKey: false, shiftKey: false}, mods || {}));
    const MOVE = {altKey: true, shiftKey: true};
    let ev = key(cell(0, 0), 'ArrowRight', MOVE);
    ok(form(cell(0, 0)).submitted === 1, 'the keys submit the cell\'s own form');
    ok(JSON.stringify(form(cell(0, 0)).posted) ===
        '{"act":"move-cell","row":"0","cell":"0","to_row":"0","to_cell":"1"}', 'right is the move form with place 2');
    ok(ev.prevented === true, 'the key is taken');
    ok(/Moving main to row 1, place 2\./.test(x.live.textContent), 'it says what it is doing');
    key(cell(0, 0), 'ArrowLeft', MOVE);
    ok(form(cell(0, 0)).submitted === 1 && /cannot move further/.test(x.live.textContent),
      'left on the first submits nothing more and says so');
    key(cell(0, 1), 'ArrowDown', MOVE);
    ok(form(cell(0, 1)).posted.to_row === '1' && form(cell(0, 1)).posted.to_cell === '1',
      'down moves to the next row, at the same place or the end of it');
    key(cell(0, 0), 'ArrowUp', MOVE);
    ok(form(cell(0, 0)).submitted === 1 && /no row that way/.test(x.live.textContent),
      'there is nothing above the first row');
    key(cell(0, 0), 'ArrowRight');
    ok(x.w.focused === cell(0, 1) && form(cell(0, 0)).submitted === 1,
      'a plain arrow moves the focus and writes nothing');
    key(cell(0, 1), 'ArrowDown');
    ok(x.w.focused === cell(1, 0), 'a plain down goes to the cell below');
    ev = key(cell(0, 1), 'x', MOVE);
    ok(!ev.prevented, 'other keys are left alone');
    ev = key(cell(2, 0), 'Enter');
    ok(x.w.focused === cell(2, 0).querySelector('input[name=width]') && ev.prevented, 'Enter edits the cell');
    const rowForm = (r) => x.rows[r].moves.filter((f) => f.querySelector('[name=to_row]'))[0];
    key(x.rows[1].head, 'ArrowDown', MOVE);
    const moved = x.rows[1].moves.filter((f) => f.submitted === 1);
    ok(moved.length === 1 && JSON.stringify(moved[0].posted) === '{"act":"move-row","row":"1","to_row":"2"}',
      'a row key is the row\'s own move form');
    ok(/Moving row 2 to place 3\./.test(x.live.textContent), 'it says which row');
    key(x.rows[0].head, 'ArrowUp', MOVE);
    ok(x.rows[0].moves.every((f) => f.submitted === 0) && /cannot move further/.test(x.live.textContent),
      'the first row cannot go up');
    key(x.rows[2].head, 'ArrowUp');
    ok(x.rows[2].moves.every((f) => f.submitted === 0), 'a plain arrow on a row writes nothing');
    /* Drag a cell. */
    const dragged = cell(1, 0);
    form(dragged).submitted = 0;
    x.root.fire('dragstart', {target: dragged, dataTransfer: {setData() {}}});
    ev = x.root.fire('dragover', {target: x.rows[2].ol, clientY: 100});
    ok(ev.prevented && x.rows[2].ol.classes.has('drop-here'), 'a row takes the drop');
    x.root.fire('drop', {target: x.rows[2].ol, clientY: 100});
    ok(form(dragged).submitted === 1 && JSON.stringify(form(dragged).posted) ===
        '{"act":"move-cell","row":"1","cell":"0","to_row":"2","to_cell":"2"}', 'a drop is the move form with the spot');
    ok(!x.rows[2].ol.classes.has('drop-here'), 'the mark is cleared');
    const above = cell(0, 0);
    x.root.fire('dragstart', {target: above, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: x.rows[2].ol, clientY: -5});
    ok(form(above).posted.to_row === '2' && form(above).posted.to_cell === '0', 'above the first is place 0');
    const stay = cell(2, 1);
    form(stay).submitted = 0;
    x.root.fire('dragstart', {target: stay, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: new El('p'), clientY: 0});
    ok(form(stay).submitted === 0, 'a drop that is not on a row does nothing');
    /* Drag a row by its heading. */
    const from = x.rows[0];
    x.root.fire('dragstart', {target: from.head, dataTransfer: {setData() {}}});
    ev = x.root.fire('dragover', {target: x.rows[2].head, clientY: 215});
    ok(ev.prevented && x.rows[2].head.classes.has('drop-after'), 'a heading takes a row, and says after');
    x.root.fire('drop', {target: x.rows[2].head, clientY: 215});
    const sent = from.moves.filter((f) => f.submitted === 1);
    ok(sent.length === 1 && sent[0].posted.row === '0' && sent[0].posted.to_row === '2',
      'dropped below the last heading, the row goes last');
    ok(!x.rows[2].head.classes.has('drop-after'), 'the mark is cleared');
    const second = x.rows[1];
    second.moves.forEach((f) => { f.submitted = 0; });
    x.root.fire('dragstart', {target: second.head, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: x.rows[2].head, clientY: 202});
    ok(second.moves.every((f) => f.submitted === 0), 'dropped just above the next heading, the row stays');
    const third = x.rows[2];
    x.root.fire('dragstart', {target: third.head, dataTransfer: {setData() {}}});
    x.root.fire('drop', {target: x.rows[2].head, clientY: 202});
    ok(third.moves.every((f) => f.submitted === 0), 'a row dropped on itself does nothing');
    ok(!/innerHTML|eval\(|document\.write/.test(script), 'the script writes text only');
  } catch (e) {
    log.push('  (' + e.message + ')');
    return {pass: false, log};
  }
  return {pass: true, log};
}

const MUTATIONS = [
  ['cell-submit-removed', '    form.submit();\n  }\n  function sendRow', '  }\n  function sendRow'],
  ['cell-place-off-by-one', 'form.querySelector(\"[name=to_cell]\").value = String(place);',
    'form.querySelector(\"[name=to_cell]\").value = String(place + 1);'],
  ['cell-row-not-set', '    form.querySelector(\"[name=to_row]\").value = String(row);\n', ''],
  ['cell-nothing-said', '    say(\"Moving \" + label(cell)', '    (\"Moving \" + label(cell)'],
  ['plain-arrow-moves-a-cell', '      if (move) {\n        event.preventDefault();\n        if (key === \"ArrowLeft\"',
    '      if (true) {\n        event.preventDefault();\n        if (key === \"ArrowLeft\"'],
  ['row-key-without-modifiers', 'if (head && event.target === head && move && (key', 'if (head && event.target === head && (key'],
  ['drop-spot-ignores-the-pointer', '    var all = cells(list, dragged);\n    var j;\n    var box;\n    for (j = 0; j < all.length; j += 1) {\n      box = all[j].getBoundingClientRect();\n      if (event.clientY < box.top + box.height / 2) return j;',
    '    var all = cells(list, dragged);\n    var j;\n    var box;\n    for (j = 0; j < all.length; j += 1) {\n      box = all[j].getBoundingClientRect();\n      return j;'],
  ['drop-off-a-row-moves', 'if (item.hasAttribute(\"data-cell\") && list) {', 'if (item.hasAttribute(\"data-cell\")) {'],
  ['row-drop-ignores-direction', 'place = below ? (from < at ? at : at + 1) : (from < at ? at - 1 : at);', 'place = at;'],
  ['enter-does-not-focus', '          field.focus();\n          say(\"Editing ', '          say(\"Editing '],
  ['markup-written', '  function say(text) {\n    live.textContent = text;\n  }\n  function lists()',
    '  function say(text) {\n    live.innerHTML = text;\n  }\n  function lists()'],
];

const script = fs.readFileSync(process.argv[2], 'utf8');
const base = run(script);
if (!base.pass) {
  console.log(base.log.join('\n'));
  console.log('check-structurejs: the script fails the check');
  process.exit(1);
}
console.log('check-structurejs: the script passes (' + base.log.length + ' checks)');
let bad = 0;
for (const [name, from, to] of MUTATIONS) {
  const at = script.indexOf(from);
  if (at < 0 || script.indexOf(from, at + 1) >= 0) {
    console.log('check-structurejs: ' + name + ': THE EDIT DID NOT APPLY (' + (at < 0 ? 'not found' : 'found twice') + '), nothing was tested');
    bad += 1;
    continue;
  }
  const r = run(script.slice(0, at) + to + script.slice(at + from.length));
  if (r.pass) {
    console.log('check-structurejs: ' + name + ': PASSED with the defect made');
    bad += 1;
  } else {
    console.log('check-structurejs: ' + name + ': made, and the check failed (' + r.log[r.log.length - 1].trim() + ')');
  }
}
if (bad) {
  process.exit(1);
}
console.log('check-structurejs: every defect made turned the check red');
