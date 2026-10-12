# defiant-web

HTTP adapter for Defiant. It accepts one listener, reads HTTP/1.1, and calls
`defiant-core` for the tenant, the session and the page.

`DEFIANT_LISTEN` is the address. An empty value binds nothing. `/alive` and
`/ready` are answered before a tenant is chosen.

The library is LGPL-3.0-only. Public headers are `@stability free`.
