# Third-Party Licenses

This document inventories every dependency used by Omega, its license, source,
and whether it is distributed to consumers of the installed library.

Update this file when adding, removing, or version-bumping any dependency.
Updating this file is a release checklist requirement and a PR blocker when
adding a new dependency.

---

## libremidi

- **Version:** v5.1.0
- **License:** MIT
- **Source:** https://github.com/celtera/libremidi
- **Ships to consumers:** yes — linked into the installed library

## midifile

- **Version:** 98917df5b1bf0d6e8d4c0e5fff86d6b05343e793 (commit hash; no version tags)
- **License:** BSD 2-Clause
- **Source:** https://github.com/craigsapp/midifile
- **Ships to consumers:** yes — linked into the installed library

## Catch2

- **Version:** v3.5.2
- **License:** BSL-1.0
- **Source:** https://github.com/catchorg/Catch2
- **Ships to consumers:** no — test framework only, never installed

## Ableton Link (optional)

- **Version:** Link-3.1.5
- **License:** GPL v2+
- **Source:** https://github.com/Ableton/link
- **Ships to consumers:** only when built with `-DOMEGA_WITH_LINK=ON`
- **Note:** Enabling Ableton Link changes the combined Omega+Link build license to GPL v2+.
  See `docs/design/07-extensions.md` for details.
