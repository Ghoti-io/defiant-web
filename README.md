# defiant-web

HTTP adapter for Defiant. It accepts one listener, reads HTTP/1.1, and calls
`defiant-core` for the tenant, the session and the page.

`DEFIANT_LISTEN` is the address. An empty value binds nothing. `/alive` and
`/ready` are answered before a tenant is chosen.

## The session cookie

Signing in sets one cookie and nothing else holds a session. A server made with
no options sets exactly

    __Host-session=<token>; Secure; HttpOnly; SameSite=Strict; Path=/

`Secure` keeps it to HTTPS, `HttpOnly` keeps it from scripts, and the
`__Host-` prefix with `Path=/` and no `Domain` makes a browser accept it only
from this host and refuse one set for it by a sibling host of the domain. The
server reads only the cookie of that name. Signing out (below) clears it with
the attributes it was set with.

`gdefweb_server_create()` takes the cookie policy as `GDEFWEB_Server_Options`
(start from `gdefweb_server_options_default()`; NULL is the same thing). Each
relaxation is a named option, and each is limited to where it is needed:

| Option | Value | Cookie | When it is for |
|---|---|---|---|
| `same_site` | `GDEFWEB_COOKIE_SAME_SITE_STRICT` (default) | `SameSite=Strict` | Everything that can be Strict |
| `same_site` | `GDEFWEB_COOKIE_SAME_SITE_LAX` | `SameSite=Lax` | A deployment whose people arrive by links from other sites (see below) |
| `transport` | `GDEFWEB_COOKIE_TRANSPORT_SECURE` (default) | `__Host-session`, `Secure` | Every deployment, including one behind a proxy that ends TLS |
| `transport` | `GDEFWEB_COOKIE_TRANSPORT_PLAIN_LOOPBACK` | `session`, no `Secure` | Development over plain HTTP on the same machine |

A browser ignores a `__Host-` cookie that is not `Secure`, so the plain-HTTP
mode cannot keep the prefix and sets `session`. Because that cookie is readable
by anyone who can see the connection, creation fails with
`GDEFWEB_ERR_INSECURE` unless `DEFIANT_LISTEN` is a loopback address (127.0.0.0/8
or `[::1]`); a server with no listener is refused too, since nothing says where
it would be reached from. A server behind a TLS-terminating proxy listens on
loopback and keeps the secure policy: the browser's connection is HTTPS, which
is what `Secure` is about.

