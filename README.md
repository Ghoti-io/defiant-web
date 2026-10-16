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

`/admin/templates` is the template editor. It lists the templates whose kind
the principal may edit (the permission `core.template.edit.<kind>`, which
nobody holds until a changeset grants it), `/admin/templates/<name>` edits one
(Validate, Preview, Save with the template's name typed), and
`/admin/templates/<name>/history` lists its revisions and reverts the latest.
A template the principal may not edit is answered like one that does not
exist. A module that subscribes to the `template-write` decision hook is asked
about every save, once `gdefweb_server_set_host()` has given the server the
process's module host. The rules are in defiant-core's `documentation/templates.md`.

`/admin/access` is the access matrix: a read-only table of what each role and
each enabled module grants (who grants it, the permission, the action, the
type, the condition in plain words and a risk of low, medium or high). It is
there for the administrator only; to anyone else it is a page that does not
exist. Every text in it was written by someone, a module's author included, and
is escaped. The rows and their order are defiant-core's
(`gdefcore_access_matrix()`, `documentation/access.md`).

## Tests and gates

`make test PREFIX=...` also runs `check-seams` (no private header is included
across a seam), `check-plants` (the response scanner and the two-tenant tests
are shown to fail, by name, when a defect is planted in a scratch copy of the
server), `check-tpladmin` (the same for the template editor), `check-access` (the same
for the access matrix page), and on Linux
`check-defiant` and `check-demo`. The scanner uses
`defiant-test`, which has to be installed in the same prefix.
