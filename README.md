# defiant-web

HTTP adapter for Defiant. It accepts one listener, reads HTTP/1.1, and calls
`defiant-core` for the tenant, the session and the page.

`DEFIANT_LISTEN` is the address. An empty value binds nothing. `/alive` and
`/ready` are answered before a tenant is chosen.

The library is LGPL-3.0-only. Public headers are `@stability free`.

## The admin pages

`/admin` is the server-rendered admin, for a signed-in principal with the
`admin` role. Every action is a signed form that answers with a 303, and none
needs JavaScript.

`/admin/templates` is the template editor. It lists the templates the principal
may edit (the permission `core.template.edit`, which nobody holds until a
changeset grants it, with a condition that names the templates it covers: a
kind, or a classification), `/admin/templates/<name>` edits one
(Validate, Preview, Save with the template's name typed), and
`/admin/templates/<name>/history` lists its revisions and reverts the latest.
A template the principal may not edit is answered like one that does not
exist. Nothing denies: a module can add a grant (the pages of one classification,
for its people), once `gdefweb_server_set_host()` has given the server the
process's module host, and cannot take one away. The rules are in defiant-core's
`documentation/templates.md`.

`/admin/access` is the access matrix: a read-only table of what each role and
each enabled module grants (who grants it, the permission, the action, the
type, the condition in plain words and a risk of low, medium or high). It is
there for the administrator only; to anyone else it is a page that does not
exist. Every text in it was written by someone, a module's author included, and
is escaped. The rows and their order are defiant-core's
(`gdefcore_access_matrix()`, `documentation/access.md`).

Every admin page carries its navigation: the admin menu, the breadcrumbs and,
where a page has them, tabs (a template's Edit and History). They are the
core's widgets (`documentation/menus.md` there), drawn from the admin pages as
routes in a route table of their own, each with the permission its page already
depends on. An entry is shown when its route's own check passes for the page's
values, so the Templates entry is there for someone who may edit a template and
the Menus entry for someone who may edit menus (`core.menu.edit`, which nobody
holds until a changeset grants it); the other pages need only the administrator's
session, which is what gates the whole area. The widgets are drawn with stored
templates (`menu`, `tabs`, `breadcrumbs`), so a site restyles them like any
other.

`/admin/menus` lists every entry of every menu and tab, with whether the site
hid it, changed it, and whether the administrator would see it. A generated entry
is changed with an override that stores only what is filled in (a label, a
weight, a parent, a menu, hide or show), and Reset takes it away; an entry the
site made is added, edited and removed. The page is there for whoever may follow
its route (`core.menu.edit`); to anyone else it is a page that does not exist.
Each change is a signed form and a changeset that carries the request, so the
core decides again, entry by entry, when it is applied.

Every action on an entry is a form that works with scripts off: add, rename, hide
or show, remove (a site entry; a generated one is hidden), move up or down,
indent under the entry above, outdent out from under the parent, and move to a
parent and a position. Each posts, redirects to the entry's place on the page and
leaves a message, and each is one changeset (a move that has to number its
siblings again writes them in the same one), taken back whole by the button on
`/admin/undo`. A form names the version of the menus its page showed; if the
menus have changed since, nothing is written, a message says so, and the page
shown is the menus as they are. A move under the entry itself or one of its own
entries, or under an entry that is not in the menu, is refused the same way. The
one script the library serves (`/defiant.js`, hashed in the page and in the
policy, no other script) adds a drag, onto the upper part of a row to go above
it, the middle to go under it, the lower part to go below it, and the keys Alt and
Shift with Up, Down, Right and Left to move, indent and outdent a focused row;
both fill in and submit the page's one move form, so a drag, a key and the form
are the same post. The script says what it is doing in a live region, shows the
messages a post left in the same region, and writes text only as text.

`/admin/settings` is the settings pages. The core generates the form of a group
(`documentation/settings.md` there); this library draws the page around it, signs
it for the session and the administrator, and reads the post: a value is checked
before anything is written and a bad one refuses the whole form with a sentence
that names the setting, a form drawn before another save is refused as stale, and
a save that changes nothing says so and writes nothing. A page exists where a route
placed one in the admin menu: the kernel's (`/admin/settings/core`, with a tab for
its group) or a route an enabled module declares at `/admin/settings/<module>`
with tabs `/admin/settings/<module>/<group>`; a group no route placed has none.
The routes ask `core.settings.edit`, which nobody holds until a changeset grants
it, and the write is decided again by the core for each setting
(`core.settings.edit` may carry a condition on the setting's group). An
environment-local secret is shown as Set or Not set and never again.

`/admin/more` is the admin directory. It lists the pages the admin menu leads to
for this administrator, grouped under the module that declared each (the kernel's
are under `core`), and nothing in it is named here: an enabled module's links
appear under its own heading, and a link whose route the administrator's own check
refuses is absent. A save, an error or a refusal on any admin page answers with a
303 and leaves a message for the session (`documentation/messages.md` there): the
next page the same session is shown says it once, escaped, in the page's
navigation, and a different session is never shown it. A public page shows it too
when its layout places the messages widget.

## Tests and gates

`make test PREFIX=...` also runs `check-seams` (no private header is included
across a seam), `check-noveto` (nothing here names the veto on template edits that
defiant-core retired), `check-plants` (the response scanner and the two-tenant tests
are shown to fail, by name, when a defect is planted in a scratch copy of the
server), `check-tpladmin` (the same for the template editor), `check-nav` (the same for the
admin navigation and the menus page), `check-settingsadmin` (the same for the settings pages), `check-messageadmin` (the same for the
admin directory and the messages a save leaves), `check-access` (the same
for the access matrix page), and on Linux
`check-defiant` and `check-demo`. The scanner uses
`defiant-test`, which has to be installed in the same prefix.
