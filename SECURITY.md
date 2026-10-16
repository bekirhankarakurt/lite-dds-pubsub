# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.x | Yes |
| < 1.0 | No |

## Reporting a vulnerability

Please do not open a public issue for security problems. Report them privately through
[GitHub Security Advisories](https://github.com/bekirhankarakurt/lite-dds-pubsub/security/advisories/new).
Include the affected version, a description of the impact, and a reproducer or packet capture
(`dds_spy --record`) if possible. You can expect an initial response within 7 days.

## Scope notes

The wire protocol has no authentication or encryption. Any host on the multicast group can inject
packets, so deploy it only on networks you trust. Malformed or truncated packets are expected input
and must never crash a participant; reports of crashes, hangs or memory errors triggered by network
input are in scope.
