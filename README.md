# defiant-web

HTTP adapter for Defiant. It accepts one listener, reads HTTP/1.1, and calls
`defiant-core` for the tenant, the session and the page.

`DEFIANT_LISTEN` is the address. An empty value binds nothing. `/alive` and
`/ready` are answered before a tenant is chosen.

The library is LGPL-3.0-only. Public headers are `@stability free`.

## Tests and gates

`make test PREFIX=...` also runs `check-seams` (no private header is included
across a seam), `check-plants` (the response scanner and the two-tenant tests
are shown to fail, by name, when a defect is planted in a scratch copy of the
server), and on Linux `check-defiant` and `check-demo`. The scanner uses
`defiant-test`, which has to be installed in the same prefix.
