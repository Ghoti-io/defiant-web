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

## Tests and gates

`make test PREFIX=...` also runs `check-seams` (no private header is included
across a seam), `check-plants` (the response scanner and the two-tenant tests
are shown to fail, by name, when a defect is planted in a scratch copy of the
server), `check-tpladmin` (the same for the template editor), and on Linux
`check-defiant` and `check-demo`. The scanner uses
`defiant-test`, which has to be installed in the same prefix.