**Why Strict, and what to do about it.** With `SameSite=Strict` a browser does
not send the cookie on a request that started on another site. A person who
follows a link to a page of this site from an email, a chat or another web page
arrives signed out on that first request, and signed in on the next, because
the next starts from this site. If that is wrong for a deployment (the front
page of a signed-in people's site, say), choose Lax: the cookie is then sent on
a top-level navigation by GET from anywhere. Nothing that changes anything
depends on SameSite either way: every state-changing request is checked for its
Origin and for the signed form token the session's own page carried.

The `defiant` program reads `DEFIANT_COOKIE_SAMESITE` (`strict` or `lax`) and
`DEFIANT_COOKIE_TRANSPORT` (`secure` or `plain-loopback`); anything else, or
`plain-loopback` on a listener that is not loopback, is refused at start before
anything is created.

Where the platform has `SIGUSR1`, the program answers it with one line on
standard error, `defiant: stats compiled=N rendered=M`: the template bodies it
has compiled and the ones it has run since it started. A stored template is read
and compiled each time a page is drawn, so an edit shows on the very next
request and nothing is built ahead; the two counts move together, and a program
that compiled ahead of its requests would show the first ahead of the second.

**Signing out.** Every signed-in admin page carries a "Sign out" form that posts
to `/logout` with a signed token bound to that session, made for that purpose.
`/logout` checks the Origin first: a request from another origin, or with none,
is answered 403 and sets nothing, whatever its cookie is.

- *A live session.* The token is required. A request without one, with a damaged
  one, or with one signed for a different sign-in or another form, is answered
  403 and the session is left as it was. With the token, the session is **ended
  on the server**, not only forgotten by the browser: the server asks the core to
  revoke this one session (`gdefcore_account_session_revoke`), so the token stops
  working at once for every later request, wherever it was copied, on the public
  pages as on the admin ones. Only this session ends. The person's other
  sign-ins (another browser, another device) and everyone else's are not touched;
  signing out of all of them is a separate act (`gdefcore_account_logout_everywhere`),
  which sign-out does not do. The answer is 303 to `/login` with the cookie
  cleared, carrying exactly the attributes it was set with.
- *A cookie that names no live session* (absent, damaged, expired, or already
  revoked or ended). There is nothing to forge and nothing to end, so no token is
  asked for and the server changes nothing; the answer is still 303 to `/login`
  with the clearing cookie, so a dead cookie is not left in the browser. A second
  sign-out of a session already signed out is this case.
- *When the session cannot be ended.* If the core cannot record the revocation, or
  the store cannot say whether the session is live, the answer is 500 with a
  small fixed HTML page whose first line says "You are still signed in", and the
  cookie is **not** cleared: nothing is reported as signed out that is not. The
  page asks nothing of the store (it is what failed), carries no form (a retry
  needs a fresh signed token), and links back to `/admin`. Trying again is safe.
  Every response carries `Cache-Control: no-store`.

The library is LGPL-3.0-only. Public headers are `@stability free`.

## The admin pages

`/admin` is the server-rendered admin. Who may use a page is decided by the
page's own route check and by nothing else: a signed-in session says who someone
is, never what they may do, and the role named `admin` is asked nothing. Every
page has a permission:

| Page | Permission |
|---|---|
| `/admin`, `/admin/more` | Use the admin area (`core.admin.use`) |
| `/admin/post` | Write posts (`core.post.write`) |
| `/admin/front` | Edit layouts (`core.layout.edit`) |
| `/admin/undo` | Undo changes (`core.admin.undo`), and per thing undone the permission of that thing |
| `/admin/access` | See what is granted (`core.access.read`) |
| `/admin/templates` | Edit templates (`core.template.edit`) |
| `/admin/menus` | Edit menu entries and overrides (`core.menu.edit`) |
| `/admin/settings` | Edit settings (`core.settings.edit`) |

The core declares the new ones (`documentation/access.md` there) and the program
grants them all to the `admin` role on its first start, as ordinary grants a
changeset can take away. A page the check refuses answers as a page that does not
exist, and a refusal never says which permission was missing. Signing in sets one cookie,
`__Host-session`, and sends a person who has an admin page to go to to the first
such page (a person granted only `core.menu.edit` lands on `/admin/menus`);
anyone else goes to `/`. There is no second cookie for the admin area: the session
says who someone is, each page's own check says what they may do and is asked on
every request, so a grant made after signing in takes effect on the next request
with no new sign-in. Every state-changing admin request must also carry the signed
form token the page gave that session for that purpose, whatever the cookie's
SameSite; a request without one, or with another form's or another sign-in's, changes
nothing. The menu hangs under the admin home, so a
person without `core.admin.use` reaches a page they hold the permission of but is
not shown the menu.

Every action is a signed form that answers with a 303, and none needs JavaScript.

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
there for whoever holds `core.access.read`; to anyone else it is a page that
does not exist. Every text in it was written by someone, a module's author included, and
is escaped. The rows and their order are defiant-core's
(`gdefcore_access_matrix()`, `documentation/access.md`).

Every admin page carries its navigation: the admin menu, the breadcrumbs and,
where a page has them, tabs (a template's Edit and History). They are the
core's widgets (`documentation/menus.md` there), drawn from the admin pages as
routes in a route table of their own, each with the permission its page already
depends on. An entry is shown when its route's own check passes for the page's
values, so the Templates entry is there for someone who may edit a template and
the Menus entry for someone who may edit menus (`core.menu.edit`); every page
asks a permission of its own, in the table above. The widgets are drawn with stored
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

`GET /assets/<owner>/<name>` serves a file a module ships (`asset.h` in
defiant-core), to the tenant that has the module enabled and to nobody else: a
disabled module's files, another owner's, a name with an encoded form, a dot
segment, a backslash or a second slash, and an unknown host are all the same
fixed 404. The type is the one the module declared for the file, sent with
`X-Content-Type-Options: nosniff`, never one made from the name or the bytes;
the bytes are exactly the declared ones, binary included. The hash is the
`ETag`, and `If-None-Match` is answered 304 with no body. An address that carries
the matching `?v=<hash>` is cached for a year as immutable; any other (none, an
old hash, another file's) is `no-cache`, so a link written before an edit never
serves old text from a cache. An SVG is sent with a `sandbox` policy of its own.
A page names a file with `asset("owner/name")`, which writes the hash address.
An asset is public: it is sent to anyone who asks, so nothing secret belongs in
one.

A page is sent one of two content policies. A public page's lets it load the
site's own stylesheets, scripts, images and fonts from this origin
(`default-src 'none'; style-src 'self'; script-src 'self' ...; img-src 'self';
font-src 'self'; frame-ancestors 'none'`): no inline script or style, no
`eval` and no other origin. An admin page, the sign-in page and the library's own
two files keep the admin's, which allows one script by hash and no site file, so
a script a site ships can never run where an admin form's token is.

`/admin/stylesheets` and `/admin/scripts` are the editor of the site's own files
(`siteasset.h` in defiant-core): the site's files, the new-file form, and the
files modules ship, read only, with their owner. They are two pages because they
are two permissions (`core.asset.css.edit` and `core.asset.js.edit`), so someone
who may write a stylesheet is not offered a script. One file has its own page
with its public address and its history, which reverts the latest change. Every
action is a signed form bound to the sign-in, its purpose and its file; a form
opened before another save is stale and writes nothing; a script is saved only
with its name typed (it runs with the authority of whoever loads it), and a
delete and a revert type the name for both kinds. A refused form is drawn again
with the text typed, so nothing is lost to a redirect. The form holds up to 32 KiB
of text; a larger file is shipped by a module or written through the core. A
name in an address is spelled as a file name or the page is not there, and a
grant narrowed to some files lists and opens only those.
A file a page template's structure generates (`layout-<template>.css`) is listed
with the template that made it, and its page shows the text with no form: it is
changed by changing the structure, or by marking the template custom.

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
for the access matrix page), `check-adminarea` (the same for the permission gate of the admin
area and for the signature check of every write), `check-cookies` (the same for the cookie
policy and for signing out), `check-sizes` (every struct with a size member is probed),
and on Linux
`check-defiant` and `check-demo`. `make demo` also changes the
site's title, a menu entry, a template and a widget while requests keep arriving,
and requires that none fails, that none shows half of a change and that the
process and its compile counts are those of a program that rebuilt nothing.
The scanner uses
`defiant-test`, which has to be installed in the same prefix.
