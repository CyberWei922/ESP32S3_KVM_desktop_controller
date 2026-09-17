# m1ddc upstream record

- Project: `m1ddc`
- Upstream: <https://github.com/waydabber/m1ddc>
- License: MIT (see `LICENSE` in this directory)
- Vendored commit: `04d949794102eb8df01ad3681afff6464a3eede2`
- Commit date: 2026-08-02
- Imported files: `i2c.m`, `ioregistry.m`, `i2c.h`, `ioregistry.h`, and `utils.h`

StatsForKVM intentionally does not include m1ddc's command-line argument parser.
The small bridge in `Stats/KVM` exposes only display discovery and DDC input
read/write operations required by this project.

Local maintenance patches initialize failed I/O return values, use strict C
prototypes, and release Core Foundation/Objective-C objects created during
repeated display discovery. Functional DDC packet and transport behavior is
otherwise kept aligned with the recorded upstream commit.

Two unused convenience functions that returned retained `IOAVServiceRef`
objects were removed from this focused integration. StatsForKVM uses only the
explicit `DDCTransport` entry point and releases every returned service.

The vendored source uses Apple private display APIs. It is intended for this
personal Apple Silicon deployment and must be revalidated after macOS updates.
