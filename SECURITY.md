# Security policy

## Reporting a vulnerability

Email wangxiao1254@gmail.com. Please do not open a public issue for a
vulnerability.

A useful report says which version or commit is affected, which API or
message reaches the faulty code, and how to reproduce the problem.

## Supported versions

- `main` (the 1.0 pre-releases): supported.
- `0.3.x` (branch `v0.3.x`): security fixes only.
- Older releases: not supported.

## Trust model

emp-tool trusts the local party's own code, compiled correctly from this
source, and everything that party controls. It does not trust the other
parties it communicates with. The Security section of
[README.md](README.md) lists the guarantees emp-tool does and does not give.
