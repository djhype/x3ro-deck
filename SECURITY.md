# Security policy

## Supported versions

Security fixes go into the latest release only.

## Reporting a vulnerability

Please don't open a public issue. Report it privately through GitHub's
private vulnerability reporting: open the repository's **Security** tab and
choose **Report a vulnerability**. Include the version, your OS, the steps to
reproduce, and what an attacker could do.

You'll get an acknowledgement within a week. Fixes are released as soon as
they are ready, and the advisory credits the reporter unless you'd rather
stay anonymous.

## Scope

x3ro deck runs agent CLIs, a session daemon and a hook that talk over local
sockets (named pipes on Windows) owned by the current user. Reports about
another user reaching those sockets, hook payloads that run code, or the
session daemon exposing sessions are especially welcome.
