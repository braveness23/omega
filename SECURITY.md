# Security Policy

## Supported Versions

Omega is pre-1.0. Security fixes will be applied to the latest commit on `main` only.

Once 1.0 is released, this policy will be updated with a supported version matrix.

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Report vulnerabilities privately via GitHub's [Security Advisories](https://github.com/braveness23/omega/security/advisories/new) feature. This keeps the report confidential until a fix is ready.

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix if you have one

You will receive an acknowledgment within 48 hours. We will coordinate a disclosure timeline with you before publishing anything publicly.

## Scope

Omega is a C++ library. Relevant security concerns include:

- Buffer overflows or out-of-bounds reads in MIDI/SMF parsing
- Integer overflows in timing arithmetic
- Use-after-free in session or engine lifecycle
- Unsafe handling of untrusted session files (`.omega`, SMF)

**Out of scope**: vulnerabilities in dependencies (libremidi, midifile, Ableton Link). Report those to their respective projects.
