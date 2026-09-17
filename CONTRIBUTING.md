# Contributing to Outsider

Thank you for helping improve Outsider. Bug reports, plugin compatibility
findings and patches are all welcome.

## Copyright assignment

Outsider is dual-licensed: under the GPLv2 for everyone, and under a
commercial license that General Arcade (Pte. Ltd.) grants to projects that
cannot use the GPLv2. Offering that second option is only possible while
General Arcade holds the copyright to the whole code base.

For that reason, **by submitting a contribution (a pull request, patch, or
code sent in any other form) you assign the copyright in that contribution
to General Arcade (Pte. Ltd.)**, and you confirm that:

- the contribution is your own original work, or you have the right to
  assign it;
- it does not include code under a license that would conflict with either
  the GPLv2 or General Arcade's commercial license;
- you understand that General Arcade may distribute it under the GPLv2,
  under its commercial license, or both.

General Arcade in turn always makes the code base available under the
GPLv2, so your contribution stays free software.

If you cannot agree to this, please open an issue describing the change
instead of sending code, or write to contact@generalarcade.com.

## Sending a change

- Open an issue first for anything larger than a small fix, so the approach
  can be agreed before you spend time on it.
- Keep the existing style: C17 and C++ for the native side, ES5-compatible
  JavaScript in the shims (QuickJS runs them without a JIT, so keep hot
  paths simple), Python 3.8+ in `tools/`.
- Every new source file starts with the copyright line and the SPDX
  expression `GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial`.
- Add or update tests under `tests/` and make sure `ctest --test-dir build`
  passes. `tests/test_build_pipeline.py` covers the packaging tools.
- When a change fixes a game, say which game and plugin in the commit
  message; a plugin name is often the only way to find the problem again.
